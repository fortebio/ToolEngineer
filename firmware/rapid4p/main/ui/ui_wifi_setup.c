/**
 * ui_wifi_setup.c — màn provisioning SoftAP, dựng theo luồng 2-QR của CrossInk
 * (https://github.com/uxjulia/CrossInk, `CrossPointWebServerActivity::renderServerRunning`)
 * và bổ sung phản hồi trạng thái mà CrossInk không cần.
 *
 * ── Hai QR ──────────────────────────────────────────────────────────────────
 *   1. QR cấu hình WiFi  — payload `WIFI:T:nopass;S:<ssid>;;` theo spec zxing
 *      (https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config).
 *      Camera iOS/Android quét là hỏi "Tham gia mạng ...?" → hết bước mò trong
 *      danh sách WiFi và gõ tên.
 *   2. QR URL trang web — quét là mở trình duyệt đúng trang, hết bước gõ IP.
 * Kèm chữ SSID và URL bên dưới làm đường lui khi camera không quét được.
 *
 * ── Vì sao màn này phải có trạng thái ───────────────────────────────────────
 * Thiết bị chỉ có MỘT radio. Lúc thử mật khẩu người dùng gửi, STA nối vào
 * router ở kênh khác thì AP buộc phải nhảy theo, và MỌI điện thoại đang bám AP
 * đều rớt. Trang web vì thế KHÔNG bảo đảm hiện được kết quả — màn hình thiết bị
 * là kênh phản hồi duy nhất còn chắc chắn. Nó phải nói rõ đang chờ, đang thử,
 * hay hỏng vì lý do gì, kèm bước tiếp theo.
 *
 * ── KHÁC CrossInk có chủ đích ───────────────────────────────────────────────
 *   - CrossInk nhét `http://crosspoint.local/` (mDNS) vào QR, để IP làm chữ phụ.
 *     Ta làm NGƯỢC LẠI: QR mang thẳng IP `http://192.168.4.1/`. QR là thứ để
 *     QUÉT chứ không phải để gõ, nên nó cần chắc chắn nhất; tên .local phụ thuộc
 *     mDNS (iOS phân giải .local qua multicast, KHÔNG đi qua DNS server của ta
 *     nên captive_dns.c không đỡ được). Chữ IP vẫn hiện để gõ tay.
 *   - Do đó KHÔNG cần component mDNS: DNS hijack trong captive_dns.c đã trả mọi
 *     tên miền về IP của AP rồi.
 *
 * Escape SSID: spec WIFI: bắt buộc chèn '\' trước các ký tự \ ; , : "
 * SSID của ta có dấu hai chấm ("FBT-Rapid4P-53:C8") nên BẮT BUỘC escape, không
 * thì điện thoại đọc sai tên mạng. (CrossInk không escape vì SSID của họ sạch.)
 *
 * ── Bố cục 2026-09-17 (làm lại theo ui_theme.h, review qua webcam) ─────────
 * 800×480 landscape: header 56 (tiêu đề + chip trạng thái) · nội dung 328 =
 * HAI thẻ QR CẠNH NHAU (mỗi QR ~168 px — bản cũ xếp dọc bị ép còn 88 px, khó
 * quét) hoặc bảng trạng thái (đang thử / lỗi + bước tiếp theo / thành công) ·
 * footer 96 = thanh hành động (ui_reader gắn "Quay lại" bên trái, gợi ý đường
 * lui bên phải). QR giữ đen-trên-trắng + quiet zone để camera bắt được.
 * Chuỗi màn này còn tiếng Việt cứng (portal web cũng vậy) — chưa qua ui_strings.
 */
#include "ui_wifi_setup.h"
#include "ui_theme.h"
#include "ui_logo.h"
#include "rapid4p.h"
#include "display.h"
#include "boards/board.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define WIFI_SETUP_DEFAULT_URL "http://192.168.4.1/"

/* Kích thước theo thang màn (ui_theme.h). 4.3": 2 thẻ cột cạnh nhau (QR 200). 2.8": chiều CAO là
 * ràng buộc (content 140 px) nên 2 thẻ cột chỉ cho QR ~74 px (< 2,5 px/module, không quét được) →
 * MỘT thẻ full width bố cục HÀNG (QR 128 px bên trái, chữ bên phải), hiện thẻ của bước đang làm
 * (① khi chờ điện thoại, ② khi đã có máy bám AP), chạm thẻ để đổi tay. */
