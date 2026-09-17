/**
 * dashboard.c — xem dashboard.h.
 */
#include "dashboard.h"
#include "rapid4p.h"
#include "core/task_profile.h"
#include "core/wifi_mgr.h"
#include "app/measure.h"
#include "app/calib_store.h"
#include "network/result_upload.h"
#include "ui/ui_reader.h"
#include "ui/ui_strings.h"

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG_DASH "r4p.dash"

extern const char dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const char dashboard_html_end[]   asm("_binary_dashboard_html_end");

static httpd_handle_t s_srv = NULL;
static volatile bool s_suspended = false;   /* portal đang cần cổng 80 */

/* ===== tiện ích ===== */
static const char *ui_state_name(ui_state_t st)
{
    static const char *const names[UI_COUNT] = {
        "start", "sample", "tube", "prepare", "measuring", "result", "calib",
        "settings", "language", "wifi", "update", "threshold", "thredit" };
    return st < UI_COUNT ? names[st] : "?";
}

/* Pha cho client (như `phase` của Rapid+): idle · prepare · measuring · result · calib · wifi */
static const char *phase_name(ui_state_t st)
{
    switch (st) {
        case UI_MEASURING: return "measuring";
        case UI_RESULT:    return "result";
        case UI_CALIB:     return "calib";
        case UI_PREPARE:   return "prepare";
        case UI_WIFI:      return "wifi";
        case UI_CHOOSE_SAMPLE: case UI_CHOOSE_TUBE: return "choose";
        default:           return "idle";
    }
}

static int sick_from_name(const char *n)
{
    for (int i = 0; i < R4P_SICK_COUNT; i++) if (strcmp(n, r4p_sick_name((r4p_sick_t)i)) == 0) return i;
    return -1;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
    free(s);
    return r;
}

static bool query_param(httpd_req_t *req, const char *key, char *out, size_t cap)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1 || qlen > 256) return false;
    char q[256];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, cap) == ESP_OK;
}

/* ===== handlers ===== */
static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, dashboard_html_start, dashboard_html_end - dashboard_html_start - 1);
}

static esp_err_t state_get(httpd_req_t *req)
{
    const measure_result_t *m = measure_last();
    const r4p_settings_t *c = calib_store_get();
    const ui_state_t st = ui_reader_state();
    const bool busy = measure_busy();

    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(dev, "id", g_r4p_cfg.device_id[0] ? g_r4p_cfg.device_id : "");
    cJSON_AddStringToObject(dev, "fw", R4P_FW_VERSION);
    cJSON_AddStringToObject(dev, "ip", wifi_mgr_ip_address() ? wifi_mgr_ip_address() : "");
    cJSON_AddNumberToObject(dev, "heap", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(dev, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(dev, "lang", r4p_lang_display_name(r4p_str_get_lang()));

    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "screen", st);
    cJSON_AddStringToObject(ui, "name", ui_state_name(st));
    cJSON_AddStringToObject(root, "phase", phase_name(st));

    cJSON *sen = cJSON_AddObjectToObject(root, "sensors");
    cJSON_AddNumberToObject(sen, "alive", measure_sensors_alive());
    cJSON_AddNumberToObject(sen, "total", R4P_SLOTS);

    cJSON *me = cJSON_AddObjectToObject(root, "measure");
    cJSON_AddBoolToObject(me, "busy", busy);
    cJSON_AddStringToObject(me, "sick", r4p_sick_name(m->sick));
    cJSON_AddStringToObject(me, "sick_label", r4p_sick_label(m->sick));
    cJSON_AddStringToObject(me, "sample", r4p_sample_name(m->sample));
    cJSON_AddStringToObject(me, "sample_label", r4p_sample_label(m->sample));
    cJSON_AddNumberToObject(me, "round", m->round);
    cJSON_AddNumberToObject(me, "rounds", R4P_ROUNDS);
    cJSON_AddNumberToObject(me, "slot", m->slot);
    int done = busy ? ((m->round > 0 ? m->round - 1 : 0) * R4P_SLOTS + m->slot) : (st == UI_RESULT ? R4P_ROUNDS * R4P_SLOTS : 0);
    cJSON_AddNumberToObject(me, "progress", done * 100 / (R4P_ROUNDS * R4P_SLOTS));
    cJSON_AddBoolToObject(me, "has_result", st == UI_RESULT || (!busy && m->finished_us > 0));
    cJSON_AddNumberToObject(me, "threshold", (double)c->threshold[m->sick]);
    cJSON *slots = cJSON_AddArrayToObject(me, "slots");
    for (int i = 0; i < R4P_SLOTS; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON *rr = cJSON_AddArrayToObject(s, "r");
        for (int k = 0; k < R4P_ROUNDS; k++) cJSON_AddItemToArray(rr, cJSON_CreateNumber((double)m->result[i][k]));
        cJSON_AddNumberToObject(s, "avg", (double)m->average[i]);
        cJSON_AddBoolToObject(s, "positive", m->positive[i]);
        cJSON_AddBoolToObject(s, "ok", m->sensor_ok[i]);
        cJSON_AddBoolToObject(s, "calibrated", calib_store_slot_calibrated(i));
        cJSON_AddItemToArray(slots, s);
    }

    cJSON *up = cJSON_AddObjectToObject(root, "upload");
    cJSON_AddNumberToObject(up, "pending", result_upload_pending());
    cJSON_AddNumberToObject(up, "sent", result_upload_sent());

    cJSON *thr = cJSON_AddObjectToObject(root, "thresholds");
    for (int i = 0; i < R4P_SICK_COUNT; i++) {
        cJSON *t = cJSON_AddObjectToObject(thr, r4p_sick_name((r4p_sick_t)i));
        cJSON_AddStringToObject(t, "label", r4p_sick_label((r4p_sick_t)i));
        cJSON_AddNumberToObject(t, "value", (double)c->threshold[i]);
    }
    return send_json(req, root);
}

static esp_err_t control_post(httpd_req_t *req)
{
    char btn[16] = {0};
    if (!query_param(req, "btn", btn, sizeof(btn))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"btn?\"}");
    }
    /* Nút web = nút vật lý; ui_reader tự bọc display_schedule (không lv_* ở đây). */
    if (strcmp(btn, "measure") == 0)   ui_reader_on_measure_button();
    else if (strcmp(btn, "back") == 0) ui_reader_on_boot_button();
    else {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"btn\"}");
    }
    ESP_LOGI(TAG_DASH, "control btn=%s (man %d)", btn, (int)ui_reader_state());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t threshold_post(httpd_req_t *req)
{
    char sick[8] = {0}, val[8] = {0};
    if (!query_param(req, "sick", sick, sizeof(sick)) || !query_param(req, "value", val, sizeof(val))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"sick/value?\"}");
    }
    int s = sick_from_name(sick);
    long v = strtol(val, NULL, 10);
    if (s < 0 || v < 0 || v > 9999) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"range\"}");
    }
    esp_err_t r = calib_store_set_threshold((r4p_sick_t)s, (uint32_t)v);
    ESP_LOGI(TAG_DASH, "threshold %s=%ld: %s", sick, v, esp_err_to_name(r));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, r == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"nvs\"}");
}

