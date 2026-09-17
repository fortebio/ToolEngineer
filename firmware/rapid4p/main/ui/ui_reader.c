/**
 * ui_reader.c — màn hình Rapid4P. Xem ui_reader.h.
 *
 * Bố cục 800×480: header 56 px (tiêu đề trái, trạng thái WiFi/gửi/version phải) +
 * vùng nội dung 800×424 dựng lại mỗi lần đổi trạng thái (lv_obj_clean).
 * Màu "Forte_Green" 0x25F8 (RGB565 của bản ILI9341) ≈ #21BEC5.
 */
#include "ui_reader.h"
#include "display.h"
#include "ui_strings.h"
#include "fonts/lv_font_vimate.h"
#include "app/measure.h"
#include "app/calib_store.h"
#include "core/wifi_mgr.h"
#include "core/task_profile.h"
#include "network/ota_client.h"
#include "network/result_upload.h"
#include "ui/ui_wifi_setup.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define C_BG        lv_color_hex(0x000000)
#define C_FORTE     lv_color_hex(0x21BEC5)
#define C_PANEL     lv_color_hex(0x161A1E)
#define C_TEXT      lv_color_hex(0xFFFFFF)
#define C_MUTED     lv_color_hex(0x9AA3AD)
#define C_RED       lv_color_hex(0xE53935)
#define C_GREEN     lv_color_hex(0x43A047)
#define C_AMBER     lv_color_hex(0xFFB300)
#define C_BTN       lv_color_hex(0x2A3138)

#define HEADER_H    56
#define BTN_H       72          /* ≥ 64 px cho ngón tay (README-P4 §6.7) */

static struct {
    lv_obj_t *screen, *header, *title, *status, *content;
    ui_state_t state;
    r4p_dev_state_t dev_state;
    r4p_sick_t sick;
    r4p_sample_t sample;
    /* đo */
    lv_obj_t *slot_tile[R4P_SLOTS], *slot_val[R4P_SLOTS], *round_lbl, *bar;
    /* calib */
    int calib_slot, calib_step;         /* step 0 = Cao nhất (max), 1 = Thấp nhất (min) */
    uint16_t calib_max_tmp;
    bool calib_running;
    lv_obj_t *calib_status;
    /* ngưỡng */
    r4p_sick_t thr_sick;
    lv_obj_t *spin;
    /* cập nhật */
    lv_obj_t *upd_lbl, *upd_bar;
    /* header */
    bool wifi_connected;
    char ip[16];
    int pending, sent;
} s;

static void show(ui_state_t st);
static void apply_show(void *arg) { show((ui_state_t)(intptr_t)arg); }
/* Goi tu callback su kien LVGL: doi man o luot display task ke tiep, KHONG xoa widget
 * dang phat su kien ngay trong callback cua no. */
static void show_async(ui_state_t st) { display_schedule(apply_show, (void *)(intptr_t)st); }

/* ===== tiện ích widget ===== */
static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *ud,
                        int w, int h, lv_color_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(bg, 60), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = mk_label(b, txt, &lv_font_vimate_24, C_TEXT);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static lv_obj_t *mk_row(lv_obj_t *parent, int h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

static void content_clear(void)
{
    lv_obj_clean(s.content);
    lv_obj_set_flex_flow(s.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s.content, 12, 0);
    lv_obj_set_style_pad_all(s.content, 16, 0);
    memset(s.slot_tile, 0, sizeof(s.slot_tile));
    memset(s.slot_val, 0, sizeof(s.slot_val));
    s.round_lbl = s.bar = s.calib_status = s.spin = s.upd_lbl = s.upd_bar = NULL;
}

static void set_title(const char *t) { lv_label_set_text(s.title, t); }

static void header_refresh(void)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %s   " LV_SYMBOL_UPLOAD " %d/%d   %s",
             s.wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE,
             s.wifi_connected ? s.ip : "--", s.pending, s.sent, R4P_FW_VERSION);
    lv_label_set_text(s.status, buf);
    lv_obj_set_style_text_color(s.status, s.wifi_connected ? C_FORTE : C_MUTED, 0);
}

