/**
 * telemetry.c — runtime heartbeat + crash dump upload.
 *
 * JSON allocations are steered to PSRAM so large cJSON trees do not eat the
 * internal DRAM needed by WiFi/BLE/I2S DMA and LVGL.
 */
#include "telemetry.h"
#include "vimate.h"

#include "core/task_profile.h"
#include "network/ws_client.h"
#include "core/wifi_mgr.h"
#include "ui/display.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif

#define HEARTBEAT_INTERVAL_MS 20000
#define COREDUMP_UPLOAD_CHUNK 4096

static char s_last_error[96];
static bool s_heartbeat_success;
static portMUX_TYPE s_last_error_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_json_hooks_installed = false;

static void *json_malloc(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void json_free(void *ptr) {
    heap_caps_free(ptr);
}

void telemetry_install_json_hooks(void) {
    if (s_json_hooks_installed) return;
    cJSON_Hooks hooks = {
        .malloc_fn = json_malloc,
        .free_fn = json_free,
    };
    cJSON_InitHooks(&hooks);
    s_json_hooks_installed = true;
    ESP_LOGI(TAG_MAIN, "cJSON allocator: PSRAM preferred");
}

void telemetry_set_last_error(const char *msg) {
    portENTER_CRITICAL(&s_last_error_lock);
    if (msg && msg[0]) {
        strlcpy(s_last_error, msg, sizeof(s_last_error));
    } else {
        s_last_error[0] = 0;
    }
    portEXIT_CRITICAL(&s_last_error_lock);
}

bool telemetry_has_successful_heartbeat(void) {
    bool sent;
    portENTER_CRITICAL(&s_last_error_lock);
    sent = s_heartbeat_success;
    portEXIT_CRITICAL(&s_last_error_lock);
    return sent;
}

static int wifi_rssi(void) {
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

static esp_err_t send_heartbeat_once(void) {
    if (!ws_client_is_connected()) return ESP_ERR_INVALID_STATE;

    int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
    char json[384];
    int n = snprintf(json, sizeof(json),
                     "{\"type\":\"heartbeat\",\"fw_version\":\"%s\","
                     "\"uptime_sec\":%lld,\"rssi\":%d,"
                     "\"heap_internal_free\":%u,\"heap_internal_min\":%u,"
                     "\"psram_free\":%u,\"psram_min\":%u,"
                     "\"display_hw_ready\":%s,\"display_hdmi_ready\":%s,"
                     "\"display_status\":\"%s\"}",
                     VIMATE_FW_VERSION,
                     (long long)uptime_sec,
                     wifi_rssi(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                     display_hw_ready() ? "true" : "false",
                     display_hdmi_ready() ? "true" : "false",
                     display_runtime_status());
    if (n <= 0 || n >= (int)sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ws_client_send_text(json, (size_t)n);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_last_error_lock);
        s_heartbeat_success = true;
        portEXIT_CRITICAL(&s_last_error_lock);
        ESP_LOGI(TAG_MAIN, "heartbeat sent uptime=%llds internal=%u",
                 (long long)uptime_sec,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    } else {
        ESP_LOGW(TAG_MAIN, "heartbeat send failed err=%s", esp_err_to_name(err));
    }
    return err;
}

static void heartbeat_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));
    /* Dùng chính task này (không tốn stack mới — RAM nội P4 chật) để nuôi icon sóng
     * WiFi: 3 s/lần đọc RSSI (RPC sang C5 trên P4, vài ms), heartbeat vẫn 20 s/lần. */
    const int rssi_period_ms = 3000;
    int elapsed_ms = HEARTBEAT_INTERVAL_MS;   /* gửi heartbeat ngay vòng đầu */
    while (1) {
        if (elapsed_ms >= HEARTBEAT_INTERVAL_MS) {
            (void)send_heartbeat_once();
            elapsed_ms = 0;
        }
        bool up = wifi_mgr_is_connected();
        display_set_wifi_rssi(up ? wifi_rssi() : 0, up);
        vTaskDelay(pdMS_TO_TICKS(rssi_period_ms));
        elapsed_ms += rssi_period_ms;
    }
}

esp_err_t telemetry_start(void) {
    BaseType_t ok = xTaskCreatePinnedToCore(heartbeat_task, "heartbeat",
                                            4096, NULL,
                                            VIMATE_TASK_PRIO_HEARTBEAT, NULL,
                                            VIMATE_TASK_CORE_UI);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t telemetry_try_upload_coredump(void) {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    if (g_vimate_server.device_token[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_core_dump_image_check() != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t flash_addr = 0;
    size_t dump_size = 0;
    esp_err_t err = esp_core_dump_image_get(&flash_addr, &dump_size);
    if (err != ESP_OK || dump_size == 0) {
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!part || part->address != flash_addr) {
        return ESP_ERR_NOT_FOUND;
    }

    char url[240];
    snprintf(url, sizeof(url), "%s/api/device-coredumps/%s",
             g_vimate_server.base_url, g_vimate_server.mac_id);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;

    char auth[120];
    snprintf(auth, sizeof(auth), "Bearer %s", g_vimate_server.device_token);
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_header(cli, "Device-Id", g_vimate_server.mac_id);
    esp_http_client_set_header(cli, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(cli, "X-FW-Version", VIMATE_FW_VERSION);

    err = esp_http_client_open(cli, dump_size);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }

    uint8_t *buf = heap_caps_malloc(COREDUMP_UPLOAD_CHUNK,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(COREDUMP_UPLOAD_CHUNK);
    if (!buf) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }

    size_t sent = 0;
    while (sent < dump_size) {
        size_t n = dump_size - sent;
        if (n > COREDUMP_UPLOAD_CHUNK) n = COREDUMP_UPLOAD_CHUNK;
        err = esp_partition_read(part, sent, buf, n);
        if (err != ESP_OK) break;
        int written = esp_http_client_write(cli, (const char *)buf, n);
        if (written != (int)n) {
            err = ESP_FAIL;
            break;
        }
        sent += n;
    }
    heap_caps_free(buf);

    int status = 0;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(cli);
        status = esp_http_client_get_status_code(cli);
        if (status < 200 || status >= 300) {
            err = ESP_FAIL;
        }
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

    if (err == ESP_OK) {
        ESP_LOGW(TAG_MAIN, "coredump uploaded (%u bytes), erase local copy",
                 (unsigned)dump_size);
        esp_core_dump_image_erase();
    } else {
        ESP_LOGW(TAG_MAIN, "coredump upload failed err=%s status=%d sent=%u/%u",
                 esp_err_to_name(err), status, (unsigned)sent,
                 (unsigned)dump_size);
        telemetry_set_last_error("coredump upload failed");
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
