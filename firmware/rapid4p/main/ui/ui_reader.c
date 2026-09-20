/**
 * ui_reader.c — màn hình Rapid4P. Xem ui_reader.h.
 *
 * Bố cục 800×480 (làm lại 2026-09-17 theo review qua webcam + skill ui-ux-pro-max):
 *   header  56 px : tiêu đề trái (teal) · trạng thái WiFi/gửi/version phải (Montserrat, có icon)
 *   content 328 px: nội dung màn, flex column CĂN GIỮA, dựng lại mỗi lần đổi trạng thái
 *   footer  96 px : THANH HÀNH ĐỘNG cố định — Quay lại/Huỷ LUÔN bên trái, hành động chính
 *                   LUÔN bên phải (teal), phá huỷ (đỏ) ở giữa và phải xác nhận.
 * Quy ước: nút ≥ 72 px cao, cách ≥ 12 px; nhấn đổi màu 120 ms; trạng thái không truyền
 * bằng màu đơn thuần (icon + chữ); Việt/Trung dùng font vimate, icon dùng Montserrat.
 * Màu "Forte_Green" 0x25F8 (RGB565 của bản ILI9341) ≈ #21BEC5.
 */
#include "ui_reader.h"
#include "display.h"
#include "ui_strings.h"
#include "app/measure.h"
#include "app/calib_store.h"
#include "core/wifi_mgr.h"
#include "core/task_profile.h"
#include "network/ota_client.h"
#include "network/result_upload.h"
#include "ui/ui_wifi_setup.h"
#include "ui/ui_logo.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ui_theme.h"     /* token màu / kích thước / font dùng chung */

static struct {
    lv_obj_t *screen, *header, *title, *status, *content, *footer;
    ui_state_t state;
    r4p_dev_state_t dev_state;
    r4p_sick_t sick;
    r4p_sample_t sample;
    /* đo */
    lv_obj_t *slot_tile[R4P_SLOTS], *slot_val[R4P_SLOTS], *slot_sub[R4P_SLOTS];
    lv_obj_t *round_lbl, *bar, *bar_lbl;
    /* calib */
    int calib_slot, calib_step;         /* step 0 = Cao nhất (max), 1 = Thấp nhất (min) */
    uint16_t calib_max_tmp;
    bool calib_running;
    lv_obj_t *calib_status;
    /* ngưỡng */
    r4p_sick_t thr_sick;
    int32_t thr_value;
    lv_obj_t *thr_lbl;
    /* cập nhật */
    lv_obj_t *upd_lbl, *upd_bar;
    /* hộp thoại xác nhận (overlay trên s.screen) */
    lv_obj_t *modal;
    void (*modal_yes)(void);
    lv_obj_t *wifi_back_btn;            /* nút Back gắn lên màn ui_wifi_setup (chỉ 1) */
    /* header */
    bool wifi_connected;
    char ip[16];
    int pending, sent;
    /* 2.8": softkey 3 nút vật lý + con trỏ danh sách (nút ĐỎ ▼ / TRẮNG ▲ / XANH chọn) */
    lv_obj_t *sk_cell[R4P_KEY_COUNT];
    lv_obj_t *list_items[8];
    int list_n, cursor;
} s;

static lv_style_transition_dsc_t s_tr_press;   /* đổi màu nền 120 ms khi nhấn/nhả */

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

/* Nhãn văn bản dài: xuống dòng, rộng hết content, canh giữa (2.8": câu gợi ý 14–18 px tràn ngang nếu 1 dòng). */
static lv_obj_t *mk_text(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = mk_label(parent, txt, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

/* Chọn font lớn nhất trong danh sách mà chuỗi vừa bề rộng cho trước (đo lv_text_get_size). */
static const lv_font_t *fit_font_from(const lv_font_t *const *cands, int n, const char *txt, int max_w,
                                      int letter_space)
{
    lv_point_t sz;
    for (int i = 0; i < n - 1; i++) {
        lv_text_get_size(&sz, txt, cands[i], letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= max_w) return cands[i];
    }
    return cands[n - 1];
}

/* Nhãn nút: F_BODY → F_SMALL → F_TINY (4.3": 24 → 18 → 14; 2.8": 18 → 14). */
static const lv_font_t *fit_font(const char *txt, int max_w)
{
    static const lv_font_t *const cands[] = { F_BODY, F_SMALL, F_TINY };
    return fit_font_from(cands, 3, txt, max_w, 0);
}

/* Nút chạm: icon (Montserrat, tuỳ chọn) + nhãn (vimate, tự co font cho vừa nút).
 * bg = màu nền; chữ trên teal dùng C_ON_FORTE, còn lại C_TEXT. */
static inline bool on_accent_bg(lv_color_t bg) { return lv_color_to_u32(bg) == lv_color_to_u32(C_FORTE); }

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb,
                        void *ud, int w, int h, lv_color_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, on_accent_bg(bg) ? C_FORTE_DK : lv_color_darken(bg, 70), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_PANEL, LV_STATE_DISABLED);
    lv_obj_set_style_radius(b, UI_RADIUS, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, UI_BTN_PAD, 0);
    lv_obj_set_style_transition(b, &s_tr_press, 0);
    lv_obj_set_style_transition(b, &s_tr_press, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, UI_BTN_PAD - 2, 0);

    const bool on_accent = on_accent_bg(bg);
    lv_color_t fg = on_accent ? C_ON_FORTE : C_TEXT;
    int inner = w - 2 * UI_BTN_PAD;
    if (icon) {
        lv_obj_t *i = mk_label(b, icon, F_ICON, fg);
        lv_obj_set_style_text_color(i, C_MUTED, LV_STATE_DISABLED);
        inner -= UI_BTN_ICON_W;
    }
    if (txt && txt[0]) {
        lv_obj_t *l = mk_label(b, txt, fit_font(txt, inner), fg);
        lv_obj_set_style_text_color(l, C_MUTED, LV_STATE_DISABLED);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, LV_SIZE_CONTENT);
    }   /* txt NULL = nút icon-only (2.8": nút phụ ở footer) */
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

/* Khoá nút: làm tay thay vì LV_STATE_DISABLED — theme mặc định đè style disabled thành khối
 * xám sáng, chữ mất (camera 2026-09-17). */
