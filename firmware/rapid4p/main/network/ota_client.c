#include "ota_client.h"
#include "engineer_api.h"
#include "rapid4p.h"
#include "core/nvs_store.h"
#include "esp_check.h"
#include "core/wifi_mgr.h"
#include "core/task_profile.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define OTA_BOOT_VALID_FALLBACK_S  90
#define CHECK_BODY_MAX             1024

static volatile bool s_busy;
static bool s_validated;
static bool s_display_ok;
static bool s_report_updated;

static void mark_valid(const char *why)
{
    if (s_validated) return;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGW(TAG_OTA, "app %s danh dau HOP LE (%s) - het rollback", run->label, why);
    }
    s_validated = true;
}

static void boot_valid_timer_cb(void *arg)
{
    (void)arg;
    if (s_display_ok) mark_valid("man hinh len, het thoi gian cho");
    else ESP_LOGW(TAG_OTA, "khong mark valid: display loi -> reset se rollback");
}

/* GET /ota/check → out_ver/out_url; ESP_OK = có bản mới; ESP_ERR_NOT_FOUND = không. */
static esp_err_t ota_check(char *out_ver, size_t ver_n, char *out_url, size_t url_n)
{
    if (!engineer_api_has_token()) return ESP_ERR_INVALID_STATE;
    char path[256];
    snprintf(path, sizeof(path), "/ota/check?device=%s&ver=%s&product=%s&hw=%s%s",
             g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : "", R4P_FW_VERSION,
             R4P_PRODUCT_KEY, R4P_HW_VERSION, s_report_updated ? "&updated=1" : "");
    char url[384];
    engineer_api_url(path, url, sizeof(url));
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    engineer_api_set_headers(c);
    esp_err_t e = esp_http_client_open(c, 0);
    if (e != ESP_OK) { esp_http_client_cleanup(c); return e; }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    char *body = calloc(1, CHECK_BODY_MAX);
    int n = body ? esp_http_client_read_response(c, body, CHECK_BODY_MAX - 1) : -1;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (!body) return ESP_ERR_NO_MEM;
    if (status != 200 || n <= 0) {
        ESP_LOGW(TAG_OTA, "/ota/check HTTP %d (%d B)", status, n);
        free(body);
        return status == 401 ? ESP_ERR_NOT_ALLOWED : ESP_FAIL;
    }
    /* Server trả lời = bản này nói chuyện được với server → app hợp lệ. */
    mark_valid("/ota/check tra loi");
    if (s_report_updated) { s_report_updated = false; nvs_store_erase("ota_updated"); }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    esp_err_t r = ESP_ERR_NOT_FOUND;
    const cJSON *up = cJSON_GetObjectItem(root, "update");
    if (cJSON_IsTrue(up)) {
        const cJSON *jver = cJSON_GetObjectItem(root, "ver");
        const cJSON *jurl = cJSON_GetObjectItem(root, "url");
        const char *ver = cJSON_IsString(jver) ? jver->valuestring : "";
        if (cJSON_IsString(jurl) && jurl->valuestring[0] && strcmp(ver, R4P_FW_VERSION) != 0) {
            strlcpy(out_ver, ver, ver_n);
            strlcpy(out_url, jurl->valuestring, url_n);
            r = ESP_OK;
        } else {
            ESP_LOGI(TAG_OTA, "server co %s = ban dang chay -> khong nap", ver);
        }
    } else {
        const cJSON *jr = cJSON_GetObjectItem(root, "reason");
        ESP_LOGI(TAG_OTA, "khong co ban moi (%s)", cJSON_IsString(jr) ? jr->valuestring : "-");
    }
    cJSON_Delete(root);
    return r;
}

static esp_err_t http_init_cb(esp_http_client_handle_t client)
{
    return engineer_api_set_headers(client);
}