/* ===== callbacks nút ===== */
static void on_goto(lv_event_t *e) { show_async((ui_state_t)(intptr_t)lv_event_get_user_data(e)); }

static void on_pick_sample(lv_event_t *e)
{
    s.sample = (r4p_sample_t)(intptr_t)lv_event_get_user_data(e);
    show_async(UI_CHOOSE_TUBE);
}

static void on_pick_tube(lv_event_t *e)
{
    s.sick = (r4p_sick_t)(intptr_t)lv_event_get_user_data(e);
    show_async(UI_PREPARE);
}

static void start_measure(void)
{
    esp_err_t r = measure_start(s.sick, s.sample);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_UI, "measure_start: %s", esp_err_to_name(r));
        return;
    }
    show_async(UI_MEASURING);
}

static void on_measure(lv_event_t *e) { (void)e; start_measure(); }

static void on_abort(lv_event_t *e)
{
    (void)e;
    measure_abort();
    show_async(UI_PREPARE);
}

static void on_finish(lv_event_t *e)
{
    (void)e;
    show_async(UI_CHOOSE_SAMPLE);
}

static void on_pick_lang(lv_event_t *e)
{
    r4p_lang_t l = (r4p_lang_t)(intptr_t)lv_event_get_user_data(e);
    calib_store_set_lang(l);
    r4p_str_set_lang(l);
    show_async(UI_START);
}

static void on_calib_read(lv_event_t *e)
{
    (void)e;
    if (s.calib_running) return;
    if (measure_calib_start(s.calib_slot) == ESP_OK) {
        s.calib_running = true;
        if (s.calib_status) lv_label_set_text(s.calib_status, r4p_str(STR_CALIBRATING));
    }
}

static void on_calib_clear(lv_event_t *e)
{
    (void)e;
    calib_store_format_calib();
    s.calib_slot = 0;
    s.calib_step = 0;
    show_async(UI_CALIB);
}

static void on_pick_thr(lv_event_t *e)
{
    s.thr_sick = (r4p_sick_t)(intptr_t)lv_event_get_user_data(e);
    show_async(UI_THRESHOLD_EDIT);
}

static void on_spin_inc(lv_event_t *e) { (void)e; if (s.spin) lv_spinbox_increment(s.spin); }
static void on_spin_dec(lv_event_t *e) { (void)e; if (s.spin) lv_spinbox_decrement(s.spin); }

static void on_thr_save(lv_event_t *e)
{
    (void)e;
    if (!s.spin) return;
    int32_t v = lv_spinbox_get_value(s.spin);
    if (v < 0) v = 0;
    calib_store_set_threshold(s.thr_sick, (uint32_t)v);
    show_async(UI_THRESHOLD);
}

static void on_wifi_back(lv_event_t *e)
{
    (void)e;
    wifi_mgr_stop_provisioning();
    lv_screen_load(s.screen);
    show_async(UI_SETTINGS);
}

/* ===== OTA từ màn (task nền → display_schedule) ===== */
typedef struct { int pct; char msg[32]; esp_err_t err; bool final; } upd_msg_t;

static void apply_update_msg(void *arg)
{
    upd_msg_t *m = arg;
    if (s.state == UI_UPDATE && s.upd_lbl) {
        char buf[96];
        if (m->final) {
            if (m->err == ESP_ERR_NOT_FOUND) snprintf(buf, sizeof(buf), "%s", r4p_str(STR_NO_UPDATE));
            else if (m->err == ESP_ERR_WIFI_NOT_CONNECT) snprintf(buf, sizeof(buf), "WiFi: --");
            else if (m->err == ESP_ERR_INVALID_STATE) snprintf(buf, sizeof(buf), "Token: --");
            else snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_UPDATE), esp_err_to_name(m->err));
        } else {
            snprintf(buf, sizeof(buf), "%s %s %d%%", r4p_str(STR_UPDATING), m->msg, m->pct);
        }
        lv_label_set_text(s.upd_lbl, buf);
        if (s.upd_bar) lv_bar_set_value(s.upd_bar, m->pct, LV_ANIM_OFF);
    }
    free(m);
}