static __attribute__((unused)) void btn_set_disabled(lv_obj_t *b)   /* chỉ thang 4.3" */
{
    lv_obj_set_style_bg_color(b, C_PANEL, 0);
    lv_obj_set_style_bg_color(b, C_PANEL, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, C_BORDER, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(b); i++)
        lv_obj_set_style_text_color(lv_obj_get_child(b, i), C_MUTED, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *mk_row(lv_obj_t *parent, int h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, GAP + 4, 0);
    return r;
}

/* Chip trạng thái: icon + chữ 18 px trên nền card, viền màu ngữ nghĩa. */
static lv_obj_t *mk_chip(lv_obj_t *parent, const char *icon, const char *txt, lv_color_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, UI_CHIP_H);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, UI_CHIP_H / 2, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, color, 0);
    lv_obj_set_style_pad_hor(c, UI_BTN_PAD + 2, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 8, 0);
    if (icon) mk_label(c, icon, F_ICON_SM, color);
    mk_label(c, txt, F_SMALL, C_TEXT);
    return c;
}

/* ===== thanh hành động (footer) =====
 * left: Quay lại/Huỷ · mid: phụ hoặc phá huỷ · right: hành động chính (teal). */
static lv_obj_t *footer_left(const char *icon, const char *txt, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = mk_btn(s.footer, icon, txt, cb, ud, UI_BTN_BACK_W, BTN_H, C_BTN);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
    return b;
}
static __attribute__((unused)) lv_obj_t *footer_right(const char *icon, const char *txt, lv_event_cb_t cb, void *ud, lv_color_t bg)
{
    lv_obj_t *b = mk_btn(s.footer, icon, txt, cb, ud, UI_BTN_MAIN_W, BTN_H, bg);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    return b;
}
static __attribute__((unused)) lv_obj_t *footer_mid(const char *icon, const char *txt, lv_event_cb_t cb, void *ud, lv_color_t bg)
{
#if UI_SCALE_SMALL
    /* 2.8": không đủ chỗ cho 3 nút chữ → nút phụ/phá huỷ = icon 44×44 ngay cạnh nút trái. */
    (void)txt;
    lv_obj_t *b = mk_btn(s.footer, icon, NULL, cb, ud, UI_BTN_ICON_ONLY_W, BTN_H, bg);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, UI_BTN_BACK_W + GAP, 0);
#else
    lv_obj_t *b = mk_btn(s.footer, icon, txt, cb, ud, UI_BTN_DANGER_W, BTN_H, bg);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);
#endif
    return b;
}

static void content_clear(void)
{
    lv_obj_clean(s.content);
    lv_obj_clean(s.footer);
    lv_obj_set_flex_flow(s.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s.content, GAP, 0);
    lv_obj_set_style_pad_hor(s.content, UI_PAD, 0);
    lv_obj_set_style_pad_ver(s.content, UI_SCALE_SMALL ? 4 : 8, 0);
    memset(s.slot_tile, 0, sizeof(s.slot_tile));
    memset(s.slot_val, 0, sizeof(s.slot_val));
    memset(s.slot_sub, 0, sizeof(s.slot_sub));
    s.round_lbl = s.bar = s.bar_lbl = s.calib_status = s.thr_lbl = s.upd_lbl = s.upd_bar = NULL;
    memset(s.list_items, 0, sizeof(s.list_items));
    s.list_n = 0;
    s.cursor = 0;
    memset(s.sk_cell, 0, sizeof(s.sk_cell));   /* ô trong s.footer vừa bị clean; ô trên màn WiFi xoá ở softkeys_clear */
}

/* ===== danh sách có con trỏ (2.8": ĐỎ ▼ · TRẮNG ▲ · XANH chọn; chạm thẳng vẫn được) ===== */
static void list_focus(int i)
{
    for (int k = 0; k < s.list_n; k++) {
        lv_obj_t *it = s.list_items[k];
        if (!it) continue;
        lv_obj_set_style_border_width(it, k == i ? 2 : 0, 0);
        lv_obj_set_style_border_color(it, C_FORTE, 0);
        lv_obj_set_style_bg_color(it, k == i ? C_BORDER : C_BTN, 0);
    }
    s.cursor = i;
}
static void list_move(int d)
{
    if (s.list_n <= 0) return;
    list_focus((s.cursor + d + s.list_n) % s.list_n);
}
static void list_activate(void)
{
    if (s.list_n > 0 && s.list_items[s.cursor]) lv_obj_send_event(s.list_items[s.cursor], LV_EVENT_CLICKED, NULL);
}
static void list_add(lv_obj_t *it)
{
    if (s.list_n < (int)(sizeof(s.list_items) / sizeof(s.list_items[0]))) s.list_items[s.list_n++] = it;
}

#if UI_SCALE_SMALL
/* ===== softkey bar 2.8": 3 ô thẳng hàng với 3 nút vật lý XANH · ĐỎ · TRẮNG dưới màn =====
 * Chấm màu + nhãn (không dựa vào màu đơn thuần: vị trí ô = vị trí nút). Chạm ô = nhấn nút.
 * Hành động thật nằm ở apply_key() theo s.state — một nguồn sự thật cho cả nút cơ lẫn chạm. */
static lv_color_t key_color(r4p_key_t k)
{
    switch (k) {
        case R4P_KEY_GREEN: return C_GREEN;
        case R4P_KEY_RED:   return C_RED;
        default:            return C_TEXT;      /* trắng */
    }
}
static void on_softkey(lv_event_t *e) { ui_reader_on_key((r4p_key_t)(intptr_t)lv_event_get_user_data(e), false); }

static void softkeys_clear(void)
{
    for (int k = 0; k < R4P_KEY_COUNT; k++) {
        if (s.sk_cell[k] && lv_obj_is_valid(s.sk_cell[k])) lv_obj_delete(s.sk_cell[k]);
        s.sk_cell[k] = NULL;
    }
}

