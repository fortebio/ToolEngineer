/**
 * wifi_mgr.c — WiFi STA + AP provisioning fallback.
 *
 * Production hardened:
 *   - Exponential reconnect (5s → 60s cap) khi disconnect
 *   - Save creds vào NVS namespace "wifi_creds"
 *   - Signal global event group when connected/disconnected
 *
 * Provisioning portal: ESP32 mở AP "<brand>-Setup-XXXX" (XXXX = MAC suffix),
 * HTTP server `/wifi` form chọn SSID + nhập password. AP và BLE cùng dùng một
 * candidate state machine: chỉ commit NVS sau khi nhận IP, lỗi thì rollback.
 */
#include "wifi_mgr.h"
#include "rapid4p.h"
#include "nvs_store.h"
#include "system_info.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "captive_dns.h"
#include "core/task_profile.h"
#include "boards/board.h"
#include "lwip/inet.h"
#include "ui/ui_wifi_setup.h"
#include "cJSON.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "network/dashboard.h"   /* dashboard nhường cổng 80 cho portal */

static bool s_connected = false;
static bool s_wifi_initialized = false;
static bool s_wifi_started = false;
/* KHAC s_wifi_started: co nay do SU KIEN WIFI_EVENT_STA_START dat, con
 * s_wifi_started chi ghi lai rang esp_wifi_start() da tra ve. Tren P4 thi WiFi
 * nam ben co-processor C5, esp_wifi_start() tra ve TRUOC khi C5 kip bao STA da
 * len hang tram ms — va esp_wifi_scan_start() trong khoang do tra thang
 * ESP_ERR_WIFI_STATE. */
static volatile bool s_sta_start_evt = false;
static bool s_should_autoconnect = false;
static int  s_retry_ms = 5000;
static char s_ip_address[16] = {0};
static httpd_handle_t s_prov_httpd = NULL;
static TimerHandle_t s_reconnect_timer = NULL;
/* 15/09/2026 (P4, mang "FBT"): STA "Connected" nhung 53 s khong co GOT_IP -> app roi
 * vao provisioning ma AP lai khong bat duoc (xem wifi_mgr_start_provisioning). Canh
 * gac: sau CONNECTED ma qua WIFI_DHCP_WAIT_MS chua co IP thi disconnect de duong
 * retry binh thuong (associate lai -> DHCP lai) chay, thay vi dung im toi 60 s. */
#define WIFI_DHCP_WAIT_MS 20000
static esp_timer_handle_t s_dhcp_timer = NULL;
static void dhcp_wait_timer_cb(void *arg) {
    (void)arg;
    if (s_connected) return;
    ESP_LOGW(TAG_WIFI, "STA da associate nhung %d s khong nhan duoc IP (DHCP) -> "
             "disconnect de thu lai", WIFI_DHCP_WAIT_MS / 1000);
    esp_wifi_disconnect();
}
static void wifi_apply_band_lock(const char *why);
static void dhcp_wait_arm(bool on) {
    if (!s_dhcp_timer) return;
    esp_timer_stop(s_dhcp_timer);
    if (on) esp_timer_start_once(s_dhcp_timer, (uint64_t)WIFI_DHCP_WAIT_MS * 1000ULL);
}
static EventGroupHandle_t s_candidate_events = NULL;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_candidate_active = false;
static uint8_t s_candidate_disconnect_reason = 0;
static char s_candidate_ip[16] = {0};
static wifi_prov_status_t s_prov_status = {
    .state = WIFI_PROV_STATE_IDLE,
};
static wifi_prov_observer_t s_prov_observer = NULL;
static void *s_prov_observer_ctx = NULL;

#define CANDIDATE_GOT_IP_BIT       BIT0
#define CANDIDATE_DISCONNECTED_BIT BIT1
#define CANDIDATE_ATTEMPTS         3
#define CANDIDATE_ATTEMPT_MS       12000
#define CANDIDATE_REBOOT_DELAY_MS  4000

typedef struct {
    char ssid[33];
    char password[65];
} candidate_request_t;

static bool candidate_is_active(void) {
    bool active;
    portENTER_CRITICAL(&s_state_lock);
    active = s_candidate_active;
    portEXIT_CRITICAL(&s_state_lock);
    return active;
}

const char *wifi_mgr_provision_state_name(wifi_prov_state_t state) {
    switch (state) {
        case WIFI_PROV_STATE_VALIDATING: return "validating";
        case WIFI_PROV_STATE_CONNECTED: return "connected";
        case WIFI_PROV_STATE_ERROR: return "error";
        case WIFI_PROV_STATE_IDLE:
        default: return "idle";
    }
}

/* Day trang thai len MAN HINH THIET BI.
 *
 * Trang web KHONG bao dam bao duoc ket qua: thiet bi chi co mot radio, luc STA
 * thu router o kenh khac thi AP buoc phai nhay kenh theo va MOI dien thoai dang
 * bam portal deu rot ngay giua luc dang poll /status. Man hinh thiet bi la kenh
 * phan hoi duy nhat con chac chan, nen moi chuyen trang thai deu duoc guong len
 * do. Khong dung wifi_mgr_set_provision_observer() vi ble_wifi_prov.c da chiem
 * cho quan sat duy nhat do (tren ban S3). */
static void mirror_status_to_screen(const wifi_prov_status_t *st) {
    if (!ui_wifi_setup_is_active()) return;   /* doc bool, khong khoa LVGL */
    switch (st->state) {
        case WIFI_PROV_STATE_VALIDATING:
            ui_wifi_setup_set_state(UI_WIFI_SETUP_VALIDATING, NULL);
            break;
        case WIFI_PROV_STATE_CONNECTED:
            ui_wifi_setup_set_state(UI_WIFI_SETUP_SUCCESS, st->ip_address);
            break;
        case WIFI_PROV_STATE_ERROR:
            ui_wifi_setup_set_state(UI_WIFI_SETUP_ERROR, st->detail);
            break;
        case WIFI_PROV_STATE_IDLE:
        default:
            break;
    }
}

static void set_provision_status(wifi_prov_state_t state, const char *detail,
                                 const char *ip_address) {
    wifi_prov_status_t snapshot = {0};
    wifi_prov_observer_t observer;
    void *observer_ctx;

    portENTER_CRITICAL(&s_state_lock);
    s_prov_status.state = state;
    strlcpy(s_prov_status.detail, detail ? detail : "", sizeof(s_prov_status.detail));
    strlcpy(s_prov_status.ip_address, ip_address ? ip_address : "",
            sizeof(s_prov_status.ip_address));
    snapshot = s_prov_status;
    observer = s_prov_observer;
    observer_ctx = s_prov_observer_ctx;
    portEXIT_CRITICAL(&s_state_lock);

    if (observer) {
        observer(&snapshot, observer_ctx);
    }
    mirror_status_to_screen(&snapshot);
}

void wifi_mgr_get_provision_status(wifi_prov_status_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_state_lock);
    *out = s_prov_status;
    portEXIT_CRITICAL(&s_state_lock);
}

void wifi_mgr_set_provision_observer(wifi_prov_observer_t observer, void *ctx) {
    portENTER_CRITICAL(&s_state_lock);
    s_prov_observer = observer;
    s_prov_observer_ctx = observer ? ctx : NULL;
    portEXIT_CRITICAL(&s_state_lock);
}

