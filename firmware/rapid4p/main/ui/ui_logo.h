/**
 * ui_logo.h — logo FORTE BIOTECH vẽ vector (5 tam giác + chữ), không cần ảnh/decoder.
 *
 * Toạ độ lấy từ file logo chuẩn 2000×1780 px (nhận 2026-09-17): cột trái 3 tam giác
 * (teal, teal, xanh lục), cột phải tam giác lớn trên (teal đậm) + tam giác nhỏ dưới (mint),
 * chữ FORTE / BIOTECH nằm giữa hai cột, màu teal #26C5CF. Vẽ bằng lv_draw_triangle trong
 * LV_EVENT_DRAW_MAIN nên sắc nét ở mọi kích thước; chữ dùng font vimate (Montserrat — gần
 * với font gốc) chọn theo chiều cao.
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tạo logo cao `h` px (rộng tự tính theo tỉ lệ 2000:1780). with_text=false → chỉ 5 tam
 * giác (dùng khi h < ~90, chữ sẽ không đọc được). Gọi trong LVGL/display task. */
lv_obj_t *ui_logo_create(lv_obj_t *parent, int h, bool with_text);

#ifdef __cplusplus
}
#endif
