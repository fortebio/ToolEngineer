#include "result_upload.h"
#include "engineer_api.h"
#include "rapid4p.h"
#include "app/calib_store.h"
#include "core/nvs_store.h"
#include "core/wifi_mgr.h"
#include "core/task_profile.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

/* Hàng đợi vòng trong NVS: rq_head/rq_tail (u32, tăng mãi), bản ghi rq_<idx % RQ_CAP>.
 * Đầy → bỏ bản ghi CŨ nhất (giữ kết quả mới). 8 × ~700 B ≈ 6 KB (NVS 24 KB còn chỗ cho WiFi
 * blob, calib, token; 16 bản ≈ 15 KB là quá sát). */
#define RQ_CAP        8
#define RQ_MAX_JSON   1100   /* 5 khe: 5 mảng × 5 phần tử ≈ 1 KB; NVS 8 × 1,1 KB ≈ 9 KB */
#define UPLOAD_PATH   "/rapid4p/results"   /* server POST catch-all: path chỉ để log */
#define RETRY_BASE_MS 15000
#define RETRY_MAX_MS  300000

static SemaphoreHandle_t s_wake;
static uint32_t s_head, s_tail;   /* tail = bản kế tiếp cần gửi, head = ô trống kế tiếp */
static int s_sent;
static SemaphoreHandle_t s_mtx;

static void rq_key(uint32_t idx, char *k, size_t n) { snprintf(k, n, "rq_%lu", (unsigned long)(idx % RQ_CAP)); }

static void rq_load(void)
{
    nvs_store_get_u32("rq_head", &s_head);
    nvs_store_get_u32("rq_tail", &s_tail);
    if (s_head < s_tail || s_head - s_tail > RQ_CAP) { s_head = s_tail = 0; }
}