static void reconnect_timer_cb(TimerHandle_t timer) {
    (void)timer;
    if (candidate_is_active() || !s_should_autoconnect) return;
    wifi_apply_band_lock("truoc reconnect");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "STA reconnect start failed: %s", esp_err_to_name(err));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                s_wifi_started = true;
                s_sta_start_evt = true;
                if (s_should_autoconnect && !candidate_is_active()) {
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_CONNECTED: {
                const wifi_event_sta_connected_t *e = data;
                ESP_LOGI(TAG_WIFI, "STA associated ssid=%.*s ch=%d — doi DHCP (toi da %d s)",
                         e ? (int)e->ssid_len : 0, e ? (const char *)e->ssid : "",
                         e ? (int)e->channel : 0, WIFI_DHCP_WAIT_MS / 1000);
#if BOARD_WIFI_BAND_2G_ONLY
                if (e && e->channel > 14 && !candidate_is_active()) {
                    ESP_LOGW(TAG_WIFI, "associate o 5 GHz (ch=%d) du da khoa 2,4 GHz -> ngat, "
                             "thu lai", (int)e->channel);
                    esp_wifi_disconnect();
                    break;
                }
#endif
                if (!candidate_is_active()) dhcp_wait_arm(true);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED:
                s_connected = false;
                s_ip_address[0] = '\0';
                dhcp_wait_arm(false);
                if (candidate_is_active()) {
                    const wifi_event_sta_disconnected_t *e = data;
                    portENTER_CRITICAL(&s_state_lock);
                    s_candidate_disconnect_reason = e ? e->reason : 0;
                    portEXIT_CRITICAL(&s_state_lock);
                    if (s_candidate_events) {
                        xEventGroupSetBits(s_candidate_events,
                                           CANDIDATE_DISCONNECTED_BIT);
                    }
                    break;
                }
                xEventGroupClearBits(g_r4p_events, R4P_EVT_WIFI_UP);
                xEventGroupSetBits(g_r4p_events, R4P_EVT_WIFI_DOWN);
                ESP_LOGW(TAG_WIFI, "STA disconnected — retry in %d ms", s_retry_ms);
                /* Event callback chạy trên default event loop. Không sleep ở
                 * đây vì sẽ chặn cả WiFi/IP events; one-shot timer thực hiện
                 * reconnect ngoài event loop. */
                if (s_reconnect_timer) {
                    if (xTimerChangePeriod(s_reconnect_timer,
                                           pdMS_TO_TICKS(s_retry_ms), 0) != pdPASS) {
                        ESP_LOGW(TAG_WIFI, "schedule reconnect timer failed");
                    }
                }
                s_retry_ms = (s_retry_ms < 60000) ? (s_retry_ms * 2) : 60000;
                break;
            case WIFI_EVENT_AP_STACONNECTED:
            case WIFI_EVENT_AP_STADISCONNECTED: {
                /* Chi lam viec nay khi portal dang chay: ham duoi khoa mutex
                 * LVGL, ma day la task cua default event loop — khong nen giu
                 * no ngoai luc provisioning. */
                if (!s_prov_httpd) break;
                wifi_sta_list_t list = {0};
                int n = (esp_wifi_ap_get_sta_list(&list) == ESP_OK) ? list.num : 0;
                ESP_LOGI(TAG_WIFI, "AP co %d thiet bi dang ket noi", n);
                /* Doi man hinh sang "da vao WiFi thiet bi - quet ma 2". Khong co
                 * moc nay thi man hinh dung yen suot, nguoi dung khong biet buoc
                 * 1 da xong hay chua va thuong quet lai ma 1 lien tuc. */
                ui_wifi_setup_set_client_count(n);
                break;
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG_WIFI, "Got IP: %s%s", ip,
                 candidate_is_active() ? " (candidate)" : "");
        if (candidate_is_active()) {
            portENTER_CRITICAL(&s_state_lock);
            strlcpy(s_candidate_ip, ip, sizeof(s_candidate_ip));
            portEXIT_CRITICAL(&s_state_lock);
            if (s_candidate_events) {
                xEventGroupSetBits(s_candidate_events, CANDIDATE_GOT_IP_BIT);
            }
            return;
        }
        strlcpy(s_ip_address, ip, sizeof(s_ip_address));
        s_connected = true;
        dhcp_wait_arm(false);
        if (s_reconnect_timer) xTimerStop(s_reconnect_timer, 0);
        s_retry_ms = 5000;   /* reset backoff */
        xEventGroupSetBits(g_r4p_events, R4P_EVT_WIFI_UP);
        xEventGroupClearBits(g_r4p_events, R4P_EVT_WIFI_DOWN);
    }
}

static esp_err_t ensure_wifi_initialized(bool need_ap) {
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") &&
        !esp_netif_create_default_wifi_sta()) {
        return ESP_ERR_NO_MEM;
    }
    if (need_ap && !esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") &&
        !esp_netif_create_default_wifi_ap()) {
        return ESP_ERR_NO_MEM;
    }

    if (!s_reconnect_timer) {
        s_reconnect_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(5000),
                                         pdFALSE, NULL, reconnect_timer_cb);
        if (!s_reconnect_timer) return ESP_ERR_NO_MEM;
    }
    if (!s_dhcp_timer) {
        const esp_timer_create_args_t dt = {
            .callback = dhcp_wait_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_dhcp_wait",
        };
        if (esp_timer_create(&dt, &s_dhcp_timer) != ESP_OK) s_dhcp_timer = NULL;
    }
    if (!s_candidate_events) {
        s_candidate_events = xEventGroupCreate();
        if (!s_candidate_events) return ESP_ERR_NO_MEM;
    }
    if (s_wifi_initialized) return ESP_OK;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t er = esp_wifi_init(&cfg);
    if (er != ESP_OK) return er;
    er = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                    on_wifi_event, NULL);
    if (er != ESP_OK) return er;
    er = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                    on_wifi_event, NULL);
    if (er != ESP_OK) return er;

    /* Firmware nay TU quan ly credential (nvs_store_get/set_wifi + configure_sta
     * moi lan khoi dong), nen ban sao ma driver WiFi tu giu trong flash chi la
     * du thua — va tren P4 no con doc hai.
     *
     * Da quan sat duoc tren board that: WiFi nam o co-processor C5, nen
     * esp_wifi_set_config() ghi thang vao FLASH CUA C5. Ban ghi do song sot qua
     * ca viec xoa sach NVS cua host. Ket qua: xoa het creds phia host roi vao
     * provisioning, C5 VAN tu dong noi lai mang cu (log: "Station mode:
     * Connected" + "Got IP" giua man hinh cai dat). Hau qua:
     *   - esp_wifi_scan_start() bi tu choi ESP_ERR_WIFI_STATE vi STA dang ban
     *     ket noi -> danh sach WiFi trong trang cau hinh RONG,
     *   - AP phai nhay sang kenh cua router cu -> da van moi dien thoai dang
     *     bam vao AP cai dat,
     *   - "quen WiFi" khong thuc su quen.
     * WIFI_STORAGE_RAM bo han ban sao do: driver chi giu cau hinh trong RAM,
     * nguon su that duy nhat la NVS cua host. */
    er = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "set_storage(RAM): %s", esp_err_to_name(er));
    }

    s_wifi_initialized = true;
    return ESP_OK;
}

/* P4 + C5 (2 bang): 15/09/2026 STA vao "FBT" o ch=36 (5 GHz), associate OK nhung DHCP
 * khong bao gio xong (3 lan lien tiep, 20 s moi lan); cac mang 2,4 GHz truoc do deu
 * Got IP. San pham thiet ke cho 2,4 GHz (S3 mot bang) -> khoa 2,4 GHz de hanh vi giong
 * S3. esp_wifi_set_band_mode chi nhan SAU esp_wifi_start (goi som: WIFI_NOT_STARTED);
 * goi lai truoc moi lan connect vi re, va RPC toi slave 2.7.0 co the tra
 * NOT_SUPPORTED -> chi log. */
static void wifi_apply_band_lock(const char *why) {
#if BOARD_WIFI_BAND_2G_ONLY
    static esp_err_t s_last = ESP_FAIL;
    esp_err_t ber = esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    if (ber != s_last) {
        ESP_LOGI(TAG_WIFI, "WiFi band 2.4 GHz only (%s): %s", why, esp_err_to_name(ber));
        s_last = ber;
    }
#else
    (void)why;
#endif
}

static esp_err_t start_wifi_if_needed(void) {
    if (s_wifi_started) return ESP_OK;
    esp_err_t er = esp_wifi_start();
    if (er == ESP_OK || er == ESP_ERR_WIFI_STATE) {
        s_wifi_started = true;
        wifi_apply_band_lock("sau start");
        return ESP_OK;
    }
    return er;
}

static esp_err_t configure_sta(const char *ssid, const char *password) {
    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    wifi_config_t wcfg = {0};
    memcpy(wcfg.sta.ssid, ssid, ssid_len);
    memcpy(wcfg.sta.password, password, password_len);
    wcfg.sta.threshold.authmode = password_len == 0
        ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wcfg.sta.pmf_cfg.capable = true;
    wcfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &wcfg);
}

esp_err_t wifi_mgr_start(void) {
    char ssid[33] = {0};
    char pass[65] = {0};
    nvs_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG_WIFI, "No SSID stored — caller fallback provisioning");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t er = ensure_wifi_initialized(false);
    if (er != ESP_OK) return er;
    s_should_autoconnect = true;
    er = esp_wifi_set_mode(WIFI_MODE_STA);
    if (er != ESP_OK) return er;
    er = configure_sta(ssid, pass);
    if (er != ESP_OK) return er;
    er = start_wifi_if_needed();
    if (er != ESP_OK) return er;
    er = esp_wifi_set_ps(WIFI_PS_NONE);
    if (er != ESP_OK) return er;
    ESP_LOGI(TAG_WIFI, "STA started, connecting to %s", ssid);
    return ESP_OK;
}

bool wifi_mgr_is_connected(void) { return s_connected; }

const char *wifi_mgr_ip_address(void) { return s_ip_address; }

static const char *candidate_error_from_reason(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "network_not_found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "auth_failed";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL:
            return "connection_failed";
        default:
            return "timeout";
    }
}

static const char *validate_credentials(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0') return "ssid_empty";
    size_t ssid_len = strlen(ssid);
    if (ssid_len > 32) return "ssid_too_long";
    if (!password) password = "";
    size_t password_len = strlen(password);
    if (password_len > 64) return "password_too_long";
    if (password_len > 0 && password_len < 8) return "password_too_short";
    if (password_len == 64) {
        for (size_t i = 0; i < password_len; ++i) {
            if (!isxdigit((unsigned char)password[i])) return "password_invalid";
        }
    }
    return NULL;
}

static void restore_previous_credentials(const char *ssid, const char *password) {
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    if (ssid && ssid[0]) {
        esp_err_t er = configure_sta(ssid, password ? password : "");
        s_should_autoconnect = (er == ESP_OK);
        portENTER_CRITICAL(&s_state_lock);
        s_candidate_active = false;
        portEXIT_CRITICAL(&s_state_lock);
        if (er == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "Candidate failed; reconnect previous SSID=%s", ssid);
            er = esp_wifi_connect();
            if (er != ESP_OK) {
                ESP_LOGW(TAG_WIFI, "Reconnect previous WiFi failed: %s",
                         esp_err_to_name(er));
            }
        }
        return;
    }

    s_should_autoconnect = false;
    portENTER_CRITICAL(&s_state_lock);
    s_candidate_active = false;
    portEXIT_CRITICAL(&s_state_lock);
}

