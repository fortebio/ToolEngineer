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
 * SSID của ta có dấu hai chấm ("GENU-Setup-53:C8") nên BẮT BUỘC escape, không
 * thì điện thoại đọc sai tên mạng. (CrossInk không escape vì SSID của họ sạch.)
 *
 * Màu: mọi cặp chữ/nền dưới đây đã tính tỉ số tương phản WCAG >= 4.5:1 cho chữ
 * thường. Giá trị cũ COL_MUTED 0x6b7280 (4.1:1) và COL_OK 0x16a34a (3.3:1)
 * KHÔNG đạt nên đã thay.
 */
#include "ui_wifi_setup.h"
#include "rapid4p.h"
#include "display.h"
#include "boards/board.h"
#include "lvgl.h"
#include "fonts/lv_font_vimate.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define WIFI_SETUP_DEFAULT_URL "http://192.168.4.1/"

/* Bảng màu dùng chung với app Flutter cha mẹ. Số trong ngoặc = tỉ số tương phản
 * với nền tương ứng, đo theo WCAG 2.1 (ngưỡng chữ thường 4.5:1). */
#define COL_BG        0xede9fe   /* nền tím nhạt */
#define COL_CARD      0xffffff
#define COL_TEXT      0x1f2937   /* trên card trắng: 12.6:1 */
#define COL_MUTED     0x4b5563   /* trên nền: 6.3:1  (cũ 0x6b7280 = 4.1:1 -> rớt) */
#define COL_TITLE     0x92400e   /* trên nền: 6.0:1  (cũ 0xb45309 = 4.2:1 -> rớt) */
#define COL_ACCENT    0x5b54e8   /* trên card trắng: 5.4:1 */
#define COL_OK        0x166534   /* trên card trắng: 5.3:1 (cũ 0x16a34a = 3.3:1 -> rớt) */
#define COL_ERR       0xb91c1c   /* trên card trắng: 6.5:1 */
#define COL_WARN      0x92400e   /* trên card trắng: 6.0:1 */

/* Nhịp giãn cách 4/8 px, không dùng số lẻ tuỳ hứng. */
#define PAD_SCREEN    12
#define GAP_ROW        8
#define CARD_PAD      10
#define CARD_GAP_TXT   6

#define QR_MAX       220
#define QR_MIN        88

/* Bao lâu thì màn lỗi tự nhường lại cho 2 QR để người dùng quét lại. */
#define ERROR_HOLD_MS 8000

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_banner       = NULL;
static lv_obj_t *s_banner_dot   = NULL;
static lv_obj_t *s_banner_lbl   = NULL;
static lv_obj_t *s_qr_panel     = NULL;
static lv_obj_t *s_status_panel = NULL;
static lv_obj_t *s_qr_wifi      = NULL;
static lv_obj_t *s_qr_url       = NULL;
static lv_obj_t *s_step1        = NULL;
static lv_obj_t *s_step2        = NULL;
static lv_obj_t *s_lbl_ssid     = NULL;
static lv_obj_t *s_lbl_url      = NULL;
static lv_obj_t *s_hint         = NULL;
static lv_obj_t *s_spinner      = NULL;
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

/* Kích thước QR tính NGƯỢC từ chiều cao còn trống, thay vì lấy một tỉ lệ cố
 * định: 480x800 của P4 và 320x240 của bo S3 chênh nhau quá xa để dùng chung
 * một hằng số, và QR bị cắt thì không quét được. */
static int32_t compute_qr_size(void) {
    const int32_t h24 = lv_font_get_line_height(&lv_font_vimate_24);
    const int32_t h18 = lv_font_get_line_height(&lv_font_vimate_18);
    const int32_t h14 = lv_font_get_line_height(&lv_font_vimate_14);

    int32_t avail = BOARD_LCD_V_RES - 2 * PAD_SCREEN;
    avail -= h24 + GAP_ROW;                 /* tiêu đề */
    avail -= (8 + h18 + 8) + GAP_ROW;       /* dải trạng thái */
    avail -= (2 * h14) + GAP_ROW;           /* dòng gợi ý (2 dòng) */

    const int32_t chrome = 2 * CARD_PAD + h18 + 2 * CARD_GAP_TXT + h24;
    int32_t qr = ((avail - GAP_ROW) / 2) - chrome;

    /* Không được rộng hơn bề ngang card. */
    const int32_t max_w = BOARD_LCD_H_RES - 2 * PAD_SCREEN - 2 * CARD_PAD - 8;
    if (qr > max_w)  qr = max_w;
    if (qr > QR_MAX) qr = QR_MAX;
    if (qr < QR_MIN) qr = QR_MIN;
    /* Log ra de kiem tra qua UART: QR bi ep xuong QR_MIN tren man nho la dau
     * hieu bo cuc khong vua, phai bo bot chu chu khong phai thu nho ma. */
    ESP_LOGI(TAG_UI, "QR size=%d (man %dx%d)", (int)qr,
             (int)BOARD_LCD_H_RES, (int)BOARD_LCD_V_RES);
    return qr;
}

