/**
 * engineer_api.h — điểm chung nói chuyện với Engineer Server (server/, hub.fortebio.tech):
 * base URL, Bearer token (NVS `api_token`, nhập qua portal), URL ghép, thời gian VN.
 *
 * Hợp đồng server (server/app/main.py):
 *   GET  /ota/check?device=&ver=&product=&hw=[&updated=1]  → {update, ver, url, sha256, size}
 *   GET  /ota/{product}/{file}                             → .bin (header x-MD5)
 *   POST /<bất kỳ>  body JSON {id_device,...}              → {ok:true,...} (catch-all)
 * Mọi route đều cần `Authorization: Bearer <RECEIVER_TOKEN>`; thiếu token = 401.
 */
#pragma once
#include "esp_err.h"
#include "esp_http_client.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nạp base URL (Kconfig) + token (NVS) vào RAM. Gọi sau nvs_store_init. */
void engineer_api_init(void);
bool engineer_api_has_token(void);
/* Ghép base + path vào out. path bắt đầu bằng '/'. */
void engineer_api_url(const char *path, char *out, size_t n);
/* Đặt header Authorization + User-Agent lên client. */
esp_err_t engineer_api_set_headers(esp_http_client_handle_t client);
/* "DD-MM-YYYY HH:MM:SS" giờ VN (server parse_ts _ALT_RE); "N/A" nếu chưa có SNTP. */
void engineer_api_time_vn(char *out, size_t n);

#ifdef __cplusplus
}
#endif