static char *build_json(const measure_result_t *r)
{
    const r4p_settings_t *c = calib_store_get();
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddStringToObject(o, "id_device", g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : g_r4p_cfg.mac_id);
    cJSON_AddStringToObject(o, "version", R4P_FW_VERSION);
    cJSON_AddStringToObject(o, "product", R4P_PRODUCT_KEY);
    cJSON_AddStringToObject(o, "hw", R4P_HW_VERSION);
    cJSON_AddStringToObject(o, "mac", g_r4p_cfg.mac_id);
    cJSON_AddStringToObject(o, "method", "append");
    cJSON_AddStringToObject(o, "type_Upload", "rapid4p_result");
    cJSON_AddStringToObject(o, "sick", r4p_sick_name(r->sick));
    cJSON_AddStringToObject(o, "sample", r4p_sample_name(r->sample));
    cJSON_AddNumberToObject(o, "slots", R4P_SLOTS);
    cJSON *jv = cJSON_AddArrayToObject(o, "slot_value");
    cJSON *jr = cJSON_AddArrayToObject(o, "slot_result");
    cJSON *jp = cJSON_AddArrayToObject(o, "slot_positive");
    cJSON *jmin = cJSON_AddArrayToObject(o, "calib_min");
    cJSON *jmax = cJSON_AddArrayToObject(o, "calib_max");
    for (int i = 0; i < R4P_SLOTS; i++) {
        cJSON_AddItemToArray(jv, cJSON_CreateNumber(r->value_raw[i]));
        cJSON_AddItemToArray(jr, cJSON_CreateNumber(r->average[i]));
        cJSON_AddItemToArray(jp, cJSON_CreateBool(r->positive[i]));
        cJSON_AddItemToArray(jmin, cJSON_CreateNumber(c->cal_min[i]));
        cJSON_AddItemToArray(jmax, cJSON_CreateNumber(c->cal_max[i]));
        char k[16];
        snprintf(k, sizeof(k), "value_sensor%d", i + 1);
        cJSON_AddNumberToObject(o, k, r->average[i]);
    }
    cJSON_AddNumberToObject(o, "threshold", c->threshold[r->sick]);
    char ts[24];
    engineer_api_time_vn(ts, sizeof(ts));
    cJSON_AddStringToObject(o, "time", ts);
    cJSON_AddNumberToObject(o, "duration_ms", (double)((r->finished_us - r->started_us) / 1000));
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

esp_err_t result_upload_enqueue(const measure_result_t *r)
{
    char *json = build_json(r);
    if (!json) return ESP_ERR_NO_MEM;
    if (strlen(json) >= RQ_MAX_JSON) {
        ESP_LOGE(TAG_NET, "payload %u B > %d - bo", (unsigned)strlen(json), RQ_MAX_JSON);
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_head - s_tail >= RQ_CAP) {
        ESP_LOGW(TAG_NET, "hang doi day - bo ban ghi cu nhat");
        s_tail++;
    }
    char k[16];
    rq_key(s_head, k, sizeof(k));
    esp_err_t e = nvs_store_set_str(k, json);
    if (e == ESP_OK) {
        s_head++;
        nvs_store_set_u32("rq_head", s_head);
        nvs_store_set_u32("rq_tail", s_tail);
    }
    xSemaphoreGive(s_mtx);
    cJSON_free(json);
    ESP_LOGI(TAG_NET, "xep hang ket qua (%s) pending=%lu", esp_err_to_name(e), (unsigned long)(s_head - s_tail));
    xEventGroupSetBits(g_r4p_events, R4P_EVT_UPLOAD_QUEUED);
    if (s_wake) xSemaphoreGive(s_wake);
    return e;
}

/* Gửi một bản ghi; ESP_OK khi server trả 2xx. */
static esp_err_t post_one(const char *json)
{
    char url[192];
    engineer_api_url(UPLOAD_PATH, url, sizeof(url));
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t e = engineer_api_set_headers(client);
    if (e != ESP_OK) { esp_http_client_cleanup(client); return e; }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, (int)strlen(json));
    e = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (e != ESP_OK) {
        ESP_LOGW(TAG_NET, "POST loi %s", esp_err_to_name(e));
        return e;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG_NET, "POST HTTP %d", status);
        /* 4xx = payload/token sai: gửi lại cũng vậy → bỏ bản ghi để không kẹt hàng đợi
         * (401 thì giữ lại chờ người dùng nhập token). 5xx = server: thử lại. */
        if (status == 401 || status >= 500) return ESP_FAIL;
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static void upload_task(void *arg)
{
    (void)arg;
    uint32_t backoff = RETRY_BASE_MS;
    while (1) {
        xSemaphoreTake(s_wake, pdMS_TO_TICKS(backoff));
        if (s_head == s_tail) { backoff = RETRY_BASE_MS; continue; }
        if (!wifi_mgr_is_connected() || !engineer_api_has_token()) continue;
        char k[16];
        static char json[RQ_MAX_JSON];
        rq_key(s_tail, k, sizeof(k));
        if (nvs_store_get_str(k, json, sizeof(json)) != ESP_OK || !json[0]) {
            /* bản ghi hỏng/mất → bỏ qua */
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_tail++;
            nvs_store_set_u32("rq_tail", s_tail);
            xSemaphoreGive(s_mtx);
            continue;
        }
        esp_err_t e = post_one(json);
        if (e == ESP_OK || e == ESP_ERR_INVALID_RESPONSE) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            nvs_store_erase(k);
            s_tail++;
            nvs_store_set_u32("rq_tail", s_tail);
            xSemaphoreGive(s_mtx);
            if (e == ESP_OK) s_sent++;
            ESP_LOGI(TAG_NET, "%s, con %lu", e == ESP_OK ? "da gui" : "bo ban ghi loi 4xx",
                     (unsigned long)(s_head - s_tail));
            backoff = RETRY_BASE_MS;
            if (s_head != s_tail) xSemaphoreGive(s_wake);
        } else {
            backoff = backoff * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : backoff * 2;
            ESP_LOGW(TAG_NET, "gui that bai, thu lai sau %lu s", (unsigned long)(backoff / 1000));
        }
    }
}

esp_err_t result_upload_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    rq_load();
    ESP_LOGI(TAG_NET, "hang doi ket qua: pending=%lu", (unsigned long)(s_head - s_tail));
    xTaskCreatePinnedToCore(upload_task, "upload", R4P_TASK_STACK_UPLOAD, NULL,
                            R4P_TASK_PRIO_BACKGROUND, NULL, R4P_TASK_CORE_IO);
    return ESP_OK;
}

int result_upload_pending(void) { return (int)(s_head - s_tail); }
int result_upload_sent(void) { return s_sent; }
