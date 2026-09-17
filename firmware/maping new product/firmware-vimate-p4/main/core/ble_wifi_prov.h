#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_wifi_prov_start(void);
esp_err_t ble_wifi_prov_stop(void);

/* Trạng thái cho icon Bluetooth trên màn (display_set_ble_state). Đẩy theo sự kiện
 * từ ble_wifi_prov.c — heartbeat task không chạy trong lúc provisioning nên không
 * poll được như icon WiFi. */
typedef enum {
    BLE_PROV_STATE_UNAVAILABLE = 0, /* target không có radio BT (ESP32-P4: stub) */
    BLE_PROV_STATE_OFF,             /* có stack, chưa bật hoặc đã tắt */
    BLE_PROV_STATE_ADVERTISING,     /* đang quảng bá VIMATE-Setup-xxxx, chờ app */
    BLE_PROV_STATE_CONNECTED,       /* điện thoại đã nối GATT */
} ble_prov_state_t;
ble_prov_state_t ble_wifi_prov_state(void);

#ifdef __cplusplus
}
#endif