static void post_update_msg(int pct, const char *msg, esp_err_t err, bool final)
{
    upd_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->pct = pct; m->err = err; m->final = final;
    if (msg) strlcpy(m->msg, msg, sizeof(m->msg));
    if (!display_schedule(apply_update_msg, m)) free(m);
}

static void ota_progress(int pct, const char *msg, void *ctx)
{
    (void)ctx;
    post_update_msg(pct, msg, ESP_OK, false);
}

static void update_task(void *arg)
{
    (void)arg;
    esp_err_t e = ota_client_check_now(ota_progress, NULL);
    post_update_msg(0, NULL, e, true);
    vTaskDelete(NULL);
}

/* ===== tiến độ đo (từ task đo) ===== */
typedef struct { measure_phase_t phase; measure_result_t r; } prog_msg_t;

static void slot_tile_set(int i, const char *txt, lv_color_t color)
{
    if (!s.slot_val[i]) return;
    lv_label_set_text(s.slot_val[i], txt);
    lv_obj_set_style_border_color(s.slot_tile[i], color, 0);
}

static void apply_progress(void *arg)
{
    prog_msg_t *m = arg;
    const measure_result_t *r = &m->r;
    char buf[48];
    switch (m->phase) {
        case MEASURE_PHASE_SLOT_START:
            if (s.state != UI_MEASURING) break;
            snprintf(buf, sizeof(buf), "%s %d/%d", r4p_str(STR_ROUND), r->round, R4P_ROUNDS);
            if (s.round_lbl) lv_label_set_text(s.round_lbl, buf);
            slot_tile_set(r->slot, "...", C_AMBER);
            if (s.bar) lv_bar_set_value(s.bar, ((r->round - 1) * R4P_SLOTS + r->slot) * 100 / (R4P_ROUNDS * R4P_SLOTS), LV_ANIM_OFF);
            break;
        case MEASURE_PHASE_SLOT_DONE:
            if (s.state != UI_MEASURING) break;
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)r->result[r->slot][r->round - 1]);
            slot_tile_set(r->slot, buf, C_FORTE);
            if (s.bar) lv_bar_set_value(s.bar, ((r->round - 1) * R4P_SLOTS + r->slot + 1) * 100 / (R4P_ROUNDS * R4P_SLOTS), LV_ANIM_OFF);
            break;
        case MEASURE_PHASE_DONE:
            result_upload_enqueue(r);
            if (s.state == UI_MEASURING) show(UI_RESULT);
            break;
        case MEASURE_PHASE_ABORTED:
            break;
        case MEASURE_PHASE_ERROR:
            if (s.state == UI_MEASURING) {
                slot_tile_set(r->slot, r4p_str(STR_SENSOR_ERROR), C_RED);
                if (s.round_lbl) {
                    snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_SENSOR_ERROR), esp_err_to_name(r->err));
                    lv_label_set_text(s.round_lbl, buf);
                }
            } else if (s.state == UI_CALIB) {
                s.calib_running = false;
                if (s.calib_status) lv_label_set_text(s.calib_status, r4p_str(STR_SENSOR_ERROR));
            }
            break;
        case MEASURE_PHASE_CALIB_DONE:
            s.calib_running = false;
            if (s.state != UI_CALIB) break;
            if (s.calib_step == 0) {
                s.calib_max_tmp = r->calib_value;
                s.calib_step = 1;
            } else {
                calib_store_set_calib(s.calib_slot, s.calib_max_tmp, r->calib_value);
                s.calib_step = 0;
                s.calib_slot++;
                if (s.calib_slot >= R4P_SLOTS) {
                    /* ReaderPlus: screen_Calib_Complete() rồi ESP.restart(). Ở đây về START,
                     * calib đã nằm trong NVS nên không cần restart. */
                    s.calib_slot = 0;
                    show(UI_START);
                    break;
                }
            }
            show(UI_CALIB);
            break;
        default: break;
    }
    free(m);
}

