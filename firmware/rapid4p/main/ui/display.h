/**
 * display.h — lớp màn hình Rapid4P (LVGL 9 + esp_lvgl_port), API chung cho mọi board.
 *
 * display.c = phần chung (đèn nền LEDC, ngủ màn, lvgl_port_init, display task + hàng đợi);
 * phần panel theo board ở display_hw_<dsi|spi>.c qua display_hw.h (2026-09-19):
 *   P4 4.3": LDO DPHY → reset cứng GPIO22 → DSI → DPI 2 fb → xoay landscape PPA (README-P4 §6.6)
 *   S3 2.8": SPI3 → ILI9341 → 2 draw buffer 40 dòng RAM nội, xoay bằng MADCTL của panel
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