#if UI_SCALE_SMALL
#define CONTENT_PAD   8
#define CARD_PAD      6
#define CARD_W        (UI_W - 2 * CONTENT_PAD)
#define QR_MAX        128
#define QR_MIN        96
#define BADGE_SZ      22
#define CHIP_H        26
#define STATUS_W      (UI_W - 2 * CONTENT_PAD)
#define STATUS_PAD    8
#define SPINNER_SZ    36
#define HINT_W        (UI_W - 2 * UI_FOOTER_PAD - UI_BTN_BACK_W - GAP)
#else
#define CONTENT_PAD   12
#define CARD_PAD      10
#define CARD_W        372
#define QR_MAX        200
#define QR_MIN        120
#define BADGE_SZ      30
#define CHIP_H        38
#define STATUS_W      700
#define STATUS_PAD    20
#define SPINNER_SZ    56
#define HINT_W        520
#endif

/* Bao lâu thì màn lỗi tự nhường lại cho 2 QR để người dùng quét lại. */
#define ERROR_HOLD_MS 8000

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_header       = NULL;
static lv_obj_t *s_chip         = NULL;      /* chip trạng thái ở header phải */
static lv_obj_t *s_chip_icon    = NULL;
static lv_obj_t *s_chip_lbl     = NULL;
static lv_obj_t *s_content      = NULL;
static lv_obj_t *s_footer       = NULL;
static lv_obj_t *s_qr_panel     = NULL;
static lv_obj_t *s_status_panel = NULL;
static lv_obj_t *s_qr_wifi      = NULL;
static lv_obj_t *s_qr_url       = NULL;
static lv_obj_t *s_card1        = NULL;      /* thẻ bước 1 / bước 2 (2.8": chỉ một thẻ hiện) */
static lv_obj_t *s_card2        = NULL;
#if UI_SCALE_SMALL
static bool s_small_flip        = false;     /* người dùng chạm thẻ để xem bước kia */
#endif
static lv_obj_t *s_step1        = NULL;
static lv_obj_t *s_step1_badge  = NULL;
static lv_obj_t *s_step2        = NULL;
static lv_obj_t *s_lbl_ssid     = NULL;
static lv_obj_t *s_lbl_url      = NULL;
static lv_obj_t *s_hint         = NULL;
static lv_obj_t *s_spinner      = NULL;
static lv_obj_t *s_st_icon      = NULL;
static lv_obj_t *s_st_title     = NULL;
static lv_obj_t *s_st_body      = NULL;
static lv_timer_t *s_back_timer = NULL;

static bool s_active   = false;
static int  s_clients  = 0;
static ui_wifi_setup_state_t s_state = UI_WIFI_SETUP_WAITING;
static char s_ssid[33] = {0};
static char s_url[40]  = WIFI_SETUP_DEFAULT_URL;

/* ─────────────────────────── tiện ích ─────────────────────────── */