static lv_obj_t *make_card(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, CARD_PAD, 0);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* Một "thẻ" chứa tiêu đề bước + QR + chữ mô tả bên dưới. */
static void make_qr_card(lv_obj_t *parent, const char *step_text, int32_t qr_size,
                         lv_obj_t **out_step, lv_obj_t **out_qr,
                         lv_obj_t **out_caption) {
    lv_obj_t *card = make_card(parent);

    lv_obj_t *step = lv_label_create(card);
    lv_label_set_text(step, step_text);
    lv_obj_set_style_text_font(step, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(step, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_pad_bottom(step, CARD_GAP_TXT, 0);
    if (out_step) *out_step = step;

    lv_obj_t *qr = lv_qrcode_create(card);
    lv_qrcode_set_size(qr, qr_size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    /* Vùng lặng (quiet zone) — thiếu nó nhiều camera không bắt được mã. */
    lv_qrcode_set_quiet_zone(qr, true);
    if (out_qr) *out_qr = qr;

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, "");
    lv_obj_set_style_text_font(cap, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_pad_top(cap, CARD_GAP_TXT, 0);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cap, LV_PCT(100));
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    if (out_caption) *out_caption = cap;
}

static void build_banner(lv_obj_t *parent) {
    s_banner = lv_obj_create(parent);
    lv_obj_remove_style_all(s_banner);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_banner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_banner, 14, 0);
    lv_obj_set_style_pad_ver(s_banner, 8, 0);
    lv_obj_set_width(s_banner, LV_PCT(100));
    lv_obj_set_height(s_banner, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_banner, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);

    /* Chấm màu chỉ là phụ trợ — trạng thái LUÔN được nói bằng chữ bên cạnh,
     * không bao giờ chỉ dựa vào màu. */
    s_banner_dot = lv_obj_create(s_banner);
    lv_obj_remove_style_all(s_banner_dot);
    lv_obj_set_size(s_banner_dot, 12, 12);
    lv_obj_set_style_radius(s_banner_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_banner_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_right(s_banner_dot, 8, 0);

    s_banner_lbl = lv_label_create(s_banner);
    lv_obj_set_style_text_font(s_banner_lbl, &lv_font_vimate_18, 0);
    lv_label_set_long_mode(s_banner_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(s_banner_lbl, 1);
    lv_obj_set_style_text_align(s_banner_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_banner_lbl, "");
}

static void build_status_panel(lv_obj_t *parent) {
    s_status_panel = make_card(parent);
    /* flex_grow quyet dinh chieu cao theo truc doc, nen KHONG dat height o day
     * — dat them chi lam nguoi doc sau tuong no co tac dung. */
    lv_obj_set_flex_grow(s_status_panel, 1);
    lv_obj_set_flex_align(s_status_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_status_panel, 20, 0);

    s_spinner = lv_spinner_create(s_status_panel);
    lv_obj_set_size(s_spinner, 56, 56);
    lv_spinner_set_anim_params(s_spinner, 1000, 60);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_margin_bottom(s_spinner, 16, 0);

    s_st_title = lv_label_create(s_status_panel);
    lv_obj_set_style_text_font(s_st_title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_st_title, lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(s_st_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_st_title, LV_PCT(100));
    lv_obj_set_style_text_align(s_st_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_st_title, "");

    s_st_body = lv_label_create(s_status_panel);
    lv_obj_set_style_text_font(s_st_body, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(s_st_body, lv_color_hex(COL_MUTED), 0);
    lv_label_set_long_mode(s_st_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_st_body, LV_PCT(100));
    lv_obj_set_style_text_align(s_st_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_st_body, 12, 0);
    lv_label_set_text(s_st_body, "");

    lv_obj_add_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
}

static void build_screen(void) {
    if (s_screen) return;

    const int32_t qr_size = compute_qr_size();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, PAD_SCREEN, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_screen, GAP_ROW, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, R4P_BRAND_NAME " · Cài đặt WiFi");
    lv_obj_set_style_text_font(title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TITLE), 0);

    build_banner(s_screen);

    /* Vùng nội dung: hoặc 2 QR, hoặc bảng trạng thái — luôn đúng một cái hiện.
     * Cả hai đều flex_grow 1 nên khi đổi qua lại chiều cao không nhảy. */
    s_qr_panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_qr_panel);
    lv_obj_set_width(s_qr_panel, LV_PCT(100));
    lv_obj_set_flex_grow(s_qr_panel, 1);
    lv_obj_set_flex_flow(s_qr_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_qr_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_qr_panel, GAP_ROW, 0);
    lv_obj_clear_flag(s_qr_panel, LV_OBJ_FLAG_SCROLLABLE);

    make_qr_card(s_qr_panel, "1 · Quét để vào WiFi thiết bị",
                 qr_size, &s_step1, &s_qr_wifi, &s_lbl_ssid);
    make_qr_card(s_qr_panel, "2 · Quét để mở trang cài đặt",
                 qr_size, &s_step2, &s_qr_url, &s_lbl_url);

    build_status_panel(s_screen);

    s_hint = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_hint, &lv_font_vimate_14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COL_MUTED), 0);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_hint, "");
}