/* ===== vòng đời ===== */
static esp_err_t server_start(void)
{
    if (s_srv) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_open_sockets = 6;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    cfg.core_id = R4P_TASK_CORE_IO;
    cfg.task_priority = R4P_TASK_PRIO_BACKGROUND;
    esp_err_t r = httpd_start(&s_srv, &cfg);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_DASH, "httpd_start: %s", esp_err_to_name(r));
        s_srv = NULL;
        return r;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = root_get },
        { .uri = "/api/state",     .method = HTTP_GET,  .handler = state_get },
        { .uri = "/api/control",   .method = HTTP_POST, .handler = control_post },
        { .uri = "/api/threshold", .method = HTTP_POST, .handler = threshold_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(s_srv, &routes[i]);
    ESP_LOGI(TAG_DASH, "dashboard http://%s/", wifi_mgr_ip_address() ? wifi_mgr_ip_address() : "?");
    return ESP_OK;
}

static void server_stop(void)
{
    if (!s_srv) return;
    httpd_stop(s_srv);
    s_srv = NULL;
    ESP_LOGI(TAG_DASH, "dashboard dung");
}

/* Poll trạng thái mạng 1 s: lên khi STA có IP và portal không chạy, xuống khi ngược lại
 * (bit sự kiện WiFi chỉ MỘT người tiêu thụ — main task — nên đây poll, xem CLAUDE.md §5).
 * Gọi httpd_start/stop từ task này (không từ handler HTTP) nên không tự đóng socket đang dùng. */
static void dash_task(void *arg)
{
    (void)arg;
    for (;;) {
        const bool want = wifi_mgr_is_connected() && !s_suspended;
        if (want && !s_srv) server_start();
        else if (!want && s_srv) server_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t dashboard_init(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(dash_task, "r4p_dash", 3072, NULL, R4P_TASK_PRIO_DIAGNOSTICS,
                                            NULL, R4P_TASK_CORE_IO);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void dashboard_suspend(void)
{
    s_suspended = true;
    server_stop();
}

void dashboard_resume(void) { s_suspended = false; }

bool dashboard_running(void) { return s_srv != NULL; }
