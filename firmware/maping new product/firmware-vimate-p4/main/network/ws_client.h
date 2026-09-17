/**
 * ws_client.h — WebSocket client tới VIMATE Core server.
 *
 * Connection: wss://vimate.vn/ws/ with headers:
 *   Authorization: Bearer <device_token>
 *   Device-Id: <MAC>
 *   Client-Id: <UUID>  (optional)
 *
 * Handles:
 *   - Auto-reconnect exponential
 *   - Send binary (Opus audio frames) + text (JSON envelope)
 *   - Dispatch incoming → envelope.c parse → handlers
 *   - Detect "not_activated" / "no_active_plan" error → drive UI state
 */
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ws_client_start(void);
esp_err_t ws_client_stop(void);
esp_err_t ws_client_restart(const char *reason);
esp_err_t ws_client_send_text(const char *data, size_t len);
esp_err_t ws_client_send_binary(const uint8_t *data, size_t len);
bool ws_client_is_connected(void);
bool ws_client_is_protocol_ready(void);
void ws_client_mark_protocol_ready(void);
void ws_client_mark_server_gate_reached(void);
bool ws_client_has_server_gate_response(void);
/* Clear a persisted token after a definitive server rejection (HTTP 401 or
 * not_activated). WiFi credentials are intentionally preserved. */
void ws_client_handle_auth_rejected(const char *reason);
/* true nếu client đã được tạo (driver đang lo connect/reconnect). false nghĩa là
 * start chưa từng thành công (vd fail lúc boot) → main loop sẽ thử ws_client_start lại. */
bool ws_client_is_started(void);
uint32_t ws_client_down_ms(void);
uint32_t ws_client_connected_ms(void);

#ifdef __cplusplus
}
#endif
