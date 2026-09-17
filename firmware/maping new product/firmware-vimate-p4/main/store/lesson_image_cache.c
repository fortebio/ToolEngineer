/**
 * lesson_image_cache.c — cache ảnh bài học từ server xuống SPIFFS.
 *
 * Storage: partition "lesson_spiffs", mount ở /spiffs_lesson.
 * File naming: /spiffs_lesson/L_<hash>.jpg, hash theo URL gốc để runtime
 * lookup deterministic với cùng URL mà lesson gửi xuống.
 */
#include "lesson_image_cache.h"
#include "vimate.h"
#include "network/http_dl.h"
#include "util/url_rewrite.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LESSON_IMG_BASE "/spiffs_lesson"

static void hash8(const char *s, char *out) {
    uint32_t h = 5381;
    for (const char *p = s; *p; p++) h = ((h << 5) + h) + (uint8_t)*p;
    snprintf(out, 9, "%08lx", (unsigned long)h);
}

esp_err_t lesson_image_cache_init(void) {
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = LESSON_IMG_BASE,
        .partition_label = "lesson_spiffs",
        .max_files = 12,
        .format_if_mount_failed = true,
    };
    esp_err_t r = esp_vfs_spiffs_register(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_CACHE, "lesson image cache mount fail: %s", esp_err_to_name(r));
        return r;
    }
    ESP_LOGI(TAG_CACHE, "lesson image cache mounted");
    return ESP_OK;
}

#define MAX_SYNC_IMAGES 40
#define MAX_URL_LEN 160

void lesson_image_cache_sync_blocking(void) {
    /* Legacy SPIFFS fallback: chỉ lấy ảnh bài học của khóa active.
     * SD-card course cache dùng endpoint /course-cache riêng; endpoint này
     * giữ boot nhanh và an toàn khi SD chưa mount được. */
    char url[200];
    snprintf(url, sizeof(url),
        "%s/api/devices/%s/lesson-images",
        g_vimate_server.base_url, g_vimate_server.mac_id);

    char auth[120] = {0};
    if (g_vimate_server.device_token[0] != '\0') {
        snprintf(auth, sizeof(auth), "Bearer %s", g_vimate_server.device_token);
    }

    uint8_t *body = NULL;
    size_t len = 0;
    if (http_dl_get_to_buf_auth(url, auth, &body, &len) != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "lesson image list GET fail");
        return;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    free(body);
    if (!root) {
        ESP_LOGW(TAG_CACHE, "lesson image list JSON parse fail");
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;
    cJSON *images = cJSON_GetObjectItem(data, "images");

    // Trích xuất URLs ra mảng phẳng tạm thời trên heap để tránh phình stack.
    // Lớp mảng phẳng này chỉ chiếm khoảng 40 * 160 = 6.4KB RAM.
    char (*urls)[MAX_URL_LEN] = malloc(MAX_SYNC_IMAGES * MAX_URL_LEN);
    int url_count = 0;

    if (urls && cJSON_IsArray(images)) {
        cJSON *it;
        cJSON_ArrayForEach(it, images) {
            if (url_count >= MAX_SYNC_IMAGES) break;
            const char *url_s = NULL;
            if (cJSON_IsString(it)) {
                url_s = it->valuestring;
            } else {
                cJSON *u = cJSON_GetObjectItem(it, "url");
                if (cJSON_IsString(u)) url_s = u->valuestring;
            }
            if (url_s && url_s[0]) {
                strlcpy(urls[url_count], url_s, MAX_URL_LEN);
                url_count++;
            }
        }
    }

    // Giải phóng cJSON và giải phóng cây struct đồ sộ NGAY LẬP TỨC để thu hồi RAM.
    // Điều này giải phóng hàng trăm KB bộ nhớ trước khi thực hiện TLS handshakes cực kỳ ngốn RAM.
    cJSON_Delete(root);

    if (!urls) {
        ESP_LOGE(TAG_CACHE, "Failed to allocate memory for URL sync list (OOM)");
        return;
    }

    int count = 0;
    for (int i = 0; i < url_count; i++) {
        const char *url_s = urls[i];
        char h[12];
        hash8(url_s, h);
        char dest[64];
        snprintf(dest, sizeof(dest), "%s/L_%s.jpg", LESSON_IMG_BASE, h);
        struct stat st;
        if (stat(dest, &st) == 0 && st.st_size > 100) {
            count++;
            continue;
        }

        char fetch_url[200];
        const char *dl_url = url_rewrite_localhost_media(url_s, fetch_url, sizeof(fetch_url));
        if (http_dl_get_to_file(dl_url, dest) == ESP_OK) {
            count++;
            ESP_LOGI(TAG_CACHE, "lesson image cached: %s -> %s", dl_url, dest);
        }
        
        // Yield CPU + Cho phép các luồng WebSocket, WiFi và task nền khác chạy để tránh trigger watchdog
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    free(urls);
    ESP_LOGI(TAG_CACHE, "lesson image cache sync done: %d images", count);
}

#if CONFIG_VIMATE_LESSON_IMAGE_BACKGROUND_SYNC
static void lesson_sync_task(void *arg) {
    (void)arg;
    lesson_image_cache_sync_blocking();
    vTaskDelete(NULL);
}
#endif

/* Chạy sync SPIFFS ở TASK NỀN khi bật cấu hình debug. Production mặc định tắt:
 * bulk HTTPS + SPIFFS sau WS connected có thể làm nghẽn WakeNet/LVGL trên board
 * SPI LCD. Runtime vẫn tải ảnh hiện tại theo nhu cầu qua ui_image. */
void lesson_image_cache_sync_async(void) {
#if CONFIG_VIMATE_LESSON_IMAGE_BACKGROUND_SYNC
    xTaskCreate(lesson_sync_task, "lesson_cache", 8192, NULL,
                tskIDLE_PRIORITY + 2, NULL);
#else
    ESP_LOGI(TAG_CACHE, "lesson image background sync disabled");
#endif
}

bool lesson_image_cache_path(const char *url, char *out_path, size_t out_size) {
    if (!url || !out_path) return false;
    char h[12];
    hash8(url, h);
    snprintf(out_path, out_size, LESSON_IMG_BASE "/L_%s.jpg", h);
    struct stat st;
    return stat(out_path, &st) == 0 && st.st_size > 100;
}
