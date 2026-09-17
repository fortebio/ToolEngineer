/**
 * ota_client.h — Query /ota/v1/ endpoint, parse response, download + flash nếu version mới.
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ota_client_check_once(void);  /* sync; trả về sau khi check (và update nếu cần) */
void ota_client_start_periodic_task(void); /* spawn task chạy mỗi CONFIG_VIMATE_OTA_INTERVAL_SEC */
/* Restore an activation challenge that was shown before a reboot/power loss. */
void ota_client_restore_pending_activation(void);
void ota_client_start_activation_poll(const char *activation_code);
/* Re-register promptly after the server rejects/deletes the persisted device
 * identity. Retries OTA discovery without blocking the UI/main loop. */
void ota_client_start_registration_recovery(void);

#ifdef __cplusplus
}
#endif
