/**
 * ui_theme.h — token thiết kế dùng chung cho mọi màn LVGL của Rapid4P (ui_reader, ui_wifi_setup).
 *
 * Nguồn sự thật: `system/brand/tokens.json` (màu lấy từ file logo chuẩn 2000×1780; thang kích
 * thước/chữ theo màn ở `typography.scales`) — đổi token thì sửa JSON trước rồi chép sang đây.
 * Nền tối "navy ám teal" để hợp với logo. Mọi cặp chữ/nền ≥ 4,5:1 (đo 2026-09-17): text/bg 17:1 ·
 * muted/bg 9,5:1 · teal/bg 8,9:1 · green/bg 9,5:1 · red/bg 5,4:1 · on-brand/teal 7,6:1.
 * Icon `LV_SYMBOL_*` chỉ có trong Montserrat; chữ Việt/Trung dùng vimate (không có dải symbol,
 * không có U+2014 "—").
 *
 * HAI THANG theo màn (2026-09-19, chọn compile-time từ BOARD_LCD_H_RES):
 *   lcd-4.3in  800×480 (P4)  — thang gốc, số giữ nguyên như trước.
 *   lcd-2.8in  320×240 (S3)  — UI_SCALE_SMALL 1: header 32 · footer 52 · nút 44 (≈ 7,9 mm, tương
 *              đương 72 px trên 4.3") · gap 6 · chữ 18/14, số khe 24 (tự hạ 18 nếu tràn ô).
 * Mọi bề rộng tuyệt đối của ui_reader.c đi qua token UI_* dưới đây — KHÔNG ghi số px trong màn.
 */
#pragma once
#include "lvgl.h"
#include "fonts/lv_font_vimate.h"
#include "boards/board.h"

/* ===== màu ===== */
#define C_BG        lv_color_hex(0x0A1418)   /* navy-950: nền, ám teal */
#define C_PANEL     lv_color_hex(0x10202A)   /* navy-900: header, footer, hộp thoại */
#define C_CARD      lv_color_hex(0x152A35)   /* navy-800: ô khe, chip, thẻ */
#define C_BORDER    lv_color_hex(0x1F3A46)   /* navy-700 */
#define C_BTN       lv_color_hex(0x2A3F4C)   /* navy-600: nút phụ */
#define C_FORTE     lv_color_hex(0x26C5CF)   /* teal-500: chủ đạo (wordmark logo) — hành động chính */
#define C_FORTE_DK  lv_color_hex(0x17A6BF)   /* teal-700: pressed, đầu gradient */
#define C_MINT      lv_color_hex(0x6FE6C3)   /* mint-300: điểm nhấn sáng, cuối gradient */
#define C_ON_FORTE  lv_color_hex(0x06262A)   /* on-brand: chữ trên nền teal/green */
#define C_TEXT      lv_color_hex(0xF2F8F9)
#define C_MUTED     lv_color_hex(0xA9BCC4)
#define C_GREEN     lv_color_hex(0x1BD1A5)   /* green-500 của logo: thành công / âm tính */
#define C_AMBER     lv_color_hex(0xFFB300)   /* cảnh báo (ngoài logo, ngữ nghĩa) */
#define C_RED       lv_color_hex(0xEF5350)   /* nguy hiểm / dương tính (ngoài logo, ngữ nghĩa) */