static void softkeys_on(lv_obj_t *host, const char *g, const char *r, const char *w)
{
    const char *txt[R4P_KEY_COUNT] = { g, r, w };
    const int cell_w = (UI_W - 2 * UI_FOOTER_PAD - 2 * GAP) / 3;
    softkeys_clear();
    for (int k = 0; k < R4P_KEY_COUNT; k++) {
        lv_obj_t *c = lv_obj_create(host);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, cell_w, BTN_H);
        lv_obj_set_pos(c, k * (cell_w + GAP), (FOOTER_H - BTN_H) / 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_radius(c, UI_RADIUS, 0);
        lv_obj_set_style_bg_color(c, txt[k] ? C_CARD : C_PANEL, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(c, 3, 0);
        lv_obj_set_style_border_color(c, txt[k] ? key_color((r4p_key_t)k) : C_BORDER, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(c, 6, 0);
        lv_obj_set_scrollable(c, false);
        if (txt[k]) {
            lv_obj_t *dot = lv_obj_create(c);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, key_color((r4p_key_t)k), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            /* LV_SYMBOL_* (U+F000+) là UTF-8 3 byte bắt đầu 0xEF — chỉ Montserrat có glyph. */
            const bool sym = ((unsigned char)txt[k][0] == 0xEF);
            mk_label(c, txt[k], sym ? F_ICON_SM : F_SMALL, C_TEXT);
            lv_obj_set_style_bg_color(c, C_BTN, LV_STATE_PRESSED);
            lv_obj_set_style_transition(c, &s_tr_press, 0);
            lv_obj_add_event_cb(c, on_softkey, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        } else {
            lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        }
        s.sk_cell[k] = c;
    }
}
static void softkeys(const char *g, const char *r, const char *w) { softkeys_on(s.footer, g, r, w); }
#endif

static void set_title(const char *t) { lv_label_set_text(s.title, t); }

/* Tiêu đề luồng đo: "Bước n/3 · <tên màn>" (tiến độ đa bước). */
static void set_step_title(int step, const char *name)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %d/3  ·  %s", r4p_str(STR_STEP), step, name);
    set_title(buf);
}

static void header_refresh(void)
{
    char buf[96];
#if UI_STATUS_FULL
    snprintf(buf, sizeof(buf), "%s %s   " LV_SYMBOL_UPLOAD " %d/%d   %s",
             s.wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE,
             s.wifi_connected ? s.ip : "--", s.pending, s.sent, R4P_FW_VERSION);
#else
    /* Header 320 px: chỉ icon WiFi + số kết quả chờ gửi; IP + version ở màn chính (build_start). */
    snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_UPLOAD " %d",
             s.wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE, s.pending);
#endif
    lv_label_set_text(s.status, buf);
    lv_obj_set_style_text_color(s.status, s.wifi_connected ? C_FORTE : C_MUTED, 0);
}

/* ===== hộp thoại xác nhận (phá huỷ) ===== */
static void modal_close(void)
{
    if (s.modal && lv_obj_is_valid(s.modal)) lv_obj_delete(s.modal);
    s.modal = NULL;
    s.modal_yes = NULL;
}
static void apply_modal_close(void *arg) { (void)arg; modal_close(); }
static void on_modal_no(lv_event_t *e) { (void)e; display_schedule(apply_modal_close, NULL); }
static void apply_modal_yes(void *arg)
{
    (void)arg;
    void (*fn)(void) = s.modal_yes;
    modal_close();
    if (fn) fn();
}
static void on_modal_yes(lv_event_t *e) { (void)e; display_schedule(apply_modal_yes, NULL); }

static void confirm_show(const char *question, void (*on_yes)(void))
{
    modal_close();
    s.modal_yes = on_yes;
    lv_obj_t *ov = lv_obj_create(s.screen);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);       /* nuốt chạm phía dưới */
    s.modal = ov;

    lv_obj_t *card = lv_obj_create(ov);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, UI_MODAL_W, UI_MODAL_H);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS + 4, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_pad_all(card, UI_MODAL_PAD, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, GAP, 0);
    mk_label(hdr, LV_SYMBOL_WARNING, F_ICON, C_AMBER);
    lv_obj_t *q = mk_label(hdr, question, F_BODY, C_TEXT);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q, UI_MODAL_Q_W);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), BTN_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *bno = mk_btn(row, NULL, r4p_str(STR_CANCEL), on_modal_no, NULL, UI_MODAL_BTN_NO_W, BTN_H, C_BTN);
    lv_obj_t *byes = mk_btn(row, LV_SYMBOL_TRASH, r4p_str(STR_CONFIRM), on_modal_yes, NULL, UI_MODAL_BTN_YES_W, BTN_H, C_RED);
#if UI_SCALE_SMALL
    /* Nút cơ: XANH = Huỷ, ĐỎ = Xác nhận (apply_key) — vạch màu trên nút để người dùng thấy. */
    lv_obj_set_style_border_side(bno, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bno, 3, 0);
    lv_obj_set_style_border_color(bno, C_GREEN, 0);
    lv_obj_set_style_border_side(byes, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(byes, 3, 0);
    lv_obj_set_style_border_color(byes, C_RED, 0);
#else
    (void)bno; (void)byes;
#endif
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

static __attribute__((unused)) void on_measure(lv_event_t *e) { (void)e; start_measure(); }   /* 4.3": nút footer; 2.8": apply_key */

static __attribute__((unused)) void on_abort(lv_event_t *e)
{
    (void)e;
    measure_abort();
    show_async(UI_PREPARE);
}

static __attribute__((unused)) void on_finish(lv_event_t *e)
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
        if (s.calib_status) {
            lv_label_set_text(s.calib_status, r4p_str(STR_CALIBRATING));
            lv_obj_set_style_text_color(s.calib_status, C_AMBER, 0);
        }
    }
}

static void do_calib_clear(void)
{
    calib_store_format_calib();
    s.calib_slot = 0;
    s.calib_step = 0;
    show(UI_CALIB);
}
static void on_calib_clear(lv_event_t *e)
{
    (void)e;
    static char q[96];
    snprintf(q, sizeof(q), r4p_str(STR_CLEAR_CALIB_ASK), R4P_SLOTS);
    confirm_show(q, do_calib_clear);
}

static void on_pick_thr(lv_event_t *e)
{
    s.thr_sick = (r4p_sick_t)(intptr_t)lv_event_get_user_data(e);
    show_async(UI_THRESHOLD_EDIT);
}

static void thr_apply_delta(int32_t d)
{
    s.thr_value += d;
    if (s.thr_value < 0) s.thr_value = 0;
    if (s.thr_value > 9999) s.thr_value = 9999;
    if (s.thr_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%ld", (long)s.thr_value);
        lv_label_set_text(s.thr_lbl, buf);
    }
}
/* Chạm ngắn ±10, giữ để lặp ±50 (SHORT_CLICKED + LONG_PRESSED_REPEAT, không dùng CLICKED
 * vì CLICKED bắn thêm 1 lần khi nhả sau giữ). */
static void on_thr_btn(lv_event_t *e)
{
    int32_t sign = (int32_t)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) thr_apply_delta(sign * 10);
    else if (code == LV_EVENT_LONG_PRESSED_REPEAT) thr_apply_delta(sign * 50);
}

static void on_thr_save(lv_event_t *e)
{
    (void)e;
    calib_store_set_threshold(s.thr_sick, (uint32_t)s.thr_value);
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
            else if (m->err == ESP_ERR_WIFI_NOT_CONNECT) snprintf(buf, sizeof(buf), "%s", r4p_str(STR_NO_WIFI));
            else if (m->err == ESP_ERR_INVALID_STATE) snprintf(buf, sizeof(buf), "%s", r4p_str(STR_NO_TOKEN));
            else snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_UPDATE), esp_err_to_name(m->err));
            lv_obj_set_style_text_color(s.upd_lbl, m->err == ESP_OK ? C_GREEN : C_AMBER, 0);
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

