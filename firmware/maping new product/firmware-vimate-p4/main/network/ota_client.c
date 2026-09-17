/**
 * ota_client.c — OTA flow:
 *
 *   1. POST /ota/v1/ với headers Device-Id + Application + body version
 *   2. Parse JSON response: firmware.{version, url, force}, websocket.url
 *   3. Compare version với VIMATE_FW_VERSION
 *   4. Khác + url hợp lệ → esp_https_ota() download + flash + reboot
 *
 * Verify SHA256 sau download (esp_https_ota internal hỗ trợ image header check).
 */
#include "ota_client.h"
#include "vimate.h"
#include "system_info.h"
#include "boards/board.h"
#include "core/nvs_store.h"
#include "core/task_profile.h"
#include "audio/audio_pipeline.h"
#include "ui/display.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/portmacro.h"
#include <string.h>

#define OTA_POST_TIMEOUT_MS 10000
#define OTA_MIN_INTERNAL_FREE_BYTES   (32 * 1024)
#define OTA_MIN_INTERNAL_LARGEST_BLOCK (12 * 1024)
#define ACTIVATION_POLL_FAST_INTERVAL_MS 5000
#define ACTIVATION_POLL_SLOW_INTERVAL_MS 30000
#define ACTIVATION_POLL_FAST_TRIES       60
#define NVS_PENDING_ACTIVATION_KEY       "act_code"
#define REGISTRATION_RECOVERY_INTERVAL_MS 10000
#define REGISTRATION_RECOVERY_MAX_TRIES   60

/* Buffer cho response body — đủ cho JSON ~2KB */
static char s_rx_buf[2048];
static int  s_rx_len = 0;
static portMUX_TYPE s_ota_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_ota_running = false;
static portMUX_TYPE s_activation_poll_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_activation_poll_running = false;
static portMUX_TYPE s_registration_recovery_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_registration_recovery_running = false;

static void activation_code_clear(void) {
    esp_err_t er = nvs_store_erase(NVS_PENDING_ACTIVATION_KEY);
    if (er != ESP_OK && er != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG_OTA, "erase pending activation code: %s", esp_err_to_name(er));
    }
    g_vimate_server.activation_code[0] = '\0';
}

static bool ota_try_begin(void) {
    bool ok = false;
    portENTER_CRITICAL(&s_ota_mux);
    if (!s_ota_running) {
        s_ota_running = true;
        ok = true;
    }
    portEXIT_CRITICAL(&s_ota_mux);
    return ok;
}

static void ota_end(void) {
    portENTER_CRITICAL(&s_ota_mux);
    s_ota_running = false;
    portEXIT_CRITICAL(&s_ota_mux);
}

static esp_err_t on_http_event(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy = evt->data_len;
        if (s_rx_len + copy > (int)sizeof(s_rx_buf) - 1) copy = sizeof(s_rx_buf) - 1 - s_rx_len;
        if (copy > 0) {
            memcpy(s_rx_buf + s_rx_len, evt->data, copy);
            s_rx_len += copy;
            s_rx_buf[s_rx_len] = 0;
        }
    } else if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        s_rx_len = 0;
    }
    return ESP_OK;
}

