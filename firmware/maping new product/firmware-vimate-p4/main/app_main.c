/**
 * app_main.c — Bootstrap orchestrator.
 *
 * Order:
 *   1. NVS flash init (lưu WiFi creds + device token)
 *   2. Event loop + event group
 *   3. Board init (LCD, audio I2S, GPIO buttons, LED)
 *   4. UI init (LVGL display task)
 *   5. WiFi connect (resume creds; nếu fail → provisioning AP mode)
 *   6. OTA check (HTTPS GET /ota/v1/ — lấy WS URL + activation token nếu activated)
 *   7. Mount cache partitions
 *   8. WebSocket connect (Authorization Bearer) → handle messages
 *   9. Post-boot cache sync
 *
 * Mọi step async qua FreeRTOS task; event group signal khi step xong.
 */

#include "vimate.h"

/* ==== NEO TLS — SỬA PANIC "pseudostack overflow" CỦA OPUS TRÊN ESP32-P4 (12/09/2026) ====
 * Triệu chứng: gói TTS thật đầu tiên → `FATAL ERROR: pseudostack overflow at
 * opus_decoder.c:386` → abort → reboot, lặp lại 100%. Loa không bao giờ kêu.
 * Nguyên nhân KHÔNG ở Opus mà ở TLS của ESP-IDF 5.5.1 trên P4 khi app KHÔNG có biến
 * `_Thread_local` nào có giá trị khởi tạo (chỉ có .tbss — hai con trỏ pseudostack của
 * micro-opus):
 *   - sections.ld của P4 chèn `. = ALIGN(_esp_pmp_align_size)` (SPIRAM_RODATA +
 *     PRE_CONFIGURE_MEMORY_PROTECTION) VÀO TRONG `.flash.tdata` → section này rộng
 *     0x48 byte toàn đệm, NOBITS, không mang cờ TLS.
 *   - Linker vì thế đặt PT_TLS bắt đầu ở `.flash.tbss`; compiler sinh `tp + 0`,
 *     `tp + 4` cho global_stack / scratch_ptr.
 *   - FreeRTOS (port.c uxInitialiseStackTLS) lại chép `_thread_local_data_end -
 *     _start` = 0x48 byte "tdata" (rác, vì NOBITS) rồi mới đặt bss = 0 SAU đó, và
 *     cho tp trỏ vào đầu vùng chép.
 *   → mọi biến TLS đọc lệch 0x48 byte: global_stack != 0 (rác) → Opus tưởng đã cấp
 *     pseudostack, bỏ qua cấp phát, kiểm tra tràn thấy tràn → CELT_FATAL.
 * Bằng chứng: `readelf -l` PT_TLS vaddr = _thread_local_bss_start (0x483dba80) trong khi
 * _thread_local_data_start = 0x483dba38; `nm` global_stack = TLS offset 0.
 * Sửa: có MỘT biến TLS khởi tạo khác 0 là `.flash.tdata` thành PROGBITS mang cờ TLS,
 * PT_TLS bắt đầu đúng ở đầu tdata, offset của .tbss = 0x48 + … khớp với cách port
 * xếp → mọi `_Thread_local` đúng lại. Cây S3 (Xtensa) không dính: không có PMP align.
 * KHÔNG xoá biến này. Bản diag còn tự kiểm ở spk_task (opus_codec_selftest). */
__attribute__((used)) _Thread_local int g_vimate_tls_anchor = 1;
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/wifi_mgr.h"
#include "core/ble_wifi_prov.h"
#include "core/nvs_store.h"
#include "core/task_profile.h"
#include "core/system_info.h"
#include "core/diagnostics.h"
#include "core/telemetry.h"
#include "network/ota_client.h"
#include "network/ota_boot_validation.h"
#include "network/ws_client.h"
#include "protocol/envelope.h"
#include "protocol/mcp_handler.h"
#include "ui/display.h"
#include "ui/ui_home.h"
#include "ui/ui_emotion.h"
#include "ui/ui_activation.h"
#include "ui/ui_image.h"
#include "audio/audio_pipeline.h"
#include "audio/i2s_output.h"
#include "store/asset_pack.h"
#include "store/course_media_cache.h"
#include "store/lesson_image_cache.h"
#include "store/emotion_sync.h"
#include "input/button.h"
#include "input/touch.h"
#include "actuator/servo_emotion.h"
#include "boards/board.h"

/* Global state — declared in vimate.h */
EventGroupHandle_t g_vimate_events;
vimate_server_config_t g_vimate_server;

#define WIFI_RECONFIG_BLE_DELAY_MS 120000
#define WIFI_RECONFIG_AP_FALLBACK_MS 300000
#define WS_SUPERVISOR_GRACE_MS 20000
#define WS_SUPERVISOR_RESTART_COOLDOWN_MS 30000
#define WS_REFRESH_BEFORE_HARD_DROP_MS 0
#define WS_REFRESH_DISCONNECT_GRACE_MS 10000
#define LISTEN_MAX_MS 12000
#define LISTEN_MIN_MANUAL_STOP_MS 1200
#define LISTEN_MIN_MANUAL_STOP_FRAMES 3
#define NVS_FORCE_AP_PROV_KEY "force_ap_prov"

#ifndef BOARD_AUDIO_BOOT_TEST_BEEP
#define BOARD_AUDIO_BOOT_TEST_BEEP 0
#endif

