#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *url_rewrite_localhost_media(const char *url, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
