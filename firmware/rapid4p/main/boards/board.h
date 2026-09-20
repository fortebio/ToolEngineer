#pragma once

/*
 * board.h — chọn board phần cứng theo Kconfig + `#ifndef` mặc định cho mọi knob.
 *
 * Mọi module dùng chân/địa chỉ/tần số PHẢI include file này, KHÔNG include thẳng
 * board_esp32*.h (luật HAL, MAPPING-Rapid4P.md §1.4 + CLAUDE.md).
 *
 * Hai board (2026-09-19):
 *   CONFIG_RAPID4P_BOARD_P4_43LCD → board_esp32p4_43lcd.h  (ESP32-P4C5, LCD 4.3" DSI, khoá `rapid4p`)
 *   CONFIG_RAPID4P_BOARD_S3_28LCD → board_esp32s3_28lcd.h  (ESP32-S3 ES3N28P, LCD 2.8" SPI, khoá `rapid4p-s3`)
 * Thêm board = thêm header + mục Kconfig + profile sdkconfig.defaults.<board> + khoá registry;
 * driver (display_hw_<x>.c, touch.c, thư mục sensor) KHÔNG sửa — chỉ đọc BOARD_*.
 */

#include "sdkconfig.h"

#if defined(CONFIG_RAPID4P_BOARD_P4_43LCD)
#include "board_esp32p4_43lcd.h"
#elif defined(CONFIG_RAPID4P_BOARD_S3_28LCD)
#include "board_esp32s3_28lcd.h"
#else
#error "Chua chon board: bat CONFIG_RAPID4P_BOARD_P4_43LCD hoac CONFIG_RAPID4P_BOARD_S3_28LCD (sdkconfig.defaults.<board>)"
#endif

/* ===== Nhận dạng sản phẩm (khoá kho OTA Engineer Server + PCB) ===== */
#ifndef BOARD_PRODUCT_KEY
#define BOARD_PRODUCT_KEY "rapid4p"
#endif
#ifndef BOARD_HW_VERSION
#define BOARD_HW_VERSION "unknown"
#endif

/* ===== Màn hình: đúng MỘT trong hai giao tiếp ===== */
#ifndef BOARD_LCD_USE_MIPI_DSI
#define BOARD_LCD_USE_MIPI_DSI 0
#endif
#ifndef BOARD_LCD_USE_SPI
#define BOARD_LCD_USE_SPI 0
#endif
#if BOARD_LCD_USE_MIPI_DSI + BOARD_LCD_USE_SPI != 1
#error "Board phai chon dung mot: BOARD_LCD_USE_MIPI_DSI hoac BOARD_LCD_USE_SPI"
#endif
#ifndef BOARD_LCD_ROTATION
#define BOARD_LCD_ROTATION 0
#endif
/* Panel SPI: số dòng mỗi draw buffer LVGL (×2 buffer, RAM nội DMA). */
#ifndef BOARD_LCD_DRAW_BUF_LINES
#define BOARD_LCD_DRAW_BUF_LINES 40
#endif
#ifndef BOARD_LCD_SPI_MODE
#define BOARD_LCD_SPI_MODE 0
#endif

/* ===== Chạm: đúng MỘT driver ===== */
#ifndef BOARD_TOUCH_USE_ST7123
#define BOARD_TOUCH_USE_ST7123 0
#endif
#ifndef BOARD_TOUCH_USE_FT6236
#define BOARD_TOUCH_USE_FT6236 0
#endif
#if BOARD_TOUCH_USE_ST7123 + BOARD_TOUCH_USE_FT6236 != 1
#error "Board phai chon dung mot driver cham: BOARD_TOUCH_USE_ST7123 hoac BOARD_TOUCH_USE_FT6236"
#endif
#ifndef BOARD_TOUCH_TASK_PRIO
#define BOARD_TOUCH_TASK_PRIO 5
#endif
#ifndef BOARD_TOUCH_INT_ACTIVE_LOW
#define BOARD_TOUCH_INT_ACTIVE_LOW 0
#endif
#ifndef BOARD_TOUCH_SWAP_XY
#define BOARD_TOUCH_SWAP_XY 0
#endif
#ifndef BOARD_TOUCH_MIRROR_X
#define BOARD_TOUCH_MIRROR_X 0
#endif
#ifndef BOARD_TOUCH_MIRROR_Y
#define BOARD_TOUCH_MIRROR_Y 0
#endif

/* ===== WiFi / nút / LED ===== */
#ifndef BOARD_WIFI_BAND_2G_ONLY
#define BOARD_WIFI_BAND_2G_ONLY 0
#endif
#ifndef BOARD_BTN_BOOT_GPIO
#define BOARD_BTN_BOOT_GPIO -1
#endif
#ifndef BOARD_BTN_MEASURE_GPIO
#define BOARD_BTN_MEASURE_GPIO -1
#endif
/* 3 nút vật lý XANH/ĐỎ/TRẮNG (vỏ máy Rapid 2.8"); -1 = board không có → UI softkey vẫn chạm được. */
#ifndef BOARD_BTN_GREEN_GPIO
#define BOARD_BTN_GREEN_GPIO -1
#endif
#ifndef BOARD_BTN_RED_GPIO
#define BOARD_BTN_RED_GPIO -1
#endif
#ifndef BOARD_BTN_WHITE_GPIO
#define BOARD_BTN_WHITE_GPIO -1
#endif
#ifndef BOARD_BTN_HOLD_MS
#define BOARD_BTN_HOLD_MS 1500   /* giữ nút softkey = hành động phụ (ReaderPlus dùng 5 s cho chức năng ẩn) */
#endif
#ifndef BOARD_STATUS_LED_GPIO
#define BOARD_STATUS_LED_GPIO -1   /* LED đơn báo trạng thái (chưa có driver — tư liệu) */
#endif

/* ===== Bo cảm biến ===== */
#ifndef BOARD_SENSOR_SLOTS
#define BOARD_SENSOR_SLOTS 4
#endif
#ifndef BOARD_SLOT_LED_ON_LEVEL
#define BOARD_SLOT_LED_ON_LEVEL 1
#endif
#ifndef BOARD_SLOT_LED_PWM_DEFAULT
#define BOARD_SLOT_LED_PWM_DEFAULT 127
#endif