esp_err_t ota_client_check_once(void) {
    if (!ota_try_begin()) {
        ESP_LOGW(TAG_OTA, "OTA check skipped: another OTA check is running");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ESP_OK;
    cJSON *root = NULL;

    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (internal_free < OTA_MIN_INTERNAL_FREE_BYTES ||
        internal_largest < OTA_MIN_INTERNAL_LARGEST_BLOCK) {
        ESP_LOGW(TAG_OTA,
                 "OTA check skipped: low internal heap free=%u largest=%u",
                 (unsigned)internal_free, (unsigned)internal_largest);
        ret = ESP_ERR_NO_MEM;
        goto done;
    }

    /* panel_res cho server biết kích thước LCD của thiết bị
     * (320×240 cho 2.8", 480×320 cho 3.5", ...). */
    char body[320];
    snprintf(body, sizeof(body),
        "{\"version\":\"%s\",\"application\":{\"version\":\"%s\"},"
        "\"mac_address\":\"%s\",\"chip_model_name\":\"%s\","
        "\"panel_res\":\"%dx%d\"}",
        VIMATE_FW_VERSION, VIMATE_FW_VERSION, g_vimate_server.mac_id,
        system_info_get_chip_model(), BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    esp_http_client_config_t cfg = {
        .url = g_vimate_server.ota_url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http_event,
        .timeout_ms = OTA_POST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach, /* trust public CA roots */
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        ret = ESP_FAIL;
        goto done;
    }
    esp_http_client_set_header(cli, "Device-Id", g_vimate_server.mac_id);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));

    s_rx_len = 0;
    esp_err_t r = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    if (r != ESP_OK || status != 200) {
        ESP_LOGW(TAG_OTA, "OTA check fail: err=%s status=%d", esp_err_to_name(r), status);
        ret = ESP_FAIL;
        goto done;
    }
    ESP_LOGI(TAG_OTA, "OTA response (%d bytes): %.*s", s_rx_len, s_rx_len, s_rx_buf);

    /* Parse: { data: { firmware: { version, url, force }, websocket: { url, token } } } */
    root = cJSON_Parse(s_rx_buf);
    if (!root) {
        ESP_LOGE(TAG_OTA, "OTA: parse JSON fail");
        ret = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;   /* tùy server có wrap {data:...} hay không */

    cJSON *fw = cJSON_GetObjectItem(data, "firmware");
    cJSON *ws = cJSON_GetObjectItem(data, "websocket");
    cJSON *activation = cJSON_GetObjectItem(data, "activation");
    if (activation && !g_vimate_server.activated) {
        cJSON *code = cJSON_GetObjectItem(activation, "code");
        cJSON *message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(code) && code->valuestring[0]) {
            strlcpy(g_vimate_server.activation_code, code->valuestring,
                    sizeof(g_vimate_server.activation_code));
            display_show_activation(
                g_vimate_server.activation_code,
                cJSON_IsString(message) ? message->valuestring : NULL);
            ota_client_start_activation_poll(g_vimate_server.activation_code);
            ESP_LOGI(TAG_OTA, "Activation code shown from OTA response: %s",
                     g_vimate_server.activation_code);
        }
    }
    if (ws) {
        cJSON *url = cJSON_GetObjectItem(ws, "url");
        if (cJSON_IsString(url) && url->valuestring[0]) {
            /* Bảo mật: CHỈ nhận wss:// — chống downgrade sang ws:// plaintext
             * (MITM chèn lệnh/audio xuống thiết bị). */
            if (strncmp(url->valuestring, "wss://", 6) == 0) {
                strlcpy(g_vimate_server.ws_url, url->valuestring, sizeof(g_vimate_server.ws_url));
                ESP_LOGI(TAG_OTA, "WS URL updated: %s", g_vimate_server.ws_url);
            } else {
                ESP_LOGW(TAG_OTA, "Bỏ qua WS URL không phải wss:// : %s", url->valuestring);
            }
        }
    }
    cJSON *stime = cJSON_GetObjectItem(data, "server_time");
    if (stime) {
        /* Đồng bộ giờ cho header home (SNTP hay bị chặn) — VN offset=420 (UTC+7). */
        cJSON *ts = cJSON_GetObjectItem(stime, "timestamp");
        cJSON *tz = cJSON_GetObjectItem(stime, "timezone_offset");
        if (cJSON_IsNumber(ts)) {
            display_set_server_time((int64_t)ts->valuedouble,
                                    cJSON_IsNumber(tz) ? (int)tz->valuedouble : 420);
        }
    }
    if (fw) {
        cJSON *vj = cJSON_GetObjectItem(fw, "version");
        cJSON *uj = cJSON_GetObjectItem(fw, "url");
        cJSON *fj = cJSON_GetObjectItem(fw, "force");
        if (cJSON_IsString(vj) && cJSON_IsString(uj) && uj->valuestring[0]) {
            const char *new_ver = vj->valuestring;
            const char *new_url = uj->valuestring;
            bool force = cJSON_IsNumber(fj) && fj->valueint == 1;
            /* Defensive: ignore OTA pointing to legacy xiaozhi binary path —
             * server có thể đang trả OTA của firmware cũ. Native VIMATE binary
             * sẽ có URL chứa "vimate-fw" hoặc tương tự. */
            bool is_legacy_url = (strstr(new_url, "xiaozhi") != NULL);
            /* Bảo mật: CHỈ tải OTA qua https:// (TLS verify bằng crt_bundle).
             * http:// = không TLS → MITM đẩy firmware tùy ý lên cả fleet. */
            bool is_secure_url = (strncmp(new_url, "https://", 8) == 0);
            if (!is_secure_url) {
                ESP_LOGW(TAG_OTA, "Bỏ qua OTA — URL không phải https:// (chống MITM firmware): %s", new_url);
            } else if (is_legacy_url) {
                ESP_LOGW(TAG_OTA, "Ignore OTA — URL trỏ tới firmware xiaozhi cũ (%s)", new_url);
            } else if (strcmp(new_ver, VIMATE_FW_VERSION) != 0) {
                ESP_LOGW(TAG_OTA, "New version %s available (current %s)%s",
                         new_ver, VIMATE_FW_VERSION, force ? " [FORCE]" : "");
                /* Download + flash */
                esp_http_client_config_t ota_cfg = {
                    .url = new_url,
                    .crt_bundle_attach = esp_crt_bundle_attach,
                    .timeout_ms = 60000,
                    .keep_alive_enable = true,
                };
                esp_https_ota_config_t ota = { .http_config = &ota_cfg };
                esp_err_t err = esp_https_ota(&ota);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG_OTA, "OTA success — rebooting");
                    cJSON_Delete(root);
                    root = NULL;
                    ota_end();
                    esp_restart();
                } else {
                    ESP_LOGE(TAG_OTA, "OTA download fail: %s", esp_err_to_name(err));
                    ret = err;
                }
            } else {
                ESP_LOGI(TAG_OTA, "Firmware version up-to-date (%s)", new_ver);
            }
        }
    }
