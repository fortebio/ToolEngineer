#pragma once

/*
 * board.h — chọn board phần cứng theo Kconfig + `#ifndef` mặc định cho mọi knob.
 *
 * Mọi module dùng chân/địa chỉ/tần số PHẢI include file này, KHÔNG include thẳng
 * board_esp32p4_43lcd.h (luật HAL, MAPPING-Rapid4P.md §1.4 + CLAUDE.md).
 * Rapid4P hiện chỉ có MỘT board; giữ cấu trúc choice để thêm board sau (vd. bo
 * production có schematic bo cảm biến riêng) mà không sửa driver.
 */

#include "sdkconfig.h"

#if defined(CONFIG_RAPID4P_BOARD_P4_43LCD)
#include "board_esp32p4_43lcd.h"
#else
#error "Chua chon board: bat CONFIG_RAPID4P_BOARD_P4_43LCD trong sdkconfig.defaults"
#endif

/* Panel MIPI-DSI (ESP32-P4). */
#ifndef BOARD_LCD_USE_MIPI_DSI
#define BOARD_LCD_USE_MIPI_DSI 0
#endif
#ifndef BOARD_LCD_ROTATION
#define BOARD_LCD_ROTATION 0
#endif
#ifndef BOARD_TOUCH_TASK_PRIO
#define BOARD_TOUCH_TASK_PRIO 5
#endif
#ifndef BOARD_TOUCH_INT_ACTIVE_LOW
#define BOARD_TOUCH_INT_ACTIVE_LOW 0
#endif
#ifndef BOARD_WIFI_BAND_2G_ONLY
#define BOARD_WIFI_BAND_2G_ONLY 0
#endif
#ifndef BOARD_BTN_BOOT_GPIO
#define BOARD_BTN_BOOT_GPIO -1
#endif
#ifndef BOARD_BTN_MEASURE_GPIO
#define BOARD_BTN_MEASURE_GPIO -1
#endif
#ifndef BOARD_SENSOR_SLOTS
#define BOARD_SENSOR_SLOTS 4
#endif
#ifndef BOARD_SLOT_LED_ON_LEVEL
#define BOARD_SLOT_LED_ON_LEVEL 1
#endif
#ifndef BOARD_SLOT_LED_PWM_DEFAULT
#define BOARD_SLOT_LED_PWM_DEFAULT 127
#endif
