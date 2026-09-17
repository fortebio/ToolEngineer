/**
 * button.h — nút vật lý: BOOT (GPIO35) + ĐO (BTN3 GPIO0). Quét chung một task 20 ms.
 *   BOOT tap        → R4P_EVT_BTN_PRESS
 *   BOOT giữ ≥ 5 s  → R4P_EVT_BTN_LONG  (xoá WiFi, vào SoftAP — app_main xử lý)
 *   ĐO tap          → R4P_EVT_BTN_MEASURE (ui_reader: xác nhận / bắt đầu đo)
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
