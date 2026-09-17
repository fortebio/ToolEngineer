/**
 * ui_error.c — DEPRECATED. Forward to display_set_message.
 */
#include "ui_error.h"
#include "display.h"

void ui_error_show(const char *title, const char *body) {
    display_set_message(title ? title : "Lỗi", body);
}
