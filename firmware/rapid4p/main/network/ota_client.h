/**
 * ota_client.h — OTA qua Engineer Server (server/app/main.py::ota_check), 2 slot app +
 * rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
 *
 *   GET {base}/ota/check?device=<id>&ver=<R4P_FW_VERSION>&product=rapid4p&hw=<hw>[&updated=1]
 *   → {"update":true,"ver":"v0.2.0","url":"https://.../ota/rapid4p/rapid4p_v0.2.0.bin",...}
 *   So sánh version là việc của firmware: `ver` ≠ bản đang chạy → tải `url` bằng
 *   esp_https_ota (kèm Bearer) → ghi cờ NVS ota_updated=1 → restart. Lần boot sau, lượt
 *   check đầu gửi `updated=1` (server ghi "vừa cập nhật") rồi xoá cờ.
 *
 *   Rollback guard: app mới chưa được "mark valid" thì lần reset kế tiếp bootloader quay
 *   về slot cũ. Ta mark valid khi (a) /ota/check trả lời được (mạng + TLS + server OK)
 *   hoặc (b) sau OTA_BOOT_VALID_FALLBACK_S giây mà màn hình lên (không WiFi cũng không
 *   được kẹt ở bản cũ mãi).
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ota_progress_cb_t)(int percent, const char *msg, void *ctx);

esp_err_t ota_client_init(bool display_ok);
/* Task định kỳ: check ngay khi WiFi lên rồi mỗi CONFIG_RAPID4P_OTA_INTERVAL_SEC. */
esp_err_t ota_client_start_periodic(void);
/* Check + nạp ngay (nút "Cập nhật" trên màn). Chặn tới khi xong; ESP_OK = đã nạp và
 * sắp restart; ESP_ERR_NOT_FOUND = không có bản mới; lỗi khác = mạng/server. */
esp_err_t ota_client_check_now(ota_progress_cb_t cb, void *ctx);
bool ota_client_busy(void);

#ifdef __cplusplus
}
#endif
