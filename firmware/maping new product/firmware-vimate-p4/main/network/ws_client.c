/**
 * ws_client.c — WebSocket client với reconnect exponential.
 *
 * On connect:
 *   - Auto send hello JSON (sample_rate, audio_params)
 *
 * On text message:
 *   - cJSON parse → dispatch by "type" → envelope_handle()
 *
 * On binary message:
 *   - Forward bytes vào audio_pipeline_speaker_push() (Opus frame → decode → play)
 *
 * On error JSON:
 *   - type=error → check code → drive UI state
 */
#include "ws_client.h"
#include "vimate.h"
#include "core/task_profile.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "protocol/envelope.h"
#include "audio/audio_pipeline.h"
#include "core/nvs_store.h"
#include "ui/ui_home.h"
#include "ui/ui_activation.h"
#include "ui/ui_error.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include <string.h>

static esp_websocket_client_handle_t s_client = NULL;
static bool s_connected = false;
static bool s_protocol_ready = false;
static bool s_server_gate_response = false;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_send_lock = NULL;

/* Thời gian chờ ghi 1 khung. esp_websocket_client 1.8: ghi trả 0 (poll timeout) =
 * ABORT kết nối → 6 s nối lại, mất lượt nói. Log P4 13/09: 3 lần rớt trong 2 phiên,
 * đều "Poll timeout … timeout_ms=500" lúc WiFi mesh khựng > 0,5 s khi đang gửi
 * abort/home_select/opus. Text (lệnh) chờ tới 3 s; opus mic 1 s (khựng lâu hơn thì bỏ
 * khung, nhưng không giết kết nối). Trong lúc chờ, sender khác nhận ESP_ERR_TIMEOUT
 * sau 50 ms (s_send_lock) — như cũ. */
#ifndef WS_SEND_TEXT_TIMEOUT_MS
#define WS_SEND_TEXT_TIMEOUT_MS   3000
#endif
#ifndef WS_SEND_BIN_TIMEOUT_MS
#define WS_SEND_BIN_TIMEOUT_MS    1000
#endif
static TickType_t s_last_start_tick = 0;
static TickType_t s_connected_tick = 0;
static TickType_t s_last_disconnected_tick = 0;
static uint32_t s_restart_count = 0;

static uint32_t elapsed_ms_since(TickType_t start, TickType_t now) {
    if (start == 0) return 0;
    return (uint32_t)((now - start) * portTICK_PERIOD_MS);
}

static void mark_disconnected(void) {
    s_connected = false;
    portENTER_CRITICAL(&s_state_lock);
    s_protocol_ready = false;
    portEXIT_CRITICAL(&s_state_lock);
    s_connected_tick = 0;
    if (g_vimate_events) {
        xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_READY);
    }
    if (s_last_disconnected_tick == 0) {
        s_last_disconnected_tick = xTaskGetTickCount();
    }
}

static const char *ws_error_type_name(esp_websocket_error_type_t type) {
    switch (type) {
        case WEBSOCKET_ERROR_TYPE_NONE: return "none";
        case WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT: return "tcp";
        case WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT: return "pong_timeout";
        case WEBSOCKET_ERROR_TYPE_HANDSHAKE: return "handshake";
        case WEBSOCKET_ERROR_TYPE_SERVER_CLOSE: return "server_close";
        default: return "unknown";
    }
}

static void on_ws_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    esp_websocket_event_data_t *e = (esp_websocket_event_data_t *)event_data;
    if (e && e->client && e->client != s_client) {
        ESP_LOGW(TAG_WS, "ignore stale WS event id=%ld", (long)id);
        return;
    }
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG_WS, "WS CONNECTED");
            s_connected = true;
            s_connected_tick = xTaskGetTickCount();
            s_last_disconnected_tick = 0;
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_CONNECTED);
            xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_DISCONNECT);
            /* Transport up chưa đồng nghĩa server-ready. Gửi hello ngay; chỉ
             * server hello ACK mới mở UI/audio và OTA validation. */
            if (s_connected) envelope_send_hello();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG_WS, "WS DISCONNECTED down_ms=%u restart_count=%u",
                     (unsigned)ws_client_down_ms(), (unsigned)s_restart_count);
            mark_disconnected();
            xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_CONNECTED);
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_DISCONNECT);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (e->op_code == 0x01) {
                /* Text frame — JSON envelope */
                envelope_handle_text((const char *)e->data_ptr, e->data_len);
            } else if (e->op_code == 0x02) {
                /* Binary — Opus audio frame downstream */
                audio_pipeline_speaker_push((const uint8_t *)e->data_ptr, e->data_len);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            if (e) {
                const esp_websocket_error_codes_t *err = &e->error_handle;
                ESP_LOGE(TAG_WS,
                         "WS ERROR type=%s tls=%s(%d) tls_stack=%d flags=%d sock_errno=%d http=%d",
                         ws_error_type_name(err->error_type),
                         esp_err_to_name(err->esp_tls_last_esp_err),
                         (int)err->esp_tls_last_esp_err,
                         err->esp_tls_stack_err,
                         err->esp_tls_cert_verify_flags,
                         err->esp_transport_sock_errno,
                         err->esp_ws_handshake_status_code);
                if (err->esp_ws_handshake_status_code == 401) {
                    ws_client_handle_auth_rejected("http_401");
                }
            } else {
                ESP_LOGE(TAG_WS, "WS ERROR");
            }
            mark_disconnected();
            xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_CONNECTED);
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_DISCONNECT);
            break;
        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGW(TAG_WS, "WS CLOSED");
            mark_disconnected();
            xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_CONNECTED);
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_DISCONNECT);
            break;
        case WEBSOCKET_EVENT_FINISH:
            ESP_LOGW(TAG_WS, "WS TASK FINISH");
            mark_disconnected();
            xEventGroupClearBits(g_vimate_events, VIMATE_EVT_WS_CONNECTED);
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_DISCONNECT);
            break;
        default: break;
    }
}

