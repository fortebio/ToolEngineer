/**
 * slot_led.h — LED chiếu sáng từng slot: 4 chân enable + 1 PWM độ sáng chung (LEDC).
 * Port từ FBT-ReaderPlus-1.0/src/sensor.cpp (LED_on/LED_off/LED_off_All, PWM_LED[]).
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t slot_led_init(void);
void slot_led_on(int slot);          /* 0..3: PWM = độ sáng slot đó + enable */
void slot_led_off(int slot);         /* enable LOW + PWM 0 */
void slot_led_off_all(void);
void slot_led_set_brightness(int slot, uint8_t pwm_0_255);
uint8_t slot_led_get_brightness(int slot);

#ifdef __cplusplus
}
#endif
