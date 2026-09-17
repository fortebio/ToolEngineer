/**
 * ui_home.c — DEPRECATED. Forward to new display API.
 *
 * "Home screen" giờ chính là lv_screen_active (no scr swap). Init = setup_ui.
 */
#include "ui_home.h"
#include "display.h"
#include "ui_image.h"

esp_err_t ui_home_init(void) {
    return display_setup_ui();
}

void ui_home_show_state(vimate_dev_state_t s) {
    display_set_state(s);
}

void ui_home_show_message(const char *title, const char *body) {
    display_set_message(title, body);
}

void ui_home_show_stt(const char *text) {
    display_set_chat_message("user", text);
}

void ui_home_activate(void) {
    /* No-op: single screen architecture. Để revert về home, hide popups
     * bằng display_set_message(NULL, NULL). */
    ui_image_hide();
    display_set_message(NULL, NULL);
    display_set_state(DEV_STATE_READY);
}