done:
    if (root) cJSON_Delete(root);
    ota_end();
    return ret;
}

static esp_err_t ota_client_claim_activation_once(const char *activation_code) {
    if (!activation_code || !activation_code[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_try_begin()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = NULL;
    char url[192];
    snprintf(url, sizeof(url), "%s/ota/v1/activate", g_vimate_server.base_url);
    char body[96];
    snprintf(body, sizeof(body), "{\"activationCode\":\"%s\"}", activation_code);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http_event,
        .timeout_ms = OTA_POST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        goto done;
    }
    esp_http_client_set_header(cli, "Device-Id", g_vimate_server.mac_id);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));

    s_rx_len = 0;
    esp_err_t r = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_OTA, "activation poll HTTP fail: %s", esp_err_to_name(r));
        goto done;
    }
    if (status == 202) {
        ESP_LOGI(TAG_OTA, "activation pending");
        ret = ESP_ERR_INVALID_STATE;
        goto done;
    }
    if (status == 404) {
        ESP_LOGW(TAG_OTA,
                 "Activation record no longer exists; refresh device registration");
        ret = ESP_ERR_NOT_FOUND;
        goto done;
    }
    if (status != 200) {
        ESP_LOGW(TAG_OTA, "activation poll status=%d body=%.*s", status, s_rx_len, s_rx_buf);
        goto done;
    }

    root = cJSON_Parse(s_rx_buf);
    if (!root) {
        ret = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;
    cJSON *token = cJSON_GetObjectItem(data, "authToken");
    cJSON *ws = cJSON_GetObjectItem(data, "websocket");
    if (ws) {
        cJSON *ws_url = cJSON_GetObjectItem(ws, "url");
        if (cJSON_IsString(ws_url) && ws_url->valuestring[0]) {
            strlcpy(g_vimate_server.ws_url, ws_url->valuestring, sizeof(g_vimate_server.ws_url));
        }
    }
    if (!cJSON_IsString(token) || !token->valuestring[0]) {
        ESP_LOGW(TAG_OTA, "activation response missing authToken");
        ret = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (strlen(token->valuestring) >= sizeof(g_vimate_server.device_token)) {
        ESP_LOGE(TAG_OTA, "activation token too long");
        ret = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    ret = nvs_store_set_str("dev_token", token->valuestring);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_OTA, "save activation token: %s", esp_err_to_name(ret));
        goto done;
    }
    strlcpy(g_vimate_server.device_token, token->valuestring,
            sizeof(g_vimate_server.device_token));
    activation_code_clear();
    ESP_LOGI(TAG_OTA, "activation token saved; reboot to reconnect authenticated");
    display_set_message("Đã kích hoạt", "Thiết bị sẽ kết nối lại ngay.");
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();

done:
    if (root) cJSON_Delete(root);
    ota_end();
    return ret;
}

