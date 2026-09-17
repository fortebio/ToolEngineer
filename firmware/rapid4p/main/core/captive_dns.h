#pragma once

/*
 * captive_dns.h — DNS "hijack" tối giản cho chế độ SoftAP provisioning.
 *
 * Trả về CÙNG một địa chỉ IP (IP của AP, 192.168.4.1) cho MỌI truy vấn A.
 * Đây là mảnh còn thiếu để captive portal hoạt động: điện thoại vừa join AP là
 * bắn ngay một HTTP probe tới tên miền của hãng
 * (connectivitycheck.gstatic.com, captive.apple.com, www.msftconnecttest.com…).
 * Không có DNS trả lời thì:
 *   - probe fail ở bước resolve → hệ điều hành KHÔNG mở trang đăng nhập,
 *   - Android báo "Internet có thể không khả dụng" và thường tự nhảy về 4G,
 *     cắt luôn kết nối tới thiết bị,
 *   - người dùng buộc phải tự gõ 192.168.4.1.
 * Có DNS này + handler 404→302 trong wifi_mgr.c thì probe rơi vào trang cấu
 * hình của mình → OS tự bật cửa sổ captive portal.
 *
 * Chỉ chạy trong lúc provisioning; wifi_mgr_stop_provisioning() sẽ tắt.
 */

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bật DNS server trên cổng 53, mọi truy vấn A đều trả `resolve_to`.
 * Gọi lại khi đang chạy = no-op trả ESP_OK. */
esp_err_t captive_dns_start(esp_ip4_addr_t resolve_to);

/* Tắt và giải phóng. An toàn khi chưa từng start. */
void captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
