/**
 * mcp_handler.h — handle MCP JSON-RPC 2.0 tool calls từ VIMATE server.
 *
 * 3 tool VIMATE backend gọi xuống device:
 *   - self.edu.show_image(url)
 *   - self.edu.show_card(image/url, title, subtitle)
 *   - self.edu.show_reward(stars)
 *
 * Reply success: {"result": {"shown": true}}
 * Reply error:   {"error": {"code": -32603, "message": "..."}}
 */
#pragma once
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* payload = JSON object {jsonrpc, id, method, params} */
void mcp_handler_dispatch(const cJSON *payload);
void mcp_handler_note_listen_after_tts(void);

#ifdef __cplusplus
}
#endif