static void candidate_task(void *arg) {
    candidate_request_t *request = arg;
    char previous_ssid[33] = {0};
    char previous_password[65] = {0};
    nvs_store_get_wifi(previous_ssid, sizeof(previous_ssid),
                       previous_password, sizeof(previous_password));

    s_should_autoconnect = false;
    if (s_reconnect_timer) xTimerStop(s_reconnect_timer, 0);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t er = configure_sta(request->ssid, request->password);
    bool got_ip = false;
    const char *failure = "connection_failed";
    if (er == ESP_OK) {
        for (int attempt = 1; attempt <= CANDIDATE_ATTEMPTS; ++attempt) {
            portENTER_CRITICAL(&s_state_lock);
            s_candidate_disconnect_reason = 0;
            s_candidate_ip[0] = '\0';
            portEXIT_CRITICAL(&s_state_lock);
            xEventGroupClearBits(s_candidate_events,
                                 CANDIDATE_GOT_IP_BIT | CANDIDATE_DISCONNECTED_BIT);

            er = esp_wifi_connect();
            if (er != ESP_OK) {
                ESP_LOGW(TAG_WIFI, "Candidate connect attempt %d start: %s",
                         attempt, esp_err_to_name(er));
                failure = "connection_failed";
                vTaskDelay(pdMS_TO_TICKS(800));
                continue;
            }

            EventBits_t bits = xEventGroupWaitBits(
                s_candidate_events,
                CANDIDATE_GOT_IP_BIT | CANDIDATE_DISCONNECTED_BIT,
                pdTRUE, pdFALSE, pdMS_TO_TICKS(CANDIDATE_ATTEMPT_MS));
            if (bits & CANDIDATE_GOT_IP_BIT) {
                got_ip = true;
                break;
            }

            uint8_t reason;
            portENTER_CRITICAL(&s_state_lock);
            reason = s_candidate_disconnect_reason;
            portEXIT_CRITICAL(&s_state_lock);
            failure = candidate_error_from_reason(reason);
            ESP_LOGW(TAG_WIFI,
                     "Candidate SSID=%s attempt %d/%d failed reason=%u (%s)",
                     request->ssid, attempt, CANDIDATE_ATTEMPTS,
                     (unsigned)reason, failure);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

    if (got_ip) {
        er = nvs_store_set_wifi(request->ssid, request->password);
        if (er == ESP_OK) {
            char candidate_ip[16];
            portENTER_CRITICAL(&s_state_lock);
            strlcpy(candidate_ip, s_candidate_ip, sizeof(candidate_ip));
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(TAG_WIFI, "Candidate WiFi verified and committed: SSID=%s IP=%s",
                     request->ssid, candidate_ip);
            set_provision_status(WIFI_PROV_STATE_CONNECTED, "restarting",
                                 candidate_ip);
            free(request);
            vTaskDelay(pdMS_TO_TICKS(CANDIDATE_REBOOT_DELAY_MS));
            esp_restart();
        }
        ESP_LOGE(TAG_WIFI, "Candidate connected but NVS commit failed: %s",
                 esp_err_to_name(er));
        failure = "nvs";
    } else if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "Candidate config failed: %s", esp_err_to_name(er));
        failure = "connection_failed";
    }

    set_provision_status(WIFI_PROV_STATE_ERROR, failure, NULL);
    restore_previous_credentials(previous_ssid, previous_password);
    free(request);
    vTaskDelete(NULL);
}

esp_err_t wifi_mgr_submit_credentials(const char *ssid, const char *password) {
    if (!password) password = "";
    const char *validation_error = validate_credentials(ssid, password);
    if (validation_error) {
        set_provision_status(WIFI_PROV_STATE_ERROR, validation_error, NULL);
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_state_lock);
    if (s_candidate_active) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_candidate_active = true;
    portEXIT_CRITICAL(&s_state_lock);

    esp_err_t er = ensure_wifi_initialized(false);
    if (er != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_candidate_active = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_provision_status(WIFI_PROV_STATE_ERROR, "wifi_init", NULL);
        return er;
    }
    er = start_wifi_if_needed();
    if (er != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_candidate_active = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_provision_status(WIFI_PROV_STATE_ERROR, "wifi_start", NULL);
        return er;
    }

    candidate_request_t *request = calloc(1, sizeof(*request));
    if (!request) {
        portENTER_CRITICAL(&s_state_lock);
        s_candidate_active = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_provision_status(WIFI_PROV_STATE_ERROR, "no_memory", NULL);
        return ESP_ERR_NO_MEM;
    }
    strlcpy(request->ssid, ssid, sizeof(request->ssid));
    strlcpy(request->password, password, sizeof(request->password));

    /* Publish validating before the worker can report connected/error. If the
     * task runs immediately after xTaskCreate, publishing afterwards can move
     * the externally visible state backwards from connected to validating. */
    set_provision_status(WIFI_PROV_STATE_VALIDATING, NULL, NULL);
    BaseType_t created = xTaskCreate(candidate_task, "wifi_candidate", 5120,
                                     request, 6, NULL);
    if (created != pdPASS) {
        free(request);
        portENTER_CRITICAL(&s_state_lock);
        s_candidate_active = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_provision_status(WIFI_PROV_STATE_ERROR, "no_memory", NULL);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG_WIFI, "Validating candidate WiFi SSID=%s", ssid);
    return ESP_OK;
}

/* ====================== Provisioning (APSTA + Scan + JSON API) ======================
 *
 * UX (theo xiaozhi):
 *   1. Phone join open network "<R4P_SETUP_PREFIX>-XXXX" (brand hiện tại: GENU-Setup)
 *   2. Mở 192.168.4.1 → trang HTML JS fetch /scan → render list SSID (sort RSSI desc)
 *   3. Tap 1 SSID → fill input → nhập pass → POST /save (JSON)
 *   4. Server thử connect; thành công → save NVS → reboot. Thất bại → rollback.
 */

#define SCAN_MAX_APS       24
#define SCAN_CACHE_TTL_MS  30000

/* Chi giu 3 truong trang web can, thay vi ca wifi_ap_record_t (~80 byte/AP).
 * 24 AP => ~900 byte thay vi ~2KB, va khong phai giu buffer scan song lau. */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t authmode;
} prov_ap_t;

static prov_ap_t s_aps[SCAN_MAX_APS];
static uint8_t   s_aps_count   = 0;
static int64_t   s_aps_time_us = 0;   /* 0 = chua quet duoc lan nao */

static bool wifi_auth_supported(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return true;
        default:
            return false;
    }
}

/* Loc + gop ket qua scan vao cache. */
static void prov_scan_store(const wifi_ap_record_t *recs, uint16_t n) {
    uint8_t out = 0;
    for (uint16_t i = 0; i < n && out < SCAN_MAX_APS; i++) {
        const wifi_ap_record_t *r = &recs[i];
        if (r->ssid[0] == 0) continue;                    /* SSID an */
        if (!wifi_auth_supported(r->authmode)) continue;

        /* Router mesh / repeater / dual-band phat CUNG mot ten tren nhieu kenh,
         * moi cai la mot ban ghi rieng. Khong gop thi danh sach hien ra 3-4
         * dong trung ten, nguoi dung khong biet chon cai nao. Giu ban manh
         * nhat. */
        bool dup = false;
        for (uint8_t j = 0; j < out; j++) {
            if (strcmp(s_aps[j].ssid, (const char *)r->ssid) == 0) {
                if (r->rssi > s_aps[j].rssi) s_aps[j].rssi = r->rssi;
                dup = true;
                break;
            }
        }
        if (dup) continue;

        strlcpy(s_aps[out].ssid, (const char *)r->ssid, sizeof(s_aps[out].ssid));
        s_aps[out].rssi     = r->rssi;
        s_aps[out].authmode = (uint8_t)r->authmode;
        out++;
    }
    s_aps_count   = out;
    s_aps_time_us = esp_timer_get_time();
}

/* Mot lan scan blocking. CHi goi khi that su can — xem prov_scan_handler.
 *
 * Trong che do APSTA chi co MOT radio: luc scan, radio bo kenh cua AP di quet
 * vong 13 kenh. Dien thoai dang bam AP thay thiet bi "chet" suot khoang do —
 * DHCP renew truot, ket noi HTTP treo. Dwell rut xuong 40-80ms/kenh (mac dinh
 * 120ms) de bot di, nhung DO TREN BOARD THAT van mat ~6 giay tron mot vong
 * (24 ban ghi): RPC qua SDIO sang C5 dat hon nhieu so voi WiFi chay tai cho.
 * Vi the phai ham nong san (prov_prewarm_task) va cache 30s, thay vi quet moi
 * lan trang goi /scan. */
