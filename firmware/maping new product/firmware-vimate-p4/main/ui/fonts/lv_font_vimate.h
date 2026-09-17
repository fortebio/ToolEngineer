/**
 * lv_font_vimate.h — Vietnamese-supporting fonts cho LVGL.
 *
 * Generated bằng lv_font_conv với Montserrat-Medium + ranges:
 *   0x20-0x7F       Basic Latin
 *   0xA0-0xFF       Latin-1 Supplement
 *   0x100-0x17F     Latin Extended-A (Ă, Đ, Ơ, Ư...)
 *   0x1E00-0x1EFF   Latin Extended Additional (ấ, ầ, ẩ, ặ, ẫ, ậ, ắ, ằ, ẳ, ẵ, ặ...)
 *   0x2026          Ellipsis "…"
 *
 * Dùng thay cho lv_font_montserrat_* (không có dấu Việt).
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(lv_font_vimate_14)
LV_FONT_DECLARE(lv_font_vimate_18)
LV_FONT_DECLARE(lv_font_vimate_24)
LV_FONT_DECLARE(lv_font_vimate_48)
/* Chỉ gồm 0-9 và ':' để số countdown 72px rõ mà không tăng BIN lớn. */
LV_FONT_DECLARE(lv_font_countdown_72)

#ifdef __cplusplus
}
#endif
