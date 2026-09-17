/**
 * ui_emotion.c — DEPRECATED, forward to display_set_emotion.
 *
 * Trước đây tự manage widget riêng trên lv_layer_top với asset_pack SPIFFS
 * lookup → gây crash transform_rgb565a8 + SPIFFS race. Toàn bộ logic giờ
 * nằm trong display.c (single source of truth) — emoji_image là persistent
 * widget trên lv_screen_active(), scale overscan để avatar tràn LCD 320×240.
 */
#include "ui_emotion.h"
#include "display.h"

void ui_emotion_show(vimate_emotion_t e) {
    display_set_emotion(e);
}

void ui_emotion_hide(void) {
    /* No-op: emoji_image là persistent widget. Để "ẩn" overlay khác đè qua
     * display_show_preview_image() hoặc display_set_message() (chúng tự
     * move_foreground). */
}