static esp_err_t prov_scan_now(void) {
    /* Dang thu credential cua nguoi dung -> STA can toan quyen radio. Scan luc
     * nay lam hong ca hai viec. */
    if (candidate_is_active()) return ESP_ERR_INVALID_STATE;
    /* Chua co STA_START thi scan chac chan tra ESP_ERR_WIFI_STATE. Ve som de
     * khong tra gia mot vong goi RPC sang co-processor cho mot cai chac hong;
     * ben goi se dung cache. */
    if (!s_sta_start_evt) return ESP_ERR_WIFI_NOT_STARTED;

    wifi_scan_config_t sc = {0};
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    sc.scan_time.active.min = 40;
    sc.scan_time.active.max = 80;

    esp_err_t r = esp_wifi_scan_start(&sc, true);   /* blocking */
    if (r != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "scan start fail: %s", esp_err_to_name(r));
        return r;
    }

    /* wifi_ap_record_t la struct lon; cap phat tam thay vi de tren stack cua
     * task httpd (6KB). */
    wifi_ap_record_t *recs = calloc(SCAN_MAX_APS, sizeof(*recs));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    uint16_t n = SCAN_MAX_APS;
    r = esp_wifi_scan_get_ap_records(&n, recs);
    if (r == ESP_OK) {
        prov_scan_store(recs, n);
        ESP_LOGI(TAG_WIFI, "scan: %u ban ghi -> %u mang", n, s_aps_count);
    } else {
        ESP_LOGW(TAG_WIFI, "scan get records: %s", esp_err_to_name(r));
    }
    free(recs);
    return r;
}

/* GET /scan[?force=1] → {"aps":[{ssid,rssi,auth}],"age":<giay>,"busy":bool}
 *
 * Mac dinh tra CACHE. Trang chi can danh sach de chon, khong can moi lan tai
 * lai la mot lan quet — va moi lan quet la mot lan AP tam ngat. Cache duoc ham
 * nong ngay luc bat provisioning (truoc khi co ai ket noi) nen lan mo trang dau
 * tien da co du lieu san. Nut "Quet lai" moi dung force=1. */
static esp_err_t prov_scan_handler(httpd_req_t *req) {
    bool force = false;
    char query[48];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "force", val, sizeof(val)) == ESP_OK) {
            force = (val[0] == '1');
        }
    }

    const bool busy  = candidate_is_active();
    const bool empty = (s_aps_time_us == 0);
    const bool stale = !empty &&
        (esp_timer_get_time() - s_aps_time_us) > (int64_t)SCAN_CACHE_TTL_MS * 1000;

    if (!busy && (force || empty || stale)) {
        prov_scan_now();
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "aps");
    for (uint8_t i = 0; i < s_aps_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", s_aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", s_aps[i].rssi);
        cJSON_AddNumberToObject(o, "auth", s_aps[i].authmode);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddNumberToObject(root, "age",
        s_aps_time_us ? (double)((esp_timer_get_time() - s_aps_time_us) / 1000000) : -1);
    cJSON_AddBoolToObject(root, "busy", busy);

    char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
    cJSON_free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static int receive_request_body(httpd_req_t *req, char *buf, size_t len) {
    size_t received = 0;
    int timeouts = 0;
    while (received < len) {
        int n = httpd_req_recv(req, buf + received, len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            /* `continue` tran nhu truoc la vong lap VO HAN: esp_http_server chi
             * co MOT task xu ly, nen mot client gui do dang roi im lang se khoa
             * ca portal — moi may khac mat trang cai dat. Chan lai o 2 lan cho
             * (recv_wait_timeout giay moi lan). */
            if (++timeouts > 2) {
                ESP_LOGW(TAG_WIFI, "request body timeout (%u/%u byte)",
                         (unsigned)received, (unsigned)len);
                return HTTPD_SOCK_ERR_TIMEOUT;
            }
            continue;
        }
        if (n <= 0) return n;
        received += (size_t)n;
        timeouts = 0;   /* co du lieu chay ve -> dong ho cho bat dau lai */
    }
    return (int)received;
}

/* POST /save with JSON {"ssid":"...","password":"..."}. Returns as soon
 * as candidate validation starts; clients poll /status for the real result. */