#if BOARD_AUDIO_BOOT_TEST_BEEP
static void boot_speaker_test_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG_MAIN, "Speaker test tone start");
    i2s_output_start();
    i2s_output_set_volume(90);

    enum { CHUNK_SAMPLES = 240 };
    int16_t pcm[CHUNK_SAMPLES];
    int phase = 0;
    int period = BOARD_SPK_SAMPLE_RATE / 880;
    if (period < 2) {
        period = 2;
    }
    size_t remaining = (size_t)((BOARD_SPK_SAMPLE_RATE * 450) / 1000);
    while (remaining > 0) {
        size_t n = remaining > CHUNK_SAMPLES ? CHUNK_SAMPLES : remaining;
        for (size_t i = 0; i < n; i++) {
            pcm[i] = (phase < (period / 2)) ? 12000 : -12000;
            phase++;
            if (phase >= period) {
                phase = 0;
            }
        }
        int written = i2s_output_write_pcm16(pcm, n, 500);
        if (written <= 0) {
            ESP_LOGW(TAG_MAIN, "Speaker test tone write failed");
            break;
        }
        remaining -= (size_t)written;
    }
    i2s_output_stop();
    ESP_LOGI(TAG_MAIN, "Speaker test tone done");
    vTaskDelete(NULL);
}

static void boot_speaker_test_start(void) {
    BaseType_t ok = xTaskCreatePinnedToCore(
        boot_speaker_test_task, "spk_test", 3072, NULL,
        VIMATE_TASK_PRIO_BACKGROUND, NULL, VIMATE_TASK_CORE_IO);
    if (ok != pdPASS) {
        ESP_LOGW(TAG_MAIN, "Speaker test tone task start failed");
    }
}
#endif

/* Timeout phiên nghe dùng esp_timer (chạy trong task esp_timer có sẵn) THAY VÌ
 * tạo task riêng mỗi lần nghe. Bug cũ: xTaskCreate hết internal RAM → fallback
 * set bit timeout NGAY → cửa sổ nghe sập tức thì → "nói không nghe rõ". esp_timer
 * không cấp stack nên không OOM; timer tạo 1 lần, tái dùng. */
static volatile uint32_t s_listen_seq;       /* tăng mỗi lần cancel/arm mới */
static volatile uint32_t s_listen_timer_seq; /* seq mà lần arm hiện tại thuộc về */
static esp_timer_handle_t s_listen_timer = NULL;

static void listen_timeout_cb(void *arg) {
    (void)arg;
    /* Chỉ kích nếu phiên nghe chưa bị hủy/đổi kể từ lúc arm (chống race stop). */
    if (g_vimate_events && s_listen_timer_seq == s_listen_seq) {
        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_LISTEN_TIMEOUT);
    }
}

static void listen_timeout_cancel(void) {
    s_listen_seq++;
    if (s_listen_timer) {
        esp_timer_stop(s_listen_timer); /* INVALID_STATE nếu chưa chạy — bỏ qua */
    }
    if (g_vimate_events) {
        xEventGroupClearBits(g_vimate_events, VIMATE_EVT_LISTEN_TIMEOUT);
    }
}

static void listen_timeout_arm(int timeout_ms) {
    listen_timeout_cancel();
    if (timeout_ms <= 0) timeout_ms = LISTEN_MAX_MS;
    if (!s_listen_timer) {
        const esp_timer_create_args_t a = {
            .callback = listen_timeout_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "listen_to",
        };
        if (esp_timer_create(&a, &s_listen_timer) != ESP_OK) {
            s_listen_timer = NULL;
            /* Hiếm: không tạo được timer → KHÔNG kết thúc nghe ngay (tránh tái lập
             * bug cũ). Để VAD on-device + endpoint server tự chốt phiên nghe. */
            ESP_LOGW(TAG_MAIN, "listen timeout timer create failed — dựa VAD/endpoint");
            return;
        }
    }
    s_listen_timer_seq = s_listen_seq;
    esp_err_t r = esp_timer_start_once(s_listen_timer, (uint64_t)timeout_ms * 1000);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "listen timeout start_once: %s — dựa VAD/endpoint",
                 esp_err_to_name(r));
    }
}

static bool has_saved_wifi_credentials(void) {
    char ssid[33] = {0};
    esp_err_t status = nvs_store_get_wifi(ssid, sizeof(ssid), NULL, 0);
    return status == ESP_OK && ssid[0] != '\0';
}

