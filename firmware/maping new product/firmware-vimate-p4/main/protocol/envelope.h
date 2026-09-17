/**
 * envelope.h — JSON message envelope cho VIMATE protocol.
 *
 * Message types (server → device):
 *   "hello"  → server ack
 *   "stt"    → transcript
 *   "llm"    → emotion + emoji
 *   "tts"    → audio stream state (start/sentence_start/stop)
 *   "mcp"    → JSON-RPC 2.0 tool call (show_image/card/reward)
 *   "error"  → not_activated / no_active_plan
 *
 * Reference: /Users/digits/DEV/Go-Xiaozhi/server/docs/FIRMWARE_PROTOCOL.md
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t envelope_send_hello(void);
esp_err_t envelope_send_listen_start(void);
/* Như envelope_send_listen_start nhưng gắn mode="wake" khi from_wake=true
 * (wake-word "Hi Lily") để server vào chế độ Trò chuyện thay vì tự đọc bài.
 * Tap/auto-listen (from_wake=false) giữ nguyên hành vi cũ (kick bài học). */
esp_err_t envelope_send_listen_start_ex(bool from_wake);
esp_err_t envelope_send_listen_stop(void);
esp_err_t envelope_send_abort(void);
esp_err_t envelope_send_mcp_result(int id, const char *json_result);
esp_err_t envelope_send_home_select(const char *id); /* Pha C: chạm ô home */

/* Called from ws_client on incoming text frame. */
void envelope_handle_text(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
