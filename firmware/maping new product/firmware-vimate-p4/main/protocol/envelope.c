/**
 * envelope.c — parse + dispatch JSON messages từ VIMATE server.
 *
 * Refactored: tất cả UI ops đi qua display API (single source of truth).
 * KHÔNG còn ui_home/ui_emotion/ui_error gọi trực tiếp.
 */
#include "envelope.h"
#include "vimate.h"
#include "boards/board.h"
#include "network/ws_client.h"
#include "core/ble_wifi_prov.h"
#include "core/nvs_store.h"
#include "core/task_profile.h"
#include "core/wifi_mgr.h"
#include "network/ota_client.h"
#include "protocol/mcp_handler.h"
#include "media/video_control.h"
#include "ui/display.h"
#include "ui/ui_image.h"
#include "audio/opus_codec.h"
#include "mbedtls/base64.h"
#include "audio/audio_pipeline.h"
#include "actuator/servo_emotion.h"
#include "esp_timer.h"

/* Mốc lần cuối hiện STT trên EDU — giữ chữ trẻ nói trên màn ~4s trước khi
 * câu trả lời AI thay. */
static int64_t s_last_stt_us = 0;
#include "store/emotion_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Task wrapper cho emotion_sync_blocking — xTaskCreate cần function nhận void*. */
static void emo_resync_task(void *arg) {
    (void)arg;
    emotion_sync_blocking();
    display_set_emotion(EMOTION_NEUTRAL);
    vTaskDelete(NULL);
}

typedef struct {
    int ttl_sec;
    uint32_t generation;
} ble_provision_ttl_t;

static uint32_t s_ble_provision_generation = 0;

static void ble_provision_timeout_task(void *arg) {
    ble_provision_ttl_t *ttl = arg;
    int ttl_sec = ttl ? ttl->ttl_sec : 300;
    uint32_t generation = ttl ? ttl->generation : 0;
    free(ttl);
    if (ttl_sec <= 0) ttl_sec = 300;
    vTaskDelay(pdMS_TO_TICKS(ttl_sec * 1000));
    if (generation == s_ble_provision_generation && wifi_mgr_is_connected()) {
        ESP_LOGI(TAG_WS, "BLE provisioning TTL expired — stop advertising");
        ble_wifi_prov_stop();
    }
    vTaskDelete(NULL);
}