static esp_err_t prov_save_handler(httpd_req_t *req) {
    int len = req->content_len;
    if (len <= 0 || len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char *buf = malloc(len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int n = receive_request_body(req, buf, (size_t)len);
    if (n <= 0) { free(buf); return ESP_FAIL; }
    buf[n] = 0;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false,\"err\":\"json\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    const cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *jpass = cJSON_GetObjectItem(root, "password");
    const cJSON *jdev = cJSON_GetObjectItem(root, "device_id");
    if (!cJSON_IsString(jssid) || !jssid->valuestring[0]) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false,\"err\":\"ssid_empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    /* Rapid4P: mã máy (id_device của ReaderPlus, WiFiManager custom_id_device) nhập
     * cùng lúc với WiFi. Lưu NGAY vào NVS — không phụ thuộc kết quả nối WiFi, vì
     * người dùng có thể chỉ muốn đặt mã máy. Rỗng = giữ mã cũ. */
    if (cJSON_IsString(jdev) && jdev->valuestring[0]) {
        char id[sizeof(g_r4p_cfg.device_id)] = {0};
        strlcpy(id, jdev->valuestring, sizeof(id));
        for (char *c = id; *c; c++) {
            if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                  (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) { *c = '_'; }
        }
        if (nvs_store_set_str("device_id", id) == ESP_OK) {
            strlcpy(g_r4p_cfg.device_id, id, sizeof(g_r4p_cfg.device_id));
            ESP_LOGI(TAG_WIFI, "portal: device_id=%s", id);
        }
    }
    /* Token Engineer Server (RECEIVER_TOKEN) — KHÔNG nằm trong firmware (luật #1
     * CLAUDE.md gốc: không commit bí mật; token trong .bin `strings` là ra). Nhập một
     * lần qua portal, lưu NVS, không bao giờ in ra log. Rỗng = giữ token cũ. */
    const cJSON *jtok = cJSON_GetObjectItem(root, "api_token");
    if (cJSON_IsString(jtok) && jtok->valuestring[0]) {
        if (nvs_store_set_str("api_token", jtok->valuestring) == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "portal: api_token da luu (%d ky tu)", (int)strlen(jtok->valuestring));
        }
    }
    const char *password = cJSON_IsString(jpass) ? jpass->valuestring : "";
    esp_err_t se = wifi_mgr_submit_credentials(jssid->valuestring, password);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (se != ESP_OK) {
        wifi_prov_status_t status;
        wifi_mgr_get_provision_status(&status);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "ok", false);
        const char *detail = se == ESP_ERR_INVALID_STATE
            ? "busy" : (status.detail[0] ? status.detail : esp_err_to_name(se));
        cJSON_AddStringToObject(response, "err", detail);
        char *json = cJSON_PrintUnformatted(response);
        httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
        cJSON_free(json);
        cJSON_Delete(response);
        return ESP_OK;
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, "{\"ok\":true,\"state\":\"validating\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* GET /status -> current candidate validation state. */
static esp_err_t prov_status_handler(httpd_req_t *req) {
    wifi_prov_status_t status;
    wifi_mgr_get_provision_status(&status);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state",
                            wifi_mgr_provision_state_name(status.state));
    if (status.detail[0]) cJSON_AddStringToObject(root, "detail", status.detail);
    if (status.ip_address[0]) cJSON_AddStringToObject(root, "ip", status.ip_address);
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* GET / -> trang cau hinh (SPA mot file, khong tai gi tu ben ngoai).
 *
 * Rang buoc dat ra cach viet trang nay:
 *   - Khong Internet: moi CSS/JS/icon phai nam trong chinh chuoi nay. Khong
 *     duoc tham chieu CDN, font Google hay anh ngoai — chung se treo cho toi
 *     khi timeout roi hong.
 *   - Cua so captive portal cua iOS/Android la WebView RUT GON, khong co thanh
 *     dia chi, khong reload duoc. Trang phai tu xoay xo trong mot lan tai.
 *   - Ten SSID xung quanh la du lieu NGUOI LA dat: mot AP ten `<img onerror=..>`
 *     hoan toan hop le. Vi vay ten mang LUON duoc do bang textContent, khong bao
 *     gio noi vao chuoi HTML.
 *   - Icon ve bang SVG inline chu khong dung emoji: emoji phu thuoc font he
 *     dieu hanh, moi may hien mot kieu va khong to mau theo giao dien duoc.
 *   - Co ca giao dien sang va toi: dien thoai de che do toi ma trang chi co mau
 *     sang thi chu xam tren nen xam, doc khong ra.
 */
static esp_err_t prov_get_handler(httpd_req_t *req) {
    static const char *html =
"<!DOCTYPE html><html lang=vi><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>"
"<title>" R4P_BRAND_NAME " - Cai dat WiFi</title><style>"
/* ---- token mau: moi cap chu/nen deu >= 4.5:1 o CA HAI che do ---- */
":root{--bg:#f5f3ff;--card:#fff;--tx:#111827;--mu:#4b5563;--ac:#4f46e8;"
"--er:#b91c1c;--ok:#166534;--bd:#d8d3f0;--baroff:#cbd5e1;--skel:#e9e6f7}"
"@media(prefers-color-scheme:dark){:root{--bg:#0f172a;--card:#1e293b;--tx:#f1f5f9;"
"--mu:#cbd5e1;--ac:#a5b4fc;--er:#fca5a5;--ok:#86efac;--bd:#334155;"
"--baroff:#475569;--skel:#334155}}"
"*{box-sizing:border-box}"
"body{margin:0;padding:16px;background:var(--bg);color:var(--tx);"
"font:16px/1.5 system-ui,-apple-system,Roboto,Arial,sans-serif}"
"main{max-width:480px;margin:0 auto}"
"h1{font-size:22px;margin:0}h2{font-size:16px;margin:0}"
".sub{color:var(--mu);font-size:14px;margin:2px 0 0}"
".hd{display:flex;align-items:center;gap:10px;margin-bottom:14px}"
".hd svg{color:var(--ac);flex:none}"
/* ---- chi bao 3 buoc ---- */
".steps{display:flex;gap:6px;list-style:none;padding:0;margin:0 0 14px}"
".steps li{flex:1;font-size:12px;color:var(--mu);text-align:center;"
"padding-top:20px;position:relative}"
".steps li:before{content:attr(data-n);position:absolute;top:0;left:50%;"
"transform:translateX(-50%);width:18px;height:18px;border-radius:50%;"
"border:2px solid var(--baroff);font-size:11px;line-height:14px}"
".steps li.done{color:var(--ok)}.steps li.done:before{content:'\\2713';"
"border-color:var(--ok);color:var(--ok)}"
".steps li.cur{color:var(--ac);font-weight:600}"
".steps li.cur:before{border-color:var(--ac);color:var(--ac)}"
/* ---- the ---- */
".card{background:var(--card);border-radius:14px;padding:14px;margin:0 0 12px;"
"border:1px solid var(--bd)}"
".cardhd{display:flex;justify-content:space-between;align-items:center;gap:8px;"
"margin-bottom:6px}"
/* ---- dong mang wifi: la <button> that, cao 52px (>44px muc toi thieu) ---- */
".net{display:flex;align-items:center;gap:10px;width:100%;min-height:52px;"
"padding:8px 10px;margin:4px 0;border:1px solid transparent;border-radius:10px;"
"background:none;color:var(--tx);font:inherit;text-align:left;cursor:pointer}"
".net:hover{background:var(--bg)}"
".net[aria-pressed=true]{border-color:var(--ac);background:var(--bg)}"
".net .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;"
"white-space:nowrap;font-weight:500}"
".net .db{color:var(--mu);font-size:13px;flex:none}"
".net svg{flex:none;color:var(--ac)}"
/* ---- o nhap ---- */
"label{display:block;font-size:14px;font-weight:600;margin:12px 0 4px}"
"input{width:100%;min-height:48px;padding:12px;border:1px solid var(--bd);"
"border-radius:10px;background:var(--card);color:var(--tx);font-size:16px}"
"input[aria-invalid=true]{border-color:var(--er)}"
".pw{display:flex;gap:8px;align-items:center}"
".eye{flex:none;width:48px;height:48px;display:grid;place-items:center;"
"border:1px solid var(--bd);border-radius:10px;background:var(--card);"
"color:var(--mu);cursor:pointer}"
".help{font-size:13px;color:var(--mu);margin:6px 0 0}"
/* min-height: cho loi mot cho co san, KHONG bao gio an/hien lam xe dich bo cuc.
 * Truoc khi co dong nay: nguoi dung go mat khau ngan -> cham nut "Kiem tra va
 * ket noi" -> cu cham lam o nhap mat tieu diem -> loi hien ra -> nut TUT XUONG
 * mot dong -> ngon tay nha ra o cho khong con nut nua -> cu cham roi vao hu
 * khong, phai cham lan hai. Da tai hien duoc bang trinh duyet that. */
".fe{font-size:14px;color:var(--er);margin:6px 0 0;font-weight:500;min-height:21px}"
/* ---- nut ---- */
"button{font:inherit}"
".primary{width:100%;min-height:52px;margin-top:16px;border:0;border-radius:10px;"
"background:#4f46e8;color:#fff;font-size:17px;font-weight:600;cursor:pointer}"
".primary[disabled]{background:var(--baroff);color:var(--mu);cursor:default}"
".ghost{min-height:44px;padding:0 12px;display:inline-flex;align-items:center;"
"gap:6px;border:1px solid var(--ac);border-radius:9px;background:none;"
"color:var(--ac);font-size:14px;font-weight:600;cursor:pointer}"
".ghost[disabled]{opacity:.55;cursor:default}"
/* ---- vong lay net ban phim: khong bao gio xoa, chi ve dep hon ---- */
":focus-visible{outline:3px solid var(--ac);outline-offset:2px}"
/* ---- thong bao ---- */
".msg{margin-top:12px;padding:12px;border-radius:10px;border:1px solid var(--bd);"
"background:var(--card);font-size:15px}"
".msg h3{margin:0 0 4px;font-size:16px}"
".msg.e{border-color:var(--er)}.msg.e h3{color:var(--er)}"
".msg.o{border-color:var(--ok)}.msg.o h3{color:var(--ok)}"
".msg p{margin:0;color:var(--mu)}"
".warn{margin:12px 0 0;padding:10px;border-radius:8px;background:var(--bg);"
"font-size:13px;color:var(--mu)}"
".mu{color:var(--mu);font-size:13px;margin:8px 0 0}"
/* ---- xuong gia luc dang tai: giu nguyen chieu cao nen trang khong giat ---- */
".sk{height:52px;border-radius:10px;background:var(--skel);margin:4px 0;"
"animation:pl 1.2s ease-in-out infinite}"
"@keyframes pl{0%,100%{opacity:1}50%{opacity:.5}}"
"@media(prefers-reduced-motion:reduce){.sk{animation:none}}"
"[hidden]{display:none!important}"
"</style></head><body><main>"
"<div class=hd>"
"<svg width=32 height=32 viewBox='0 0 24 24' fill=none stroke=currentColor "
"stroke-width=2 stroke-linecap=round aria-hidden=true>"
"<path d='M5 12.55a11 11 0 0 1 14 0'/><path d='M8.5 16.4a6 6 0 0 1 7 0'/>"
"<path d='M2 8.82a15 15 0 0 1 20 0'/><path d='M12 20h.01'/></svg>"
"<div><h1>" R4P_BRAND_NAME "</h1>"
"<p class=sub>Ket noi thiet bi vao WiFi nha</p></div></div>"
"<ol class=steps>"
"<li class=done data-n=1>Vao WiFi thiet bi</li>"
"<li class=cur data-n=2 id=st2>Chon WiFi nha</li>"
"<li data-n=3 id=st3>Ket noi</li></ol>"
"<section class=card>"
"<div class=cardhd><h2 id=nl>Mang WiFi xung quanh</h2>"
"<button type=button id=rescan class=ghost>"
"<svg width=16 height=16 viewBox='0 0 24 24' fill=none stroke=currentColor "
"stroke-width=2 stroke-linecap=round aria-hidden=true>"
"<path d='M21 12a9 9 0 1 1-2.6-6.4'/><path d='M21 3v6h-6'/></svg>Quet lai</button>"
"</div><div id=list aria-labelledby=nl aria-busy=true></div>"
"<p id=age class=mu></p></section>"
"<form class=card id=frm novalidate>"
"<label for=ssid>Ten WiFi (SSID)</label>"
"<input id=ssid autocomplete=off autocapitalize=none autocorrect=off "
"spellcheck=false aria-describedby=ssiderr placeholder='Chon o tren hoac go tay'>"
"<p id=ssiderr class=fe></p>"
"<div id=pwbox><label for=pass>Mat khau WiFi</label><div class=pw>"
"<input id=pass type=password autocomplete=current-password "
"aria-describedby='passhelp passerr'>"
"<button type=button id=eye class=eye aria-label='Hien mat khau' aria-pressed=false>"
"<svg width=22 height=22 viewBox='0 0 24 24' fill=none stroke=currentColor "
"stroke-width=2 stroke-linecap=round aria-hidden=true id=eyeic>"
"<path d='M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7-10-7-10-7z'/>"
/* Khoang trang truoc "/>" la BAT BUOC khi thuoc tinh khong dat nhay: khong co
 * no thi bo phan tich HTML doc gia tri thanh "3/" va bo qua ca thuoc tinh —
 * con nguoi cua mat khong ve ra. */
"<circle cx=12 cy=12 r=3 /></svg></button></div>"
"<p id=passhelp class=help>It nhat 8 ky tu. De trong neu WiFi khong dat mat khau.</p>"
"<p id=passerr class=fe></p></div>"
"<label for=devid>Ma may (ID thiet bi)</label>"
"<input id=devid autocomplete=off autocapitalize=characters spellcheck=false maxlength=20 "
"placeholder='VD: R4P00001 - de trong neu giu ma cu'>"
"<label for=tok>Token may chu (Engineer Server)</label>"
"<input id=tok type=password autocomplete=off spellcheck=false maxlength=128 "
"placeholder='De trong neu giu token cu'>"
"<button type=submit id=save class=primary>Kiem tra va ket noi</button>"
/* Canh bao nay la BAT BUOC, khong phai cho dep: mot radio duy nhat nen luc thu
 * router o kenh khac, AP nhay kenh va dien thoai rot khoi trang ngay giua chung.
 * Khong noi truoc thi nguoi dung tuong thiet bi hong. */
"<p class=warn>Trong luc kiem tra, dien thoai co the tam rot khoi WiFi cua thiet"
" bi &mdash; day la binh thuong. Ket qua luon hien tren man hinh thiet bi.</p>"
"</form><div id=msg class=msg tabindex=-1 role=status aria-live=polite hidden></div>"
"</main><script>"
"var $=function(i){return document.getElementById(i)};"
"var aps=[],sel=null,tries=0,polling=false;"
/* Vach song: 4 cot, to dam theo do manh. Kem so dBm ben canh de khong phu thuoc
 * rieng vao hinh anh. */
"function bars(r){var n=r>-55?4:r>-67?3:r>-78?2:1,s='';"
"for(var i=0;i<4;i++){var h=4+i*4;"
/* Khoang trang truoc "/>" — thieu no thi fill thanh "currentColor/", trinh
 * duyet KHONG bao loi ma lang le to den het, moi vach song trong nhu nhau va
 * chi bao cuong do mat sach y nghia. */
"s+=`<rect x=${1+i*5} y=${18-h} width=3 height=${h} rx=1 fill=${i<n?'currentColor':'var(--baroff)'} />`}"
"return `<svg width=21 height=20 viewBox='0 0 21 20' aria-hidden=true>${s}</svg>`}"
"function lock(){return `<svg width=16 height=16 viewBox='0 0 24 24' fill=none "
"stroke=currentColor stroke-width=2 aria-hidden=true>"
"<rect x=3 y=11 width=18 height=11 rx=2 /><path d='M7 11V7a5 5 0 0 1 10 0v4'/></svg>`}"
"function secure(a){return a!=0}"
"function pwbox(on){$('pwbox').hidden=!on;if(!on)$('pass').value=''}"
"function render(){var l=$('list');"
"if(!aps.length){l.innerHTML='<p class=mu>Khong thay mang WiFi nao. Bam Quet lai.</p>';return}"
"l.innerHTML='';"
"aps.slice().sort(function(a,b){return b.rssi-a.rssi}).forEach(function(ap){"
"var b=document.createElement('button');b.type='button';b.className='net';"
"b.setAttribute('aria-pressed',String(ap.ssid===sel));"
"b.innerHTML=`${bars(ap.rssi)}<span class=nm></span>"
"<span class=db>${ap.rssi}dBm</span>${secure(ap.auth)?lock():''}`;"
/* textContent, KHONG noi chuoi: ten SSID do nguoi la dat ra. */
"b.querySelector('.nm').textContent=ap.ssid;"
"b.onclick=function(){sel=ap.ssid;$('ssid').value=ap.ssid;fe('ssid',null);"
"pwbox(secure(ap.auth));render();if(secure(ap.auth))$('pass').focus()};"
"l.appendChild(b)})}"
"function skel(){return '<div class=sk></div><div class=sk></div><div class=sk></div>'}"
"function loadScan(force){var l=$('list');l.setAttribute('aria-busy','true');"
"if(!aps.length)l.innerHTML=skel();$('rescan').disabled=true;"
/* Mot vong quet lam AP tam ngat ~6 giay. Noi truoc de nguoi dung khong tuong
 * thiet bi treo va rut dien giua chung. */
"if(force)$('age').textContent='Dang quet - thiet bi tam ngat vai giay, dung tat.';"
"fetch('/scan'+(force?'?force=1':''),{cache:'no-store'}).then(function(r){return r.json()})"
".then(function(d){aps=d.aps||[];render();"
"$('age').textContent=d.busy?'Dang kiem tra WiFi - tam dung quet.'"
":(d.age>=0?('Cap nhat '+(d.age<5?'vua xong':d.age+' giay truoc')):'')})"
".catch(function(){$('list').innerHTML="
"'<p class=fe>Khong lay duoc danh sach. Bam Quet lai.</p>'})"
".then(function(){l.setAttribute('aria-busy','false');$('rescan').disabled=false})}"
"$('rescan').onclick=function(){loadScan(true)};"
"$('eye').onclick=function(){var p=$('pass'),on=p.type==='password';"
"p.type=on?'text':'password';this.setAttribute('aria-pressed',String(on));"
"this.setAttribute('aria-label',on?'An mat khau':'Hien mat khau');p.focus()};"
/* Loi gan LIEN o nhap, khong don hoi len mot cho chung chung. */
"function fe(id,m){var e=$(id+'err'),f=$(id);if(!e)return;"
/* Chi doi CHU, khong an/hien the — the luon giu cho san (xem .fe min-height). */
"e.textContent=m||'';"
"if(m)f.setAttribute('aria-invalid','true');else f.removeAttribute('aria-invalid')}"
"function okPass(){var p=$('pass').value;"
"if($('pwbox').hidden)return true;"
"if(p.length>0&&p.length<8){fe('pass','Mat khau WiFi phai tu 8 ky tu tro len.');return false}"
"fe('pass',null);return true}"
"$('pass').addEventListener('blur',okPass);"
/* Kiem tra khi ROI o nhap, nhung XOA loi ngay khi go du dai: de loi cu nam do
 * trong lúc nguoi dung da sua xong la vo ly va lam ho tuong minh van sai. */
"$('pass').addEventListener('input',function(){var v=$('pass').value;"
"if(v.length===0||v.length>=8)fe('pass',null)});"
"function step(n){$('st2').className=n>2?'done':'cur';"
"$('st3').className=n>2?'cur':''}"
"function say(kind,title,body){var m=$('msg');m.hidden=false;"
"m.className='msg'+(kind?' '+kind:'');"
/* Loi phai la role=alert de trinh doc man hinh doc ngay, khong doi luot. */
"m.setAttribute('role',kind==='e'?'alert':'status');"
"m.innerHTML='<h3></h3><p></p>';"
"m.querySelector('h3').textContent=title;m.querySelector('p').textContent=body||''}"
"function errText(e){return({auth_failed:'Sai mat khau WiFi',"
"network_not_found:'Khong tim thay mang WiFi',"
"connection_failed:'Khong ket noi duoc WiFi',timeout:'WiFi khong phan hoi',"
"password_too_short:'Mat khau phai co it nhat 8 ky tu',"
"password_too_long:'Mat khau qua dai',password_invalid:'Mat khau khong hop le',"
"ssid_empty:'Chua chon mang WiFi',ssid_too_long:'Ten WiFi qua dai',"
"busy:'Thiet bi dang kiem tra mot mang khac',nvs:'Khong luu duoc cau hinh'})[e]"
"||e||'Loi khong xac dinh'}"
/* Moi loi deu kem BUOC TIEP THEO, dung bo nguoi dung o day. */
"function nextStep(e){if(e==='auth_failed'||e==='password_invalid')"
"return 'Nhap lai mat khau roi bam Thu lai.';"
"if(e==='network_not_found')return 'Dua thiet bi lai gan router roi bam Quet lai. "
"Thiet bi chi dung duoc WiFi 2.4GHz.';"
"if(e==='password_too_short')return 'Mat khau WiFi phai tu 8 ky tu tro len.';"
"if(e==='nvs')return 'Khoi dong lai thiet bi roi cai dat lai.';"
"return 'Kiem tra router con hoat dong roi bam Thu lai.'}"
"function fail(code){step(2);say('e',errText(code),nextStep(code));"
"var b=$('save');b.disabled=false;b.textContent='Thu lai';$('msg').focus()}"
"function done(ip){step(3);say('o','Da ket noi WiFi',"
"(ip?('Dia chi IP '+ip+'. '):'')+'Thiet bi dang khoi dong lai. Ban co the dong trang nay.');"
"var b=$('save');b.disabled=true;b.textContent='Da ket noi'}"
"function poll(){if(!polling)return;"
"fetch('/status',{cache:'no-store'}).then(function(r){return r.json()})"
".then(function(d){tries=0;"
"if(d.state==='connected'){polling=false;done(d.ip)}"
"else if(d.state==='error'){polling=false;fail(d.detail)}"
"else{say('','Dang kiem tra...','Dang thu mat khau va ket noi WiFi.');"
"setTimeout(poll,800)}})"
/* Mat ket noi o day la DU KIEN, khong phai loi: AP vua nhay kenh theo router. */
".catch(function(){tries++;"
"say('',tries>3?'Mat ket noi voi thiet bi':'Dang kiem tra...',"
"tries>3?'Day la binh thuong khi thiet bi chuyen sang WiFi nha. "
"Xem ket qua tren man hinh thiet bi.'"
":'Dang thu mat khau va ket noi WiFi.');"
"if(tries<40)setTimeout(poll,1200);else polling=false})}"
"$('frm').addEventListener('submit',function(e){e.preventDefault();"
"var s=$('ssid').value.trim(),p=$('pass').value;"
"if(!s){fe('ssid','Chon hoac nhap ten WiFi.');$('ssid').focus();return}"
"fe('ssid',null);if(!okPass()){$('pass').focus();return}"
"var b=$('save');b.disabled=true;b.textContent='Dang kiem tra...';"
"say('','Dang gui...','Dang gui thong tin WiFi toi thiet bi.');"
"fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:s,password:$('pwbox').hidden?'':p,device_id:$('devid').value.trim(),api_token:$('tok').value.trim()})})"
".then(function(r){return r.json()}).then(function(d){"
"if(d.ok){step(3);tries=0;polling=true;poll()}else{fail(d.err)}})"
".catch(function(){fail('connection_failed')})});"
"loadScan(false);"
"</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/* ---------------- Captive portal plumbing ----------------
 *
 * Truoc day chi co 2 handler /generate_204 va /hotspot-detect.html, KHONG co
 * DNS va KHONG co redirect. Hau qua tren may that:
 *   - Dien thoai join AP -> probe connectivitycheck.gstatic.com /
 *     captive.apple.com -> khong ai resolve -> he dieu hanh khong bao gio mo
 *     trang cau hinh; Android con bao "khong co Internet" roi tu nhay ve 4G.
 *   - Moi URL khac (Windows /connecttest.txt, iOS /library/test/success.html,
 *     Firefox /canonical.html, hoac go nham duong dan) tra 404 tran.
 * Gio: DHCP cap DNS = IP cua AP + option 114 (RFC 8910) tro thang toi portal,
 * DNS tra moi ten mien ve AP, va 404 -> 302 ve trang cau hinh.
 */

/* Chuoi nay duoc dhcpserver GIU CON TRO (khong copy) nen phai song lau dai. */
static char s_portal_uri[40] = "http://192.168.4.1/";
static char s_portal_origin[32] = "http://192.168.4.1";

static esp_netif_t *prov_ap_netif(void) {
    return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
}

/* Cau hinh DHCP server cua AP: cap DNS tro ve chinh minh + quang cao URL
 * portal. Phai stop dhcps truoc khi doi option roi start lai. */
static void prov_setup_dhcps_dns(esp_netif_t *ap) {
    if (!ap) return;

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(ap, &ip_info) == ESP_OK && ip_info.ip.addr) {
        char ipbuf[16];
        esp_ip4addr_ntoa(&ip_info.ip, ipbuf, sizeof(ipbuf));
        snprintf(s_portal_uri, sizeof(s_portal_uri), "http://%s/", ipbuf);
        snprintf(s_portal_origin, sizeof(s_portal_origin), "http://%s", ipbuf);
    }

    esp_netif_dhcps_stop(ap);   /* loi khi chua chay -> bo qua */

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip_info.ip.addr;
    esp_err_t er = esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
    if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "AP set_dns_info: %s", esp_err_to_name(er));
    }

    /* OFFER_DNS = 0x02 (dhcpserver.h). Khong bat co nay thi dhcps khong bo
     * option 6 vao DHCP OFFER va may khach khong biet hoi DNS o dau. */
    uint8_t offer_dns = 0x02;
    er = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
                                ESP_NETIF_DOMAIN_NAME_SERVER,
                                &offer_dns, sizeof(offer_dns));
    if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "AP dhcps OFFER_DNS: %s", esp_err_to_name(er));
    }

    /* RFC 8910 option 114: iOS 14+ / Android 11+ mo thang URL nay, khong can
     * doan qua probe. May cu bo qua option la khong sao. */
    er = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
                                ESP_NETIF_CAPTIVEPORTAL_URI,
                                s_portal_uri, strlen(s_portal_uri));
    if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "AP dhcps captive URI: %s", esp_err_to_name(er));
    }

    er = esp_netif_dhcps_start(ap);
    if (er != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "AP dhcps_start: %s", esp_err_to_name(er));
    }
}

