/**
 * ui_reader.h — toàn bộ màn hình Rapid4P (LVGL 9, logical 800×480, chạm).
 *
 * Máy trạng thái giữ ĐÚNG tập trạng thái của FBT-ReaderPlus-1.0 (e_statuslcd,
 * MAPPING §3.3) nhưng điều hướng bằng nút chạm thay 3 nút cơ (MAPPING §4.3):
 *   START → CHOOSE_SAMPLE → CHOOSE_TUBE → PREPARE → MEASURING → RESULT
 *   START → CALIB (4 slot × Max/Min) ; START → SETTINGS → {LANGUAGE, WIFI, UPDATE, THRESHOLD}
 *
 * Luật: mọi hàm ui_reader_* gọi từ task khác đều tự bọc display_schedule; chỉ code
 * trong file này (chạy trong LVGL/display task) mới gọi lv_*.
 */
#pragma once
#include "esp_err.h"
#include "rapid4p.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_START = 0,
    UI_CHOOSE_SAMPLE,
    UI_CHOOSE_TUBE,
    UI_PREPARE,
    UI_MEASURING,
    UI_RESULT,
    UI_CALIB,
    UI_SETTINGS,
    UI_LANGUAGE,
    UI_WIFI,
    UI_UPDATE,
    UI_THRESHOLD,
    UI_THRESHOLD_EDIT,
    UI_COUNT
} ui_state_t;

/* Dựng màn + đăng ký callback tiến độ đo. Gọi SAU display_init + measure_init. */
esp_err_t ui_reader_init(void);
ui_state_t ui_reader_state(void);

/* Sự kiện từ ngoài (thread-safe, qua display_schedule) */
void ui_reader_on_measure_button(void);      /* BTN3 GPIO0: xác nhận/đo như nút ĐO trên màn */
void ui_reader_on_boot_button(void);         /* BOOT tap: thoát màn WiFi / quay lại */
/* 3 nút vật lý XANH/ĐỎ/TRẮNG (vỏ máy Rapid 2.8"): hành động = softkey của màn đang hiện
 * (ui_reader.c apply_key). hold = giữ ≥ BOARD_BTN_HOLD_MS. Chạm ô softkey trên màn gọi cùng hàm. */
void ui_reader_on_key(r4p_key_t key, bool hold);
/* Bring-up trục chạm (CONFIG_RAPID4P_TOUCH_LOG): touch.c gọi TRONG LVGL task khi nhấn xuống →
 * header phải hiện "T x,y r x,y" thay trạng thái WiFi để người cầm máy đọc số ngay trên LCD. */
void ui_reader_touch_debug(int x, int y, int raw_x, int raw_y);
/* Dev console: nhảy thẳng tới màn (xem bố cục qua camera). Không dùng trong luồng thật. */
void ui_reader_show_debug(ui_state_t st);
void ui_reader_set_dev_state(r4p_dev_state_t s);
void ui_reader_set_wifi(bool connected, const char *ip);
void ui_reader_set_upload_stats(int pending, int sent);

#ifdef __cplusplus
}
#endif
