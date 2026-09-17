/**
 * nvs_store.h — Wrapper NVS cho lưu device token, WiFi creds.
 * Namespace: "vimate".
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_store_init(void);

/* Generic helpers — return ESP_OK nếu thành công */
esp_err_t nvs_store_set_str(const char *key, const char *value);
esp_err_t nvs_store_get_str(const char *key, char *out, size_t maxlen);
esp_err_t nvs_store_set_u32(const char *key, uint32_t value);
esp_err_t nvs_store_get_u32(const char *key, uint32_t *out);
esp_err_t nvs_store_erase(const char *key);
esp_err_t nvs_store_set_blob(const char *key, const void *data, size_t len);
esp_err_t nvs_store_get_blob(const char *key, void *out, size_t len);   /* NOT_FOUND / INVALID_SIZE */

/* WiFi creds lưu CHUNG 1 blob (atomic): cúp điện giữa lúc ghi không để lại
 * ssid mới + pass cũ lệch nhau. Đọc fallback key cũ wifi_ssid/wifi_pass cho
 * thiết bị OTA từ bản trước (chưa có blob). */
esp_err_t nvs_store_set_wifi(const char *ssid, const char *pass);
esp_err_t nvs_store_get_wifi(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz);

/* Wipe hết namespace vimate → factory reset (giữ WiFi creds riêng) */
esp_err_t nvs_store_factory_reset(void);

#ifdef __cplusplus
}
#endif
