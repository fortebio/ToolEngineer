/**
 * ui_theme.h — token thiết kế dùng chung cho mọi màn LVGL của Rapid4P (ui_reader, ui_wifi_setup).
 *
 * Nguồn sự thật: `system/brand/tokens.json` (màu lấy từ file logo chuẩn 2000×1780) — đổi màu
 * thì sửa JSON trước rồi chép sang đây. Nền tối "navy ám teal" để hợp với logo. Mọi cặp chữ/nền
 * ≥ 4,5:1 (đo 2026-09-17): text/bg 17:1 · muted/bg 9,5:1 · teal/bg 8,9:1 · green/bg 9,5:1 ·
 * red/bg 5,4:1 · on-brand/teal 7,6:1.
 * Icon `LV_SYMBOL_*` chỉ có trong Montserrat; chữ Việt/Trung dùng vimate (không có dải symbol,
 * không có U+2014 "—").
 */
#pragma once
#include "lvgl.h"
#include "fonts/lv_font_vimate.h"

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

/* ===== kích thước ===== */
#define HEADER_H    56
#define HEADER_LOGO_H 40   /* logo mark (không chữ) góc trái header; tiêu đề bắt đầu sau HEADER_TITLE_X */
#define HEADER_TITLE_X (12 + HEADER_LOGO_H * 2000 / 1780 + 12)
#define FOOTER_H    96
#define BTN_H       72          /* ≥ 64 px cho ngón tay (README-P4 §6.7) */
#define GAP         12

/* ===== font ===== */
#define F_TITLE     (&lv_font_vimate_24)
#define F_BODY      (&lv_font_vimate_24)
#define F_SMALL     (&lv_font_vimate_18)
#define F_TINY      (&lv_font_vimate_14)
#define F_HERO      (&lv_font_vimate_48)
#define F_ICON      (&lv_font_montserrat_24)  /* LV_SYMBOL_* */
#define F_ICON_SM   (&lv_font_montserrat_18)
