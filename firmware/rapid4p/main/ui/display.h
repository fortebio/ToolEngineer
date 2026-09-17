/**
 * display.h — lớp phần cứng màn hình Rapid4P (ST7102 MIPI-DSI + LVGL 9 + esp_lvgl_port).
 *
 * Rút từ firmware-vimate-p4/main/ui/display.c (6758 dòng) đúng phần KHÔNG phụ thuộc
 * sản phẩm: LDO DPHY → reset cứng GPIO22 → DSI → DPI 2 fb → lvgl_port → xoay landscape
 * bằng PPA (khối DSI_ROTATE, README-P4 §6.6) → đèn nền LEDC → display task + hàng đợi.
 * Toàn bộ màn hình của reader nằm ở ui_reader.c.
 *
 * Luật (CLAUDE.md):
 *   - LVGL chỉ được gọi TRONG display task/LVGL task hoặc giữa display_lock()/unlock().
 *   - Task khác (đo, mạng, nút) đổi UI qua display_schedule(cb, arg): cb chạy trong
 *     display task dưới lock. Không lv_* từ ISR / task đo.
 *   - GPIO22 (LCD_RST = TP_RST) chỉ file này được toggle, một lần trước DSI.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bước 1: phần cứng + LVGL port. Gọi sớm trong app_main. Lỗi → trả lỗi, KHÔNG abort
 * (boot tiếp không màn để WiFi/OTA còn cứu được máy). */
esp_err_t display_init(void);
bool display_hw_ready(void);
int display_lcd_width(void);    /* logical (800) */
int display_lcd_height(void);   /* logical (480) */

/* Lock LVGL (recursive, qua lvgl_port). Dùng khi PHẢI gọi lv_* từ task khác. */
void display_lock(void);
void display_unlock(void);

/* Đẩy một callback vào display task (chạy dưới lock). Trả false nếu hàng đợi đầy /
 * chưa init — caller tự free arg trong trường hợp đó. */
bool display_schedule(void (*cb)(void *), void *arg);

/* Đèn nền 0..100 (LEDC GPIO6). Tự tắt sau N giây không có thao tác; 0 = không tắt. */
void display_set_backlight(int percent);
void display_set_sleep_timeout(int seconds);
/* Touch/nút gọi: reset đồng hồ ngủ + bật lại đèn nền nếu đang tắt. Thread-safe. */
void display_note_user_activity(void);
bool display_is_sleeping(void);

/* Thống kê xoay PPA (diag). Trả false nếu board không xoay. */
bool display_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max);
uint32_t display_flush_count(void);

#ifdef __cplusplus
}
#endif