static void activation_poll_task(void *arg) {
    char code[16] = {0};
    bool recover_registration = false;
    if (arg) {
        strlcpy(code, (const char *)arg, sizeof(code));
        free(arg);
    }
    unsigned int attempt = 0;
    while (g_vimate_server.device_token[0] == '\0') {
        if (attempt == 0) {
            vTaskDelay(pdMS_TO_TICKS(1200));
        } else {
            if (attempt == ACTIVATION_POLL_FAST_TRIES) {
                ESP_LOGI(TAG_OTA,
                         "Activation still pending; continue every %u seconds",
                         (unsigned)(ACTIVATION_POLL_SLOW_INTERVAL_MS / 1000));
            }
            int delay_ms = attempt < ACTIVATION_POLL_FAST_TRIES
                ? ACTIVATION_POLL_FAST_INTERVAL_MS
                : ACTIVATION_POLL_SLOW_INTERVAL_MS;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        esp_err_t r = ota_client_claim_activation_once(code);
        attempt++;
        if (r == ESP_OK) {
            break;
        }
        if (r == ESP_ERR_NOT_FOUND) {
            /* Admin deleted/unbound this unactivated row while it was polling.
             * Stop using the stale code so OTA discovery can auto-register the
             * MAC again and start a poll with the newly issued code. */
            activation_code_clear();
            recover_registration = true;
            break;
        }
    }
    portENTER_CRITICAL(&s_activation_poll_mux);
    s_activation_poll_running = false;
    portEXIT_CRITICAL(&s_activation_poll_mux);
    if (recover_registration) {
        display_set_message("Đang đăng ký lại",
                            "WiFi đã được giữ nguyên. Thiết bị đang lấy mã kích hoạt mới.");
        ota_client_start_registration_recovery();
    }
    vTaskDelete(NULL);
}

void ota_client_restore_pending_activation(void) {
    if (g_vimate_server.device_token[0] != '\0') {
        return;
    }
    esp_err_t er = nvs_store_get_str(
        NVS_PENDING_ACTIVATION_KEY,
        g_vimate_server.activation_code,
        sizeof(g_vimate_server.activation_code));
    if (er != ESP_OK) {
        ESP_LOGW(TAG_OTA, "restore pending activation code: %s", esp_err_to_name(er));
        g_vimate_server.activation_code[0] = '\0';
    } else if (g_vimate_server.activation_code[0] != '\0') {
        ESP_LOGI(TAG_OTA, "Pending activation restored after reboot");
    }
}

void ota_client_start_activation_poll(const char *activation_code) {
    if (!activation_code || !activation_code[0] ||
        g_vimate_server.device_token[0] != '\0') {
        return;
    }
    bool start = false;
    portENTER_CRITICAL(&s_activation_poll_mux);
    if (!s_activation_poll_running) {
        s_activation_poll_running = true;
        start = true;
    }
    portEXIT_CRITICAL(&s_activation_poll_mux);
    if (!start) {
        return;
    }
    esp_err_t save_er = nvs_store_set_str(NVS_PENDING_ACTIVATION_KEY, activation_code);
    if (save_er != ESP_OK) {
        ESP_LOGW(TAG_OTA, "persist pending activation code: %s", esp_err_to_name(save_er));
    }
    char *copy = strdup(activation_code);
    if (!copy) {
        portENTER_CRITICAL(&s_activation_poll_mux);
        s_activation_poll_running = false;
        portEXIT_CRITICAL(&s_activation_poll_mux);
        return;
    }
    if (xTaskCreatePinnedToCore(activation_poll_task, "act_poll",
            VIMATE_TASK_STACK_BACKGROUND, copy,
            VIMATE_TASK_PRIO_BACKGROUND, NULL,
            VIMATE_TASK_CORE_IO) != pdPASS) {
        free(copy);
        portENTER_CRITICAL(&s_activation_poll_mux);
        s_activation_poll_running = false;
        portEXIT_CRITICAL(&s_activation_poll_mux);
        ESP_LOGE(TAG_OTA, "Create activation poll task failed");
    }
}

static void registration_recovery_task(void *arg) {
    (void)arg;
    for (int i = 0; i < REGISTRATION_RECOVERY_MAX_TRIES; ++i) {
        if (g_vimate_server.activated || g_vimate_server.activation_code[0] != '\0') {
            break;
        }
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(REGISTRATION_RECOVERY_INTERVAL_MS));
        }
        esp_err_t er = ota_client_check_once();
        if (er != ESP_OK) {
            ESP_LOGW(TAG_OTA, "Device registration recovery attempt %d/%d: %s",
                     i + 1, REGISTRATION_RECOVERY_MAX_TRIES, esp_err_to_name(er));
        }
    }
    portENTER_CRITICAL(&s_registration_recovery_mux);
    s_registration_recovery_running = false;
    portEXIT_CRITICAL(&s_registration_recovery_mux);
    vTaskDelete(NULL);
}