static void measure_progress_cb(const measure_result_t *r, measure_phase_t phase, void *ctx)
{
    (void)ctx;
    prog_msg_t *m = malloc(sizeof(*m));
    if (!m) return;
    m->phase = phase;
    m->r = *r;
    if (!display_schedule(apply_progress, m)) free(m);
}

/* ===== dựng từng màn ===== */
static void build_start(void)
{
    set_title("FORTE BIOTECH  ·  Rapid4P");
    lv_obj_t *l = mk_label(s.content, "RAPID READER 4 SLOT", &lv_font_vimate_48, C_FORTE);
    lv_obj_set_style_pad_top(l, 24, 0);
    mk_label(s.content, r4p_str(STR_START_HINT), &lv_font_vimate_24, C_TEXT);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_DEVICE_ID),
             g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : "--");
    mk_label(s.content, buf, &lv_font_vimate_18, C_MUTED);
    if (measure_sensors_alive() < R4P_SLOTS) {
        snprintf(buf, sizeof(buf), "%s: %d/%d", r4p_str(STR_SENSOR_ERROR), measure_sensors_alive(), R4P_SLOTS);
        mk_label(s.content, buf, &lv_font_vimate_18, C_RED);
    }
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    lv_obj_set_style_pad_top(row, 24, 0);
    mk_btn(row, r4p_str(STR_MEASURE), on_goto, (void *)UI_CHOOSE_SAMPLE, 240, BTN_H, C_FORTE);
    mk_btn(row, r4p_str(STR_CALIBRATE), on_goto, (void *)UI_CALIB, 200, BTN_H, C_BTN);
    mk_btn(row, r4p_str(STR_SETTINGS), on_goto, (void *)UI_SETTINGS, 200, BTN_H, C_BTN);
}

static void build_list(const char *title, int n, const char *(*label)(int), lv_event_cb_t cb, ui_state_t back)
{
    set_title(title);
    lv_obj_t *grid = lv_obj_create(s.content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), 250);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 14, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    for (int i = 0; i < n; i++) {
        mk_btn(grid, label(i), cb, (void *)(intptr_t)i, 236, BTN_H, C_BTN);
    }
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)back, 200, BTN_H, C_BTN);
}

static const char *sample_label_i(int i) { return r4p_sample_label((r4p_sample_t)i); }
static const char *sick_label_i(int i) { return r4p_sick_label((r4p_sick_t)i); }

static void build_prepare(void)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%s  ·  %s", r4p_str(STR_RESULT_TUBE), r4p_sick_label(s.sick), r4p_sample_label(s.sample));
    set_title(buf);
    lv_obj_t *l = mk_label(s.content, r4p_str(STR_PREPARE_PUT_TUBE), &lv_font_vimate_24, C_TEXT);
    lv_obj_set_style_pad_top(l, 40, 0);
    mk_label(s.content, r4p_str(STR_PREPARE_PRESS), &lv_font_vimate_24, C_RED);
    bool calibrated = true;
    for (int i = 0; i < R4P_SLOTS; i++) calibrated = calibrated && calib_store_slot_calibrated(i);
    if (!calibrated) mk_label(s.content, r4p_str(STR_NOT_CALIBRATED), &lv_font_vimate_18, C_AMBER);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    lv_obj_set_style_pad_top(row, 30, 0);
    mk_btn(row, r4p_str(STR_MEASURE), on_measure, NULL, 260, BTN_H, C_FORTE);
    mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)UI_CHOOSE_TUBE, 200, BTN_H, C_BTN);
}

