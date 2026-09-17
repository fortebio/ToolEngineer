#include "engineer_api.h"
#include "rapid4p.h"
#include "core/nvs_store.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static char s_token[132];

void engineer_api_init(void)
{
    strlcpy(g_r4p_cfg.base_url, CONFIG_RAPID4P_SERVER_BASE, sizeof(g_r4p_cfg.base_url));
    size_t n = strlen(g_r4p_cfg.base_url);
    while (n > 0 && g_r4p_cfg.base_url[n - 1] == '/') g_r4p_cfg.base_url[--n] = 0;
    nvs_store_get_str("api_token", s_token, sizeof(s_token));
    nvs_store_get_str("device_id", g_r4p_cfg.device_id, sizeof(g_r4p_cfg.device_id));
    ESP_LOGI(TAG_NET, "Engineer Server %s device_id=%s token=%s", g_r4p_cfg.base_url,
             g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : "(chua dat)",
             s_token[0] ? "co" : "CHUA CO (nhap qua portal)");
}

bool engineer_api_has_token(void)
{
    if (!s_token[0]) nvs_store_get_str("api_token", s_token, sizeof(s_token));
    return s_token[0] != 0;
}

void engineer_api_url(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s%s", g_r4p_cfg.base_url, path);
}

esp_err_t engineer_api_set_headers(esp_http_client_handle_t client)
{
    if (!engineer_api_has_token()) return ESP_ERR_INVALID_STATE;
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent", "Rapid4P/" R4P_FW_VERSION);
    return ESP_OK;
}

void engineer_api_time_vn(char *out, size_t n)
{
    time_t now = 0;
    time(&now);
    if (now < 1600000000) {   /* trước 2020-09: SNTP chưa đồng bộ */
        strlcpy(out, "N/A", n);
        return;
    }
    struct tm tmv;
    localtime_r(&now, &tmv);   /* TZ đặt "ICT-7" trong app_main */
    strftime(out, n, "%d-%m-%Y %H:%M:%S", &tmv);
}
