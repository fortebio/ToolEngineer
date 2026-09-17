#include "ota_boot_validation.h"

#include "core/telemetry.h"
#include "core/task_profile.h"
#include "core/wifi_mgr.h"
#include "network/ota_boot_validation_policy.h"
#include "network/ws_client.h"
#include "vimate.h"

#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "OTA boot validation requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"
#endif

#define OTA_BOOT_VALIDATION_POLL_MS 1000U
#define OTA_BOOT_VALIDATION_TIMEOUT_MS 180000U

static bool s_pending_verify;
static bool s_display_ready;
static bool s_audio_ready;
static bool s_ota_api_ready;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool ota_api_ready(void) {
    bool ready;
    portENTER_CRITICAL(&s_lock);
    ready = s_ota_api_ready;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

void ota_boot_validation_note_ota_api_result(esp_err_t result) {
    if (result != ESP_OK) return;
    portENTER_CRITICAL(&s_lock);
    s_ota_api_ready = true;
    portEXIT_CRITICAL(&s_lock);
}

static void validation_task(void *arg) {
    (void)arg;
    TickType_t started = xTaskGetTickCount();
    TickType_t healthy_since = 0;

    while (s_pending_verify) {
        TickType_t now = xTaskGetTickCount();
        bool requires_server_session = g_vimate_server.activated;
        bool server_ready = !requires_server_session ||
                            ws_client_has_server_gate_response() ||
                            (ws_client_is_protocol_ready() &&
                             telemetry_has_successful_heartbeat());
        bool base_ready = s_display_ready && s_audio_ready &&
                          wifi_mgr_is_connected() && ota_api_ready() && server_ready;
        if (base_ready) {
            if (healthy_since == 0) healthy_since = now;
        } else {
            healthy_since = 0;
        }

        ota_boot_health_t health = {
            .display_ready = s_display_ready,
            .audio_ready = s_audio_ready,
            .wifi_ready = wifi_mgr_is_connected(),
            .ota_api_ready = ota_api_ready(),
            .requires_server_session = requires_server_session,
            .server_gate_reached = ws_client_has_server_gate_response(),
            .ws_protocol_ready = ws_client_is_protocol_ready(),
            .heartbeat_sent = telemetry_has_successful_heartbeat(),
            .stable_ms = healthy_since == 0 ? 0 :
                (uint32_t)((now - healthy_since) * portTICK_PERIOD_MS),
        };
        if (ota_boot_health_is_valid(&health)) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG_OTA, "OTA image validated after stable server session");
                s_pending_verify = false;
                break;
            }
            ESP_LOGE(TAG_OTA, "mark OTA valid failed: %s", esp_err_to_name(err));
        }

        uint32_t elapsed_ms = (uint32_t)((now - started) * portTICK_PERIOD_MS);
        if (elapsed_ms >= OTA_BOOT_VALIDATION_TIMEOUT_MS) {
            ESP_LOGE(TAG_OTA,
                     "OTA validation timeout: wifi=%d ota=%d ws_ready=%d heartbeat=%d display=%d audio=%d — rollback",
                     health.wifi_ready, health.ota_api_ready,
                     health.ws_protocol_ready, health.heartbeat_sent,
                     health.display_ready, health.audio_ready);
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_BOOT_VALIDATION_POLL_MS));
    }
    vTaskDelete(NULL);
}

esp_err_t ota_boot_validation_start(bool display_ready, bool audio_ready) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    s_pending_verify = true;
    s_display_ready = display_ready;
    s_audio_ready = audio_ready;
    s_ota_api_ready = false;
    BaseType_t created = xTaskCreatePinnedToCore(
        validation_task, "ota_validate", 4096, NULL,
        VIMATE_TASK_PRIO_HEARTBEAT, NULL, VIMATE_TASK_CORE_UI);
    if (created != pdPASS) {
        s_pending_verify = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG_OTA,
             "OTA image pending verify — waiting for hardware, HTTPS, WS hello and heartbeat");
    return ESP_OK;
}
