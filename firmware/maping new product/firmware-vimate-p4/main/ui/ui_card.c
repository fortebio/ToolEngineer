/**
 * ui_card.c — DEPRECATED. Forward to display_set_message + ui_image_show.
 *
 * Trước đây tạo lv_obj_t riêng trên lv_layer_top với tự manage z-order.
 * Giờ: title+subtitle qua popup overlay; image (nếu có) qua preview overlay.
 */
#include "ui_card.h"
#include "ui_image.h"
#include "display.h"

void ui_card_show(const char *image_url, const char *title, const char *subtitle) {
    display_set_message(title, subtitle);
    if (image_url && image_url[0]) {
        ui_image_show(image_url);
    }
}