static void bar_set(int pct)
{
    if (s.bar) lv_bar_set_value(s.bar, pct, LV_ANIM_ON);
    if (s.bar_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s.bar_lbl, buf);
    }
}

static void apply_progress(void *arg)
{
    prog_msg_t *m = arg;
    const measure_result_t *r = &m->r;
    char buf[48];
    switch (m->phase) {
        case MEASURE_PHASE_SLOT_START:
            if (s.state != UI_MEASURING) break;
            snprintf(buf, sizeof(buf), "%s %d/%d  ·  %s %d", r4p_str(STR_ROUND), r->round, R4P_ROUNDS,
                     r4p_str(STR_SLOT), r->slot + 1);
            if (s.round_lbl) lv_label_set_text(s.round_lbl, buf);
            slot_tile_set(r->slot, "...", C_AMBER);
            bar_set(((r->round - 1) * R4P_SLOTS + r->slot) * 100 / (R4P_ROUNDS * R4P_SLOTS));
            break;
        case MEASURE_PHASE_SLOT_DONE:
            if (s.state != UI_MEASURING) break;
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)r->result[r->slot][r->round - 1]);
            slot_tile_set(r->slot, buf, C_FORTE);
            bar_set(((r->round - 1) * R4P_SLOTS + r->slot + 1) * 100 / (R4P_ROUNDS * R4P_SLOTS));
            break;
        case MEASURE_PHASE_DONE:
            result_upload_enqueue(r);
            if (s.state == UI_MEASURING) show(UI_RESULT);
            break;
        case MEASURE_PHASE_ABORTED:
            break;
        case MEASURE_PHASE_ERROR:
            if (s.state == UI_MEASURING) {
                slot_tile_set(r->slot, LV_SYMBOL_WARNING, C_RED);
                if (s.slot_val[r->slot]) lv_obj_set_style_text_font(s.slot_val[r->slot], F_ICON, 0);
                if (s.round_lbl) {
                    snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_SENSOR_ERROR), esp_err_to_name(r->err));
                    lv_label_set_text(s.round_lbl, buf);
                    lv_obj_set_style_text_color(s.round_lbl, C_RED, 0);
                }
            } else if (s.state == UI_CALIB) {
                s.calib_running = false;
                if (s.calib_status) {
                    lv_label_set_text(s.calib_status, r4p_str(STR_SENSOR_ERROR));
                    lv_obj_set_style_text_color(s.calib_status, C_RED, 0);
                }
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

/* ===== khối dùng chung ===== */
/* N ô khe một hàng: tên khe (F_SMALL) + giá trị (F_HERO) + dòng phụ (kết quả/đơn vị).
 * Bề rộng ô suy từ số khe và bề rộng content: (UI_CONTENT_W − (N−1)×UI_TILE_GAP) / N
 *   4.3": 4 khe = 178, 5 khe = 139, 6 khe = 113 (760 px)     2.8": 5 khe = 57 px (304 px).
 * Font giá trị: 4.3" giữ 48 (4 chữ số ≈ 108 px, vừa 139 px); 2.8" đo "3000" ở 24 → 18 → 14 với
 * letter-space −2 (fit_font_from) — 57 px trừ pad/viền còn 51 px, 24 px ≈ 51 px vừa khít. */
static void build_slot_tiles(lv_obj_t *parent, const char *placeholder)
{
    const int tile_w = (UI_CONTENT_W - (R4P_SLOTS - 1) * UI_TILE_GAP) / R4P_SLOTS;
    const bool narrow = tile_w < 150;                 /* 4.3" 5 khe: 139 px → dòng phụ 14 px */
    const lv_font_t *f_sub = narrow ? F_TINY : F_SMALL;
#if UI_SCALE_SMALL
    const int inner_w = tile_w - 2 * (UI_TILE_PAD + UI_TILE_BORDER);
    static const lv_font_t *const cands[] = { F_HERO, F_BODY, F_SMALL };
    const lv_font_t *f_val = fit_font_from(cands, 3, "3000", inner_w, -2);   /* letter-space −2 như khi vẽ */
#else
    const lv_font_t *f_val = F_HERO;
#endif
    lv_obj_t *row = mk_row(parent, UI_TILE_ROW_H);
    lv_obj_set_style_pad_column(row, UI_TILE_GAP, 0);
    for (int i = 0; i < R4P_SLOTS; i++) {
        lv_obj_t *t = lv_obj_create(row);
        lv_obj_set_size(t, tile_w, UI_TILE_H);
        lv_obj_set_style_bg_color(t, C_CARD, 0);
        lv_obj_set_style_border_width(t, UI_TILE_BORDER, 0);
        lv_obj_set_style_border_color(t, C_BORDER, 0);
        lv_obj_set_style_radius(t, UI_RADIUS, 0);
        lv_obj_set_style_pad_all(t, UI_TILE_PAD, 0);
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(t, 2, 0);
        lv_obj_set_scrollable(t, false);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d", r4p_str(STR_SLOT), i + 1);
        mk_label(t, buf, F_SMALL, C_MUTED);
        s.slot_tile[i] = t;
        s.slot_val[i] = mk_label(t, placeholder, f_val, C_TEXT);
        if (narrow || UI_SCALE_SMALL) lv_obj_set_style_text_letter_space(s.slot_val[i], -2, 0);
        s.slot_sub[i] = mk_label(t, "", f_sub, C_MUTED);
    }
}

static lv_obj_t *mk_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, GAP + 4, 0);
    lv_obj_set_style_pad_row(grid, GAP + 4, 0);
    return grid;
}

