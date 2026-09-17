/**
 * main.c — Entry point. Chỉ chứa app_main() — gọi sang app_main.c để
 * dùng cho cả test/multi-app sau này.
 */
#include "esp_log.h"
#include "vimate.h"

extern void vimate_app_start(void);

void app_main(void) {
    ESP_LOGI(TAG_MAIN, "═══════════════════════════════════════");
    ESP_LOGI(TAG_MAIN, "%s Firmware %s — booting up", VIMATE_BRAND_NAME, VIMATE_FW_VERSION);
    ESP_LOGI(TAG_MAIN, "═══════════════════════════════════════");
    vimate_app_start();
}