/* ─────────────────────────── trạng thái ─────────────────────────── */

static void set_banner(uint32_t color, const char *text) {
    if (!s_banner_dot || !s_banner_lbl) return;
    lv_obj_set_style_bg_color(s_banner_dot, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(s_banner_lbl, lv_color_hex(color), 0);
    lv_label_set_text(s_banner_lbl, text);
}

static void show_qr_view(bool show) {
    if (!s_qr_panel || !s_status_panel) return;
    if (show) {
        lv_obj_clear_flag(s_qr_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qr_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_status_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_status_panel(const char *title, const char *body, bool busy) {
    if (!s_st_title || !s_st_body || !s_spinner) return;
    /* Trả màu tiêu đề về trung tính trước; ERROR/SUCCESS sẽ tự tô đè. Không có
     * dòng này thì màu đỏ của lần lỗi trước dính lại sang màn "đang thử". */
    lv_obj_set_style_text_color(s_st_title, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(s_st_title, title ? title : "");
    lv_label_set_text(s_st_body, body ? body : "");
    if (busy) {
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Bước 1 chuyển sang "xong" khi đã có máy bám AP — chỉ báo tiến trình, để người
 * dùng biết còn phải làm bước 2 chứ không phải quét lại từ đầu. */
static void mark_step1_done(bool done) {
    if (!s_step1) return;
    if (done) {
        lv_label_set_text(s_step1, "1 · Đã vào WiFi thiết bị — xong");
        lv_obj_set_style_text_color(s_step1, lv_color_hex(COL_OK), 0);
    } else {
        lv_label_set_text(s_step1, "1 · Quét để vào WiFi thiết bị");
        lv_obj_set_style_text_color(s_step1, lv_color_hex(COL_TEXT), 0);
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
        set_banner(COL_OK, "Đã vào WiFi thiết bị — quét mã 2");
        snprintf(buf, sizeof(buf), "Trang cài đặt chưa mở? Gõ %s vào trình duyệt.",
                 s_url);
        lv_label_set_text(s_hint, buf);
        break;

    case UI_WIFI_SETUP_VALIDATING:
        cancel_back_timer();
        show_qr_view(false);
        set_banner(COL_WARN, "Đang kiểm tra WiFi...");
        set_status_panel("Đang thử mật khẩu WiFi",
                         "Điện thoại có thể tạm rớt khỏi mạng thiết bị — "
                         "đó là bình thường. Kết quả sẽ hiện ngay tại đây.",
                         true);
        lv_label_set_text(s_hint, "Vui lòng giữ thiết bị gần router.");
        break;

    case UI_WIFI_SETUP_ERROR:
        show_qr_view(false);
        set_banner(COL_ERR, "Không kết nối được");
        set_status_panel(err_title(detail), err_next_step(detail), false);
        lv_obj_set_style_text_color(s_st_title, lv_color_hex(COL_ERR), 0);
        lv_label_set_text(s_hint, "Mã QR sẽ hiện lại sau vài giây.");
        cancel_back_timer();
        s_back_timer = lv_timer_create(back_to_qr_cb, ERROR_HOLD_MS, NULL);
        if (s_back_timer) lv_timer_set_repeat_count(s_back_timer, 1);
        break;

    case UI_WIFI_SETUP_SUCCESS:
        cancel_back_timer();
        show_qr_view(false);
        set_banner(COL_OK, "Đã kết nối WiFi");
        if (detail && detail[0]) {
            snprintf(buf, sizeof(buf),
                     "Địa chỉ IP %s. Thiết bị đang khởi động lại...", detail);
        } else {
            snprintf(buf, sizeof(buf), "Thiết bị đang khởi động lại...");
        }
        set_status_panel("Kết nối thành công", buf, false);
        lv_obj_set_style_text_color(s_st_title, lv_color_hex(COL_OK), 0);
        lv_label_set_text(s_hint, "");
        break;

    case UI_WIFI_SETUP_WAITING:
    default:
        show_qr_view(true);
        mark_step1_done(false);
        set_banner(COL_ACCENT, "Đang chờ điện thoại kết nối");
        snprintf(buf, sizeof(buf),
                 "Không quét được mã? Vào WiFi \"%s\" rồi mở %s", s_ssid, s_url);
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
    if (s_step2)    lv_label_set_text(s_step2, "2 · Quét để mở trang cài đặt");

    apply_state(NULL);

    lv_scr_load(s_screen);
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