static void log_provisioning_heap(const char *stage) {
    ESP_LOGI(TAG_MAIN, "Provisioning heap %s: internal=%u largest=%u PSRAM=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void enter_provisioning_mode(const char *reason) {
    ESP_LOGW(TAG_MAIN, "%s → enter provisioning mode", reason);
    telemetry_set_last_error(reason);

    uint32_t force_ap = 0;
    nvs_store_get_u32(NVS_FORCE_AP_PROV_KEY, &force_ap);

    esp_err_t ble_status = ESP_FAIL;
    esp_err_t ap_status = ESP_FAIL;
    log_provisioning_heap("before AP/BLE");
    if (force_ap) {
        nvs_store_erase(NVS_FORCE_AP_PROV_KEY);
        ui_home_show_message("Cài đặt WiFi",
                             "Đang bật WiFi và Bluetooth để cấu hình.");
        ap_status = wifi_mgr_start_provisioning();
        if (ap_status != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "Forced AP provisioning unavailable: %s",
                     esp_err_to_name(ap_status));
        }
        log_provisioning_heap("after AP, before BLE");
        ble_status = ble_wifi_prov_start();
        if (ble_status != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "Forced BLE provisioning unavailable: %s",
                     esp_err_to_name(ble_status));
        }
    } else {
        ui_home_show_message("Cài đặt WiFi",
                             "Mở app hoặc kết nối WiFi " VIMATE_SETUP_PREFIX " để cấu hình.");
        /* AP trước: WiFi driver cần internal RAM lớn khi init. Nếu bật BLE
         * trước, ESP32-S3 có thể không còn đủ DRAM cho WiFi static RX buffers
         * → AP fail ESP_ERR_NO_MEM, người dùng không thấy hotspot. */
        ap_status = wifi_mgr_start_provisioning();
        if (ap_status != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "AP provisioning unavailable: %s",
                     esp_err_to_name(ap_status));
        }
        log_provisioning_heap("after AP, before BLE");
        ble_status = ble_wifi_prov_start();
        if (ble_status != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "BLE provisioning unavailable: %s",
                     esp_err_to_name(ble_status));
        }
    }

    if (ble_status == ESP_OK || ap_status == ESP_OK) {
        ESP_LOGI(TAG_MAIN, "Provisioning mode active — BLE=%s AP=%s",
                 esp_err_to_name(ble_status), esp_err_to_name(ap_status));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGE(TAG_MAIN, "Provisioning start fail BLE=%s AP=%s — reboot sau 30s",
             esp_err_to_name(ble_status), esp_err_to_name(ap_status));
    vTaskDelay(pdMS_TO_TICKS(30000));
    esp_restart();
}

