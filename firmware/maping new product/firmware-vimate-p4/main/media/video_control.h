#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t video_control_play(const char *source, const char *title, bool loop);
void video_control_stop(void);

#ifdef __cplusplus
}
#endif