esp_err_t ws_client_start(void) {
    if (!s_send_lock) {
        s_send_lock = xSemaphoreCreateMutex();
        if (!s_send_lock) return ESP_ERR_NO_MEM;
    }

    if (s_client) {
        if (s_connected) return ESP_OK;
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_last_start_tick = xTaskGetTickCount();
    if (s_last_disconnected_tick == 0) {
        s_last_disconnected_tick = s_last_start_tick;
    }

    /* Headers: luôn gửi Device-Id; chỉ gửi Authorization khi đã activate.
     * Server detect thiếu Authorization → reply JSON "not_activated" + code. */
    char auth_header[200];
    if (g_vimate_server.device_token[0] != '\0') {
        snprintf(auth_header, sizeof(auth_header),
            "Authorization: Bearer %s\r\nDevice-Id: %s\r\nUser-Agent: vimate-fw/%s\r\n",
            g_vimate_server.device_token, g_vimate_server.mac_id, VIMATE_FW_VERSION);
    } else {
        snprintf(auth_header, sizeof(auth_header),
            "Device-Id: %s\r\nUser-Agent: vimate-fw/%s\r\n",
            g_vimate_server.mac_id, VIMATE_FW_VERSION);
    }

    esp_websocket_client_config_t cfg = {
        .uri = g_vimate_server.ws_url,
        .headers = auth_header,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = CONFIG_VIMATE_WS_RECONNECT_BASE_MS,
        .network_timeout_ms = 15000,
        .ping_interval_sec = 30,
        .pingpong_timeout_sec = 30,
        .disable_pingpong_discon = true,
        .keep_alive_enable = true,
        .keep_alive_idle = 30,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        .buffer_size = CONFIG_WS_BUFFER_SIZE,
        /* JSON WS giờ chỉ còn control frame nhỏ; UI ops đã đi qua display
         * queue nên không cần giữ 12-16KB stack nội bộ cho WS task nữa. */
        .task_stack = VIMATE_TASK_STACK_WEBSOCKET,
        .task_prio = 5,
        /* Ghim WS task về Core IO (1) cùng audio/lwIP, để KHÔNG trôi sang
         * Core UI (0) tranh CPU với LVGL render task (đã chuyển về Core 0). */
        .task_core_id = VIMATE_TASK_CORE_IO,
    };
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG_WS, "init fail");
        return ESP_FAIL;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);
    esp_err_t r = esp_websocket_client_start(s_client);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_WS, "start: %s", esp_err_to_name(r));
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    return r;
}

esp_err_t ws_client_stop(void) {
    if (s_send_lock && xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(1500)) != pdTRUE) {
        ESP_LOGW(TAG_WS, "WS stop skipped: send lock busy");
        return ESP_ERR_TIMEOUT;
    }

    esp_websocket_client_handle_t old = s_client;
    s_client = NULL;
    mark_disconnected();
    if (s_send_lock) xSemaphoreGive(s_send_lock);

    if (!old) return ESP_OK;
    if (esp_websocket_client_is_connected(old)) {
        (void)esp_websocket_client_close(old, pdMS_TO_TICKS(1000));
    }
    esp_websocket_client_destroy(old);
    ESP_LOGI(TAG_WS, "WS client stopped");
    return ESP_OK;
}

