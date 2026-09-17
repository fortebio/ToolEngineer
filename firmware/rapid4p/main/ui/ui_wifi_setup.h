/**
 * ui_wifi_setup.h — màn provisioning SoftAP, kiểu 2 QR (theo CrossInk) + trạng thái.
 *
 * QR 1: chuỗi `WIFI:T:nopass;S:<ssid>;;` — camera điện thoại quét là vào thẳng
 *       hotspot của thiết bị, không phải mò trong danh sách WiFi.
 * QR 2: URL trang cấu hình — quét là mở trình duyệt vào đúng trang.
 *
 * Màn này KHÔNG tĩnh. Một radio duy nhất vừa phát AP vừa nối WiFi nhà, nên khi
 * thiết bị thử mật khẩu người dùng gửi, AP phải nhảy sang kênh của router và
 * điện thoại BỊ RỚT khỏi trang cấu hình. Trang web không thể báo kết quả trong
 * lúc đó — màn hình thiết bị là kênh phản hồi duy nhất còn chắc chắn, nên nó
 * phải hiện được cả tiến trình lẫn lỗi.
 */
#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_WIFI_SETUP_WAITING = 0,   /* AP đã phát, chưa ai kết nối */
    UI_WIFI_SETUP_CLIENT,        /* có thiết bị vào AP, chờ mở trang */
    UI_WIFI_SETUP_VALIDATING,    /* đang thử SSID/mật khẩu người dùng gửi */
    UI_WIFI_SETUP_ERROR,         /* thử thất bại, `detail` = mã lỗi wifi_mgr */
    UI_WIFI_SETUP_SUCCESS,       /* đã nối được, `detail` = địa chỉ IP */
} ui_wifi_setup_state_t;

/**
 * Dựng (hoặc cập nhật) và hiển thị màn provisioning. Gọi lại nhiều lần an toàn.
 *
 * @param ssid        SSID của AP mở đang phát (bắt buộc).
 * @param portal_url  URL trang cấu hình, ví dụ "http://192.168.4.1/".
 *                    NULL → dùng mặc định http://192.168.4.1/.
 */
void ui_wifi_setup_show(const char *ssid, const char *portal_url);

/**
 * Đổi trạng thái hiển thị. An toàn khi màn chưa dựng (no-op).
 *
 * @param detail  ERROR → mã lỗi của wifi_mgr ("auth_failed", "network_not_found"…);
 *                SUCCESS → địa chỉ IP; trạng thái khác bỏ qua. NULL được phép.
 */
void ui_wifi_setup_set_state(ui_wifi_setup_state_t state, const char *detail);

/** Số thiết bị đang bám vào AP; tự chuyển WAITING <-> CLIENT. */
void ui_wifi_setup_set_client_count(int clients);

/** true khi màn provisioning đã dựng và đang là màn hoạt động. */
bool ui_wifi_setup_is_active(void);

/** Footer (thanh hành động, cao FOOTER_H) của màn này — ui_reader gắn nút "Quay lại" vào đây
 *  (LV_ALIGN_LEFT_MID) để cùng quy ước với các màn khác. NULL nếu màn chưa dựng. */
lv_obj_t *ui_wifi_setup_footer(void);

#ifdef __cplusplus
}
#endif