/* Duong dan tra JSON cho trang cau hinh. Chung PHAI giu 404 that thay vi bi
 * 302 ve trang HTML — neu redirect, fetch() se nhan HTML roi r.json() nem loi
 * parse kho hieu thay vi bao dung "khong tim thay". (Quy tac lay tu CrossInk:
 * `handleNotFound()` bo qua redirect cho moi URI bat dau bang "/api/".) */
static bool prov_uri_is_api(const char *uri) {
    if (!uri) return false;
    if (strncmp(uri, "/api/", 5) == 0) return true;
    static const char *const json_paths[] = { "/scan", "/save", "/status" };
    for (size_t i = 0; i < sizeof(json_paths) / sizeof(json_paths[0]); i++) {
        size_t n = strlen(json_paths[i]);
        if (strncmp(uri, json_paths[i], n) == 0 &&
            (uri[n] == '\0' || uri[n] == '?')) {
            return true;
        }
    }
    return false;
}

/* Bat ky duong dan nao chua dang ky -> 302 ve trang cau hinh.
 * Tra ESP_OK de httpd GIU ket noi (ESP_FAIL se dong socket, browser bao loi
 * thay vi di theo redirect). */
static esp_err_t prov_redirect_handler(httpd_req_t *req, httpd_err_code_t err) {
    (void)err;
    if (prov_uri_is_api(req->uri)) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"err\":\"not_found\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", s_portal_uri);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    /* Body ngan cho trinh duyet khong tu di theo Location (hiem, nhung Windows
     * NCSI hien noi dung nay trong bong bong thong bao). */
    httpd_resp_send(req, "<html><body>Chuyen huong toi trang cau hinh WiFi..."
                         "</body></html>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Ham nong cache danh sach WiFi trong mot task rieng.
 *
 * Vi sao ham nong: mot vong quet lam AP roi kenh ~6 giay (do tren board). Neu
 * de lan /scan dau tien cua dien thoai kich hoat no, cu quet roi vao dung luc
 * dien thoai vua bam vao AP — luc mong manh nhat. Quet truoc thi trang mo ra da
 * co san danh sach.
 *
 * Vi sao phai thu lai: ngay sau khi AP len, radio chua roi. Da do tren board:
 * lan goi dau tien luon tra ESP_ERR_WIFI_STATE, va co-processor C5 con tu noi
 * lai mang cu (xem chu thich WIFI_STORAGE_RAM) khien radio ban them ~10s nua.
 *
 * Vi sao la task rieng chu khong goi thang: cho o day co the ton hang chuc
 * giay. Man hinh QR va vong lap chinh cua app KHONG duoc phep doi mot viec chi
 * de toi uu toc do mo trang. */
static void prov_prewarm_task(void *arg) {
    (void)arg;
    const int attempts = 8;
    for (int i = 1; i <= attempts; i++) {
        vTaskDelay(pdMS_TO_TICKS(i == 1 ? 500 : 3000));
        if (!s_prov_httpd) break;        /* provisioning da tat */
        esp_err_t er = prov_scan_now();
        if (er == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "da ham nong danh sach WiFi (%u mang, lan thu %d)",
                     s_aps_count, i);
            vTaskDelete(NULL);
        }
        ESP_LOGD(TAG_WIFI, "ham nong lan %d/%d: %s", i, attempts,
                 esp_err_to_name(er));
    }
    ESP_LOGW(TAG_WIFI, "khong ham nong duoc danh sach WiFi — "
             "trang se tu quet khi nguoi dung mo");
    vTaskDelete(NULL);
}

