/**
 * http_dl.h — Generic HTTPS GET → file on SPIFFS.
 * Dùng cho asset pack, lesson image cache, MCP image, card.
 */
#pragma once
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Download URL → dest_path. Tự tạo thư mục trung gian. Sử dụng CA bundle. */
esp_err_t http_dl_get_to_file(const char *url, const char *dest_path);

/* Download URL → buffer. *out là malloc-ed; caller free. */
esp_err_t http_dl_get_to_buf(const char *url, uint8_t **out, size_t *out_len);

/* Download URL → buffer with optional Authorization header. */
esp_err_t http_dl_get_to_buf_auth(const char *url, const char *auth_header,
                                  uint8_t **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