/* ===== dựng từng màn ===== */
static void build_start(void)
{
    set_title("Rapid4P");
    const int alive = measure_sensors_alive();
    char buf[64];
#if UI_SCALE_SMALL
    /* 2.8" (content 304×148): KHÔNG logo lớn/tên máy 48 px — một hàng [mark 40 + tên máy 18 teal],
     * câu gợi ý 14 px xuống dòng, chip cảm biến + mã máy, dòng IP · version (header không có chỗ). */
    lv_obj_t *brand = mk_row(s.content, UI_HOME_LOGO_H + 4);
    lv_obj_set_style_pad_column(brand, GAP + 4, 0);
    ui_logo_create(brand, UI_HOME_LOGO_H, false);
    mk_label(brand, R4P_PRODUCT_NAME, F_BODY, C_FORTE);
    mk_text(s.content, alive > 0 ? r4p_str(STR_START_HINT) : r4p_str(STR_SENSOR_NONE_HINT),
            F_SMALL, alive > 0 ? C_TEXT : C_AMBER);
#else
    ui_logo_create(s.content, UI_HOME_LOGO_H, UI_HOME_LOGO_H >= 90);   /* logo vector (ui_logo.c); chữ chỉ đọc được khi ≥ 90 px */
    lv_obj_t *hero = mk_label(s.content, R4P_PRODUCT_NAME, F_HERO, C_FORTE);
    lv_obj_set_style_pad_top(hero, 4, 0);
    mk_text(s.content, alive > 0 ? r4p_str(STR_START_HINT) : r4p_str(STR_SENSOR_NONE_HINT),
            F_BODY, alive > 0 ? C_TEXT : C_AMBER);
#endif

    lv_obj_t *chips = mk_row(s.content, UI_CHIP_ROW_H);
    snprintf(buf, sizeof(buf), "%s %d/%d", r4p_str(STR_SENSORS), alive, R4P_SLOTS);
    mk_chip(chips, alive == R4P_SLOTS ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, buf,
            alive == R4P_SLOTS ? C_GREEN : (alive > 0 ? C_AMBER : C_RED));
    snprintf(buf, sizeof(buf), "%s: %s", r4p_str(STR_DEVICE_ID),
             g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : "--");
    mk_chip(chips, NULL, buf, C_BORDER);
#if !UI_STATUS_FULL
    /* 2.8": header không chứa IP/version → hiện ở đây (dashboard cần IP). */
    snprintf(buf, sizeof(buf), "%s  ·  %s", s.wifi_connected ? s.ip : "--", R4P_FW_VERSION);
    mk_label(s.content, buf, F_TINY, C_MUTED);
#endif

#if UI_SCALE_SMALL
    /* Vỏ máy 3 nút (ReaderPlus): XANH = Bắt đầu, ĐỎ = Cài đặt, TRẮNG = Cân chỉnh. Không có cảm
     * biến: apply_key chặn Bắt đầu, câu gợi ý màu cảnh báo ở trên đã nói lý do. */
    softkeys(r4p_str(STR_START), r4p_str(STR_SETTINGS), r4p_str(STR_CALIBRATE));
#else
    footer_left(LV_SYMBOL_EDIT, r4p_str(STR_CALIBRATE), on_goto, (void *)UI_CALIB);
    footer_mid(LV_SYMBOL_SETTINGS, r4p_str(STR_SETTINGS), on_goto, (void *)UI_SETTINGS, C_BTN);
    lv_obj_t *go = footer_right(LV_SYMBOL_PLAY, r4p_str(STR_MEASURE), on_goto, (void *)UI_CHOOSE_SAMPLE, C_FORTE);
    if (alive == 0) btn_set_disabled(go);
#endif
}

static void build_list(const char *title, int n, const char *(*label)(int), lv_event_cb_t cb, ui_state_t back)
{
    set_title(title);
    lv_obj_t *grid = mk_grid(s.content);
    for (int i = 0; i < n; i++) {
        list_add(mk_btn(grid, NULL, label(i), cb, (void *)(intptr_t)i, UI_BTN_LIST_W, BTN_H, C_BTN));
    }
#if UI_SCALE_SMALL
    /* 2.8": "Quay lại" là mục cuối danh sách (kiểu điện thoại phím) — con trỏ tới được, chạm cũng được. */
    list_add(mk_btn(grid, LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)back, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_focus(0);
    softkeys(r4p_str(STR_SELECT), LV_SYMBOL_DOWN, LV_SYMBOL_UP);
#else
    (void)back;
    footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)back);
#endif
}

static const char *sample_label_i(int i) { return r4p_sample_label((r4p_sample_t)i); }
static const char *sick_label_i(int i) { return r4p_sick_label((r4p_sick_t)i); }

static void build_choose_sample(void)
{
    build_list(r4p_str(STR_CHOOSE_SAMPLE), R4P_SAMPLE_COUNT, sample_label_i, on_pick_sample, UI_START);
    set_step_title(1, r4p_str(STR_CHOOSE_SAMPLE));
}

static void build_choose_tube(void)
{
    build_list(r4p_str(STR_CHOOSE_TUBE), R4P_SICK_COUNT, sick_label_i, on_pick_tube, UI_CHOOSE_SAMPLE);
    set_step_title(2, r4p_str(STR_CHOOSE_TUBE));
}

static void build_prepare(void)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  ·  %s", r4p_sick_label(s.sick), r4p_sample_label(s.sample));
    set_step_title(3, buf);
    snprintf(buf, sizeof(buf), r4p_str(STR_PREPARE_PUT_TUBE), R4P_SLOTS);
    mk_text(s.content, buf, UI_SCALE_SMALL ? F_SMALL : F_BODY, C_TEXT);
    build_slot_tiles(s.content, "-");       /* gợi ý hình: N khe trống */
    bool calibrated = true;
    for (int i = 0; i < R4P_SLOTS; i++) calibrated = calibrated && calib_store_slot_calibrated(i);
    lv_obj_t *chips = mk_row(s.content, UI_CHIP_ROW_H - 4);
    if (!calibrated) mk_chip(chips, LV_SYMBOL_WARNING, r4p_str(STR_NOT_CALIBRATED), C_AMBER);
#if UI_SCALE_SMALL
    softkeys(r4p_str(STR_BACK), r4p_str(STR_MEASURE), NULL);      /* "Nhấn Nút Đỏ Để Đo!" như ReaderPlus */
#else
    footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_CHOOSE_TUBE);
    footer_right(LV_SYMBOL_PLAY, r4p_str(STR_MEASURE), on_measure, NULL, C_FORTE);
#endif
}

static void build_measuring(void)
{
    set_title(r4p_str(STR_MEASURING));
    s.round_lbl = mk_text(s.content, r4p_str(STR_PLEASE_WAIT), UI_SCALE_SMALL ? F_SMALL : F_BODY, C_TEXT);
    build_slot_tiles(s.content, "-");
    lv_obj_t *prow = mk_row(s.content, UI_PROGRESS_ROW_H);
    s.bar = lv_bar_create(prow);
    lv_obj_set_size(s.bar, UI_BAR_W, UI_BAR_H);
    lv_bar_set_range(s.bar, 0, 100);
    lv_bar_set_value(s.bar, 0, LV_ANIM_OFF);
    ui_theme_brand_bar(s.bar);
    s.bar_lbl = mk_label(prow, "0%", F_SMALL, C_MUTED);
    lv_obj_set_width(s.bar_lbl, UI_BAR_LBL_W);
    lv_obj_set_style_text_align(s.bar_lbl, LV_TEXT_ALIGN_RIGHT, 0);
#if UI_SCALE_SMALL
    softkeys(NULL, r4p_str(STR_STOP), NULL);
#else
    footer_right(LV_SYMBOL_STOP, r4p_str(STR_STOP), on_abort, NULL, C_RED);
#endif
}

