/**
 * calib_store.h — cấu hình bền của máy (thay EEPROM 512 B của ReaderPlus, MAPPING §3.4):
 *   cal_min / cal_max : u16 × 4 slot (blob)     — ADDR_VALUE_CALIB_MIN/MAX(i)
 *   thr_<SICK>        : u32                      — ADDR_THRESHOLD_POSITIVE(i), mặc định 600
 *   lang              : u32 (r4p_lang_t)         — ADDR_LANGUAGE, mặc định EN
 *   device_id         : str                      — ADDR_ID_DEVICE_BASE (portal ghi)
 *   led_pwm           : u8 × 4 (blob)            — PWM_LED[] (chỉ RAM ở bản gốc)
 * WiFi SSID/pass: nvs_store_set_wifi() (blob atomic, wifi_mgr dùng).
 * Namespace NVS "rapid4p" (core/nvs_store.c).
 */
#pragma once
#include "esp_err.h"
#include "rapid4p.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t cal_min[R4P_SLOTS];
    uint16_t cal_max[R4P_SLOTS];
    uint32_t threshold[R4P_SICK_COUNT];
    r4p_lang_t lang;
    uint8_t led_pwm[R4P_SLOTS];
} r4p_settings_t;

/* Nạp từ NVS vào RAM; thiếu khoá → mặc định + ghi lại. Gọi 1 lần sau nvs_store_init. */
esp_err_t calib_store_load(void);
const r4p_settings_t *calib_store_get(void);

esp_err_t calib_store_set_calib(int slot, uint16_t cal_max, uint16_t cal_min);
esp_err_t calib_store_format_calib(void);          /* ghi 0 hết như format_All_CalibSensor */
bool calib_store_slot_calibrated(int slot);        /* max > min > 0 */
esp_err_t calib_store_set_threshold(r4p_sick_t sick, uint32_t value);
esp_err_t calib_store_set_lang(r4p_lang_t lang);
esp_err_t calib_store_set_led_pwm(int slot, uint8_t pwm);

#ifdef __cplusplus
}
#endif
