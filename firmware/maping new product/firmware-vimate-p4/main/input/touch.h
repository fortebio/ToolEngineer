/**
 * touch.h — FT6236G capacitive touch driver (I2C).
 *
 * Tap màn (bất kỳ chỗ nào) tương đương BTN_PRESS event.
 * Long-press không reset thiết bị; WiFi setup chỉ dùng nút BOOT vật lý.
 * Swipe up/down/left/right tương lai có thể map cho điều khiển khác.
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_init(void);

#ifdef __cplusplus
}
#endif
