/**
 * ui_activation.c — DEPRECATED. Forward to display_show_activation.
 */
#include "ui_activation.h"
#include "display.h"

void ui_activation_show(const char *code, const char *message) {
    display_show_activation(code, message);
}