static void build_slot_tiles(lv_obj_t *parent, bool with_values)
{
    lv_obj_t *row = mk_row(parent, 150);
    for (int i = 0; i < R4P_SLOTS; i++) {
        lv_obj_t *t = lv_obj_create(row);
        lv_obj_set_size(t, 170, 140);
        lv_obj_set_style_bg_color(t, C_PANEL, 0);
        lv_obj_set_style_border_width(t, 3, 0);
        lv_obj_set_style_border_color(t, C_MUTED, 0);
        lv_obj_set_style_radius(t, 12, 0);
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(t, false);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d", r4p_str(STR_SLOT), i + 1);
        mk_label(t, buf, &lv_font_vimate_18, C_MUTED);
        s.slot_tile[i] = t;
        s.slot_val[i] = mk_label(t, with_values ? "" : "-", &lv_font_vimate_48, C_TEXT);
    }
}

static void build_measuring(void)
{
    set_title(r4p_str(STR_MEASURING));
    s.round_lbl = mk_label(s.content, r4p_str(STR_PLEASE_WAIT), &lv_font_vimate_24, C_TEXT);
    build_slot_tiles(s.content, false);
    s.bar = lv_bar_create(s.content);
    lv_obj_set_size(s.bar, 600, 18);
    lv_bar_set_range(s.bar, 0, 100);
    lv_bar_set_value(s.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s.bar, C_FORTE, LV_PART_INDICATOR);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    mk_btn(row, r4p_str(STR_STOP), on_abort, NULL, 200, BTN_H, C_RED);
}

static void build_result(void)
{
    const measure_result_t *r = measure_last();
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  ·  %s%s  ·  %s", r4p_str(STR_RESULT), r4p_str(STR_RESULT_TUBE),
             r4p_sick_label(r->sick), r4p_sample_label(r->sample));
    set_title(buf);
    build_slot_tiles(s.content, true);
    const r4p_settings_t *c = calib_store_get();
    for (int i = 0; i < R4P_SLOTS; i++) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)r->average[i]);
        lv_label_set_text(s.slot_val[i], buf);
        lv_obj_set_style_border_color(s.slot_tile[i], r->positive[i] ? C_RED : C_GREEN, 0);
        lv_obj_t *v = mk_label(s.slot_tile[i], r->positive[i] ? r4p_str(STR_POSITIVE) : r4p_str(STR_NEGATIVE),
                               &lv_font_vimate_18, r->positive[i] ? C_RED : C_GREEN);
        (void)v;
    }
    snprintf(buf, sizeof(buf), "%s: %lu   %s: %d", r4p_str(STR_THRESHOLD), (unsigned long)c->threshold[r->sick],
             r4p_str(STR_UPLOAD_PENDING), result_upload_pending());
    mk_label(s.content, buf, &lv_font_vimate_18, C_MUTED);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    mk_btn(row, r4p_str(STR_REDO), on_goto, (void *)UI_PREPARE, 220, BTN_H, C_BTN);
    mk_btn(row, r4p_str(STR_FINISH), on_finish, NULL, 220, BTN_H, C_FORTE);
}

static void build_calib(void)
{
    set_title(r4p_str(STR_CALIB_MODE));
    const r4p_settings_t *c = calib_store_get();
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %d/%d   %s%s", r4p_str(STR_SLOT), s.calib_slot + 1, R4P_SLOTS,
             r4p_str(STR_CALIB_SAMPLE), r4p_str(s.calib_step == 0 ? STR_CALIB_MAX : STR_CALIB_MIN));
    lv_obj_t *l = mk_label(s.content, buf, &lv_font_vimate_24, C_TEXT);
    lv_obj_set_style_pad_top(l, 12, 0);
    build_slot_tiles(s.content, true);
    for (int i = 0; i < R4P_SLOTS; i++) {
        snprintf(buf, sizeof(buf), "%u\n%u", c->cal_max[i], c->cal_min[i]);
        lv_label_set_text(s.slot_val[i], buf);
        lv_obj_set_style_text_font(s.slot_val[i], &lv_font_vimate_18, 0);
        lv_obj_set_style_text_align(s.slot_val[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_border_color(s.slot_tile[i], i == s.calib_slot ? C_AMBER
                                      : (calib_store_slot_calibrated(i) ? C_GREEN : C_MUTED), 0);
    }
    if (s.calib_step == 1) {
        snprintf(buf, sizeof(buf), "%s = %u", r4p_str(STR_CALIB_MAX), s.calib_max_tmp);
        mk_label(s.content, buf, &lv_font_vimate_18, C_FORTE);
    }
    s.calib_status = mk_label(s.content, s.calib_running ? r4p_str(STR_CALIBRATING) : "", &lv_font_vimate_18, C_AMBER);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    mk_btn(row, r4p_str(STR_CALIBRATE), on_calib_read, NULL, 220, BTN_H, C_FORTE);
    mk_btn(row, r4p_str(STR_CALIB_CLEAR), on_calib_clear, NULL, 240, BTN_H, C_RED);
    mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)UI_START, 180, BTN_H, C_BTN);
}