/* Chèn '\' trước các ký tự đặc biệt của chuỗi WIFI: (spec zxing). */
static void wifi_qr_escape(char *dst, size_t dst_sz, const char *src) {
    size_t j = 0;
    if (!dst || dst_sz == 0) return;
    for (size_t i = 0; src && src[i] && j + 2 < dst_sz; i++) {
        char c = src[i];
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            dst[j++] = '\\';
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

/* Mã lỗi máy của wifi_mgr -> câu tiếng Việt người dùng đọc được.
 * Dùng CÙNG bộ từ vựng với trang web trong wifi_mgr.c để hai nơi không nói
 * khác nhau về cùng một lỗi. */
static const char *err_title(const char *d) {
    if (!d || !d[0])                          return "Không kết nối được WiFi";
    if (strcmp(d, "auth_failed") == 0)        return "Sai mật khẩu WiFi";
    if (strcmp(d, "network_not_found") == 0)  return "Không tìm thấy mạng WiFi";
    if (strcmp(d, "connection_failed") == 0)  return "Không kết nối được WiFi";
    if (strcmp(d, "timeout") == 0)            return "WiFi không phản hồi";
    if (strcmp(d, "password_too_short") == 0) return "Mật khẩu quá ngắn";
    if (strcmp(d, "password_too_long") == 0)  return "Mật khẩu quá dài";
    if (strcmp(d, "password_invalid") == 0)   return "Mật khẩu không hợp lệ";
    if (strcmp(d, "ssid_empty") == 0)         return "Chưa chọn mạng WiFi";
    if (strcmp(d, "ssid_too_long") == 0)      return "Tên WiFi quá dài";
    if (strcmp(d, "nvs") == 0)                return "Không lưu được cấu hình";
    if (strcmp(d, "busy") == 0)               return "Đang kiểm tra mạng khác";
    return "Không kết nối được WiFi";
}

/* Mỗi lỗi phải kèm BƯỚC TIẾP THEO, không chỉ báo hỏng. */
static const char *err_next_step(const char *d) {
    if (!d || !d[0]) return "Quét lại mã và thử lại trên điện thoại.";
    if (strcmp(d, "auth_failed") == 0 || strcmp(d, "password_invalid") == 0) {
        return "Nhập lại mật khẩu rồi bấm Thử lại trên điện thoại.";
    }
    if (strcmp(d, "password_too_short") == 0) {
        return "Mật khẩu WiFi phải từ 8 ký tự trở lên.";
    }
    if (strcmp(d, "network_not_found") == 0) {
        return "Đưa thiết bị lại gần router, bấm Quét lại trên điện thoại. "
               "Thiết bị chỉ dùng được WiFi 2.4GHz.";
    }
    if (strcmp(d, "timeout") == 0 || strcmp(d, "connection_failed") == 0) {
        return "Kiểm tra router còn hoạt động, rồi bấm Thử lại trên điện thoại.";
    }
    if (strcmp(d, "nvs") == 0) {
        return "Khởi động lại thiết bị rồi cài đặt lại.";
    }
    return "Quét lại mã và thử lại trên điện thoại.";
}

/* ─────────────────────────── dựng màn ─────────────────────────── */

static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

/* Kích thước QR tính từ chiều cao nội dung còn trống trong thẻ (2 thẻ cạnh nhau
 * nên bề ngang không còn là ràng buộc). */
static int32_t compute_qr_size(void) {
    int32_t card_h = BOARD_LCD_V_RES - HEADER_H - FOOTER_H - 2 * CONTENT_PAD;   /* 4.3": 304 · 2.8": 136 (footer 56) -> QR 124 */
#if UI_SCALE_SMALL
    int32_t qr = card_h - 2 * CARD_PAD;               /* bố cục hàng: chỉ chiều cao ràng buộc */
#else
    const int32_t h18 = lv_font_get_line_height(F_SMALL);
    const int32_t h24 = lv_font_get_line_height(F_BODY);
    int32_t qr = card_h - 2 * CARD_PAD - h18 - h24 - 3 * 6;
    int32_t max_w = CARD_W - 2 * CARD_PAD;
    if (qr > max_w)  qr = max_w;
#endif
    if (qr > QR_MAX) qr = QR_MAX;
    if (qr < QR_MIN) qr = QR_MIN;
    ESP_LOGI(TAG_UI, "QR size=%d (man %dx%d)", (int)qr, (int)BOARD_LCD_H_RES, (int)BOARD_LCD_V_RES);
    return qr;
}

static lv_obj_t *make_card(lv_obj_t *parent, int32_t w) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(card, CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_size(card, w, LV_PCT(100));
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(card, false);
    return card;
}

/* Thẻ = huy hiệu số bước + tiêu đề bước, QR (đen trên trắng, quiet zone), chú thích.
 * 4.3": cột (hdr / QR / chú thích). 2.8": hàng (QR trái · cột phải = hdr + chú thích). */
static lv_obj_t *make_qr_card(lv_obj_t *parent, const char *num, const char *step_text, int32_t qr_size,
                              lv_obj_t **out_badge, lv_obj_t **out_step, lv_obj_t **out_qr,
                              lv_obj_t **out_caption) {
    lv_obj_t *card = make_card(parent, CARD_W);
    lv_obj_t *text_host = card;
#if UI_SCALE_SMALL
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(card, GAP + 2, 0);
    lv_obj_t *qr_s = lv_qrcode_create(card);
    lv_qrcode_set_size(qr_s, qr_size);
    lv_qrcode_set_dark_color(qr_s, lv_color_black());
    lv_qrcode_set_light_color(qr_s, lv_color_white());
    lv_qrcode_set_quiet_zone(qr_s, true);
    lv_obj_set_style_radius(qr_s, 6, 0);
    lv_obj_set_style_clip_corner(qr_s, true, 0);
    if (out_qr) *out_qr = qr_s;
    lv_obj_t *col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_scrollable(col, false);
    text_host = col;
#endif

    lv_obj_t *hdr = lv_obj_create(text_host);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, UI_SCALE_SMALL ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, UI_SCALE_SMALL ? 6 : 10, 0);

    lv_obj_t *badge = lv_obj_create(hdr);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, BADGE_SZ, BADGE_SZ);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, C_FORTE, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_t *bl = mk_label(badge, num, F_SMALL, C_ON_FORTE);
    lv_obj_center(bl);
    if (out_badge) *out_badge = badge;

    lv_obj_t *step = mk_label(hdr, step_text, F_SMALL, C_TEXT);
#if UI_SCALE_SMALL
    lv_label_set_long_mode(step, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(step, 1);
#endif
    if (out_step) *out_step = step;

#if !UI_SCALE_SMALL
    lv_obj_t *qr = lv_qrcode_create(card);
    lv_qrcode_set_size(qr, qr_size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);        /* thiếu quiet zone nhiều camera không bắt */
    lv_obj_set_style_radius(qr, 8, 0);
    lv_obj_set_style_clip_corner(qr, true, 0);
    if (out_qr) *out_qr = qr;
#endif

    lv_obj_t *cap = mk_label(text_host, "", F_BODY, C_FORTE);
    lv_label_set_long_mode(cap, UI_SCALE_SMALL ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_width(cap, LV_PCT(100));
    lv_obj_set_style_text_align(cap, UI_SCALE_SMALL ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    if (out_caption) *out_caption = cap;
    return card;
}

#if UI_SCALE_SMALL
/* 2.8": chỉ một thẻ hiện — thẻ của bước đang làm (hoặc thẻ kia nếu người dùng vừa chạm). */
static void small_pick_card(void) {
    if (!s_card1 || !s_card2) return;
    bool show2 = (s_clients > 0);
    if (s_small_flip) show2 = !show2;
    if (show2) {
        lv_obj_add_flag(s_card1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_card2, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_card1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_card2, LV_OBJ_FLAG_HIDDEN);
    }
}
static void on_card_tap(lv_event_t *e) {
    (void)e;
    s_small_flip = !s_small_flip;
    small_pick_card();
}
#endif

static void build_header(void) {
    s_header = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_header);
    lv_obj_set_size(s_header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(s_header, C_PANEL, 0);
    lv_obj_set_style_bg_opa(s_header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_header, 2, 0);
    lv_obj_set_style_border_color(s_header, C_FORTE, 0);

    lv_obj_t *logo = ui_logo_create(s_header, HEADER_LOGO_H, false);   /* logo góc trái */
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, UI_HEADER_PAD, 0);
    lv_obj_t *title = mk_label(s_header, UI_SCALE_SMALL ? "WIFI" : "CÀI ĐẶT WIFI", F_TITLE, C_FORTE);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, HEADER_TITLE_X, 0);

    /* Chip trạng thái: icon + chữ — trạng thái LUÔN nói bằng chữ, màu chỉ phụ trợ. */
    s_chip = lv_obj_create(s_header);
    lv_obj_remove_style_all(s_chip);
    lv_obj_set_size(s_chip, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_style_bg_color(s_chip, C_CARD, 0);
    lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_chip, CHIP_H / 2, 0);
    lv_obj_set_style_border_width(s_chip, 2, 0);
    lv_obj_set_style_pad_hor(s_chip, UI_BTN_PAD + 2, 0);
    lv_obj_set_flex_flow(s_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_chip, 8, 0);
    lv_obj_align(s_chip, LV_ALIGN_RIGHT_MID, -(UI_HEADER_PAD + 4), 0);
    s_chip_icon = mk_label(s_chip, LV_SYMBOL_WIFI, F_ICON_SM, C_FORTE);
    s_chip_lbl  = mk_label(s_chip, "", F_SMALL, C_TEXT);
#if UI_SCALE_SMALL
    /* Header 320 px: chip chỉ còn icon màu; câu trạng thái nằm ở thẻ + gợi ý footer. */
    lv_obj_add_flag(s_chip_lbl, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void build_status_panel(lv_obj_t *parent) {
    s_status_panel = make_card(parent, STATUS_W);
    lv_obj_set_style_pad_all(s_status_panel, STATUS_PAD, 0);
    lv_obj_set_style_pad_row(s_status_panel, 10, 0);
    lv_obj_add_flag(s_status_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_center(s_status_panel);

    s_spinner = lv_spinner_create(s_status_panel);
    lv_obj_set_size(s_spinner, SPINNER_SZ, SPINNER_SZ);
    lv_spinner_set_anim_params(s_spinner, 1000, 60);
    lv_obj_set_style_arc_color(s_spinner, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, C_FORTE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);

    s_st_icon = mk_label(s_status_panel, "", &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_style_text_font(s_st_icon, F_ICON, 0);

    s_st_title = mk_label(s_status_panel, "", F_BODY, C_TEXT);
    lv_label_set_long_mode(s_st_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_st_title, LV_PCT(100));
    lv_obj_set_style_text_align(s_st_title, LV_TEXT_ALIGN_CENTER, 0);

    s_st_body = mk_label(s_status_panel, "", F_SMALL, C_MUTED);
    lv_label_set_long_mode(s_st_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_st_body, LV_PCT(100));
    lv_obj_set_style_text_align(s_st_body, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
}

static void build_screen(void) {
    if (s_screen) return;

    const int32_t qr_size = compute_qr_size();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_screen, false);

    build_header();

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, HEADER_H);
    lv_obj_set_size(s_content, LV_PCT(100), BOARD_LCD_V_RES - HEADER_H - FOOTER_H);
    lv_obj_set_style_pad_all(s_content, CONTENT_PAD, 0);
    lv_obj_set_scrollable(s_content, false);

    /* Vùng nội dung: hoặc 2 QR cạnh nhau, hoặc bảng trạng thái — đúng một cái hiện. */
    s_qr_panel = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_qr_panel);
    lv_obj_set_size(s_qr_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_qr_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_qr_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(s_qr_panel, false);

    s_card1 = make_qr_card(s_qr_panel, "1", "Quét để vào WiFi máy", qr_size,
                           &s_step1_badge, &s_step1, &s_qr_wifi, &s_lbl_ssid);
    s_card2 = make_qr_card(s_qr_panel, "2", "Quét để mở trang cài đặt", qr_size,
                           NULL, &s_step2, &s_qr_url, &s_lbl_url);
#if UI_SCALE_SMALL
    lv_obj_add_event_cb(s_card1, on_card_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_card2, on_card_tap, LV_EVENT_CLICKED, NULL);
    small_pick_card();
#endif

    build_status_panel(s_content);

    /* Footer: ui_reader gắn "Quay lại" bên trái; gợi ý đường lui bên phải (14 px, 2 dòng). */
    s_footer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_footer);
    lv_obj_set_pos(s_footer, 0, BOARD_LCD_V_RES - FOOTER_H);
    lv_obj_set_size(s_footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_pad_hor(s_footer, UI_FOOTER_PAD, 0);
    lv_obj_set_scrollable(s_footer, false);

    s_hint = mk_label(s_footer, "", F_TINY, C_MUTED);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, HINT_W);
#if UI_SCALE_SMALL
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);   /* footer 2.8" = softkey bar 3 nút (ui_reader); URL đã có trên thẻ ② */
#endif
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_hint, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ─────────────────────────── trạng thái ─────────────────────────── */

static void set_chip(lv_color_t color, const char *icon, const char *text) {
    if (!s_chip) return;
    lv_obj_set_style_border_color(s_chip, color, 0);
    lv_obj_set_style_text_color(s_chip_icon, color, 0);
    lv_label_set_text(s_chip_icon, icon);
    lv_label_set_text(s_chip_lbl, text);
}

static void show_qr_view(bool show) {
    if (!s_qr_panel || !s_status_panel) return;
    if (show) {
        lv_obj_remove_flag(s_qr_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qr_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_status_panel(const char *icon, lv_color_t color, const char *title,
                             const char *body, bool busy) {
    if (!s_st_title || !s_st_body || !s_spinner) return;
    lv_obj_set_style_text_color(s_st_title, color, 0);
    lv_label_set_text(s_st_title, title ? title : "");
    lv_label_set_text(s_st_body, body ? body : "");
    lv_label_set_text(s_st_icon, icon ? icon : "");
    lv_obj_set_style_text_color(s_st_icon, color, 0);
    if (busy) {
        lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_st_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_st_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Bước 1 chuyển sang "xong" khi đã có máy bám AP — huy hiệu thành ✓ xanh, chữ đổi,
 * để người dùng biết còn phải làm bước 2 chứ không phải quét lại từ đầu. */
static void mark_step1_done(bool done) {
    if (!s_step1 || !s_step1_badge) return;
    lv_obj_t *bl = lv_obj_get_child(s_step1_badge, 0);
    if (done) {
        lv_label_set_text(s_step1, "Đã vào WiFi máy");
        lv_obj_set_style_text_color(s_step1, C_GREEN, 0);
        lv_obj_set_style_bg_color(s_step1_badge, C_GREEN, 0);
        lv_label_set_text(bl, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(bl, F_ICON_SM, 0);
        lv_obj_set_style_text_color(bl, C_ON_FORTE, 0);   /* trắng trên green-500 chỉ 1,9:1 */
    } else {
        lv_label_set_text(s_step1, "Quét để vào WiFi máy");
        lv_obj_set_style_text_color(s_step1, C_TEXT, 0);
        lv_obj_set_style_bg_color(s_step1_badge, C_FORTE, 0);
        lv_label_set_text(bl, "1");
        lv_obj_set_style_text_font(bl, F_SMALL, 0);
        lv_obj_set_style_text_color(bl, C_ON_FORTE, 0);
    }
}

static void cancel_back_timer(void) {
    if (s_back_timer) {
        lv_timer_delete(s_back_timer);
        s_back_timer = NULL;
    }
}

static void back_to_qr_cb(lv_timer_t *t) {
    (void)t;
    /* repeat_count = 1 nên LVGL tự xoá timer sau lần chạy này. */
    s_back_timer = NULL;
    ui_wifi_setup_set_state(s_clients > 0 ? UI_WIFI_SETUP_CLIENT
                                          : UI_WIFI_SETUP_WAITING, NULL);
}

static void apply_state(const char *detail) {
    char buf[192];

    switch (s_state) {
    case UI_WIFI_SETUP_CLIENT:
        show_qr_view(true);
        mark_step1_done(true);
#if UI_SCALE_SMALL
        s_small_flip = false;
        small_pick_card();
#endif
        set_chip(C_GREEN, LV_SYMBOL_OK, "Điện thoại đã vào - quét mã 2");
        snprintf(buf, sizeof(buf), "Trang cài đặt chưa mở? Gõ %s vào trình duyệt.", s_url);
        lv_label_set_text(s_hint, buf);
        break;

    case UI_WIFI_SETUP_VALIDATING:
        cancel_back_timer();
        show_qr_view(false);
        set_chip(C_AMBER, LV_SYMBOL_REFRESH, "Đang kiểm tra WiFi...");
        set_status_panel(NULL, C_TEXT, "Đang thử mật khẩu WiFi",
                         "Điện thoại có thể tạm rớt khỏi mạng máy - đó là bình thường. "
                         "Kết quả sẽ hiện ngay tại đây.",
                         true);
        lv_label_set_text(s_hint, "Giữ máy gần router.");
        break;

    case UI_WIFI_SETUP_ERROR:
        show_qr_view(false);
        set_chip(C_RED, LV_SYMBOL_WARNING, "Không kết nối được");
        set_status_panel(LV_SYMBOL_WARNING, C_RED, err_title(detail), err_next_step(detail), false);
        lv_label_set_text(s_hint, "Mã QR sẽ hiện lại sau vài giây.");
        cancel_back_timer();
        s_back_timer = lv_timer_create(back_to_qr_cb, ERROR_HOLD_MS, NULL);
        if (s_back_timer) lv_timer_set_repeat_count(s_back_timer, 1);
        break;

    case UI_WIFI_SETUP_SUCCESS:
        cancel_back_timer();
        show_qr_view(false);
        set_chip(C_GREEN, LV_SYMBOL_WIFI, "Đã kết nối WiFi");
        if (detail && detail[0]) {
            snprintf(buf, sizeof(buf), "Địa chỉ IP %s. Máy đang khởi động lại...", detail);
        } else {
            snprintf(buf, sizeof(buf), "Máy đang khởi động lại...");
        }
        set_status_panel(LV_SYMBOL_OK, C_GREEN, "Kết nối thành công", buf, false);
        lv_label_set_text(s_hint, "");
        break;

    case UI_WIFI_SETUP_WAITING:
    default:
        show_qr_view(true);
        mark_step1_done(false);
#if UI_SCALE_SMALL
        s_small_flip = false;
        small_pick_card();
#endif
        set_chip(C_FORTE, LV_SYMBOL_WIFI, "Đang chờ điện thoại");
        snprintf(buf, sizeof(buf), "Không quét được mã? Vào WiFi \"%s\" rồi mở %s", s_ssid, s_url);
        lv_label_set_text(s_hint, buf);
        break;
    }
}

/* ─────────────────────────── API ─────────────────────────── */

void ui_wifi_setup_show(const char *ssid, const char *portal_url) {
    if (!ssid || !ssid[0]) ssid = R4P_SETUP_PREFIX;
    if (!portal_url || !portal_url[0]) portal_url = WIFI_SETUP_DEFAULT_URL;

    display_lock();
    build_screen();

    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_url, portal_url, sizeof(s_url));

    if (s_qr_wifi) {
        char esc[80];
        char payload[128];
        wifi_qr_escape(esc, sizeof(esc), s_ssid);
        /* T:nopass = mạng mở, đúng với AP provisioning của ta. */
        snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", esc);
        if (lv_qrcode_update(s_qr_wifi, payload, strlen(payload)) != LV_RESULT_OK) {
            ESP_LOGW(TAG_UI, "QR WiFi update lỗi (payload=%s)", payload);
        }
    }
    if (s_qr_url) {
        if (lv_qrcode_update(s_qr_url, s_url, strlen(s_url)) != LV_RESULT_OK) {
            ESP_LOGW(TAG_UI, "QR URL update lỗi (url=%s)", s_url);
        }
    }
    if (s_lbl_ssid) lv_label_set_text(s_lbl_ssid, s_ssid);
    if (s_lbl_url)  lv_label_set_text(s_lbl_url, s_url);

    apply_state(NULL);

    lv_screen_load(s_screen);
    s_active = true;
    display_unlock();
    ESP_LOGI(TAG_UI, "WiFi setup screen: SSID=%s portal=%s", s_ssid, s_url);
}

void ui_wifi_setup_set_state(ui_wifi_setup_state_t state, const char *detail) {
    display_lock();
    if (!s_screen) {
        display_unlock();
        return;
    }
    s_state = state;
    apply_state(detail);
    display_unlock();
    ESP_LOGI(TAG_UI, "WiFi setup state=%d detail=%s", (int)state,
             detail ? detail : "-");
}

void ui_wifi_setup_set_client_count(int clients) {
    if (clients < 0) clients = 0;
    display_lock();
    s_clients = clients;
    /* Chỉ đổi giữa WAITING và CLIENT. Đang VALIDATING/ERROR/SUCCESS thì việc
     * máy khách vào ra không được phép cướp màn trạng thái. */
    if (s_screen && (s_state == UI_WIFI_SETUP_WAITING ||
                     s_state == UI_WIFI_SETUP_CLIENT)) {
        s_state = clients > 0 ? UI_WIFI_SETUP_CLIENT : UI_WIFI_SETUP_WAITING;
        apply_state(NULL);
    }
    display_unlock();
}

bool ui_wifi_setup_is_active(void) { return s_active; }

lv_obj_t *ui_wifi_setup_footer(void) { return s_footer; }

/* Nút ĐỎ "Đổi mã" trên softkey 2.8" (apply_key UI_WIFI): lật thẻ WiFi ↔ mã máy như chạm thẻ —
 * nông dân đeo găng không chạm được màn. 4.3": cả hai thẻ luôn hiện, không làm gì. */
void ui_wifi_setup_toggle_card(void)
{
#if UI_SCALE_SMALL
    if (!s_active) return;
    s_small_flip = !s_small_flip;
    small_pick_card();
#endif
}