void ota_client_start_registration_recovery(void) {
    bool start = false;
    portENTER_CRITICAL(&s_registration_recovery_mux);
    if (!s_registration_recovery_running) {
        s_registration_recovery_running = true;
        start = true;
    }
    portEXIT_CRITICAL(&s_registration_recovery_mux);
    if (!start) return;

    if (xTaskCreatePinnedToCore(registration_recovery_task, "reg_recover",
            VIMATE_TASK_STACK_BACKGROUND, NULL, VIMATE_TASK_PRIO_BACKGROUND,
            NULL, VIMATE_TASK_CORE_IO) != pdPASS) {
        portENTER_CRITICAL(&s_registration_recovery_mux);
        s_registration_recovery_running = false;
        portEXIT_CRITICAL(&s_registration_recovery_mux);
        ESP_LOGE(TAG_OTA, "Create device registration recovery task failed");
    }
}

static void ota_periodic_task(void *arg) {
    int interval_sec = CONFIG_VIMATE_OTA_INTERVAL_SEC;
    ESP_LOGI(TAG_OTA, "Periodic OTA interval=%ds", interval_sec);
    while (1) {
        int remaining = interval_sec;
        while (remaining > 0) {
            int step = remaining > 60 ? 60 : remaining;
            vTaskDelay(pdMS_TO_TICKS(step * 1000));
            remaining -= step;
        }
        if (audio_pipeline_speaker_is_active()) {
            ESP_LOGW(TAG_OTA, "Periodic OTA skipped: speaker is active");
            continue;
        }
        (void)ota_client_check_once();
    }
}

void ota_client_start_periodic_task(void) {
    /* Core 1 — OTA check chạy mỗi giờ, không cần Core 0 realtime. */
    xTaskCreatePinnedToCore(ota_periodic_task, "ota_period", VIMATE_TASK_STACK_BACKGROUND, NULL,
                            VIMATE_TASK_PRIO_BACKGROUND, NULL, VIMATE_TASK_CORE_IO);
}