static void build_settings(void)
{
    set_title(r4p_str(STR_SETTINGS));
    lv_obj_t *grid = lv_obj_create(s.content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), 250);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 14, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    mk_btn(grid, r4p_str(STR_LANGUAGE), on_goto, (void *)UI_LANGUAGE, 236, BTN_H, C_BTN);
    mk_btn(grid, r4p_str(STR_WIFI), on_goto, (void *)UI_WIFI, 236, BTN_H, C_BTN);
    mk_btn(grid, r4p_str(STR_UPDATE), on_goto, (void *)UI_UPDATE, 236, BTN_H, C_BTN);
    mk_btn(grid, r4p_str(STR_THRESHOLD), on_goto, (void *)UI_THRESHOLD, 236, BTN_H, C_BTN);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)UI_START, 200, BTN_H, C_BTN);
}

static const char *lang_label_i(int i) { return r4p_lang_display_name((r4p_lang_t)i); }

static const char *thr_label_i(int i)
{
    static char buf[R4P_SICK_COUNT][40];
    snprintf(buf[i], sizeof(buf[i]), "%s: %lu", r4p_sick_label((r4p_sick_t)i),
             (unsigned long)calib_store_get()->threshold[i]);
    return buf[i];
}

static void build_threshold_edit(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  ·  %s", r4p_str(STR_VALUE_SETTING), r4p_sick_label(s.thr_sick));
    set_title(buf);
    lv_obj_t *row = mk_row(s.content, 130);
    lv_obj_set_style_pad_top(row, 30, 0);
    mk_btn(row, "-", on_spin_dec, NULL, 120, 100, C_BTN);
    s.spin = lv_spinbox_create(row);
    lv_spinbox_set_range(s.spin, 0, 9999);
    lv_spinbox_set_digit_format(s.spin, 4, 0);
    lv_spinbox_set_step(s.spin, 10);
    lv_spinbox_set_value(s.spin, (int32_t)calib_store_get()->threshold[s.thr_sick]);
    lv_obj_set_size(s.spin, 260, 100);
    lv_obj_set_style_text_font(s.spin, &lv_font_vimate_48, 0);
    lv_obj_set_style_text_align(s.spin, LV_TEXT_ALIGN_CENTER, 0);
    mk_btn(row, "+", on_spin_inc, NULL, 120, 100, C_BTN);
    lv_obj_t *row2 = mk_row(s.content, BTN_H + 8);
    lv_obj_set_style_pad_top(row2, 30, 0);
    mk_btn(row2, r4p_str(STR_SAVE), on_thr_save, NULL, 220, BTN_H, C_FORTE);
    mk_btn(row2, r4p_str(STR_CANCEL), on_goto, (void *)UI_THRESHOLD, 220, BTN_H, C_BTN);
}

