/**
 * touch.h — cảm ứng ST7123 (I2C 0x55, giao thức register 16-bit) đăng ký làm
 * LVGL input device (lv_indev POINTER).
 *
 * Khác vimate-p4 (task riêng + hit-test tay): Rapid4P dùng widget LVGL chuẩn (button,
 * list, spinbox) nên để LVGL tự dispatch sự kiện chạm. read_cb chạy trong LVGL task
 * (giữa các lần render) — không cần lock, không thêm task.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gọi SAU display_init() (cần LVGL) và sau khi bus I2C_NUM_0 có thể đã được tạo
 * bởi module khác — hàm tự get-handle trước, tạo bus khi chưa có. */
esp_err_t touch_init(void);
bool touch_is_ready(void);

#ifdef __cplusplus
}
#endif