/* Trinh duyet nao cung xin /favicon.ico. De no roi vao 404->302 thi may tai ca
 * trang HTML ve lam icon roi vut di — ton bang thong tren mot ket noi von da
 * hep, va an mot socket trong so 4. 204 la cach noi "khong co, dung hoi nua". */
static esp_err_t prov_favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Xoa ban sao credential ma driver WiFi con giu ben co-processor.
 *
 * Do duoc tren board that (lap lai 3 lan, khong co dien thoai nao ket noi):
 * xoa sach NVS cua host roi khoi dong -> may vao provisioning dung nhu mong doi,
 * NHUNG ~8 giay sau van "Station mode: Connected" + "Got IP" vao mang cu. Host
 * khong he goi esp_wifi_connect(); trong log RPC cung khong co lenh nao nhu vay
 * gui sang. Nghia la C5 tu noi bang ban ghi trong flash cua CHINH NO — thu ma
 * esp_wifi_set_config() da ghi xuong tu lan cai dat truoc.
 * Hau qua: esp_wifi_scan_start() bi tu choi ESP_ERR_WIFI_STATE vi STA dang ban,
 * va AP phai nhay sang kenh cua router cu, da van moi dien thoai dang cai dat.
 *
 * WIFI_STORAGE_RAM o ensure_wifi_initialized() chi chan ghi MOI, khong xoa duoc
 * ban ghi da nam san. Nen o day tam bat lai FLASH de ghi de mot config rong,
 * roi tra ve RAM.
 *
 * CHi lam khi host khong con creds — do la luc nguoi dung that su muon doi mang.
 * Provisioning bat luc chay (mat WiFi tam thoi) thi PHAI giu config de STA tu
 * noi lai duoc; app_main dong portal ngay khi WiFi hoi phuc. */
static void prov_forget_stale_sta_config(void) {
    char ssid[33] = {0};
    char pass[65] = {0};
    nvs_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] != '\0') return;

    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    wifi_config_t empty = {0};
    esp_err_t er = esp_wifi_set_config(WIFI_IF_STA, &empty);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ESP_LOGI(TAG_WIFI, "Don credential cu trong co-processor: %s",
             esp_err_to_name(er));
}