static void build_wifi(void)
{
    set_title(r4p_str(STR_WIFI_SETTING));
    esp_err_t r = wifi_mgr_start_provisioning();
    if (r != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "WiFi: %s", esp_err_to_name(r));
        mk_label(s.content, buf, &lv_font_vimate_24, C_RED);
        lv_obj_t *row = mk_row(s.content, BTN_H + 8);
        mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)UI_SETTINGS, 200, BTN_H, C_BTN);
        return;
    }
    /* ui_wifi_setup đã lv_screen_load màn riêng của nó (2 mã QR + trạng thái). Thêm nút
     * "Quay lại" lên màn đó; thành công thì wifi_mgr tự restart máy. */
    lv_obj_t *act = lv_screen_active();
    if (act != s.screen) {
        lv_obj_t *b = mk_btn(act, r4p_str(STR_BACK), on_wifi_back, NULL, 160, 56, C_BTN);
        lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    }
}

static void build_update(void)
{
    set_title(r4p_str(STR_UPDATE));
    s.upd_lbl = mk_label(s.content, r4p_str(STR_UPDATING), &lv_font_vimate_24, C_TEXT);
    lv_obj_set_style_pad_top(s.upd_lbl, 40, 0);
    s.upd_bar = lv_bar_create(s.content);
    lv_obj_set_size(s.upd_bar, 600, 18);
    lv_bar_set_range(s.upd_bar, 0, 100);
    lv_obj_set_style_bg_color(s.upd_bar, C_FORTE, LV_PART_INDICATOR);
    lv_obj_t *row = mk_row(s.content, BTN_H + 8);
    lv_obj_set_style_pad_top(row, 30, 0);
    mk_btn(row, r4p_str(STR_BACK), on_goto, (void *)UI_SETTINGS, 200, BTN_H, C_BTN);
    if (!ota_client_busy()) {
        xTaskCreatePinnedToCore(update_task, "upd_ui", R4P_TASK_STACK_OTA, NULL,
                                R4P_TASK_PRIO_BACKGROUND, NULL, R4P_TASK_CORE_IO);
    }
}

static void show(ui_state_t st)
{
    s.state = st;
    if (lv_screen_active() != s.screen) lv_screen_load(s.screen);
    content_clear();
    switch (st) {
        case UI_START: build_start(); break;
        case UI_CHOOSE_SAMPLE: build_list(r4p_str(STR_CHOOSE_SAMPLE), R4P_SAMPLE_COUNT, sample_label_i, on_pick_sample, UI_START); break;
        case UI_CHOOSE_TUBE: build_list(r4p_str(STR_CHOOSE_TUBE), R4P_SICK_COUNT, sick_label_i, on_pick_tube, UI_CHOOSE_SAMPLE); break;
        case UI_PREPARE: build_prepare(); break;
        case UI_MEASURING: build_measuring(); break;
        case UI_RESULT: build_result(); break;
        case UI_CALIB: build_calib(); break;
        case UI_SETTINGS: build_settings(); break;
        case UI_LANGUAGE: build_list(r4p_str(STR_LANGUAGE_SETTING), R4P_LANG_COUNT, lang_label_i, on_pick_lang, UI_SETTINGS); break;
        case UI_WIFI: build_wifi(); break;
        case UI_UPDATE: build_update(); break;
        case UI_THRESHOLD: build_list(r4p_str(STR_THRESHOLD_SETTING), R4P_SICK_COUNT, thr_label_i, on_pick_thr, UI_SETTINGS); break;
        case UI_THRESHOLD_EDIT: build_threshold_edit(); break;
        default: build_start(); break;
    }
    header_refresh();
    ESP_LOGI(TAG_UI, "man %d", (int)st);
}

/* ===== sự kiện ngoài (qua display_schedule) ===== */
static void apply_measure_button(void *arg)
{
    (void)arg;
    switch (s.state) {
        case UI_START: show(UI_CHOOSE_SAMPLE); break;
        case UI_PREPARE: start_measure(); break;
        case UI_RESULT: show(UI_CHOOSE_SAMPLE); break;
        case UI_CALIB: on_calib_read(NULL); break;
        default: break;
    }
}

