/**
 * wifi_mgr.h — WiFi STA + provisioning AP fallback.
 *
 * Flow:
 *   1. wifi_mgr_start() — load creds NVS, connect STA.
 *      Trả OK ngay; signal R4P_EVT_WIFI_UP qua event group khi connected.
 *      Trả error nếu chưa có creds.
 *   2. Nếu OK fail → wifi_mgr_start_provisioning() — bật AP mở
 *      "VIMATE-Setup-XXXX", mở HTTP portal để chọn SSID + nhập password WiFi nhà.
 *   3. AP và BLE đều gọi wifi_mgr_submit_credentials(). Credentials chỉ được
 *      commit vào NVS sau khi STA nhận IP; lỗi sẽ rollback cấu hình cũ.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_PROV_STATE_IDLE = 0,
    WIFI_PROV_STATE_VALIDATING,
    WIFI_PROV_STATE_CONNECTED,
    WIFI_PROV_STATE_ERROR,
} wifi_prov_state_t;

typedef struct {
    wifi_prov_state_t state;
    char detail[32];
    char ip_address[16];
} wifi_prov_status_t;

typedef void (*wifi_prov_observer_t)(const wifi_prov_status_t *status, void *ctx);

esp_err_t wifi_mgr_start(void);
esp_err_t wifi_mgr_start_provisioning(void);
esp_err_t wifi_mgr_stop_provisioning(void);
esp_err_t wifi_mgr_submit_credentials(const char *ssid, const char *password);
void wifi_mgr_get_provision_status(wifi_prov_status_t *out);
void wifi_mgr_set_provision_observer(wifi_prov_observer_t observer, void *ctx);
const char *wifi_mgr_provision_state_name(wifi_prov_state_t state);
bool wifi_mgr_is_connected(void);
const char *wifi_mgr_ip_address(void);

#ifdef __cplusplus
}
#endif