static void build_result(void)
{
    const measure_result_t *r = measure_last();
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  ·  %s%s  ·  %s", r4p_str(STR_RESULT), r4p_str(STR_RESULT_TUBE),
             r4p_sick_label(r->sick), r4p_sample_label(r->sample));
    set_title(buf);
    build_slot_tiles(s.content, "");
    const r4p_settings_t *c = calib_store_get();
    for (int i = 0; i < R4P_SLOTS; i++) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)r->average[i]);
        lv_label_set_text(s.slot_val[i], buf);
        lv_color_t col = r->positive[i] ? C_RED : C_GREEN;
        lv_obj_set_style_border_color(s.slot_tile[i], col, 0);
        /* kết quả = icon + chữ + màu viền (không dựa vào màu đơn thuần) */
        lv_obj_delete(s.slot_sub[i]);
        lv_obj_t *sub = lv_obj_create(s.slot_tile[i]);
        lv_obj_remove_style_all(sub);
        lv_obj_set_size(sub, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(sub, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(sub, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(sub, 6, 0);
        mk_label(sub, r->positive[i] ? LV_SYMBOL_WARNING : LV_SYMBOL_OK, R4P_SLOTS > 4 ? &lv_font_montserrat_14 : F_ICON_SM, col);
        mk_label(sub, r->positive[i] ? r4p_str(STR_POSITIVE) : r4p_str(STR_NEGATIVE), R4P_SLOTS > 4 ? F_TINY : F_SMALL, col);
        s.slot_sub[i] = sub;
    }
    snprintf(buf, sizeof(buf), "%s: %lu     %s: %d", r4p_str(STR_THRESHOLD), (unsigned long)c->threshold[r->sick],
             r4p_str(STR_UPLOAD_PENDING), result_upload_pending());
    mk_text(s.content, buf, F_SMALL, C_MUTED);
#if UI_SCALE_SMALL
    softkeys(r4p_str(STR_REDO), r4p_str(STR_FINISH), NULL);        /* "Nút Xanh: Đo lại · Nút Đỏ: Kết thúc" */
#else
    footer_left(LV_SYMBOL_REFRESH, r4p_str(STR_REDO), on_goto, (void *)UI_PREPARE);
    footer_right(LV_SYMBOL_OK, r4p_str(STR_FINISH), on_finish, NULL, C_FORTE);
#endif
}

static void build_calib(void)
{
    set_title(r4p_str(STR_CALIB_MODE));
    const r4p_settings_t *c = calib_store_get();
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %d/%d   ·   %s%s", r4p_str(STR_SLOT), s.calib_slot + 1, R4P_SLOTS,
             r4p_str(STR_CALIB_SAMPLE), r4p_str(s.calib_step == 0 ? STR_CALIB_MAX : STR_CALIB_MIN));
    mk_text(s.content, buf, UI_SCALE_SMALL ? F_SMALL : F_BODY, C_TEXT);
    build_slot_tiles(s.content, "");
    for (int i = 0; i < R4P_SLOTS; i++) {
        snprintf(buf, sizeof(buf), "%u", c->cal_max[i]);
        lv_label_set_text(s.slot_val[i], buf);
        lv_obj_set_style_text_font(s.slot_val[i], F_BODY, 0);
        snprintf(buf, sizeof(buf), "%s %u", r4p_str(STR_CALIB_MIN), c->cal_min[i]);
        lv_label_set_text(s.slot_sub[i], buf);
        bool cur = (i == s.calib_slot);
        lv_obj_set_style_border_color(s.slot_tile[i], cur ? C_AMBER
                                      : (calib_store_slot_calibrated(i) ? C_GREEN : C_BORDER), 0);
        lv_obj_set_style_border_width(s.slot_tile[i], cur ? UI_TILE_BORDER + 1 : UI_TILE_BORDER, 0);
    }
    if (s.calib_step == 1) snprintf(buf, sizeof(buf), "%s = %u", r4p_str(STR_CALIB_MAX), s.calib_max_tmp);
    else buf[0] = '\0';
    s.calib_status = mk_text(s.content, s.calib_running ? r4p_str(STR_CALIBRATING) : buf, F_SMALL,
                             s.calib_running ? C_AMBER : C_FORTE);
#if UI_SCALE_SMALL
    softkeys(r4p_str(STR_BACK), r4p_str(STR_CALIBRATE), r4p_str(STR_CLEAR));   /* Xanh: Thoát · Đỏ: Cân chỉnh · Trắng: Xoá */
#else
    footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_START);
    footer_mid(LV_SYMBOL_TRASH, r4p_str(STR_CALIB_CLEAR), on_calib_clear, NULL, C_RED);
    footer_right(LV_SYMBOL_EDIT, r4p_str(STR_CALIBRATE), on_calib_read, NULL, C_FORTE);
#endif
}

