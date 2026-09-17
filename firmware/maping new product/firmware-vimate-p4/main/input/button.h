/**
 * button.h — GPIO button handler.
 * Short press → signal VIMATE_EVT_BTN_PRESS (start/stop listen)
 * Long press 5s → VIMATE_EVT_BTN_LONG (clear WiFi and force AP setup)
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t button_init(void);

#ifdef __cplusplus
}
#endif