static esp_err_t ota_download(const char *url, ota_progress_cb_t cb, void *ctx)
{
    esp_http_client_config_t hcfg = {
        .url = url, .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true, .buffer_size = 4096,
    };
    esp_https_ota_config_t ocfg = {
        .http_config = &hcfg, .http_client_init_cb = http_init_cb,
    };
    esp_https_ota_handle_t h = NULL;
    ESP_RETURN_ON_ERROR(esp_https_ota_begin(&ocfg, &h), TAG_OTA, "ota begin");
    int total = esp_https_ota_get_image_size(h);
    int last_pct = -1;
    esp_err_t e;
    while (1) {
        e = esp_https_ota_perform(h);
        if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int got = esp_https_ota_get_image_len_read(h);
        int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
        if (pct != last_pct && (pct % 5 == 0)) {
            last_pct = pct;
            ESP_LOGI(TAG_OTA, "tai %d%% (%d/%d)", pct, got, total);
            if (cb) cb(pct, "downloading", ctx);
        }
    }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG_OTA, "ota perform loi %s", esp_err_to_name(e));
        esp_https_ota_abort(h);
        return e == ESP_OK ? ESP_ERR_INVALID_SIZE : e;
    }
    e = esp_https_ota_finish(h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG_OTA, "ota finish loi %s", esp_err_to_name(e));
        return e;
    }
    nvs_store_set_u32("ota_updated", 1);
    if (cb) cb(100, "done", ctx);
    ESP_LOGW(TAG_OTA, "OTA xong - restart sau 1 s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_client_check_now(ota_progress_cb_t cb, void *ctx)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;
    if (!wifi_mgr_is_connected()) return ESP_ERR_WIFI_NOT_CONNECT;
    s_busy = true;
    char ver[32] = {0}, url[256] = {0};
    esp_err_t e = ota_check(ver, sizeof(ver), url, sizeof(url));
    if (e == ESP_OK) {
        ESP_LOGW(TAG_OTA, "co ban moi %s: %s", ver, url);
        if (cb) cb(0, ver, ctx);
        e = ota_download(url, cb, ctx);
    }
    s_busy = false;
    return e;
}

static void ota_task(void *arg)
{
    (void)arg;
    while (1) {
        /* Poll trạng thái thay vì chờ bit R4P_EVT_WIFI_UP: main task xoá bit khi nhận
         * (xClearOnExit) nên task này có thể không bao giờ thấy nó. */
        if (!wifi_mgr_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));   /* để SNTP/DHCP yên sau khi vừa có IP */
        esp_err_t e = ota_client_check_now(NULL, NULL);
        if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG_OTA, "check dinh ky: %s", esp_err_to_name(e));
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_RAPID4P_OTA_INTERVAL_SEC * 1000));
    }
}

esp_err_t ota_client_init(bool display_ok)
{
    s_display_ok = display_ok;
    uint32_t updated = 0;
    nvs_store_get_u32("ota_updated", &updated);
    s_report_updated = updated == 1;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(run, &st);
    ESP_LOGI(TAG_OTA, "dang chay %s @0x%lx state=%d ver=%s%s", run->label, (unsigned long)run->address,
             (int)st, R4P_FW_VERSION, s_report_updated ? " (vua OTA)" : "");
    if (st != ESP_OTA_IMG_PENDING_VERIFY) {
        s_validated = true;
    } else {
        const esp_timer_create_args_t a = { .callback = boot_valid_timer_cb, .name = "ota_valid" };
        esp_timer_handle_t t;
        if (esp_timer_create(&a, &t) == ESP_OK) {
            esp_timer_start_once(t, (uint64_t)OTA_BOOT_VALID_FALLBACK_S * 1000000ULL);
        }
    }
    return ESP_OK;
}

esp_err_t ota_client_start_periodic(void)
{
    return xTaskCreatePinnedToCore(ota_task, "ota", R4P_TASK_STACK_OTA, NULL, R4P_TASK_PRIO_BACKGROUND,
                                   NULL, R4P_TASK_CORE_IO) == pdPASS ? ESP_OK : ESP_FAIL;
}

bool ota_client_busy(void) { return s_busy; }
