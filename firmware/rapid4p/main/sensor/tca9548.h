/**
 * tca9548.h — mux I2C 8 kênh TCA9548A (địa chỉ BOARD_SENSOR_MUX_ADDR).
 * Port từ FBT-ReaderPlus-1.0/src/TCA9548.cpp: ghi 1 byte mask kênh; 0 = đóng hết.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tca9548_select(uint8_t channel);   /* 0..7 */
esp_err_t tca9548_close(void);
int tca9548_current(void);                   /* kênh đang mở, -1 = đóng/chưa biết */

#ifdef __cplusplus
}
#endif