static void apply_boot_button(void *arg)
{
    (void)arg;
    switch (s.state) {
        case UI_WIFI: on_wifi_back(NULL); break;
        case UI_MEASURING: measure_abort(); show(UI_PREPARE); break;
        case UI_CHOOSE_SAMPLE: case UI_CALIB: case UI_SETTINGS: show(UI_START); break;
        case UI_CHOOSE_TUBE: show(UI_CHOOSE_SAMPLE); break;
        case UI_PREPARE: show(UI_CHOOSE_TUBE); break;
        case UI_RESULT: show(UI_CHOOSE_SAMPLE); break;
        case UI_LANGUAGE: case UI_UPDATE: case UI_THRESHOLD: show(UI_SETTINGS); break;
        case UI_THRESHOLD_EDIT: show(UI_THRESHOLD); break;
        default: break;
    }
}

typedef struct { bool connected; char ip[16]; } wifi_msg_t;
static void apply_wifi(void *arg)
{
    wifi_msg_t *m = arg;
    s.wifi_connected = m->connected;
    strlcpy(s.ip, m->ip, sizeof(s.ip));
    header_refresh();
    free(m);
}

typedef struct { int pending, sent; } upl_msg_t;
static void apply_upload(void *arg)
{
    upl_msg_t *m = arg;
    s.pending = m->pending;
    s.sent = m->sent;
    header_refresh();
    free(m);
}

static void apply_dev_state(void *arg)
{
    s.dev_state = (r4p_dev_state_t)(intptr_t)arg;
    header_refresh();
}

void ui_reader_on_measure_button(void) { display_schedule(apply_measure_button, NULL); }
void ui_reader_on_boot_button(void) { display_schedule(apply_boot_button, NULL); }
void ui_reader_set_dev_state(r4p_dev_state_t st) { display_schedule(apply_dev_state, (void *)(intptr_t)st); }

void ui_reader_set_wifi(bool connected, const char *ip)
{
    wifi_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->connected = connected;
    if (ip) strlcpy(m->ip, ip, sizeof(m->ip));
    if (!display_schedule(apply_wifi, m)) free(m);
}

void ui_reader_set_upload_stats(int pending, int sent)
{
    upl_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->pending = pending;
    m->sent = sent;
    if (!display_schedule(apply_upload, m)) free(m);
}

ui_state_t ui_reader_state(void) { return s.state; }

esp_err_t ui_reader_init(void)
{
    if (!display_hw_ready()) return ESP_ERR_INVALID_STATE;
    r4p_str_set_lang(calib_store_get()->lang);
    display_lock();
    s.screen = lv_screen_active();
    lv_obj_set_style_bg_color(s.screen, C_BG, 0);
    lv_obj_set_style_bg_opa(s.screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s.screen, false);

    s.header = lv_obj_create(s.screen);
    lv_obj_remove_style_all(s.header);
    lv_obj_set_size(s.header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(s.header, C_PANEL, 0);
    lv_obj_set_style_bg_opa(s.header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s.header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s.header, 2, 0);
    lv_obj_set_style_border_color(s.header, C_FORTE, 0);
    s.title = mk_label(s.header, "", &lv_font_vimate_24, C_FORTE);
    lv_obj_align(s.title, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s.title, 470);
    s.status = mk_label(s.header, "", &lv_font_vimate_14, C_MUTED);
    lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, -16, 0);

    s.content = lv_obj_create(s.screen);
    lv_obj_remove_style_all(s.content);
    lv_obj_set_pos(s.content, 0, HEADER_H);
    lv_obj_set_size(s.content, LV_PCT(100), display_lcd_height() - HEADER_H);
    lv_obj_set_scrollable(s.content, false);

    s.pending = result_upload_pending();
    show(UI_START);
    display_unlock();

    measure_set_progress_cb(measure_progress_cb, NULL);
    ESP_LOGI(TAG_UI, "ui_reader san sang (lang=%d)", (int)r4p_str_get_lang());
    return ESP_OK;
}
