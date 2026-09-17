/**
 * course_media_cache.h — SD-card cache for purchased course media.
 *
 * Server owns access control and returns a manifest from /course-cache.
 * Firmware stores files by manifest localPath and resolves runtime image URLs
 * to local SD paths before falling back to SPIFFS/HTTP.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t course_media_cache_init(void);
bool course_media_cache_ready(void);
void course_media_cache_sync_async(void);
bool course_media_cache_path(const char *url, char *out_path, size_t out_size);
/* Số byte trống trên thẻ SD (0 nếu chưa mount/đầy/không lấy được). Dùng để chặn
 * ghi âm khi thẻ đầy thay vì ghi câm 0 byte. */
uint64_t course_media_cache_free_bytes(void);

#ifdef __cplusplus
}
#endif
