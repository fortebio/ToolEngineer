/**
 * display_hw.h — giao diện lớp phần cứng màn hình theo board (2026-09-19).
 *
 * display.c (phần chung: đèn nền LEDC, ngủ màn, hàng đợi + display task, lock, kích thước) gọi
 * đúng MỘT hiện thực, chọn trong main/CMakeLists.txt theo CONFIG_RAPID4P_BOARD_*:
 *   display_hw_dsi.c  — ESP32-P4C5 + ST7102 MIPI-DSI 480×800, xoay PPA (BOARD_LCD_USE_MIPI_DSI)
 *   display_hw_spi.c  — ESP32-S3 + ILI9341 SPI 320×240 (BOARD_LCD_USE_SPI)
 * Thêm panel mới = thêm file display_hw_<x>.c, KHÔNG sửa display.c / ui_reader.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khởi tạo panel + IO + lvgl_port_add_disp*(); trả display LVGL logical (BOARD_LCD_H_RES ×
 * BOARD_LCD_V_RES). Được gọi SAU lvgl_port_init(), TRƯỚC khi bật đèn nền. Lỗi → trả mã lỗi,
 * KHÔNG abort (display.c bỏ màn, máy vẫn boot để WiFi/OTA cứu được). */
esp_err_t display_hw_init(lv_display_t **out_disp);

/* Thống kê xoay (chỉ DSI + PPA). Trả false nếu board không xoay bằng phần cứng. */
bool display_hw_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max);

/* Số lần flush ra panel (diag). */
uint32_t display_hw_flush_count(void);

#ifdef __cplusplus
}
#endif