static void vimate_main_task(void *arg) {
    /* 1. Lấy MAC làm device-id, populate config */
    system_info_init();
    strlcpy(g_vimate_server.base_url, CONFIG_VIMATE_SERVER_BASE,
            sizeof(g_vimate_server.base_url));
    if (strncmp(g_vimate_server.base_url, "https://", 8) == 0) {
        strlcpy(g_vimate_server.ws_url, "wss://", sizeof(g_vimate_server.ws_url));
        strlcat(g_vimate_server.ws_url, g_vimate_server.base_url + 8, sizeof(g_vimate_server.ws_url));
    } else if (strncmp(g_vimate_server.base_url, "http://", 7) == 0) {
        strlcpy(g_vimate_server.ws_url, "ws://", sizeof(g_vimate_server.ws_url));
        strlcat(g_vimate_server.ws_url, g_vimate_server.base_url + 7, sizeof(g_vimate_server.ws_url));
    } else {
        strlcpy(g_vimate_server.ws_url, "wss://", sizeof(g_vimate_server.ws_url));
        strlcat(g_vimate_server.ws_url, g_vimate_server.base_url, sizeof(g_vimate_server.ws_url));
    }
    strlcat(g_vimate_server.ws_url, "/ws/", sizeof(g_vimate_server.ws_url));
    snprintf(g_vimate_server.ota_url, sizeof(g_vimate_server.ota_url),
             "%s/ota/v1/", g_vimate_server.base_url);
    /* Load device token (nếu đã activate trước) */
    nvs_store_get_str("dev_token", g_vimate_server.device_token,
                      sizeof(g_vimate_server.device_token));
    g_vimate_server.activated = (g_vimate_server.device_token[0] != '\0');
    if (!g_vimate_server.activated) {
        ota_client_restore_pending_activation();
    }
    ESP_LOGI(TAG_MAIN, "Device MAC=%s Activated=%d",
             g_vimate_server.mac_id, g_vimate_server.activated);

    /* First-time provisioning does not need codecs or media caches. Keep the
     * remaining internal heap contiguous for the WiFi and BLE controllers;
     * credential commit reboots into the normal full initialization path. */
    if (!has_saved_wifi_credentials()) {
        enter_provisioning_mode("No WiFi configured");
        return;
    }

    /* 2. WiFi — try saved creds, fallback to provisioning */
    /* Mount TOÀN BỘ storage NGAY ĐẦU khi heap ~231KB nguyên khối: SD (FATFS+
     * SDMMC cần block lớn) + 3 SPIFFS. Mount sau WiFi/TLS/UI bị NO_MEM lần
     * lượt từng cái (whack-a-mole 12/06) — sớm nhất = chắc chắn nhất. */
    esp_err_t course_cache_status = course_media_cache_init();
    (void)course_cache_status;
    emotion_sync_init();             /* mount /spiffs_emo (emoji asset core EDU) */
    asset_pack_init();
    lesson_image_cache_init();       /* EDU-only: SPIFFS cache ảnh bài học */

    ESP_LOGI(TAG_MAIN, "[1/4] WiFi connecting...");
    if (wifi_mgr_start() != ESP_OK) {
        enter_provisioning_mode("WiFi connect fail");
    }
    /* Wait WiFi up — max 60s, sau đó fallback */
    EventBits_t bits = xEventGroupWaitBits(g_vimate_events,
        VIMATE_EVT_WIFI_UP, pdFALSE, pdTRUE, pdMS_TO_TICKS(60000));
    if (!(bits & VIMATE_EVT_WIFI_UP)) {
        enter_provisioning_mode("WiFi không lên");
    }

    /* SNTP — sync clock TRƯỚC HTTPS/TLS. Cert verify check thời gian, nếu
     * clock = 1970 thì cert sẽ fail "not yet valid" → TLS handshake fail. */
    ESP_LOGI(TAG_MAIN, "SNTP syncing...");
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.start = true;
    esp_netif_sntp_init(&sntp_cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        time_t now = time(NULL);
        ESP_LOGI(TAG_MAIN, "SNTP synced: %s", ctime(&now));
    } else {
        ESP_LOGW(TAG_MAIN, "SNTP timeout — TLS cert verify có thể fail");
        telemetry_set_last_error("SNTP timeout");
    }

    (void)telemetry_try_upload_coredump();

    /* Mount TOÀN BỘ spiffs/cache SỚM (chỉ register VFS + probe SD, không mạng):
     * để sau OTA TLS thì heap phân mảnh → mount fail ESP_ERR_NO_MEM (12/06:
     * emo rồi lesson lần lượt dính) → emoji GIF/ảnh bài học không load. */
    /* (Toàn bộ storage đã mount NGAY ĐẦU app_main — trước WiFi/audio/UI.) */

    /* 3. OTA check (block — vài giây) */
    ESP_LOGI(TAG_MAIN, "[3/4] OTA check...");
    ui_home_show_state(DEV_STATE_OTA_CHECKING);
    esp_err_t ota_status = ota_client_check_once(); /* bản mới thành công sẽ tự reboot */
    ota_boot_validation_note_ota_api_result(ota_status);
    if (!g_vimate_server.activated &&
        g_vimate_server.activation_code[0] != '\0') {
        display_show_activation(g_vimate_server.activation_code, NULL);
        ota_client_start_activation_poll(g_vimate_server.activation_code);
    }

    /* (SD course cache đã mount NGAY ĐẦU app_main — trước WiFi/audio.) */

    ota_client_start_periodic_task();

    /* 5. WebSocket connect sớm khi internal heap còn liền khối. */
    ESP_LOGI(TAG_MAIN, "[4/4] WebSocket connecting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG_MAIN, "Heap before WS: internal=%u largest=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_err_t ws_status = ESP_ERR_INVALID_STATE;
    if (g_vimate_server.activated) {
        ws_status = ws_client_start();
        if (ws_status != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "WS start first try failed — retry after heap settle");
            vTaskDelay(pdMS_TO_TICKS(2000));
            ws_status = ws_client_start();
        }
        if (ws_status != ESP_OK) {
            ESP_LOGE(TAG_MAIN, "WS start fail — show error UI");
            telemetry_set_last_error("WS start fail");
            ui_home_show_message("Lỗi kết nối", "Không kết nối được tới máy chủ " VIMATE_BRAND_NAME ". Đang thử lại...");
        }
    } else {
        ESP_LOGI(TAG_MAIN, "Device not activated — skip authenticated WS until activation completes");
    }
    telemetry_start();

    /* 6. Defer HTTPS media sync. WS + audio cần internal RAM ổn định; chạy
     * TLS sync asset/lesson/emotion ngay sau khi WS lên làm AES thiếu internal
     * RAM, tụt heap và có thể rớt WS. Runtime image vẫn dùng SPIFFS cache đã
     * mount và HTTP fallback từng ảnh khi server gửi URL. */
    ESP_LOGI(TAG_MAIN, "Post-boot media sync deferred; heap now: internal=%u largest=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* 7. Loop chính — react event */
    ESP_LOGI(TAG_MAIN, "[4/4] Main loop");
    bool listening = false;
    bool wifi_reconfig_ble_active = false;
    bool wifi_reconfig_ap_active = false;
    TickType_t wifi_down_since = 0;
    TickType_t last_btn_tick = 0;
    TickType_t listen_started_tick = 0;
    TickType_t last_ws_restart_tick = 0;
    TickType_t planned_ws_refresh_until = 0;
    const TickType_t BTN_DEBOUNCE_MS = 1500;   /* 1.5s giữa các tap để tránh race */
    while (1) {
        EventBits_t e = xEventGroupWaitBits(g_vimate_events,
            VIMATE_EVT_WIFI_UP |
            VIMATE_EVT_WS_CONNECTED | VIMATE_EVT_WS_READY | VIMATE_EVT_WS_DISCONNECT |
            VIMATE_EVT_AUTH_REVOKED |
            VIMATE_EVT_BTN_PRESS | VIMATE_EVT_BTN_LONG | VIMATE_EVT_WIFI_DOWN |
            VIMATE_EVT_VAD_EOT | VIMATE_EVT_AUTO_LISTEN | VIMATE_EVT_WAKE_WORD |
            VIMATE_EVT_LISTEN_TIMEOUT | VIMATE_EVT_TTS_START |
            VIMATE_EVT_BARGE_IN | VIMATE_EVT_NAV_STOP | VIMATE_EVT_TTS_TIMEOUT,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        TickType_t now = xTaskGetTickCount();
        if (e & VIMATE_EVT_AUTH_REVOKED) {
            ESP_LOGW(TAG_MAIN, "Server revoked device identity — preserve WiFi and re-register");
            audio_pipeline_wake_stop();
            audio_pipeline_mic_stop();
            audio_pipeline_speaker_stop();
            listen_timeout_cancel();
            listening = false;
            (void)ws_client_stop();
            if (g_vimate_server.activation_code[0] != '\0') {
                display_show_activation(g_vimate_server.activation_code, NULL);
                ota_client_start_activation_poll(g_vimate_server.activation_code);
            } else {
                display_set_message("Cần kích hoạt lại",
                                    "Thiết bị đang đồng bộ với máy chủ. WiFi đã được giữ nguyên.");
            }
            ota_client_start_registration_recovery();
            continue;
        }
        /* Self-heal WS: nếu client CHƯA tạo được (ws_client_start fail cả 2 lần
         * lúc boot) thì thử lại mỗi vòng. Khi client đã tạo, để auto-reconnect chạy
         * trước; nếu vẫn down quá ngưỡng trong khi WiFi còn lên thì restart sạch. */
        if (g_vimate_server.activated && !ws_client_is_started()) {
            ws_client_start();
        } else if (g_vimate_server.activated && wifi_mgr_is_connected() &&
                   !ws_client_is_connected()) {
            uint32_t down_ms = ws_client_down_ms();
            bool restart_cooldown_ok = last_ws_restart_tick == 0 ||
                (uint32_t)((now - last_ws_restart_tick) * portTICK_PERIOD_MS) >=
                    WS_SUPERVISOR_RESTART_COOLDOWN_MS;
            if (down_ms >= WS_SUPERVISOR_GRACE_MS && restart_cooldown_ok) {
                ESP_LOGW(TAG_MAIN,
                         "WS supervisor: down %ums with WiFi up — restart client",
                         (unsigned)down_ms);
                last_ws_restart_tick = now;
                esp_err_t wr = ws_client_restart("main-supervisor-down");
                if (wr != ESP_OK) {
                    ESP_LOGW(TAG_MAIN, "WS supervisor restart failed: %s",
                             esp_err_to_name(wr));
                }
            }
        }
#if WS_REFRESH_BEFORE_HARD_DROP_MS > 0
        else if (g_vimate_server.activated && wifi_mgr_is_connected() &&
                   ws_client_is_connected() && !listening &&
                   !audio_pipeline_speaker_is_active()) {
            uint32_t connected_ms = ws_client_connected_ms();
            bool restart_cooldown_ok = last_ws_restart_tick == 0 ||
                (uint32_t)((now - last_ws_restart_tick) * portTICK_PERIOD_MS) >=
                    WS_SUPERVISOR_RESTART_COOLDOWN_MS;
            if (connected_ms >= WS_REFRESH_BEFORE_HARD_DROP_MS &&
                restart_cooldown_ok) {
                ESP_LOGW(TAG_MAIN,
                         "WS scheduled refresh before hard drop: connected=%ums",
                         (unsigned)connected_ms);
                last_ws_restart_tick = now;
                planned_ws_refresh_until =
                    now + pdMS_TO_TICKS(WS_REFRESH_DISCONNECT_GRACE_MS);
                esp_err_t wr = ws_client_restart("scheduled-refresh");
                if (wr != ESP_OK) {
                    ESP_LOGW(TAG_MAIN, "WS scheduled refresh failed: %s",
                             esp_err_to_name(wr));
                }
            }
        }
#endif
        if (e & VIMATE_EVT_WIFI_UP) {
            ESP_LOGI(TAG_MAIN, "WiFi UP");
            wifi_down_since = 0;
            if (wifi_reconfig_ble_active) {
                ESP_LOGI(TAG_MAIN, "WiFi recovered — stop BLE reconfiguration");
                ble_wifi_prov_stop();
                wifi_reconfig_ble_active = false;
            }
            if (wifi_reconfig_ap_active) {
                ESP_LOGI(TAG_MAIN, "WiFi recovered — stop AP provisioning");
                wifi_mgr_stop_provisioning();
                wifi_reconfig_ap_active = false;
            }
            if (ws_client_is_protocol_ready() && g_vimate_server.activated) {
                ui_home_show_state(DEV_STATE_READY);
            }
        }
        if (e & VIMATE_EVT_WS_READY) {
            planned_ws_refresh_until = 0;
            /* Chưa kích hoạt: server reply not_activated rồi đóng WS. KHÔNG hiện
             * "Sẵn sàng"/mặt neutral/bật wake — giữ màn nhập mã (envelope
             * not_activated gọi display_show_activation). Tránh nhấp nháy
             * Sẵn-sàng↔mã↔Lỗi khi thiết bị chưa bind. */
            if (!g_vimate_server.activated) {
                ESP_LOGI(TAG_MAIN, "WS connected nhưng CHƯA kích hoạt — giữ màn nhập mã, không vào READY");
            } else {
                ESP_LOGI(TAG_MAIN, "WS protocol ready — ready for sessions");
                ui_home_activate();
                ui_home_show_state(DEV_STATE_READY);
                /* Emotion neutral full màn — chỉ show khi WS sẵn sàng để không
                 * đè WiFi setup QR screen lúc provisioning. */
                ui_emotion_show(EMOTION_NEUTRAL);
                audio_pipeline_wake_start();
                /* EDU: chỉ sync SD course cache nếu thẻ sẵn sàng. Không prefetch
                 * toàn bộ ảnh bài học sau boot: bulk HTTPS + SPIFFS trên GENU v6
                 * từng làm nghẽn WakeNet/LVGL và gây task watchdog. Ảnh bước học
                 * vẫn tải theo nhu cầu khi server gửi URL hoặc JPG qua WS. */
                course_media_cache_sync_async();
#if CONFIG_VIMATE_LESSON_IMAGE_BACKGROUND_SYNC
                lesson_image_cache_sync_async();
#else
                ESP_LOGI(TAG_MAIN, "lesson image background sync disabled; on-demand image loading remains active");
#endif
            }
        }
        if (e & VIMATE_EVT_WS_DISCONNECT) {
            bool planned_refresh = planned_ws_refresh_until != 0 &&
                now <= planned_ws_refresh_until;
            if (planned_refresh) {
                ESP_LOGI(TAG_MAIN, "WS scheduled refresh disconnect — waiting reconnect");
                continue;
            }
            ESP_LOGW(TAG_MAIN, "WS DOWN");
            /* WS có reconnect định kỳ/ping timeout là trạng thái transient,
             * không phải "lỗi cuối" cho phụ huynh. Giữ last_error cho lỗi
             * thật như WiFi, OTA, crash/coredump. */
            audio_pipeline_wake_stop();
            audio_pipeline_mic_stop();
            audio_pipeline_speaker_stop();
            listen_timeout_cancel();
            listening = false;
            /* Chưa kích hoạt: server chủ động đóng WS sau not_activated — KHÔNG
             * phải lỗi. Giữ màn nhập mã, đừng hiện "Lỗi kết nối". */
            if (g_vimate_server.activated) {
                ui_home_show_state(DEV_STATE_ERROR);
            } else {
                ESP_LOGI(TAG_MAIN, "WS đóng (chưa kích hoạt) — giữ màn nhập mã");
            }
        }
        if (e & VIMATE_EVT_WIFI_DOWN) {
            ESP_LOGW(TAG_MAIN, "WiFi DOWN — stop active audio");
            telemetry_set_last_error("WiFi disconnected");
            audio_pipeline_wake_stop();
            audio_pipeline_mic_stop();
            audio_pipeline_speaker_stop();
            listen_timeout_cancel();
            listening = false;
            ui_home_show_state(DEV_STATE_ERROR);
            /* Giữ mốc của lần rớt đầu tiên. Mỗi retry đều phát WIFI_DOWN; nếu
             * reset mốc ở đây thì ngưỡng BLE 2 phút/AP 5 phút không bao giờ tới. */
            if (wifi_down_since == 0) {
                wifi_down_since = now;
            }
        }
        if (!wifi_mgr_is_connected()) {
            if (wifi_down_since == 0) {
                wifi_down_since = now;
            }
            if (!wifi_reconfig_ble_active &&
                (now - wifi_down_since) >= pdMS_TO_TICKS(WIFI_RECONFIG_BLE_DELAY_MS)) {
                ESP_LOGW(TAG_MAIN, "WiFi down for %d ms — enable BLE reconfiguration",
                         WIFI_RECONFIG_BLE_DELAY_MS);
                if (ble_wifi_prov_start() == ESP_OK) {
                    wifi_reconfig_ble_active = true;
                    ui_home_show_message("Cấu hình lại WiFi",
                                         "Mở app " VIMATE_BRAND_NAME ", vào thiết bị và gửi WiFi mới qua Bluetooth.");
                }
            }
            if (!wifi_reconfig_ap_active &&
                (now - wifi_down_since) >= pdMS_TO_TICKS(WIFI_RECONFIG_AP_FALLBACK_MS)) {
                ESP_LOGW(TAG_MAIN, "WiFi down for %d ms — enable AP provisioning fallback",
                         WIFI_RECONFIG_AP_FALLBACK_MS);
                ui_home_show_message("Cấu hình WiFi",
                                     "Đang bật WiFi cấu hình trên thiết bị.");
                if (wifi_mgr_start_provisioning() == ESP_OK) {
                    wifi_reconfig_ap_active = true;
                }
            }
        } else {
            wifi_down_since = 0;
            if (wifi_reconfig_ble_active) {
                ESP_LOGI(TAG_MAIN, "WiFi connected — stop BLE reconfiguration");
                ble_wifi_prov_stop();
                wifi_reconfig_ble_active = false;
            }
            if (wifi_reconfig_ap_active) {
                ESP_LOGI(TAG_MAIN, "WiFi connected — stop AP provisioning");
                wifi_mgr_stop_provisioning();
                wifi_reconfig_ap_active = false;
            }
        }
        if (e & VIMATE_EVT_NAV_STOP) {
            listen_timeout_cancel();
            listening = false;
            listen_started_tick = 0;
            audio_pipeline_mic_stop();
            audio_pipeline_speaker_stop();
            ESP_LOGI(TAG_MAIN, "Navigation stop → listen/audio idle");
        }
        /* Trợ lý cá nhân: wake-word "Hi Lily" hoặc ô "Trợ lý" (AUTO_LISTEN) → mở lượt
         * nghe → ASR → LLM → TTS. */
        if (e & (VIMATE_EVT_AUTO_LISTEN | VIMATE_EVT_WAKE_WORD)) {
            bool from_wake = (e & VIMATE_EVT_WAKE_WORD) != 0;
            if (!ws_client_is_connected()) {
                ESP_LOGW(TAG_MAIN, "Bỏ qua listen — WS chưa kết nối");
            } else if (!from_wake && !display_ai_active()) {
                ESP_LOGI(TAG_MAIN, "Bỏ qua auto-listen cũ — đã thoát AI");
            } else if (!listening) {
                audio_pipeline_wake_stop();
                esp_err_t r = envelope_send_listen_start_ex(from_wake);
                ESP_LOGI(TAG_MAIN, "%s envelope_send_listen_start → %s",
                         from_wake ? "wake" : "auto", esp_err_to_name(r));
                audio_pipeline_mic_start();
                audio_pipeline_mic_mute(false);
                ui_home_show_state(DEV_STATE_LISTENING);
                listening = true;
                listen_started_tick = now;
                listen_timeout_arm(LISTEN_MAX_MS);
            } else {
                audio_pipeline_mic_mute(false);
                ui_home_show_state(DEV_STATE_LISTENING);
                listen_timeout_arm(LISTEN_MAX_MS);
            }
        }
        if (e & VIMATE_EVT_BARGE_IN) {
            /* Voice barge-in (trẻ nói khi đang phát TTS) — nguồn: AFE fetch task
             * (VAD trên giọng ĐÃ khử echo, debounce sẵn) hoặc barge_in.c standalone
             * (khi CONFIG bật). Chuỗi như tap barge-in: báo server hủy TTS + dừng
             * loa + mở mic nghe. */
            ESP_LOGI(TAG_MAIN, "BARGE-IN: trẻ nói khi robot đang phát → ngắt TTS + nghe");
            if (ws_client_is_connected() && !listening) {
                if (audio_pipeline_speaker_is_active()) {
                    envelope_send_abort();
                    audio_pipeline_speaker_stop();
                    vTaskDelay(pdMS_TO_TICKS(80));
                }
                audio_pipeline_wake_stop();
                esp_err_t r = envelope_send_listen_start();
                ESP_LOGI(TAG_MAIN, "barge-in envelope_send_listen_start → %s",
                         esp_err_to_name(r));
                audio_pipeline_mic_start();
                audio_pipeline_mic_mute(false);
                ui_home_show_state(DEV_STATE_LISTENING);
                listening = true;
                listen_started_tick = now;
                listen_timeout_arm(LISTEN_MAX_MS);
            }
        }
        if (e & VIMATE_EVT_TTS_START) {
            if (listening) {
                ESP_LOGI(TAG_MAIN, "TTS start while listening — park listen turn");
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            }
        }
        if (e & VIMATE_EVT_TTS_TIMEOUT) {
            ESP_LOGE(TAG_MAIN, "TTS watchdog recovery -> abort stale turn + WakeNet");
            listen_timeout_cancel();
            listening = false;
            listen_started_tick = 0;
            audio_pipeline_mic_stop();
            audio_pipeline_mic_mute(false);
            if (ws_client_is_connected()) {
                (void)envelope_send_abort();
                audio_pipeline_wake_start();
                ui_home_show_state(DEV_STATE_READY);
            }
        }
        if (e & VIMATE_EVT_BTN_PRESS) {
            /* Debounce: bỏ qua tap trong vòng 1.5s từ tap trước (tránh race
             * Mic OFF→ON liên tiếp khi user tap nhanh → crash LoadProhibited). */
            TickType_t now = xTaskGetTickCount();
            if ((now - last_btn_tick) * portTICK_PERIOD_MS < BTN_DEBOUNCE_MS) {
                ESP_LOGW(TAG_MAIN, "BTN_PRESS bỏ qua (debounce)");
                continue;
            }
            last_btn_tick = now;
            ESP_LOGI(TAG_MAIN, "BTN_PRESS event — listening=%d, ws=%d",
                     listening, ws_client_is_connected());
            if (!ws_client_is_connected()) {
                ESP_LOGW(TAG_MAIN, "Bỏ qua tap — WS chưa kết nối");
                ui_home_show_message("Chờ kết nối", "Đang kết nối tới máy chủ, thử lại sau...");
                continue;
            }
            if (!listening) {
                if (audio_pipeline_speaker_is_active()) {
                    ESP_LOGI(TAG_MAIN, "Barge-in: abort speaking before listen");
                    envelope_send_abort();
                    audio_pipeline_speaker_stop();
                    vTaskDelay(pdMS_TO_TICKS(80));
                }
                audio_pipeline_wake_stop();
                esp_err_t r = envelope_send_listen_start();
                ESP_LOGI(TAG_MAIN, "envelope_send_listen_start → %s",
                         esp_err_to_name(r));
                audio_pipeline_mic_start();
                /* QUAN TRỌNG: unmute mic SEND. Có thể đang muted từ TTS
                 * trước (envelope.c set mute=true khi tts state=start).
                 * Nếu user tap trong lúc TTS chưa stop, mic stay muted
                 * → bytes=0 gửi lên server → ASR trả rỗng. */
                audio_pipeline_mic_mute(false);
                ui_home_show_state(DEV_STATE_LISTENING);
                listening = true;
                listen_started_tick = now;
                listen_timeout_arm(LISTEN_MAX_MS);
            } else {
                uint32_t listened_ms = (now - listen_started_tick) * portTICK_PERIOD_MS;
                uint32_t frames = audio_pipeline_mic_streamed_frames();
                if (listened_ms < LISTEN_MIN_MANUAL_STOP_MS &&
                    frames < LISTEN_MIN_MANUAL_STOP_FRAMES) {
                    ESP_LOGW(TAG_MAIN,
                             "BTN stop ignored: too early (%ums, frames=%u)",
                             (unsigned)listened_ms, (unsigned)frames);
                    continue;
                }
                /* Mic stop sẽ stop i2s. Vẫn cần unmute trước để chắc chắn
                 * frame cuối cùng đã encode + flush trước khi i2s tắt. */
                audio_pipeline_mic_mute(false);
                audio_pipeline_mic_stop();
                esp_err_t r = envelope_send_listen_stop();
                ESP_LOGI(TAG_MAIN, "envelope_send_listen_stop → %s",
                         esp_err_to_name(r));
                ui_home_show_state(DEV_STATE_THINKING);
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            }
        }
        if (e & VIMATE_EVT_BTN_LONG) {
            ESP_LOGW(TAG_MAIN, "Long press — clear WiFi creds and force AP setup");
            nvs_store_erase("wifi_cfg");    /* blob mới */
            nvs_store_erase("wifi_ssid");   /* key cũ (thiết bị OTA) */
            nvs_store_erase("wifi_pass");
            nvs_store_set_u32(NVS_FORCE_AP_PROV_KEY, 1);
            ui_home_show_message("Cài đặt WiFi",
                                 "Đang bật WiFi " VIMATE_SETUP_PREFIX " để cấu hình...");
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_restart();
        }
        if (e & VIMATE_EVT_VAD_EOT) {
            /* VAD trong mic_task đã detect end-of-turn (silence > 1.5s sau
             * khi nói). Gửi listen_stop từ main task (network priority) để
             * tránh DMA ISR storm khi send từ mic_task. WakeNet idle sẽ bật
             * lại sau khi server trả lời xong và quay về READY. */
            if (listening && audio_pipeline_speaker_is_active()) {
                ESP_LOGI(TAG_MAIN, "VAD EOT ignored while TTS speaking");
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            } else if (listening && ws_client_is_connected()) {
                ESP_LOGI(TAG_MAIN, "VAD EOT → listen_stop");
                audio_pipeline_mic_mute(false);
                esp_err_t r = envelope_send_listen_stop();
                ESP_LOGI(TAG_MAIN, "VAD listen_stop → %s", esp_err_to_name(r));
                audio_pipeline_mic_stop();
                ui_home_show_state(DEV_STATE_THINKING);
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            }
        }
        if (e & VIMATE_EVT_LISTEN_TIMEOUT) {
            if (listening && audio_pipeline_speaker_is_active()) {
                ESP_LOGI(TAG_MAIN, "Listen timeout ignored while TTS speaking");
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            } else if (listening) {
                ESP_LOGW(TAG_MAIN, "Listen timeout %dms → listen_stop", LISTEN_MAX_MS);
                audio_pipeline_mic_mute(false);
                if (ws_client_is_connected()) {
                    esp_err_t r = envelope_send_listen_stop();
                    ESP_LOGI(TAG_MAIN, "timeout listen_stop → %s", esp_err_to_name(r));
                }
                audio_pipeline_mic_stop();
                ui_home_show_state(DEV_STATE_THINKING);
                listen_timeout_cancel();
                listening = false;
                listen_started_tick = 0;
            }
        }
    }
}

void vimate_app_start(void) {
    /* NVS init (required first). Bắt MỌI lỗi (không chỉ NO_FREE_PAGES/NEW_VERSION):
     * corruption CRC do cúp điện giữa lúc ghi cũng erase + reinit để KHÔNG panic
     * → reboot loop. Erase mất creds → thiết bị rơi về provisioning (phục hồi
     * được), tốt hơn brick-loop. */
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "nvs_flash_init: %s — erase + reinit", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_store_init();
    telemetry_install_json_hooks();
    diagnostics_log_boot();
    const bool provisioning_boot = !has_saved_wifi_credentials();
    (void)provisioning_boot;   /* chi dung khi BOARD_AUDIO_RUNTIME_ENABLE */

    /* Event loop + netif (required cho WiFi) */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    /* Global event group */
    g_vimate_events = xEventGroupCreate();
    configASSERT(g_vimate_events);
    diagnostics_start();

    /* Init hardware: LCD + audio + buttons.
     * Boot-safety: lỗi LCD/audio KHÔNG được làm chết boot — log + tiếp tục để
     * WiFi/WS/OTA vẫn lên, cứu được thiết bị từ xa khi HW/firmware lỗi. */
    bool display_ok = (display_init() == ESP_OK);
    if (!display_ok) {
        ESP_LOGE(TAG_MAIN, "display_init lỗi — boot tiếp KHÔNG màn (giữ WS/OTA)");
    }
    bool audio_ok = true;
#if !BOARD_AUDIO_RUNTIME_ENABLE
    /* Board chua xac nhan pinout audio (vd. P4 bring-up) -> bo qua init.
     * Toan bo source audio van bien dich; chi khong dung phan cung. */
    ESP_LOGW(TAG_MAIN, "Audio runtime disabled by board profile");
#else
    if (provisioning_boot) {
        ESP_LOGI(TAG_MAIN,
                 "No saved WiFi — defer audio/cache until provisioning reboot");
    } else {
        audio_ok = (audio_pipeline_init() == ESP_OK);
        if (!audio_ok) {
            ESP_LOGE(TAG_MAIN, "audio_pipeline_init lỗi — boot tiếp KHÔNG audio (giữ WS/OTA)");
        }
    }
#endif /* BOARD_AUDIO_RUNTIME_ENABLE */
#if BOARD_AUDIO_BOOT_TEST_BEEP
    boot_speaker_test_start();
#endif
    button_init();
#if BOARD_TOUCH_RUNTIME_ENABLE
    touch_init();   /* FT6236G touch — tap màn = BTN_PRESS */
#else
    ESP_LOGW(TAG_MAIN, "Touch runtime disabled by board profile");
#endif
    esp_err_t servo_status = servo_emotion_init();
    if (servo_status != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "Emotion servo disabled: %s", esp_err_to_name(servo_status));
    }

    /* Setup UI CHỈ khi display lên OK (tránh dùng lvgl_port chưa init → crash).
     * Nếu display lỗi: display_set_state/display_schedule tự no-op (sched_q NULL). */
    if (display_ok) {
        display_setup_ui();
        display_set_state(DEV_STATE_BOOT);
    }

    esp_err_t validation_status = ota_boot_validation_start(display_ok, audio_ok);
    if (validation_status != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "không khởi động được OTA rollback guard: %s",
                 esp_err_to_name(validation_status));
    }

    /* Main task — network/control priority; giữ stack vừa đủ để trả DRAM cho TLS/WS. */
    /* Main task pin Core 0 — chỉ xử lý event UI/network, đỡ tranh CPU
     * với audio task Core 1. */
    xTaskCreatePinnedToCore(vimate_main_task, "vimate_main", 6144, NULL,
                            VIMATE_TASK_PRIO_NETWORK, NULL, VIMATE_TASK_CORE_UI);
}