esp_err_t wifi_mgr_start_provisioning(void) {
    dashboard_suspend();   /* portal và dashboard cùng cổng 80 — loại trừ nhau (như Rapid+ dashboard/TLS) */
    /* AP "<brand>-Setup-XXXX" với XXXX = MAC suffix */
    char ap_ssid[24];
    const char *mac = system_info_get_mac_str();
    snprintf(ap_ssid, sizeof(ap_ssid), R4P_SETUP_PREFIX "-%s", mac + 12);

    /* APSTA mode — AP cho phone connect + STA để scan/validate WiFi xung quanh. */
    esp_err_t er = ensure_wifi_initialized(true);
    if (er != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "WiFi provisioning init: %s", esp_err_to_name(er));
        return er;
    }

    wifi_config_t wcfg = {0};
    strlcpy((char *)wcfg.ap.ssid, ap_ssid, sizeof(wcfg.ap.ssid));
    wcfg.ap.ssid_len = strlen(ap_ssid);
    wcfg.ap.channel = 1;
    wcfg.ap.max_connection = 4;
    wcfg.ap.authmode = WIFI_AUTH_OPEN;
    wcfg.ap.password[0] = '\0';
    wcfg.ap.pmf_cfg.capable = false;
    wcfg.ap.pmf_cfg.required = false;

    /* 15/09/2026: mot radio -> AP phai cung kenh voi STA dang associate. Neu STA
     * dang noi (ke ca "associate roi ma khong co IP") thi set_config AP kenh 1 bi C5
     * tra ESP_ERR_INVALID_ARG (3/4 lan roi vao provisioning tren P4 dinh loi nay ->
     * "reboot sau 30s"). Lay kenh cua STA; van loi thi ngat STA roi thu lai kenh 1. */
    wifi_ap_record_t sta_ap = {0};
    if (esp_wifi_sta_get_ap_info(&sta_ap) == ESP_OK &&
        sta_ap.primary >= 1 && sta_ap.primary <= 13) {
        wcfg.ap.channel = sta_ap.primary;
        ESP_LOGI(TAG_WIFI, "STA dang associate %s ch=%d -> AP dung cung kenh",
                 (const char *)sta_ap.ssid, (int)sta_ap.primary);
    }
    er = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (er != ESP_OK) { ESP_LOGE(TAG_WIFI, "set_mode: %s", esp_err_to_name(er)); return er; }
    er = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (er == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG_WIFI, "set_config AP INVALID_ARG (kenh %d) -> ngat STA, thu lai kenh 1",
                 (int)wcfg.ap.channel);
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));
        wcfg.ap.channel = 1;
        er = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    }
    if (er != ESP_OK) { ESP_LOGE(TAG_WIFI, "set_config AP: %s", esp_err_to_name(er)); return er; }
    /* Phai lam TRUOC esp_wifi_start(): sau khi start thi C5 da kip bat dau tu
     * noi lai mang cu roi. */
    prov_forget_stale_sta_config();
    er = start_wifi_if_needed();
    if (er != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "wifi_start: %s", esp_err_to_name(er));
        return er;
    }
    ESP_LOGI(TAG_WIFI, "Open AP %s started (no password)", ap_ssid);

    /* Mac dinh la WIFI_PS_MIN_MODEM: radio ngu giua cac beacon. Tren AP dang
     * phuc vu portal, do la them do tre cho moi request va lam DHCP/DNS thi
     * thoang truot. Provisioning chi keo dai vai phut nen doi dien nang lay
     * do phan hoi. */
    esp_err_t pser = esp_wifi_set_ps(WIFI_PS_NONE);
    if (pser != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "set_ps(NONE): %s", esp_err_to_name(pser));
    }

    /* Chot chan phai dung TRUOC phan dung portal ben duoi.
     * wifi_mgr_start_provisioning() duoc goi lai luc runtime (app_main.c khi
     * mat WiFi), ma prov_setup_dhcps_dns() co stop/start DHCP server — chay lai
     * khi dien thoai DANG ket noi se cat lease cua no. */
    if (s_prov_httpd) {
        ESP_LOGI(TAG_WIFI, "Provisioning portal dang chay tai %s", s_portal_uri);
        ui_wifi_setup_show(ap_ssid, s_portal_uri);
        return ESP_OK;
    }

    esp_netif_t *ap_netif = prov_ap_netif();
    prov_setup_dhcps_dns(ap_netif);

    /* DNS hijack: moi ten mien -> IP cua AP. Day la thu lam captive portal
     * that su bat len thay vi bat nguoi dung tu go dia chi. */
    if (ap_netif) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            esp_err_t der = captive_dns_start(ip_info.ip);
            if (der != ESP_OK) {
                ESP_LOGW(TAG_WIFI, "captive DNS khong bat duoc: %s - "
                         "nguoi dung se phai tu go %s",
                         esp_err_to_name(der), s_portal_uri);
            }
        }
    }

    /* HTTP server portal */
    httpd_handle_t srv = NULL;
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.max_uri_handlers = 12;
    hcfg.stack_size = 6144;
    /* 2 socket la QUA IT. Trinh duyet mo song song nhieu ket noi keep-alive
     * (trang + /favicon.ico), va lru_purge_enable mac dinh = false nen khi het
     * socket httpd TU CHOI ket noi moi -> fetch('/scan') treo, trang bao
     * "Loi quet". Nang len 4 + bat LRU purge de ket noi cu bi day ra thay vi
     * chan ket noi moi. LWIP_MAX_SOCKETS da nang len 10 trong
     * sdkconfig.defaults.p4-43lcd (4 httpd + 1 ctrl + 1 DNS + du du phong). */
    hcfg.max_open_sockets = 4;
    hcfg.lru_purge_enable = true;
    /* recv: chi de nhan request (<512 byte) — 8s la rat rong rai, va
     * receive_request_body() chi cho toi da 2 lan timeout nen mot client treo
     * khoa portal nhieu nhat ~24s roi bi cat.
     * send: /scan co the phai cho mot vong quet (~1s) truoc khi tra loi, cong
     * them WiFi cham; 15s cho phia gui. */
    hcfg.recv_wait_timeout = 8;
    hcfg.send_wait_timeout = 15;
    er = httpd_start(&srv, &hcfg);
    if (er != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "httpd_start: %s", esp_err_to_name(er));
        return er;
    }

    httpd_uri_t r_root = {
        .uri = "/", .method = HTTP_GET, .handler = prov_get_handler };
    httpd_register_uri_handler(srv, &r_root);
    httpd_uri_t r_wifi = {
        .uri = "/wifi", .method = HTTP_GET, .handler = prov_get_handler };
    httpd_register_uri_handler(srv, &r_wifi);
    httpd_uri_t r_scan = {
        .uri = "/scan", .method = HTTP_GET, .handler = prov_scan_handler };
    httpd_register_uri_handler(srv, &r_scan);
    httpd_uri_t r_save = {
        .uri = "/save", .method = HTTP_POST, .handler = prov_save_handler };
    httpd_register_uri_handler(srv, &r_save);
    httpd_uri_t r_status = {
        .uri = "/status", .method = HTTP_GET, .handler = prov_status_handler };
    httpd_register_uri_handler(srv, &r_status);

    /* Captive portal: tra thang trang cau hinh tren 2 path probe pho bien nhat.
     * Android doi 204 rong, iOS doi dung chu "Success" — tra HTML 200 thay vi
     * the la CO Y: ca hai coi day la "bi chan" va bat cua so dang nhap. */
    httpd_uri_t r_cap = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = prov_get_handler };
    httpd_register_uri_handler(srv, &r_cap);
    httpd_uri_t r_hot = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = prov_get_handler };
    httpd_register_uri_handler(srv, &r_hot);
    httpd_uri_t r_fav = {
        .uri = "/favicon.ico", .method = HTTP_GET, .handler = prov_favicon_handler };
    httpd_register_uri_handler(srv, &r_fav);

    /* Con lai (Windows /connecttest.txt + /ncsi.txt, iOS
     * /library/test/success.html, Firefox /canonical.html, favicon, hay go
     * nham duong dan) deu 302 ve trang cau hinh thay vi 404 tran. */
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, prov_redirect_handler);

    s_prov_httpd = srv;
    /* Chi ve man QR SAU khi portal that su tra loi duoc — khong de nguoi dung
     * quet ma roi gap trang khong load. */
    ui_wifi_setup_show(ap_ssid, s_portal_uri);
    ESP_LOGI(TAG_WIFI, "Provisioning portal san sang tai %s", s_portal_uri);

    /* Ham nong cache danh sach WiFi o nen — xem chu thich prov_prewarm_task. */
    if (xTaskCreate(prov_prewarm_task, "wifi_prewarm", 3072, NULL,
                    R4P_TASK_PRIO_NETWORK, NULL) != pdPASS) {
        ESP_LOGW(TAG_WIFI, "khong tao duoc task ham nong danh sach WiFi");
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_stop_provisioning(void) {
    esp_err_t first_err = ESP_OK;

    captive_dns_stop();

    if (s_prov_httpd) {
        esp_err_t er = httpd_stop(s_prov_httpd);
        if (er != ESP_OK) {
            ESP_LOGW(TAG_WIFI, "httpd_stop: %s", esp_err_to_name(er));
            first_err = er;
        }
        s_prov_httpd = NULL;
    }
    dashboard_resume();    /* dash_task tự bật lại khi STA có IP */

    esp_err_t er = esp_wifi_set_mode(WIFI_MODE_STA);
    if (er != ESP_OK && er != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG_WIFI, "set STA mode after provisioning: %s", esp_err_to_name(er));
        if (first_err == ESP_OK) {
            first_err = er;
        }
    } else if (er == ESP_OK) {
        ESP_LOGI(TAG_WIFI, "AP provisioning stopped; STA mode kept");
    }

    return first_err;
}
