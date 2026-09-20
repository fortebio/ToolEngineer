/**
 * ui_logo.c — xem ui_logo.h. Toạ độ gốc đơn vị px trên khung 2000×1780 của file chuẩn.
 */
#include "ui_logo.h"
#include "fonts/lv_font_vimate.h"

#define LOGO_W 2000
#define LOGO_H 1780

typedef struct { int16_t x, y; } pt_t;
typedef struct { pt_t p[3]; uint32_t color; } tri_t;

/* 5 tam giác, theo thứ tự vẽ (không chồng nhau nên thứ tự không quan trọng). */
static const tri_t s_tris[] = {
    { { { 762,  117 }, { 2000,    0 }, { 762,  627 } }, 0x17A6BF },   /* trên phải, teal đậm */
    { { {  50,  547 }, {  690,  117 }, { 690,  880 } }, 0x26C5CF },   /* trái trên, teal */
    { { {   5,  630 }, {  690, 1010 }, {   5, 1330 } }, 0x26C5CF },   /* trái giữa, teal */
    { { {  95, 1442 }, {  690, 1092 }, { 690, 1780 } }, 0x1BD1A5 },   /* trái dưới, xanh lục */
    { { { 762, 1402 }, { 1290, 1362 }, { 762, 1790 } }, 0x6FE6C3 },   /* dưới phải, mint */
};
#define TEXT_COLOR 0x26C5CF
/* Vùng chữ trong khung gốc: FORTE x 790..1560, y 690..890 · BIOTECH x 790..1870, y 1040..1240 */

static void logo_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    /* Mark luôn theo tỉ lệ gốc từ chiều cao; object có thể RỘNG hơn để chứa chữ (xem ui_logo_create). */
    const int h = lv_area_get_height(&a);
    const int w = h * LOGO_W / LOGO_H;

    lv_draw_triangle_dsc_t d;
    for (size_t i = 0; i < sizeof(s_tris) / sizeof(s_tris[0]); i++) {
        lv_draw_triangle_dsc_init(&d);
        d.color = lv_color_hex(s_tris[i].color);
        d.opa = LV_OPA_COVER;
        for (int k = 0; k < 3; k++) {
            d.p[k].x = a.x1 + (lv_value_precise_t)s_tris[i].p[k].x * w / LOGO_W;
            d.p[k].y = a.y1 + (lv_value_precise_t)s_tris[i].p[k].y * h / LOGO_H;
        }
        lv_draw_triangle(layer, &d);
    }
}

static const lv_font_t *font_for_cap(int cap_px)
{
    /* cap height ≈ 0,7 × size với Montserrat */
    if (cap_px >= 32) return &lv_font_vimate_48;
    if (cap_px >= 16) return &lv_font_vimate_24;
    if (cap_px >= 12) return &lv_font_vimate_18;
    return &lv_font_vimate_14;
}

lv_obj_t *ui_logo_create(lv_obj_t *parent, int h, bool with_text)
{
    const int w = h * LOGO_W / LOGO_H;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_add_event_cb(o, logo_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    if (with_text) {
        const lv_font_t *f = font_for_cap(200 * h / LOGO_H);
        lv_obj_t *l1 = lv_label_create(o);
        lv_label_set_text(l1, "FORTE");
        lv_obj_set_style_text_font(l1, f, 0);
        lv_obj_set_style_text_color(l1, lv_color_hex(TEXT_COLOR), 0);
        lv_obj_set_style_text_letter_space(l1, 1, 0);
        lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 790 * w / LOGO_W, 790 * h / LOGO_H - lv_font_get_line_height(f) / 2);

        lv_obj_t *l2 = lv_label_create(o);
        lv_label_set_text(l2, "BIOTECH");
        lv_obj_set_style_text_font(l2, f, 0);
        lv_obj_set_style_text_color(l2, lv_color_hex(TEXT_COLOR), 0);
        lv_obj_set_style_text_letter_space(l2, 1, 0);
        lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 790 * w / LOGO_W, 1140 * h / LOGO_H - lv_font_get_line_height(f) / 2);
        /* Font vimate rộng hơn chữ trong file gốc (BIOTECH gốc tới x 1870/2000): logo 120 px trên 4.3" bị
         * cắt "H" (framebuffer 2026-09-21) → nới object tới mép phải của chữ, mark vẫn vẽ theo chiều cao. */
        lv_point_t sz;
        lv_text_get_size(&sz, "BIOTECH", f, 1, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int right = 790 * w / LOGO_W + sz.x + 2;
        if (right > w) lv_obj_set_width(o, right);
    }
    return o;
}