static int json_int_value(const cJSON *root, const char *key, int fallback) {
    const cJSON *item = cJSON_GetObjectItem(root, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static const char *json_string_value(const cJSON *root, const char *key, const char *fallback) {
    const cJSON *item = cJSON_GetObjectItem(root, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

static bool json_bool_value(const cJSON *root, const char *key, bool fallback) {
    const cJSON *item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static void ota_now_task(void *arg) {
    (void)arg;
    audio_pipeline_wake_stop();
    audio_pipeline_mic_stop();
    audio_pipeline_speaker_stop();
    display_set_chat_message("system", "");
    display_set_message("Cập nhật", "Đang kiểm tra OTA...");
    display_set_state(DEV_STATE_OTA_CHECKING);
    esp_err_t err = ota_client_check_once();
    if (err == ESP_ERR_NO_MEM) {
        display_set_message("Cập nhật",
                            "Bộ nhớ thấp, robot sẽ khởi động để cập nhật.");
        vTaskDelay(pdMS_TO_TICKS(900));
        esp_restart();
    }
    /* Không có bản cập nhật (hoặc lỗi khác mà không reboot): wake-word đã bị
     * dừng ở đầu task để nhường RAM cho TLS tải OTA. PHẢI bật lại, nếu không
     * robot sẽ "điếc" (không nghe wake-word) cho tới khi reboot tay. Mic và
     * speaker tự bật lại theo listen/playback nên chỉ cần khôi phục wake. */
    audio_pipeline_wake_start();
    display_set_message(NULL, NULL);
    display_set_state(DEV_STATE_READY);
    vTaskDelete(NULL);
}

static void reboot_task(void *arg) {
    (void)arg;
    display_set_chat_message("system", "");
    display_set_message("Khởi động lại", "Robot sẽ khởi động lại ngay.");
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

esp_err_t envelope_send_hello(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON_AddStringToObject(root, "firmware", VIMATE_FW_VERSION);
    cJSON_AddStringToObject(root, "board", BOARD_NAME); /* server chọn persona theo loại thiết bị */
    const char *ip = wifi_mgr_ip_address();
    if (ip && ip[0]) {
        cJSON_AddStringToObject(root, "local_ip", ip);
    }
    cJSON_AddNumberToObject(root, "sample_rate", OPUS_ENC_SAMPLE_RATE);
    cJSON *features = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddBoolToObject(features, "wake_word", audio_pipeline_wake_available());
    cJSON *ap = cJSON_AddObjectToObject(root, "audio_params");
    cJSON_AddStringToObject(ap, "format", "opus");
    cJSON_AddNumberToObject(ap, "sample_rate", OPUS_ENC_SAMPLE_RATE);
    cJSON_AddNumberToObject(ap, "frame_duration", 60);
    cJSON_AddNumberToObject(ap, "frame_duration_ms", 60);
    cJSON_AddNumberToObject(ap, "channels", 1);
    char *s = cJSON_PrintUnformatted(root);
    esp_err_t r = ws_client_send_text(s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(root);
    ESP_LOGI(TAG_WS, "→ hello");
    return r;
}

/* Pha C: trẻ chạm 1 ô trên lưới home → báo server khởi động hoạt động đó. */
esp_err_t envelope_send_home_select(const char *id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "home_select");
    cJSON_AddStringToObject(root, "id", id ? id : "");
    char *s = cJSON_PrintUnformatted(root);
    esp_err_t r = ws_client_send_text(s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(root);
    ESP_LOGI(TAG_WS, "→ home_select %s", id ? id : "");
    return r;
}

esp_err_t envelope_send_listen_start(void) {
    return envelope_send_listen_start_ex(false);
}

esp_err_t envelope_send_listen_start_ex(bool from_wake) {
    /* Wake-word → mode:"wake" để server vào HUB Trò chuyện (không tự đọc bài).
     * Tap/auto-listen giữ payload cũ (không có mode) → server kick bài như trước. */
    const char *msg = from_wake
        ? "{\"type\":\"listen\",\"state\":\"start\",\"mode\":\"wake\"}"
        : "{\"type\":\"listen\",\"state\":\"start\"}";
    ESP_LOGI(TAG_WS, "→ listen start%s", from_wake ? " (wake)" : "");
    return ws_client_send_text(msg, strlen(msg));
}

esp_err_t envelope_send_listen_stop(void) {
    const char *msg = "{\"type\":\"listen\",\"state\":\"stop\"}";
    ESP_LOGI(TAG_WS, "→ listen stop");
    return ws_client_send_text(msg, strlen(msg));
}

esp_err_t envelope_send_abort(void) {
    const char *msg = "{\"type\":\"abort\"}";
    ESP_LOGI(TAG_WS, "→ abort");
    return ws_client_send_text(msg, strlen(msg));
}

esp_err_t envelope_send_mcp_result(int id, const char *json_result) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON *p = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(p, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(p, "id", id);
    cJSON *res = cJSON_Parse(json_result ? json_result : "{}");
    cJSON_AddItemToObject(p, "result", res ? res : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(root);
    esp_err_t r = ws_client_send_text(s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(root);
    return r;
}

void envelope_handle_text(const char *data, size_t len) {
    /* KHÔNG log raw JSON — vprintf %.*s với binary/long string có thể trigger
     * UART mutex assert khi scheduler suspended ở edge case. Log type sau parse. */
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG_WS, "JSON parse fail");
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "hello") == 0) {
        const cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
        if (cJSON_IsString(session_id) && session_id->valuestring[0]) {
            ws_client_mark_protocol_ready();
        } else {
            ESP_LOGW(TAG_WS, "Server hello thiếu session_id — chưa protocol-ready");
        }
    } else if (strcmp(type->valuestring, "stt") == 0) {
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG_WS, "STT: %s", text->valuestring);
            /* Trong Agent giữ ảnh + bottom bar: chỉ dùng bubble nằm phía trên
             * bar, không bật popup toàn màn làm che nội dung. Ngoài Agent vẫn
             * giữ popup đối chiếu ASR như trước. */
            if (display_agent_active()) {
                display_set_message(NULL, NULL);
            } else {
                display_set_message_timed("Con vừa nói", text->valuestring, 4500);
            }
            char line[192];
            snprintf(line, sizeof(line), "Con nói: %s", text->valuestring);
            display_set_chat_message("user", line);
            s_last_stt_us = esp_timer_get_time();
        }
    } else if (strcmp(type->valuestring, "llm") == 0) {
        const cJSON *emotion = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(emotion)) {
            vimate_emotion_t value = vimate_emotion_from_str(emotion->valuestring);
            display_set_emotion(value);
            servo_emotion_set_emotion(value);
        }
    } else if (strcmp(type->valuestring, "tts") == 0) {
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        const cJSON *text  = cJSON_GetObjectItem(root, "text");
        const cJSON *listen_after = cJSON_GetObjectItem(root, "listen_after");
        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "start") == 0) {
                video_control_stop();
                /* Mute mic SEND (không stop i2s — restart i2s+opus gây silk
                 * burg LPC analysis hang trên frame edge case → watchdog).
                 * Nếu mic đã tắt sau VAD EOT thì lệnh mute vẫn an toàn. */
                audio_pipeline_mic_mute(true);
                audio_pipeline_speaker_start();
                display_set_state(DEV_STATE_SPEAKING);
                if (g_vimate_events) {
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_TTS_START);
                }
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                /* Giữ "Con nói: ..." trên màn ~4s để phụ huynh đối chiếu ASR;
                 * câu AI chỉ thay sau đó (vẫn phát loa bình thường). */
                if (cJSON_IsString(text) &&
                    esp_timer_get_time() - s_last_stt_us > 4000000) {
                    display_set_chat_message("assistant", text->valuestring);
                }
            } else if (strcmp(state->valuestring, "stop") == 0) {
                bool should_listen = cJSON_IsTrue(listen_after);
                /* Server (TTS cache) burst cả câu rồi gửi stop NGAY — stop tức thì
                 * = xả queue = CỤT ĐUÔI CÂU. Đợi loa phát hết (queue 128×60ms ≈
                 * trần 7.7s; bound 12s) + đuôi 120ms cho frame cuối đang ghi codec.
                 * Chặn WS task ở đây an toàn: trong TTS không có uplink, và câu kế
                 * (tts start) PHẢI chờ câu này xong mới đúng thứ tự. Abort/barge-in
                 * không đi đường này (gọi speaker_stop thẳng — xả ngay). */
                for (int w = 0; w < 1200 && audio_pipeline_speaker_pending(); w++) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                vTaskDelay(pdMS_TO_TICKS(120));
                audio_pipeline_speaker_stop();
                display_set_emotion(EMOTION_NEUTRAL);
                audio_pipeline_mic_mute(false);
                if (should_listen && display_ai_active()) {
                    mcp_handler_note_listen_after_tts();
                    ESP_LOGI(TAG_WS, "TTS stop asks listen_after → signal main");
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_AUTO_LISTEN);
                } else {
                    if (should_listen) {
                        ESP_LOGI(TAG_WS, "Ignore stale listen_after outside AI mode");
                    }
                    audio_pipeline_mic_stop();
                    audio_pipeline_wake_start();
                    display_set_chat_message("system", "");
                    display_set_state(DEV_STATE_READY);
                }
            }
        }
    } else if (strcmp(type->valuestring, "sync") == 0) {
        const cJSON *target = cJSON_GetObjectItem(root, "target");
        if (cJSON_IsString(target) && strcmp(target->valuestring, "emotions") == 0) {
            ESP_LOGI(TAG_WS, "Server yêu cầu sync emotions — spawn task");
            xTaskCreatePinnedToCore(emo_resync_task, "emo_resync",
                                    VIMATE_TASK_STACK_BACKGROUND, NULL,
                                    VIMATE_TASK_PRIO_BACKGROUND, NULL,
                                    VIMATE_TASK_CORE_IO);
        }
    } else if (strcmp(type->valuestring, "command") == 0) {
        const cJSON *cmd = cJSON_GetObjectItem(root, "command");
        if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "ble_provision") == 0) {
            const cJSON *ttl = cJSON_GetObjectItem(root, "ttl_sec");
            int ttl_sec = cJSON_IsNumber(ttl) ? ttl->valueint : 300;
            ESP_LOGI(TAG_WS, "Server command: enable BLE provisioning ttl=%d", ttl_sec);
            if (ble_wifi_prov_start() == ESP_OK) {
                s_ble_provision_generation++;
                display_set_message("Cấu hình WiFi",
                                    "Bluetooth đã bật tạm thời. Mở app " VIMATE_BRAND_NAME " để gửi WiFi mới.");
                ble_provision_ttl_t *ttl_arg = calloc(1, sizeof(*ttl_arg));
                if (ttl_arg) {
                    ttl_arg->ttl_sec = ttl_sec;
                    ttl_arg->generation = s_ble_provision_generation;
                    if (xTaskCreatePinnedToCore(ble_provision_timeout_task,
                            "ble_ttl", 3072, ttl_arg,
                            VIMATE_TASK_PRIO_BACKGROUND, NULL,
                            VIMATE_TASK_CORE_UI) != pdPASS) {
                        free(ttl_arg);
                    }
                }
            } else {
                ESP_LOGW(TAG_WS, "BLE provisioning start failed");
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "activation_complete") == 0) {
            const cJSON *token = cJSON_GetObjectItem(root, "authToken");
            if (cJSON_IsString(token) && token->valuestring[0] &&
                strlen(token->valuestring) < sizeof(g_vimate_server.device_token)) {
                if (nvs_store_set_str("dev_token", token->valuestring) == ESP_OK) {
                    strlcpy(g_vimate_server.device_token, token->valuestring,
                            sizeof(g_vimate_server.device_token));
                    g_vimate_server.activated = true;
                    ESP_LOGI(TAG_WS, "activation token saved from WS command");
                    display_set_message("Đã kích hoạt", "Thiết bị sẽ kết nối lại ngay.");
                    vTaskDelay(pdMS_TO_TICKS(700));
                    esp_restart();
                }
            } else {
                g_vimate_server.activated = true;
                display_set_message(NULL, NULL);
                display_set_chat_message("system", "");
                display_set_state(DEV_STATE_READY);
                ESP_LOGI(TAG_WS, "Server command: activation complete");
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "set_volume") == 0) {
            const cJSON *vol = cJSON_GetObjectItem(root, "volume");
            if (!cJSON_IsNumber(vol)) {
                vol = cJSON_GetObjectItem(root, "percent");
            }
            if (cJSON_IsNumber(vol)) {
                int volume = vol->valueint;
                if (volume < 0) volume = 0;
                if (volume > 100) volume = 100;
                audio_pipeline_set_volume(volume);
                const cJSON *silent = cJSON_GetObjectItem(root, "silent");
                if (!cJSON_IsTrue(silent)) {
                    char msg[48];
                    snprintf(msg, sizeof(msg), "Đã đặt %d%%", volume);
                    display_set_message_timed("Âm lượng", msg, 5000);
                }
                ESP_LOGI(TAG_WS, "Server command: set volume %d%%", volume);
            } else {
                ESP_LOGW(TAG_WS, "Server command set_volume missing numeric volume");
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "set_display") == 0) {
            int brightness = json_int_value(root, "brightnessPercent",
                json_int_value(root, "brightness", -1));
            int sleep_sec = json_int_value(root, "sleepTimeoutSec",
                json_int_value(root, "sleep_timeout_sec", -1));
            if (brightness >= 0) {
                if (brightness > 100) brightness = 100;
                if (brightness < 0) brightness = 0;
                display_set_backlight(brightness);
            }
            if (sleep_sec >= 0) {
                display_set_sleep_timeout(sleep_sec);
            }
            const cJSON *silent = cJSON_GetObjectItem(root, "silent");
            if (!cJSON_IsTrue(silent)) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Sáng %d%% · ngủ %ds",
                         brightness >= 0 ? brightness : 100,
                         sleep_sec >= 0 ? sleep_sec : 0);
                display_set_message_timed("Màn hình", msg, 5000);
            }
            ESP_LOGI(TAG_WS, "Server command: display brightness=%d sleep=%d",
                     brightness, sleep_sec);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "display_test") == 0) {
            const char *reason = json_string_value(root, "reason", "remote");
            ESP_LOGI(TAG_WS, "Server command: display_test reason=%s", reason);
            display_show_test_pattern(reason);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "set_audio_config") == 0) {
            int mic_gain = json_int_value(root, "micGainPercent",
                json_int_value(root, "mic_gain", -1));
            int vad_sensitivity = json_int_value(root, "vadSensitivity",
                json_int_value(root, "vad_sensitivity", -1));
            int vad_silence_ms = json_int_value(root, "vadSilenceMs",
                json_int_value(root, "vad_silence_ms", -1));
            const char *environment = json_string_value(root, "environment", "normal");
            const char *speaker_distance = json_string_value(root, "speakerDistance", "normal");
            int noise_level = json_int_value(root, "noiseLevel", 45);
            int speech_level = json_int_value(root, "speechLevel", 65);
            bool aec_enabled = json_bool_value(root, "aecEnabled", false);
            bool dual_mic_enabled = json_bool_value(root, "dualMicEnabled", false);
            audio_pipeline_apply_audio_config(mic_gain, vad_sensitivity, vad_silence_ms,
                                              environment, speaker_distance,
                                              noise_level, speech_level,
                                              aec_enabled, dual_mic_enabled);
            const cJSON *silent = cJSON_GetObjectItem(root, "silent");
            if (!cJSON_IsTrue(silent)) {
                display_set_message_timed("Âm thanh", "Đã cập nhật cấu hình mic/VAD.", 5000);
            }
            ESP_LOGI(TAG_WS,
                     "Server command: audio mic=%d vad=%d silence=%d env=%s distance=%s noise=%d speech=%d dual=%d aec=%d",
                     mic_gain, vad_sensitivity, vad_silence_ms,
                     environment, speaker_distance, noise_level, speech_level,
                     dual_mic_enabled ? 1 : 0, aec_enabled ? 1 : 0);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "test_speaker") == 0) {
            ESP_LOGI(TAG_WS, "Server command: speaker test tone");
            display_set_message_timed("Kiểm tra loa", "Đang phát âm thử.", 4000);
            audio_pipeline_speaker_test_tone();
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "ota_check") == 0) {
            ESP_LOGI(TAG_WS, "Server command: OTA now");
            xTaskCreatePinnedToCore(ota_now_task, "ota_now", VIMATE_TASK_STACK_OTA_NOW, NULL,
                                    VIMATE_TASK_PRIO_BACKGROUND, NULL,
                                    VIMATE_TASK_CORE_IO);
        } else if (cJSON_IsString(cmd) &&
                   (strcmp(cmd->valuestring, "start_listen") == 0 ||
                    strcmp(cmd->valuestring, "force_listen") == 0)) {
            ESP_LOGI(TAG_WS, "Server command: start listen");
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_AUTO_LISTEN);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "reboot") == 0) {
            ESP_LOGI(TAG_WS, "Server command: reboot");
            xTaskCreatePinnedToCore(reboot_task, "reboot_cmd", 3072, NULL,
                                    VIMATE_TASK_PRIO_BACKGROUND, NULL,
                                    VIMATE_TASK_CORE_UI);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "pause") == 0) {
            /* TẠM DỪNG từ app: im (dừng TTS) + điếc (ngừng mic+wake) → hết tự-nói.
             * Chỉ lệnh "resume" mới bật lại. Tin cậy vì không cần mic/touch. */
            ESP_LOGW(TAG_WS, "Server command: PAUSE (app)");
            video_control_stop();
            audio_pipeline_set_paused(true);
            display_set_message("Tạm dừng", "Bố mẹ đã tạm dừng. Mở app để cho con học tiếp.");
            display_set_state(DEV_STATE_READY);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "resume") == 0) {
            ESP_LOGW(TAG_WS, "Server command: RESUME (app)");
            audio_pipeline_set_paused(false);
            display_set_message(NULL, NULL);
            display_set_chat_message("system", "");
            display_set_state(DEV_STATE_READY);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "home") == 0) {
            /* STOP từ app → về màn hình IDLE/HOME: dừng TTS+mic, dọn UI bài học,
             * hiện mặt chờ, bật lại wake (sẵn sàng bắt đầu lại). KHÁC "pause": không
             * điếc, không giữ chỗ bài (server đã rearm idle). */
            ESP_LOGW(TAG_WS, "Server command: HOME (app STOP → idle)");
            video_control_stop();
            audio_pipeline_speaker_stop();
            audio_pipeline_mic_stop();
            audio_pipeline_set_paused(false); /* clear paused + wake_start */
            ui_image_hide();
            display_set_message(NULL, NULL);
            display_set_chat_message("system", "");
            display_set_ai_active(false); /* rời AI → chạm màn không gọi AI nữa */
            if (g_vimate_events) {
                xEventGroupClearBits(g_vimate_events,
                                     VIMATE_EVT_AUTO_LISTEN | VIMATE_EVT_BTN_PRESS |
                                     VIMATE_EVT_BARGE_IN);
                xEventGroupSetBits(g_vimate_events, VIMATE_EVT_NAV_STOP);
            }
            display_set_state(DEV_STATE_READY);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "clockbg") == 0) {
            /* Tương thích server cũ. Màn chờ mới luôn đen/trắng nên FW bỏ qua theme. */
            const cJSON *act = cJSON_GetObjectItem(root, "active");
            display_set_clock_bg(cJSON_IsBool(act) ? cJSON_IsTrue(act) : false);
        } else if (cJSON_IsString(cmd) &&
                   (strcmp(cmd->valuestring, "lesson") == 0 ||
                    strcmp(cmd->valuestring, "agent") == 0)) {
            /* Lesson giữ ảnh toàn màn. Agent chừa vùng dưới cho bottom bar
             * để trẻ thoát/đổi app bất kỳ lúc nào. */
            bool agent_mode = strcmp(cmd->valuestring, "agent") == 0;
            ESP_LOGI(TAG_WS, "Server command: %s", agent_mode ? "AGENT" : "LESSON");
            video_control_stop();
            display_hide_home();
            display_set_chat_message("system", "");
            display_set_message(NULL, NULL);
            if (agent_mode) {
                display_set_agent_active(true);
            } else {
                display_set_agent_active(false);
                display_set_ai_active(true);
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "clock") == 0) {
            /* App Đồng hồ: hiện full màn giờ. Server gửi epoch (UTC giây) +
             * tz_offset_min (lệch múi giờ) vì FW không tự set TZ từ SNTP. */
            const cJSON *eobj = cJSON_GetObjectItem(root, "epoch");
            const cJSON *zobj = cJSON_GetObjectItem(root, "tz_offset_min");
            const cJSON *wobj = cJSON_GetObjectItem(root, "wallpaper");
            int64_t epoch = cJSON_IsNumber(eobj) ? (int64_t)eobj->valuedouble : 0;
            int tzmin = cJSON_IsNumber(zobj) ? (int)zobj->valuedouble : 420;
            bool wallpaper = cJSON_IsTrue(wobj);
            ESP_LOGW(TAG_WS, "Server command: clock epoch=%lld tz=%dm wallpaper=%d",
                     (long long)epoch, tzmin, wallpaper);
            video_control_stop();
            display_show_clock(epoch + (int64_t)tzmin * 60, wallpaper);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "slideshow") == 0) {
            /* Màn chờ slideshow ảnh gia đình. enabled=false → tắt. */
            const cJSON *en = cJSON_GetObjectItem(root, "enabled");
            video_control_stop();
            bool enabled = cJSON_IsTrue(en);
            if (!enabled) {
                display_set_slideshow(false, 0, false, 0, 0, NULL, 0);
            } else {
                const cJSON *iv = cJSON_GetObjectItem(root, "interval_sec");
                const cJSON *sc = cJSON_GetObjectItem(root, "show_clock");
                const cJSON *ia = cJSON_GetObjectItem(root, "idle_after_sec");
                const cJSON *eo = cJSON_GetObjectItem(root, "epoch");
                const cJSON *zo = cJSON_GetObjectItem(root, "tz_offset_min");
                const cJSON *imgs = cJSON_GetObjectItem(root, "images");
                int interval = cJSON_IsNumber(iv) ? (int)iv->valuedouble : 8;
                bool show_clock = sc ? cJSON_IsTrue(sc) : true;
                int idle_after = cJSON_IsNumber(ia) ? (int)ia->valuedouble : 60;
                int64_t epoch = cJSON_IsNumber(eo) ? (int64_t)eo->valuedouble : 0;
                int tz = cJSON_IsNumber(zo) ? (int)zo->valuedouble : 420;
                const char *urls[20];
                int n = 0;
                if (cJSON_IsArray(imgs)) {
                    const cJSON *it = NULL;
                    cJSON_ArrayForEach(it, imgs) {
                        if (n >= 20) break;
                        if (cJSON_IsString(it) && it->valuestring[0]) urls[n++] = it->valuestring;
                    }
                }
                ESP_LOGW(TAG_WS, "Server command: slideshow en=1 n=%d iv=%d", n, interval);
                display_set_slideshow(true, interval, show_clock, idle_after,
                                      epoch + (int64_t)tz * 60, urls, n);
                /* activate=true (bấm icon "Màn hình nghỉ") → bật ngay, không chờ idle. */
                const cJSON *act = cJSON_GetObjectItem(root, "activate");
                if (cJSON_IsTrue(act)) display_start_slideshow();
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "power_sleep") == 0) {
            /* Hẹn giờ TẮT (standby): tắt màn + mute loa/mic + dừng wake-word.
             * Giữ kết nối WebSocket để nhận lệnh power_wake đánh thức.
             * Core EDU. */
            const cJSON *robj = cJSON_GetObjectItem(root, "reason");
            const char *reason = cJSON_IsString(robj) ? robj->valuestring : "hẹn giờ";
            ESP_LOGW(TAG_WS, "Server command: power_sleep (standby) reason=%s", reason);
            video_control_stop();
            audio_pipeline_set_paused(true);
            audio_pipeline_mic_mute(true);
            display_set_chat_message("system", "");
            display_set_message("Tắt theo lịch", reason);
            display_set_state(DEV_STATE_READY);
            /* Tắt backlight SAU khi đã render message (để flush xong). */
            vTaskDelay(pdMS_TO_TICKS(300));
            display_set_backlight(0);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "power_wake") == 0) {
            /* Hẹn giờ MỞ (wake from standby): bật lại màn + unmute + khởi động
             * wake-word → về trạng thái READY bình thường.
             * Core EDU. */
            const cJSON *robj = cJSON_GetObjectItem(root, "reason");
            const char *reason = cJSON_IsString(robj) ? robj->valuestring : "hẹn giờ";
            ESP_LOGW(TAG_WS, "Server command: power_wake (resume) reason=%s", reason);
            display_set_backlight(100);
            audio_pipeline_mic_mute(false);
            audio_pipeline_set_paused(false); /* clear paused flag + restart wake */
            display_set_message(NULL, NULL);
            display_set_chat_message("system", "");
            display_set_state(DEV_STATE_READY);
            display_set_message_timed("Đã bật lại", reason, 5000);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "countdown") == 0) {
            const cJSON *lobj = cJSON_GetObjectItem(root, "label");
            const cJSON *sobj = cJSON_GetObjectItem(root, "seconds_total");
            const cJSON *mobj = cJSON_GetObjectItem(root, "minutes");
            const char *label = cJSON_IsString(lobj) ? lobj->valuestring : "Đếm ngược";
            int seconds = cJSON_IsNumber(sobj) ? (int)sobj->valuedouble : 0;
            if (seconds <= 0 && cJSON_IsNumber(mobj)) seconds = (int)mobj->valuedouble * 60;
            if (seconds <= 0) seconds = 60;
            ESP_LOGW(TAG_WS, "Server command: countdown seconds=%d", seconds);
            video_control_stop();
            if (!audio_pipeline_is_paused()) display_show_countdown(label, seconds);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "reminder") == 0) {
            /* Hẹn giờ / Nhắc uống nước / Lịch học: hiện màn alarm + chuông. Bỏ qua khi đang
             * paused (app DỪNG HẲN) để không phá giấc nghỉ của bé. */
            const cJSON *kobj = cJSON_GetObjectItem(root, "kind");
            const cJSON *tobj = cJSON_GetObjectItem(root, "text");
            const char *k = cJSON_IsString(kobj) ? kobj->valuestring : "";
            const char *txt = cJSON_IsString(tobj) ? tobj->valuestring : "";
            bool is_water = (strcmp(k, "water") == 0);
            bool is_study = (strcmp(k, "study") == 0);
            const char *title = is_water ? "Đến giờ uống nước rồi!"
                                         : is_study ? "Đến giờ học rồi!"
                                         : "Hết giờ rồi!";
            ESP_LOGW(TAG_WS, "Server command: reminder alarm kind=%s", k);
            video_control_stop();
            if (!audio_pipeline_is_paused()) {
                /* Màn nhắc dễ thương (icon đồng hồ/giọt nước cười); tự ẩn ~14s,
                 * chạm để tắt. VOICE nhắc do server gửi TTS ngay sau (không beep
                 * để khỏi xung đột i2s với TTS). */
                display_show_alarm((txt && txt[0]) ? txt : title, is_water);
            }
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "play_video") == 0) {
            const char *source = json_string_value(root, "path",
                json_string_value(root, "url", ""));
            const char *title = json_string_value(root, "title", "");
            bool loop = json_bool_value(root, "loop", false);
            ESP_LOGW(TAG_WS, "Server command: play_video source=%s loop=%d",
                     source, loop ? 1 : 0);
            video_control_play(source, title, loop);
        } else if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "stop_video") == 0) {
            ESP_LOGW(TAG_WS, "Server command: stop_video");
            video_control_stop();
        }
    } else if (strcmp(type->valuestring, "home") == 0) {
        /* Home: courses dùng carousel bìa lớn; agents/tiện ích dùng lưới icon. */
        const cJSON *items = cJSON_GetObjectItem(root, "items");
        if (cJSON_IsArray(items)) {
            const char *ids[8] = {0};
            const char *titles[8] = {0};
            const char *subtitles[8] = {0};
            const char *cover_urls[8] = {0};
            int n = 0;
            const cJSON *it = NULL;
            cJSON_ArrayForEach(it, items) {
                if (n >= 8) break;
                const cJSON *idobj = cJSON_GetObjectItem(it, "id");
                const cJSON *tobj = cJSON_GetObjectItem(it, "title");
                const cJSON *sobj = cJSON_GetObjectItem(it, "subtitle");
                const cJSON *cobj = cJSON_GetObjectItem(it, "coverUrl");
                ids[n] = cJSON_IsString(idobj) ? idobj->valuestring : "";
                titles[n] = cJSON_IsString(tobj) ? tobj->valuestring : "";
                subtitles[n] = cJSON_IsString(sobj) ? sobj->valuestring : "";
                cover_urls[n] = cJSON_IsString(cobj) ? cobj->valuestring : "";
                n++;
            }
            /* Header giao diện mới: chào bé + tổng sao (additive, cũ bỏ qua). */
            const cJSON *screen_obj = cJSON_GetObjectItem(root, "screen");
            const cJSON *gobj = cJSON_GetObjectItem(root, "greeting");
            const cJSON *sobj = cJSON_GetObjectItem(root, "starsTotal");
            const char *screen = cJSON_IsString(screen_obj) ? screen_obj->valuestring : "";
            const char *greeting = cJSON_IsString(gobj) ? gobj->valuestring : NULL;
            int stars = cJSON_IsNumber(sobj) ? (int)sobj->valuedouble : 0;
            ESP_LOGI(TAG_WS, "home payload: screen=%s items=%d stars=%d",
                     screen, n, stars);
            display_set_ai_active(false); /* về lưới menu → chạm không gọi AI */
            display_show_home(screen, ids, titles, subtitles, cover_urls,
                              n, greeting, stars);
        }
    } else if (strcmp(type->valuestring, "quiz") == 0) {
        /* Màn trắc nghiệm 4 nút chạm (mockup màn 4). options[] → nút; step/total
         * → tiến độ. Trẻ chạm nút → touch_task gửi home_select("quizans_<i>"). */
        const cJSON *qobj = cJSON_GetObjectItem(root, "question");
        const cJSON *opts = cJSON_GetObjectItem(root, "options");
        const cJSON *stobj = cJSON_GetObjectItem(root, "step");
        const cJSON *toobj = cJSON_GetObjectItem(root, "total");
        const char *question = cJSON_IsString(qobj) ? qobj->valuestring : "";
        const char *labels[4] = {0};
        int n = 0;
        if (cJSON_IsArray(opts)) {
            const cJSON *o = NULL;
            cJSON_ArrayForEach(o, opts) {
                if (n >= 4) break;
                labels[n++] = cJSON_IsString(o) ? o->valuestring : "";
            }
        }
        int step = cJSON_IsNumber(stobj) ? (int)stobj->valuedouble : 0;
        int total = cJSON_IsNumber(toobj) ? (int)toobj->valuedouble : 0;
        display_show_quiz(question, labels, n, step, total);
    } else if (strcmp(type->valuestring, "quiz_hide") == 0) {
        display_hide_quiz();
    } else if (strcmp(type->valuestring, "stats") == 0) {
        /* Màn thống kê tuần (mockup màn 10). */
        const cJSON *mi = cJSON_GetObjectItem(root, "minutes");
        const cJSON *le = cJSON_GetObjectItem(root, "lessons");
        const cJSON *da = cJSON_GetObjectItem(root, "days");
        const cJSON *st = cJSON_GetObjectItem(root, "stars");
        display_show_stats(cJSON_IsNumber(mi) ? (int)mi->valuedouble : 0,
                           cJSON_IsNumber(le) ? (int)le->valuedouble : 0,
                           cJSON_IsNumber(da) ? (int)da->valuedouble : 0,
                           cJSON_IsNumber(st) ? (int)st->valuedouble : 0);
    } else if (strcmp(type->valuestring, "water") == 0) {
        /* Màn uống nước (mockup màn 9) — ml + 2 nút chạm. */
        const cJSON *td = cJSON_GetObjectItem(root, "todayMl");
        const cJSON *gl = cJSON_GetObjectItem(root, "goalMl");
        display_show_water(cJSON_IsNumber(td) ? (int)td->valuedouble : 0,
                           cJSON_IsNumber(gl) ? (int)gl->valuedouble : 2000);
    } else if (strcmp(type->valuestring, "progress") == 0) {
        /* Màn lộ trình học (mockup màn 6): title + percent + lessons[]. */
        const cJSON *tobj = cJSON_GetObjectItem(root, "title");
        const cJSON *pobj = cJSON_GetObjectItem(root, "percent");
        const cJSON *lessons = cJSON_GetObjectItem(root, "lessons");
        const char *names[6] = {0};
        int stars[6] = {0};
        bool locked[6] = {0};
        int n = 0;
        if (cJSON_IsArray(lessons)) {
            const cJSON *it = NULL;
            cJSON_ArrayForEach(it, lessons) {
                if (n >= 6) break;
                const cJSON *nm = cJSON_GetObjectItem(it, "name");
                const cJSON *st = cJSON_GetObjectItem(it, "stars");
                const cJSON *lk = cJSON_GetObjectItem(it, "locked");
                names[n] = cJSON_IsString(nm) ? nm->valuestring : "";
                stars[n] = cJSON_IsNumber(st) ? (int)st->valuedouble : 0;
                locked[n] = cJSON_IsBool(lk) ? cJSON_IsTrue(lk) : false;
                n++;
            }
        }
        display_show_progress(cJSON_IsString(tobj) ? tobj->valuestring : "Lộ trình học",
                              cJSON_IsNumber(pobj) ? (int)pobj->valuedouble : 0,
                              names, stars, locked, n);
    } else if (strcmp(type->valuestring, "timetable") == 0) {
        const char *morning[DISPLAY_TIMETABLE_DAYS] = {0};
        const char *afternoon[DISPLAY_TIMETABLE_DAYS] = {0};
        const char *morning_detail[DISPLAY_TIMETABLE_DAYS] = {0};
        const char *afternoon_detail[DISPLAY_TIMETABLE_DAYS] = {0};
        const cJSON *class_obj = cJSON_GetObjectItem(root, "classLabel");
        const char *class_label = cJSON_IsString(class_obj) ? class_obj->valuestring : "";
        const cJSON *days = cJSON_GetObjectItem(root, "days");
        if (cJSON_IsArray(days)) {
            const cJSON *row = NULL;
            cJSON_ArrayForEach(row, days) {
                const cJSON *dobj = cJSON_GetObjectItem(row, "day");
                int day = cJSON_IsNumber(dobj) ? (int)dobj->valuedouble : 0;
                if (day < 1 || day > DISPLAY_TIMETABLE_DAYS) continue;
                const cJSON *mobj = cJSON_GetObjectItem(row, "morning");
                const cJSON *aobj = cJSON_GetObjectItem(row, "afternoon");
                const cJSON *mdobj = cJSON_GetObjectItem(row, "morningDetail");
                const cJSON *adobj = cJSON_GetObjectItem(row, "afternoonDetail");
                morning[day - 1] = cJSON_IsString(mobj) ? mobj->valuestring : "";
                afternoon[day - 1] = cJSON_IsString(aobj) ? aobj->valuestring : "";
                morning_detail[day - 1] =
                    cJSON_IsString(mdobj) ? mdobj->valuestring : morning[day - 1];
                afternoon_detail[day - 1] =
                    cJSON_IsString(adobj) ? adobj->valuestring : afternoon[day - 1];
            }
        }
        ESP_LOGI(TAG_WS, "timetable payload received");
        video_control_stop();
        display_show_timetable(class_label, morning, afternoon, morning_detail,
                               afternoon_detail, DISPLAY_TIMETABLE_DAYS);
    } else if (strcmp(type->valuestring, "image") == 0) {
        /* Image lesson: WS task chỉ enqueue URL rồi trả về. Render ảnh chạy
         * trong worker tạo sẵn lúc boot bằng stack internal DRAM; không dùng
         * PSRAM stack vì đường đọc SPIFFS/flash cần stack hợp lệ khi cache off. */
        const cJSON *url_obj = cJSON_GetObjectItem(root, "url");
        if (cJSON_IsString(url_obj)) {
            if (url_obj->valuestring[0]) {
                ESP_LOGI(TAG_WS, "image: enqueue render -> %s", url_obj->valuestring);
                video_control_stop();
                /* Ảnh sắp tải + render → báo mic_task hoãn gửi audio ở đầu cửa
                 * sổ nghe để không tranh RAM (TLS/decode) gây rớt WS/OOM. Cờ tự
                 * hết hạn nên an toàn (xem audio_pipeline_note_image_busy). */
                audio_pipeline_note_image_busy(3000);
                if (!ui_image_show_async(url_obj->valuestring)) {
                    ESP_LOGE(TAG_WS, "image enqueue failed");
                }
            } else {
                ESP_LOGI(TAG_WS, "image: clear preview");
                ui_image_hide();
                display_set_message(NULL, NULL);
                display_set_chat_message("system", "");
                display_set_state(DEV_STATE_READY);
            }
        }
    } else if (strcmp(type->valuestring, "mcp") == 0) {
        const cJSON *payload = cJSON_GetObjectItem(root, "payload");
        mcp_handler_dispatch(payload);
    } else if (strcmp(type->valuestring, "error") == 0) {
        const cJSON *code = cJSON_GetObjectItem(root, "code");
        const cJSON *msg = cJSON_GetObjectItem(root, "message");
        const cJSON *actCode = cJSON_GetObjectItem(root, "activation_code");
        const char *c = cJSON_IsString(code) ? code->valuestring : "";
        const char *m = cJSON_IsString(msg) ? msg->valuestring : "";
        ESP_LOGW(TAG_WS, "Server error: %s — %s", c, m);
        if (strcmp(c, "not_activated") == 0) {
            /* Server can unbind/delete a device while firmware still holds the
             * old token. Clear only device authorization; WiFi remains valid. */
            ws_client_handle_auth_rejected("not_activated");
            ws_client_mark_server_gate_reached();
            const char *ac = cJSON_IsString(actCode) ? actCode->valuestring : "------";
            strlcpy(g_vimate_server.activation_code, ac,
                    sizeof(g_vimate_server.activation_code));
            display_show_activation(ac, m);
            ota_client_start_activation_poll(ac);
        } else if (strcmp(c, "no_active_plan") == 0) {
            ws_client_mark_server_gate_reached();
            display_set_message("Chưa mua gói", m);
        } else {
            display_set_message("Lỗi server", m);
        }
    }

    cJSON_Delete(root);
}
