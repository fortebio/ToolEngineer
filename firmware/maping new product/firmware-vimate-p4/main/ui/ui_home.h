/**
 * ui_home.h — màn home/state machine chính.
 * Thread-safe: tự lock LVGL internal.
 */
#pragma once
#include "vimate.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_home_init(void);
void ui_home_show_state(vimate_dev_state_t s);
void ui_home_show_message(const char *title, const char *body);
void ui_home_show_stt(const char *text);
/* Hard switch về home screen — dùng khi cần fallback an toàn. */
void ui_home_activate(void);

#ifdef __cplusplus
}
#endif