/* Gradient thương hiệu cho thanh tiến độ (theo cột phải logo: teal đậm → mint). */
static inline void ui_theme_brand_bar(lv_obj_t *bar)
{
    lv_obj_set_style_bg_color(bar, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, C_FORTE_DK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, C_MINT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
}

/* ===== thang theo màn ===== */
#define UI_W            BOARD_LCD_H_RES
#define UI_H            BOARD_LCD_V_RES

#if BOARD_LCD_H_RES >= 800
/* ---------- lcd-4.3in (800×480) ---------- */
#define UI_SCALE_SMALL  0
#define HEADER_H        56
#define HEADER_LOGO_H   40      /* logo mark (không chữ) góc trái header; tiêu đề bắt đầu sau HEADER_TITLE_X */
#define FOOTER_H        96
#define BTN_H           72      /* ≥ 64 px cho ngón tay (README-P4 §6.7) */
#define GAP             12
#define UI_PAD          20      /* lề ngang content */
#define UI_FOOTER_PAD   20
#define UI_HEADER_PAD   12
#define UI_RADIUS       14
#define UI_BTN_PAD      12      /* pad_hor nút */
#define UI_BTN_ICON_W   34      /* chỗ cho icon trong nút (fit_font trừ đi) */
#define UI_BTN_BACK_W   210
#define UI_BTN_MAIN_W   260
#define UI_BTN_DANGER_W 200
#define UI_BTN_LIST_W   236     /* nút danh sách mẫu/bệnh (lưới) */
#define UI_BTN_GRID_W   300     /* lưới cài đặt 2×2 */
#define UI_MODAL_W      560
#define UI_MODAL_H      240
#define UI_MODAL_PAD    24
#define UI_MODAL_Q_W    440     /* câu hỏi hộp thoại */
#define UI_MODAL_BTN_NO_W  230
#define UI_MODAL_BTN_YES_W 250
#define UI_BAR_W        560
#define UI_BAR_H        16
#define UI_BAR_LBL_W    60
#define UI_TILE_H       156
#define UI_TILE_GAP     (GAP + 4)
#define UI_TILE_PAD     8
#define UI_TILE_BORDER  3
#define UI_TITLE_W      430     /* = 800 − HEADER_TITLE_X − trạng thái dài nhất (~270) − lề */
#define UI_TITLE_H      30
#define UI_HOME_LOGO_H  120     /* logo lớn màn chính (có chữ khi ≥ 90) */
#define UI_CHIP_H       40
#define UI_CHIP_ROW_H   48
#define UI_ROW_SMALL_H  40
#define UI_PROGRESS_ROW_H 28
#define UI_THR_BTN_W    130
#define UI_THR_BTN_H    110
#define UI_THR_CARD_W   260
#define UI_THR_ROW_H    120
#define UI_STATUS_FULL  1       /* header đủ: icon WiFi + IP + upload n/n + version */

#define F_TITLE     (&lv_font_vimate_24)
#define F_BODY      (&lv_font_vimate_24)
#define F_SMALL     (&lv_font_vimate_18)
#define F_TINY      (&lv_font_vimate_14)
#define F_HERO      (&lv_font_vimate_48)
#define F_ICON      (&lv_font_montserrat_24)  /* LV_SYMBOL_* */
#define F_ICON_SM   (&lv_font_montserrat_18)

#else
/* ---------- lcd-2.8in (320×240) ---------- */
#define UI_SCALE_SMALL  1
#define HEADER_H        32
#define HEADER_LOGO_H   24
#define FOOTER_H        52
#define BTN_H           44      /* ≈ 7,9 mm trên panel 2.8" */
#define GAP             6
#define UI_PAD          8
#define UI_FOOTER_PAD   6
#define UI_HEADER_PAD   6
#define UI_RADIUS       8
#define UI_BTN_PAD      6
#define UI_BTN_ICON_W   24
#define UI_BTN_BACK_W   80
#define UI_BTN_MAIN_W   116
#define UI_BTN_DANGER_W 100
#define UI_BTN_LIST_W   140
#define UI_BTN_GRID_W   140
#define UI_MODAL_W      224
#define UI_MODAL_H      150
#define UI_MODAL_PAD    10
#define UI_MODAL_Q_W    170
#define UI_MODAL_BTN_NO_W  96
#define UI_MODAL_BTN_YES_W 104
#define UI_BAR_W        220
#define UI_BAR_H        10
#define UI_BAR_LBL_W    40
#define UI_TILE_H       84
#define UI_TILE_GAP     4
#define UI_TILE_PAD     2
#define UI_TILE_BORDER  1
#define UI_TITLE_W      160
#define UI_TITLE_H      22
#define UI_HOME_LOGO_H  56
#define UI_CHIP_H       26
#define UI_CHIP_ROW_H   30
#define UI_ROW_SMALL_H  26
#define UI_PROGRESS_ROW_H 20
#define UI_THR_BTN_W    56
#define UI_THR_BTN_H    64
#define UI_THR_CARD_W   140
#define UI_THR_ROW_H    72
#define UI_STATUS_FULL  0       /* header gọn: icon WiFi + upload n; IP/version xuống màn chính */

#define F_TITLE     (&lv_font_vimate_18)
#define F_BODY      (&lv_font_vimate_18)
#define F_SMALL     (&lv_font_vimate_14)
#define F_TINY      (&lv_font_vimate_14)
#define F_HERO      (&lv_font_vimate_24)
#define F_ICON      (&lv_font_montserrat_18)
#define F_ICON_SM   (&lv_font_montserrat_14)
#endif

#define HEADER_TITLE_X  (UI_HEADER_PAD + HEADER_LOGO_H * 2000 / 1780 + UI_HEADER_PAD)
#define UI_CONTENT_W    (UI_W - 2 * UI_PAD)          /* 760 / 304 */