static void build_settings(void)
{
    set_title(r4p_str(STR_SETTINGS));
    lv_obj_t *grid = mk_grid(s.content);
#if UI_SCALE_SMALL
    /* 2.8": danh sách 3 cột chữ (không icon — 94 px không đủ), mục cuối = Quay lại; điều hướng bằng 3 nút. */
    list_add(mk_btn(grid, NULL, r4p_str(STR_LANGUAGE), on_goto, (void *)UI_LANGUAGE, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_add(mk_btn(grid, NULL, r4p_str(STR_WIFI), on_goto, (void *)UI_WIFI, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_add(mk_btn(grid, NULL, r4p_str(STR_UPDATE), on_goto, (void *)UI_UPDATE, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_add(mk_btn(grid, NULL, r4p_str(STR_THRESHOLD), on_goto, (void *)UI_THRESHOLD, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_add(mk_btn(grid, LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_START, UI_BTN_LIST_W, BTN_H, C_BTN));
    list_focus(0);
    softkeys(r4p_str(STR_SELECT), LV_SYMBOL_DOWN, LV_SYMBOL_UP);
#else
    lv_obj_set_width(grid, 2 * UI_BTN_GRID_W + GAP + 4);   /* 4 mục → lưới 2×2 */
    mk_btn(grid, LV_SYMBOL_LIST, r4p_str(STR_LANGUAGE), on_goto, (void *)UI_LANGUAGE, UI_BTN_GRID_W, BTN_H, C_BTN);
    mk_btn(grid, LV_SYMBOL_WIFI, r4p_str(STR_WIFI), on_goto, (void *)UI_WIFI, UI_BTN_GRID_W, BTN_H, C_BTN);
    mk_btn(grid, LV_SYMBOL_DOWNLOAD, r4p_str(STR_UPDATE), on_goto, (void *)UI_UPDATE, UI_BTN_GRID_W, BTN_H, C_BTN);
    mk_btn(grid, LV_SYMBOL_EDIT, r4p_str(STR_THRESHOLD), on_goto, (void *)UI_THRESHOLD, UI_BTN_GRID_W, BTN_H, C_BTN);
    footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_START);
#endif
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
    s.thr_value = (int32_t)calib_store_get()->threshold[s.thr_sick];
    lv_obj_t *row = mk_row(s.content, UI_THR_ROW_H);
    lv_obj_set_style_pad_column(row, 2 * GAP + 4, 0);
    lv_obj_t *bm = mk_btn(row, LV_SYMBOL_MINUS, "", NULL, NULL, UI_THR_BTN_W, UI_THR_BTN_H, C_BTN);
    lv_obj_add_event_cb(bm, on_thr_btn, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(bm, on_thr_btn, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)-1);
    lv_obj_t *card = lv_obj_create(row);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, UI_THR_CARD_W, UI_THR_BTN_H);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, C_FORTE, 0);
    snprintf(buf, sizeof(buf), "%ld", (long)s.thr_value);
    s.thr_lbl = mk_label(card, buf, F_HERO, C_TEXT);
    lv_obj_center(s.thr_lbl);
    lv_obj_t *bp = mk_btn(row, LV_SYMBOL_PLUS, "", NULL, NULL, UI_THR_BTN_W, UI_THR_BTN_H, C_BTN);
    lv_obj_add_event_cb(bp, on_thr_btn, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(bp, on_thr_btn, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)1);
#if UI_SCALE_SMALL
    mk_text(s.content, r4p_str(STR_HOLD_HINT_THR), F_TINY, C_MUTED);
    softkeys("+10", "-10", r4p_str(STR_SAVE));      /* ReaderPlus: Xanh tăng · Đỏ giảm · Trắng kế tiếp/lưu */
#else
    mk_label(s.content, "+/- 10   " LV_SYMBOL_LOOP " +/- 50", F_ICON_SM, C_MUTED); /* giữ để lặp */
    footer_left(LV_SYMBOL_CLOSE, r4p_str(STR_CANCEL), on_goto, (void *)UI_THRESHOLD);
    footer_right(LV_SYMBOL_SAVE, r4p_str(STR_SAVE), on_thr_save, NULL, C_FORTE);
#endif
}

static void build_wifi(void)
{
    set_title(r4p_str(STR_WIFI_SETTING));
    esp_err_t r = wifi_mgr_start_provisioning();
    if (r != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "WiFi: %s", esp_err_to_name(r));
        mk_label(s.content, buf, F_BODY, C_RED);
        footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_SETTINGS);
        return;
    }
    /* ui_wifi_setup đã lv_screen_load màn riêng của nó (2 mã QR + trạng thái, cùng ui_theme).
     * Màn đó static: xoá nút cũ trước rồi gắn "Quay lại" vào footer của nó, bên trái, cùng
     * quy ước thanh hành động của mọi màn. */
    lv_obj_t *host = ui_wifi_setup_footer();
    if (host) {
#if UI_SCALE_SMALL
        softkeys_on(host, r4p_str(STR_BACK), NULL, NULL);          /* XANH = Quay lại (apply_key UI_WIFI) */
#else
        if (s.wifi_back_btn && lv_obj_is_valid(s.wifi_back_btn)) lv_obj_delete(s.wifi_back_btn);
        lv_obj_t *b = mk_btn(host, LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_wifi_back, NULL, UI_BTN_BACK_W, BTN_H, C_BTN);
        lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
        s.wifi_back_btn = b;
#endif
    }
}

static void build_update(void)
{
    set_title(r4p_str(STR_UPDATE));
    const bool online = wifi_mgr_is_connected();
    lv_obj_t *hdr = mk_row(s.content, UI_ROW_SMALL_H);
    mk_label(hdr, online ? LV_SYMBOL_DOWNLOAD : LV_SYMBOL_WARNING, F_ICON, online ? C_FORTE : C_AMBER);
    s.upd_lbl = mk_label(hdr, online ? r4p_str(STR_UPDATING) : r4p_str(STR_NO_WIFI),
                         F_BODY, online ? C_TEXT : C_AMBER);
    s.upd_bar = lv_bar_create(s.content);
    lv_obj_set_size(s.upd_bar, UI_BAR_W, UI_BAR_H);
    lv_bar_set_range(s.upd_bar, 0, 100);
    ui_theme_brand_bar(s.upd_bar);
    if (!online) lv_obj_add_flag(s.upd_bar, LV_OBJ_FLAG_HIDDEN);
    mk_label(s.content, "Firmware " R4P_FW_VERSION, F_SMALL, C_MUTED);
#if UI_SCALE_SMALL
    softkeys(r4p_str(STR_BACK), NULL, NULL);
#else
    footer_left(LV_SYMBOL_LEFT, r4p_str(STR_BACK), on_goto, (void *)UI_SETTINGS);
#endif
    if (online && !ota_client_busy()) {
        xTaskCreatePinnedToCore(update_task, "upd_ui", R4P_TASK_STACK_OTA, NULL,
                                R4P_TASK_PRIO_BACKGROUND, NULL, R4P_TASK_CORE_IO);
    }
}

static void show(ui_state_t st)
{
    s.state = st;
    modal_close();
#if UI_SCALE_SMALL
    softkeys_clear();                     /* ô softkey trên footer màn WiFi (host khác s.footer) */
#endif
    if (lv_screen_active() != s.screen) lv_screen_load(s.screen);
    content_clear();
    switch (st) {
        case UI_START: build_start(); break;
        case UI_CHOOSE_SAMPLE: build_choose_sample(); break;
        case UI_CHOOSE_TUBE: build_choose_tube(); break;
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
        case UI_START: if (measure_sensors_alive() > 0) show(UI_CHOOSE_SAMPLE); break;
        case UI_PREPARE: start_measure(); break;
        case UI_RESULT: show(UI_CHOOSE_SAMPLE); break;
        case UI_CALIB: on_calib_read(NULL); break;
        default: break;
    }
}

/* 3 nút vật lý XANH/ĐỎ/TRẮNG (và chạm ô softkey): MỘT nguồn sự thật theo màn — nhãn ô softkey ở
 * từng build_* phải khớp bảng này. Quy ước kế thừa ReaderPlus: XANH = bắt đầu/chọn/đo lại,
 * ĐỎ = đo/kết thúc/xuống, TRẮNG = lên/xoá. Danh sách (s.list_n > 0): XANH chọn · ĐỎ ▼ · TRẮNG ▲. */
static void apply_key(void *arg)
{
    const int v = (int)(intptr_t)arg;
    const r4p_key_t key = (r4p_key_t)(v & 0xFF);
    const bool hold = (v & 0x100) != 0;
    if (s.modal) {                                      /* hộp thoại: XANH = Huỷ, ĐỎ = Xác nhận */
        if (key == R4P_KEY_GREEN) modal_close();
        else if (key == R4P_KEY_RED) apply_modal_yes(NULL);
        return;
    }
    if (s.list_n > 0) {
        if (key == R4P_KEY_GREEN) list_activate();
        else if (key == R4P_KEY_RED) list_move(+1);
        else list_move(-1);
        return;
    }
    switch (s.state) {
        case UI_START:
            if (key == R4P_KEY_GREEN) { if (measure_sensors_alive() > 0) show(UI_CHOOSE_SAMPLE); }
            else if (key == R4P_KEY_RED) show(UI_SETTINGS);
            else show(UI_CALIB);
            break;
        case UI_PREPARE:
            if (key == R4P_KEY_GREEN) show(UI_CHOOSE_TUBE);
            else if (key == R4P_KEY_RED) start_measure();
            break;
        case UI_MEASURING:
            if (key == R4P_KEY_RED) { measure_abort(); show(UI_PREPARE); }
            break;
        case UI_RESULT:
            if (key == R4P_KEY_GREEN) show(UI_PREPARE);
            else if (key == R4P_KEY_RED) show(UI_CHOOSE_SAMPLE);
            break;
        case UI_CALIB:
            if (key == R4P_KEY_GREEN) show(UI_START);
            else if (key == R4P_KEY_RED) on_calib_read(NULL);
            else on_calib_clear(NULL);
            break;
        case UI_THRESHOLD_EDIT:
            if (key == R4P_KEY_GREEN) thr_apply_delta(hold ? 50 : 10);
            else if (key == R4P_KEY_RED) thr_apply_delta(hold ? -50 : -10);
            else if (hold) show(UI_THRESHOLD);            /* giữ TRẮNG = huỷ */
            else on_thr_save(NULL);
            break;
        case UI_UPDATE:
            if (key == R4P_KEY_GREEN) show(UI_SETTINGS);
            break;
        case UI_WIFI:
            if (key == R4P_KEY_GREEN) on_wifi_back(NULL);
            break;
        default:
            break;
    }
}

void ui_reader_on_key(r4p_key_t key, bool hold)
{
    display_schedule(apply_key, (void *)(intptr_t)((int)key | (hold ? 0x100 : 0)));
}

static void apply_boot_button(void *arg)
{
    (void)arg;
    if (s.modal) { modal_close(); return; }          /* BOOT = Huỷ hộp thoại */
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

void ui_reader_touch_debug(int x, int y, int raw_x, int raw_y)
{
    /* Gọi từ indev_read_cb (đã trong LVGL task, dưới lock của port) — không cần display_schedule. */
    if (!s.status || !lv_obj_is_valid(s.status)) return;
    char buf[48];
    /* Chỉ toạ độ logical (ngắn, không đè tiêu đề 2.8"); raw có trong log UART/USB-JTAG. */
    (void)raw_x; (void)raw_y;
    snprintf(buf, sizeof(buf), "T %d,%d", x, y);
    lv_label_set_text(s.status, buf);
    lv_obj_set_style_text_color(s.status, C_AMBER, 0);
}
void ui_reader_on_boot_button(void) { display_schedule(apply_boot_button, NULL); }
static void apply_confirm_demo(void *arg)
{
    (void)arg;
    static char q[96];
    snprintf(q, sizeof(q), r4p_str(STR_CLEAR_CALIB_ASK), R4P_SLOTS);
    confirm_show(q, NULL);
}
void ui_reader_show_debug(ui_state_t st)
{
    if (st < UI_COUNT) show_async(st);
    else if (st == UI_COUNT) display_schedule(apply_confirm_demo, NULL);   /* "ui confirm": xem hộp thoại */
}
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

    static const lv_style_prop_t tr_props[] = { LV_STYLE_BG_COLOR, 0 };
    lv_style_transition_dsc_init(&s_tr_press, tr_props, lv_anim_path_ease_out, 120, 0, NULL);

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
    lv_obj_t *logo = ui_logo_create(s.header, HEADER_LOGO_H, false);   /* logo góc trái */
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, UI_HEADER_PAD, 0);
    s.title = mk_label(s.header, "", F_TITLE, C_FORTE);
    lv_obj_align(s.title, LV_ALIGN_LEFT_MID, HEADER_TITLE_X, 0);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);
    /* UI_TITLE_W = UI_W − HEADER_TITLE_X − trạng thái dài nhất − lề (4.3": ~270 px icon IP n/n version).
     * Cao cố định 1 dòng: DOT chỉ cắt khi thiếu CAO. */
    lv_obj_set_size(s.title, UI_TITLE_W, UI_TITLE_H);
    /* Trạng thái: Montserrat (có LV_SYMBOL_*), chuỗi chỉ ASCII (icon, IP, số, version). */
    s.status = mk_label(s.header, "", F_ICON_SM, C_MUTED);
    lv_obj_align(s.status, LV_ALIGN_RIGHT_MID, -(UI_HEADER_PAD + 4), 0);

    const int lcd_h = display_lcd_height();
    s.content = lv_obj_create(s.screen);
    lv_obj_remove_style_all(s.content);
    lv_obj_set_pos(s.content, 0, HEADER_H);
    lv_obj_set_size(s.content, LV_PCT(100), lcd_h - HEADER_H - FOOTER_H);
    lv_obj_set_scrollable(s.content, false);

    s.footer = lv_obj_create(s.screen);
    lv_obj_remove_style_all(s.footer);
    lv_obj_set_pos(s.footer, 0, lcd_h - FOOTER_H);
    lv_obj_set_size(s.footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_pad_hor(s.footer, UI_FOOTER_PAD, 0);
    lv_obj_set_scrollable(s.footer, false);

    s.pending = result_upload_pending();
    show(UI_START);
    display_unlock();

    measure_set_progress_cb(measure_progress_cb, NULL);
    ESP_LOGI(TAG_UI, "ui_reader san sang (lang=%d)", (int)r4p_str_get_lang());
    return ESP_OK;
}
