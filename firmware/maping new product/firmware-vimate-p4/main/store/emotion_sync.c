/**
 * emotion_sync.c — mount và đọc emotion assets từ SPIFFS "emo_spiffs".
 */
#include "emotion_sync.h"
#include "vimate.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <sys/stat.h>

#define EMO_BASE "/spiffs_emo"

static char s_path_buf[64];  /* dùng cho emotion_sync_path() trả ra */

esp_err_t emotion_sync_init(void) {
    /* Idempotent: display.c (mặt robot P4) mount sớm trong display_setup_ui, rồi
     * vimate_main_task gọi lại theo thứ tự cũ → không log lỗi lần hai. */
    if (esp_spiffs_mounted("emo_spiffs")) {
        return ESP_OK;
    }
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = EMO_BASE,
        .partition_label = "emo_spiffs",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t r = esp_vfs_spiffs_register(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "emo spiffs mount fail: %s", esp_err_to_name(r));
        return r;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("emo_spiffs", &total, &used);
    ESP_LOGI(TAG_MAIN, "emo spiffs ready: %u/%u bytes used", (unsigned)used, (unsigned)total);
    return ESP_OK;
}

/* Check .gif first then .png — GIF priority cho animation. */
bool emotion_sync_has_custom(const char *emotion_key) {
    if (!emotion_key) return false;
    char p[64];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%.20s.gif", EMO_BASE, emotion_key);
    if (stat(p, &st) == 0 && st.st_size > 0) return true;
    snprintf(p, sizeof(p), "%s/%.20s.png", EMO_BASE, emotion_key);
    return stat(p, &st) == 0 && st.st_size > 0;
}

const char *emotion_sync_path(const char *emotion_key) {
    if (!emotion_key) return NULL;
    struct stat st;
    snprintf(s_path_buf, sizeof(s_path_buf), "%s/%.20s.gif", EMO_BASE, emotion_key);
    if (stat(s_path_buf, &st) == 0 && st.st_size > 0) return s_path_buf;
    snprintf(s_path_buf, sizeof(s_path_buf), "%s/%.20s.png", EMO_BASE, emotion_key);
    return s_path_buf;
}

void emotion_sync_blocking(void) {
    ESP_LOGI(TAG_MAIN, "emotion sync skipped for EDU; using bundled Chuppy GIF pack");
}