esp_err_t ws_client_restart(const char *reason) {
    ESP_LOGW(TAG_WS,
             "WS supervisor restart reason=%s down_ms=%u internal=%u largest=%u",
             reason && reason[0] ? reason : "(none)",
             (unsigned)ws_client_down_ms(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    if (s_send_lock && xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(1500)) != pdTRUE) {
        ESP_LOGW(TAG_WS, "WS restart skipped: send lock busy");
        return ESP_ERR_TIMEOUT;
    }

    esp_websocket_client_handle_t old = s_client;
    s_client = NULL;
    mark_disconnected();
    if (s_send_lock) xSemaphoreGive(s_send_lock);

    if (old) {
        if (esp_websocket_client_is_connected(old)) {
            (void)esp_websocket_client_close(old, pdMS_TO_TICKS(1000));
        }
        esp_websocket_client_destroy(old);
    }
    s_restart_count++;
    return ws_client_start();
}

esp_err_t ws_client_send_text(const char *data, size_t len) {
    if (!s_connected || !s_client || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_send_lock && xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_connected || !s_client || !esp_websocket_client_is_connected(s_client)) {
        if (s_send_lock) xSemaphoreGive(s_send_lock);
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_text(s_client, data, len, pdMS_TO_TICKS(WS_SEND_TEXT_TIMEOUT_MS));
    if (sent <= 0 && (!s_client || !esp_websocket_client_is_connected(s_client))) {
        mark_disconnected();
    }
    if (s_send_lock) xSemaphoreGive(s_send_lock);
    return sent > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_client_send_binary(const uint8_t *data, size_t len) {
    if (!s_connected || !s_client || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_send_lock && xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_connected || !s_client || !esp_websocket_client_is_connected(s_client)) {
        if (s_send_lock) xSemaphoreGive(s_send_lock);
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_bin(s_client, (const char *)data, len, pdMS_TO_TICKS(WS_SEND_BIN_TIMEOUT_MS));
    if (sent <= 0 && (!s_client || !esp_websocket_client_is_connected(s_client))) {
        mark_disconnected();
    }
    if (s_send_lock) xSemaphoreGive(s_send_lock);
    return sent > 0 ? ESP_OK : ESP_FAIL;
}

bool ws_client_is_connected(void) {
    bool driver_connected = s_client && esp_websocket_client_is_connected(s_client);
    if (!driver_connected && s_connected) {
        mark_disconnected();
    }
    return s_connected && driver_connected;
}

bool ws_client_is_protocol_ready(void) {
    bool ready;
    portENTER_CRITICAL(&s_state_lock);
    ready = s_protocol_ready;
    portEXIT_CRITICAL(&s_state_lock);
    return ws_client_is_connected() && ready;
}

void ws_client_mark_protocol_ready(void) {
    if (!ws_client_is_connected()) return;
    portENTER_CRITICAL(&s_state_lock);
    s_protocol_ready = true;
    portEXIT_CRITICAL(&s_state_lock);
    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WS_READY);
    ESP_LOGI(TAG_WS, "WS protocol READY (server hello accepted)");
}

void ws_client_mark_server_gate_reached(void) {
    portENTER_CRITICAL(&s_state_lock);
    s_server_gate_response = true;
    portEXIT_CRITICAL(&s_state_lock);
}

void ws_client_handle_auth_rejected(const char *reason) {
    if (g_vimate_server.device_token[0] == '\0' && !g_vimate_server.activated) {
        return;
    }

    ESP_LOGW(TAG_WS, "Device authorization rejected (%s) — clear token, keep WiFi",
             reason && reason[0] ? reason : "unknown");
    esp_err_t er = nvs_store_erase("dev_token");
    if (er != ESP_OK && er != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG_WS, "Erase rejected device token failed: %s", esp_err_to_name(er));
        /* Clear RAM regardless. A later activation writes a fresh token; the
         * next boot may retry this recovery if flash erase truly failed. */
    }
    g_vimate_server.device_token[0] = '\0';
    g_vimate_server.activation_code[0] = '\0';
    g_vimate_server.activated = false;
    ws_client_mark_server_gate_reached();
    if (g_vimate_events) {
        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_AUTH_REVOKED);
    }
}

bool ws_client_has_server_gate_response(void) {
    bool reached;
    portENTER_CRITICAL(&s_state_lock);
    reached = s_server_gate_response;
    portEXIT_CRITICAL(&s_state_lock);
    return reached;
}

bool ws_client_is_started(void) { return s_client != NULL; }

uint32_t ws_client_down_ms(void) {
    if (s_client && s_connected && esp_websocket_client_is_connected(s_client)) {
        return 0;
    }
    TickType_t now = xTaskGetTickCount();
    TickType_t base = s_last_disconnected_tick ? s_last_disconnected_tick : s_last_start_tick;
    return elapsed_ms_since(base, now);
}

uint32_t ws_client_connected_ms(void) {
    if (!s_client || !s_connected || !esp_websocket_client_is_connected(s_client) ||
        s_connected_tick == 0) {
        return 0;
    }
    return elapsed_ms_since(s_connected_tick, xTaskGetTickCount());
}
