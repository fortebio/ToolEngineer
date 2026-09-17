/**
 * lesson_image_cache.h — Pre-cache ảnh trong khóa học xuống SPIFFS.
 *
 * Firmware sync trước khi mở WebSocket để runtime hiển thị ảnh bài học không
 * phải tạo thêm HTTPS session cạnh tranh với audio/TLS.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lesson_image_cache_init(void);
void lesson_image_cache_sync_blocking(void);
void lesson_image_cache_sync_async(void);
bool lesson_image_cache_path(const char *url, char *out_path, size_t out_size);

#ifdef __cplusplus
}
#endif
