/**
 * sensor_bus.h — bus I2C RIÊNG của bo cảm biến 4 slot (BOARD_SENSOR_I2C_NUM) + mux
 * TCA9548A + một handle TCS34725 dùng chung cho 4 kênh (địa chỉ cố định 0x29, mux
 * chọn kênh nào thì handle nói chuyện với con đó).
 *
 * Tách khỏi bus GPIO7/8 (touch/codec/PMIC) theo MAPPING-Rapid4P.md §2.2. Nếu sau này
 * schematic bo con treo lên bus chung: đổi BOARD_SENSOR_I2C_NUM = I2C_NUM_0 và SDA/SCL
 * = 7/8 — sensor_bus_init() tự get-handle bus đã có, không tạo lại.
 */
#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensor_bus_init(void);
i2c_master_bus_handle_t sensor_bus_handle(void);
i2c_master_dev_handle_t sensor_bus_mux_dev(void);
i2c_master_dev_handle_t sensor_bus_tcs_dev(void);
/* Dò từng thiết bị (mux + 4 TCS) — ghi log, trả số TCS trả lời. */
int sensor_bus_probe(void);

#ifdef __cplusplus
}
#endif
