/**
 * app_main.c — boot orchestrator Rapid4P (khung kế thừa firmware-vimate-p4/main/app_main.c,
 * bỏ audio/WS/MCP). Thứ tự (MAPPING §2.3):
 *
 *   nvs_flash_init (lỗi → erase + reinit) → nvs_store → calib_store → engineer_api
 *   → diag → event loop/netif → display_init → button → touch → measure (sensor bus riêng)
 *   → result_upload → ui_reader → ota_client (rollback guard) → main task
 *   main task: system_info → SNTP (TZ VN) → WiFi STA nếu có creds → vòng sự kiện
 *
 * Boot-safety: LCD/cảm biến lỗi KHÔNG được giết boot — log rồi đi tiếp để WiFi/OTA còn
 * cứu được máy từ xa.
 */
#include "rapid4p.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "core/nvs_store.h"
#include "core/system_info.h"
#include "core/wifi_mgr.h"
#include "app/calib_store.h"
#include "app/measure.h"
#include "network/engineer_api.h"
#include "network/ota_client.h"
#include "network/result_upload.h"
#include "ui/display.h"
#include "ui/ui_reader.h"
#include "input/button.h"
#include "input/touch.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

EventGroupHandle_t g_r4p_events;
r4p_config_t g_r4p_cfg;

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "other_wdt";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "other";
    }
}

/* diag heap 60 s/lần — cùng dạng dòng "diag heap internal=" của cây P4 để tool đọc
 * log dùng lại được. */
static void diag_timer_cb(void *arg)
{
    (void)arg;
    uint32_t full = 0, area = 0, us = 0;
    display_rotate_stats(&full, &area, &us);
    ESP_LOGI(TAG_MAIN, "diag heap internal=%u min=%u psram=%u min=%u flush=%lu rotate full=%lu area=%lu max=%lu us",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)display_flush_count(), (unsigned long)full, (unsigned long)area, (unsigned long)us);
}

static void main_task(void *arg)
{
    (void)arg;
    system_info_init();

    /* Giờ VN cho trường `time` của payload (server parse DD-MM-YYYY HH:MM:SS giờ VN). */
    setenv("TZ", "ICT-7", 1);
    tzset();
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.start = true;
    esp_netif_sntp_init(&sntp_cfg);

    esp_err_t w = wifi_mgr_start();
    if (w == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG_MAIN, "chua co WiFi - may chay offline; vao Cai dat > WiFi de cau hinh");
        ui_reader_set_dev_state(R4P_STATE_READY);
    } else if (w != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "wifi_mgr_start: %s", esp_err_to_name(w));
    } else {
        ui_reader_set_dev_state(R4P_STATE_WIFI_CONNECTING);
    }
    ota_client_start_periodic();

    int tick = 0;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(g_r4p_events,
            R4P_EVT_WIFI_UP | R4P_EVT_WIFI_DOWN | R4P_EVT_BTN_PRESS | R4P_EVT_BTN_LONG |
            R4P_EVT_BTN_MEASURE | R4P_EVT_MEASURE_DONE | R4P_EVT_UPLOAD_QUEUED,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
        if (bits & R4P_EVT_WIFI_UP) {
            ESP_LOGI(TAG_MAIN, "WiFi up %s", wifi_mgr_ip_address());
            ui_reader_set_wifi(true, wifi_mgr_ip_address());
            ui_reader_set_dev_state(R4P_STATE_READY);
        }
        if (bits & R4P_EVT_WIFI_DOWN) {
            ui_reader_set_wifi(false, NULL);
        }
        if (bits & R4P_EVT_BTN_PRESS) ui_reader_on_boot_button();
        if (bits & R4P_EVT_BTN_MEASURE) ui_reader_on_measure_button();
        if (bits & R4P_EVT_BTN_LONG) {
            /* Giữ BOOT 5 s: xoá WiFi đã lưu rồi khởi động lại (như vimate). Token/mã máy
             * giữ nguyên. */
            ESP_LOGW(TAG_MAIN, "BOOT giu 5 s -> xoa WiFi, restart");
            nvs_store_set_wifi("", "");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        if ((bits & (R4P_EVT_MEASURE_DONE | R4P_EVT_UPLOAD_QUEUED)) || (++tick % 2 == 0)) {
            ui_reader_set_upload_stats(result_upload_pending(), result_upload_sent());
        }
    }
}

void r4p_app_start(void)
{
    ESP_LOGW(TAG_MAIN, "Rapid4P %s board=%s reset=%s", R4P_FW_VERSION, BOARD_NAME,
             reset_reason_name(esp_reset_reason()));

    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "nvs_flash_init: %s - erase + reinit", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_store_init();
    calib_store_load();
    engineer_api_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    g_r4p_events = xEventGroupCreate();
    configASSERT(g_r4p_events);

    bool display_ok = (display_init() == ESP_OK);
    if (!display_ok) ESP_LOGE(TAG_MAIN, "display_init loi - boot tiep KHONG man (giu WiFi/OTA)");

    button_init();
    if (display_ok) touch_init();

    measure_init();          /* bus I2C riêng + LED slot; lỗi cảm biến không chặn boot */
    result_upload_init();

    if (display_ok) {
        ui_reader_init();
        display_set_sleep_timeout(CONFIG_RAPID4P_SCREEN_SLEEP_SEC);
    }
    ota_client_init(display_ok);

#if CONFIG_RAPID4P_DIAG_ENABLE
    const esp_timer_create_args_t dargs = { .callback = diag_timer_cb, .name = "diag" };
    esp_timer_handle_t dt;
    if (esp_timer_create(&dargs, &dt) == ESP_OK) {
        esp_timer_start_periodic(dt, (uint64_t)CONFIG_RAPID4P_DIAG_INTERVAL_SEC * 1000000ULL);
    }
#endif

    xTaskCreatePinnedToCore(main_task, "r4p_main", 6144, NULL, R4P_TASK_PRIO_NETWORK, NULL, R4P_TASK_CORE_UI);
}
