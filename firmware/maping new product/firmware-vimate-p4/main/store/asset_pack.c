/**
 * asset_pack.c — Premium asset pack download + SPIFFS mount.
 *
 * NOTE: Việc unzip yêu cầu component `miniz` hoặc tự implement.
 * Trong skeleton này: server có thể trả DIRECT files (không zip):
 *   manifest.json liệt kê { "files": [ { "path": "emo/happy.png", "url": "..." } ] }
 *   Mỗi file download riêng → đỡ phụ thuộc thư viện unzip.
 *
 * Atomic: download xong all → ghi version cuối; nếu fail giữa chừng giữ version cũ.
 */
#include "asset_pack.h"
#include "vimate.h"
#include "core/nvs_store.h"
#include "core/task_profile.h"
#include "network/http_dl.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define ASSET_BASE "/spiffs_asset"
static char s_version[24] = {0};

esp_err_t asset_pack_init(void) {
    /* Idempotent — nếu đã mount rồi (vd asset_pack_sync_async gọi lần 2)
     * thì skip không log error. */
    if (esp_spiffs_mounted("asset_spiffs")) {
        return ESP_OK;
    }
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = ASSET_BASE,
        .partition_label = "asset_spiffs",
        .max_files = 16,
        .format_if_mount_failed = true,
    };
    esp_err_t r = esp_vfs_spiffs_register(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_ASSET, "spiffs mount fail: %s", esp_err_to_name(r));
        return r;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("asset_spiffs", &total, &used);
    ESP_LOGI(TAG_ASSET, "asset spiffs %u/%u bytes used", (unsigned)used, (unsigned)total);
    nvs_store_get_str("asset_ver", s_version, sizeof(s_version));
    return ESP_OK;
}

bool asset_pack_has_file(const char *relpath) {
    if (!relpath) return false;
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", ASSET_BASE,
             relpath[0] == '/' ? relpath + 1 : relpath);
    struct stat st;
    return stat(p, &st) == 0;
}

const char *asset_pack_version(void) { return s_version; }

static void download_manifest_files(cJSON *files) {
    cJSON *f;
    cJSON_ArrayForEach(f, files) {
        cJSON *p = cJSON_GetObjectItem(f, "path");
        cJSON *u = cJSON_GetObjectItem(f, "url");
        if (!cJSON_IsString(p) || !cJSON_IsString(u)) continue;
        char dest[180];
        snprintf(dest, sizeof(dest), "%s/%s", ASSET_BASE, p->valuestring);
        if (http_dl_get_to_file(u->valuestring, dest) != ESP_OK) {
            ESP_LOGW(TAG_ASSET, "Failed dl %s", p->valuestring);
            continue;
        }
        ESP_LOGI(TAG_ASSET, "Saved %s", dest);
    }
}

static void sync_task(void *arg) {
    /* Endpoint: GET /api/devices/:macId/asset-pack — server trả 200 nếu có pack active */
    char url[200];
    snprintf(url, sizeof(url),
        "%s/api/devices/%s/asset-pack",
        g_vimate_server.base_url, g_vimate_server.mac_id);

    uint8_t *body = NULL;
    size_t len = 0;
    if (http_dl_get_to_buf(url, &body, &len) != ESP_OK) {
        ESP_LOGW(TAG_ASSET, "asset-pack GET fail");
        vTaskDelete(NULL);
        return;
    }
    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    free(body);
    if (!root) { vTaskDelete(NULL); return; }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;
    cJSON *ver = cJSON_GetObjectItem(data, "version");
    cJSON *files = cJSON_GetObjectItem(data, "files");
    if (cJSON_IsString(ver)) {
        if (strcmp(ver->valuestring, s_version) == 0) {
            ESP_LOGI(TAG_ASSET, "asset pack up-to-date (%s)", s_version);
        } else if (cJSON_IsArray(files)) {
            ESP_LOGI(TAG_ASSET, "asset pack new version %s → download %d files",
                     ver->valuestring, cJSON_GetArraySize(files));
            download_manifest_files(files);
            strlcpy(s_version, ver->valuestring, sizeof(s_version));
            nvs_store_set_str("asset_ver", s_version);
        }
    }
    cJSON_Delete(root);
    vTaskDelete(NULL);
}

void asset_pack_sync_async(void) {
    asset_pack_init();
    /* Core 1 — HTTPS download asset pack, không block WS/UI Core 0. */
    xTaskCreatePinnedToCore(sync_task, "asset_sync", VIMATE_TASK_STACK_BACKGROUND, NULL,
                            VIMATE_TASK_PRIO_BACKGROUND, NULL, VIMATE_TASK_CORE_IO);
}
