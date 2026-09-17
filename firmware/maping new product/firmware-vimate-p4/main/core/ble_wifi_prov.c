/**
 * BLE WiFi provisioning for the VIMATE parent app.
 *
 * Service UUID:        2f234454-cf6d-4a0f-adf2-f4911ba9ffa6
 * Command characteristic UUID:
 *                      2f234455-cf6d-4a0f-adf2-f4911ba9ffa6
 * Status characteristic UUID:
 *                      2f234456-cf6d-4a0f-adf2-f4911ba9ffa6
 *
 * Command payload is a small UTF-8 JSON object:
 *   {"ssid":"Home","password":"secret"}
 */
#include "ble_wifi_prov.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "vimate.h"
#include "ui/display.h"

#if !defined(CONFIG_BT_NIMBLE_ENABLED)
/* ESP32-P4 khong co radio Bluetooth (SOC_BT_SUPPORTED = 0) nen NimBLE khong
 * duoc build. Giu nguyen API; app_main da xu ly ble_status != ESP_OK bang cach
 * roi ve provisioning qua SoftAP (xem vimate_provisioning_task). */
esp_err_t ble_wifi_prov_start(void) {
    ESP_LOGW("vimate.ble", "BLE provisioning khong kha dung tren target nay - dung SoftAP");
    display_set_ble_state(BLE_PROV_STATE_UNAVAILABLE);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ble_wifi_prov_stop(void) {
    return ESP_OK;
}

ble_prov_state_t ble_wifi_prov_state(void) {
    return BLE_PROV_STATE_UNAVAILABLE;
}

#else  /* CONFIG_BT_NIMBLE_ENABLED */

#include "nvs_store.h"
#include "system_info.h"
#include "wifi_mgr.h"
#include "vimate.h"
#include "ui/ui_home.h"

#include "cJSON.h"
#include "esp_err.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TAG_BLE "vimate.ble"

static const ble_uuid128_t s_service_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x54, 0x44, 0x23, 0x2f);
static const ble_uuid128_t s_command_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x55, 0x44, 0x23, 0x2f);
static const ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x56, 0x44, 0x23, 0x2f);

static uint8_t s_own_addr_type;
static bool s_started = false;
static char s_device_name[24] = VIMATE_SETUP_PREFIX;
static char s_status[128] = "{\"state\":\"idle\"}";
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_status_val_handle;
static bool s_stopping = false;
static SemaphoreHandle_t s_lifecycle_mutex = NULL;
static ble_prov_state_t s_state = BLE_PROV_STATE_OFF;

ble_prov_state_t ble_wifi_prov_state(void) {
    return s_state;
}

/* Gọi từ host task NimBLE lẫn task gọi start/stop — display_set_ble_state đi qua
 * hàng đợi display nên an toàn từ mọi task. */
static void set_state(ble_prov_state_t st) {
    if (s_state == st) return;
    s_state = st;
    display_set_ble_state(st);
}
static char s_pending_auth_token[sizeof(g_vimate_server.device_token)] = {0};

static void ble_wifi_prov_advertise(void);

static void set_status(const char *state, const char *detail) {
    portENTER_CRITICAL(&s_status_lock);
    if (detail && detail[0]) {
        snprintf(s_status, sizeof(s_status), "{\"state\":\"%s\",\"detail\":\"%s\"}",
                 state, detail);
    } else {
        snprintf(s_status, sizeof(s_status), "{\"state\":\"%s\"}", state);
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (s_status_val_handle != 0) {
        ble_gatts_chr_updated(s_status_val_handle);
    }
}

static void provisioning_status_observer(const wifi_prov_status_t *status, void *ctx) {
    (void)ctx;
    if (!status || !s_started) return;

    switch (status->state) {
        case WIFI_PROV_STATE_VALIDATING:
            set_status("validating", NULL);
            ui_home_show_message("Đang kiểm tra WiFi",
                                 "Thiết bị đang xác thực mật khẩu và kết nối.");
            break;
        case WIFI_PROV_STATE_CONNECTED:
            if (s_pending_auth_token[0]) {
                esp_err_t er = nvs_store_set_str("dev_token", s_pending_auth_token);
                if (er != ESP_OK) {
                    ESP_LOGE(TAG_BLE, "save device token: %s", esp_err_to_name(er));
                    set_status("error", "token_nvs");
                    return;
                }
                strlcpy(g_vimate_server.device_token, s_pending_auth_token,
                        sizeof(g_vimate_server.device_token));
                g_vimate_server.activated = true;
            }
            set_status("connected", "restarting");
            ui_home_show_message("WiFi đã kết nối",
                                 "Cấu hình đã được lưu. Thiết bị đang khởi động lại.");
            break;
        case WIFI_PROV_STATE_ERROR:
            set_status("error", status->detail);
            ui_home_show_message("Không kết nối được WiFi",
                                 "Kiểm tra tên mạng, mật khẩu rồi thử lại trong app.");
            break;
        case WIFI_PROV_STATE_IDLE:
        default:
            set_status("idle", NULL);
            break;
    }
}

static int write_json_from_mbuf(struct os_mbuf *om, char *out, size_t out_len) {
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0 || len >= out_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    int rc = ble_hs_mbuf_to_flat(om, out, out_len - 1, &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    out[len] = '\0';
    return 0;
}

static int handle_command_write(struct os_mbuf *om) {
    char payload[320];
    int rc = write_json_from_mbuf(om, payload, sizeof(payload));
    if (rc != 0) {
        set_status("error", "payload_too_large");
        return rc;
    }

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        set_status("error", "json");
        return 0;
    }
    cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *jpass = cJSON_GetObjectItem(root, "password");
    cJSON *jtoken = cJSON_GetObjectItem(root, "authToken");
    if (!cJSON_IsString(jtoken)) {
        jtoken = cJSON_GetObjectItem(root, "deviceToken");
    }
    if (!cJSON_IsString(jtoken)) {
        jtoken = cJSON_GetObjectItem(root, "token");
    }
    if (!cJSON_IsString(jssid) || !jssid->valuestring[0]) {
        cJSON_Delete(root);
        set_status("error", "ssid_empty");
        return 0;
    }
    if (strlen(jssid->valuestring) > 32) {
        cJSON_Delete(root);
        set_status("error", "ssid_too_long");
        return 0;
    }
    const char *password = cJSON_IsString(jpass) ? jpass->valuestring : "";
    if (strlen(password) > 64) {
        cJSON_Delete(root);
        set_status("error", "password_too_long");
        return 0;
    }
    const char *auth_token = cJSON_IsString(jtoken) ? jtoken->valuestring : "";
    if (strlen(auth_token) >= sizeof(g_vimate_server.device_token)) {
        cJSON_Delete(root);
        set_status("error", "token_too_long");
        return 0;
    }

    ESP_LOGI(TAG_BLE, "BLE provisioning received SSID=%s", jssid->valuestring);
    strlcpy(s_pending_auth_token, auth_token, sizeof(s_pending_auth_token));
    esp_err_t er = wifi_mgr_submit_credentials(jssid->valuestring, password);
    cJSON_Delete(root);
    if (er != ESP_OK) {
        wifi_prov_status_t status;
        wifi_mgr_get_provision_status(&status);
        const char *detail = er == ESP_ERR_INVALID_STATE
            ? "busy" : (status.detail[0] ? status.detail : "submit_failed");
        ESP_LOGE(TAG_BLE, "submit candidate: %s (%s)",
                 esp_err_to_name(er), detail);
        set_status("error", detail);
        return 0;
    }
    set_status("validating", NULL);
    return 0;
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        ble_uuid_cmp(ctxt->chr->uuid, &s_command_uuid.u) == 0) {
        return handle_command_write(ctxt->om);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        ble_uuid_cmp(ctxt->chr->uuid, &s_status_uuid.u) == 0) {
        char status[sizeof(s_status)];
        portENTER_CRITICAL(&s_status_lock);
        strlcpy(status, s_status, sizeof(status));
        portEXIT_CRITICAL(&s_status_lock);
        int rc = os_mbuf_append(ctxt->om, status, strlen(status));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_command_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_val_handle,
            },
            {0},
        },
    },
    {0},
};

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG_BLE, "connect status=%d", event->connect.status);
            if (event->connect.status != 0) {
                ble_wifi_prov_advertise();
            } else {
                set_state(BLE_PROV_STATE_CONNECTED);
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG_BLE, "disconnect reason=%d", event->disconnect.reason);
            if (!s_stopping) {
                ble_wifi_prov_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (!s_stopping) {
                ble_wifi_prov_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG_BLE, "subscribe conn=%d attr=%d notify=%d",
                     event->subscribe.conn_handle, event->subscribe.attr_handle,
                     event->subscribe.cur_notify);
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG_BLE, "mtu=%d", event->mtu.value);
            return 0;
        default:
            return 0;
    }
}

static void ble_wifi_prov_advertise(void) {
    if (!s_started || s_stopping) return;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "adv fields rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t[]) { s_service_uuid };
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGW(TAG_BLE, "scan rsp fields rc=%d", rc);
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "adv start rc=%d", rc);
        return;
    }
    set_state(BLE_PROV_STATE_ADVERTISING);   /* cũng là trạng thái sau khi app ngắt */
}

static void on_reset(int reason) {
    ESP_LOGE(TAG_BLE, "reset reason=%d", reason);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "ensure addr rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "infer addr rc=%d", rc);
        return;
    }
    wifi_prov_status_t status;
    wifi_mgr_get_provision_status(&status);
    provisioning_status_observer(&status, NULL);
    ble_wifi_prov_advertise();
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_wifi_prov_start(void) {
    if (!s_lifecycle_mutex) {
        s_lifecycle_mutex = xSemaphoreCreateMutex();
        if (!s_lifecycle_mutex) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_started) {
        wifi_mgr_set_provision_observer(provisioning_status_observer, NULL);
        xSemaphoreGive(s_lifecycle_mutex);
        return ESP_OK;
    }

    const char *mac = system_info_get_mac_str();
    if (mac && strlen(mac) >= 17) {
        snprintf(s_device_name, sizeof(s_device_name), VIMATE_SETUP_PREFIX "-%s", mac + 12);
    }
    s_stopping = false;
    s_status_val_handle = 0;
    s_pending_auth_token[0] = '\0';

    esp_err_t er = nimble_port_init();
    if (er != ESP_OK) {
        ESP_LOGE(TAG_BLE, "nimble init: %s", esp_err_to_name(er));
        xSemaphoreGive(s_lifecycle_mutex);
        return er;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = NULL;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "gatt count rc=%d", rc);
        nimble_port_deinit();
        xSemaphoreGive(s_lifecycle_mutex);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "gatt add rc=%d", rc);
        nimble_port_deinit();
        xSemaphoreGive(s_lifecycle_mutex);
        return ESP_FAIL;
    }
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG_BLE, "set name rc=%d", rc);
        nimble_port_deinit();
        xSemaphoreGive(s_lifecycle_mutex);
        return ESP_FAIL;
    }
    wifi_mgr_set_provision_observer(provisioning_status_observer, NULL);
    s_started = true;
    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG_BLE,
             "BLE provisioning open (no pairing/PIN) as %s", s_device_name);
    xSemaphoreGive(s_lifecycle_mutex);
    return ESP_OK;
}

esp_err_t ble_wifi_prov_stop(void) {
    if (!s_lifecycle_mutex) return ESP_OK;
    if (xSemaphoreTake(s_lifecycle_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_started) {
        xSemaphoreGive(s_lifecycle_mutex);
        return ESP_OK;
    }
    s_stopping = true;
    wifi_mgr_set_provision_observer(NULL, NULL);
    ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG_BLE, "nimble stop rc=%d", rc);
    } else {
        esp_err_t er = nimble_port_deinit();
        if (er != ESP_OK) {
            ESP_LOGW(TAG_BLE, "nimble deinit: %s", esp_err_to_name(er));
            rc = -1;
        }
    }
    s_started = false;
    s_stopping = false;
    s_status_val_handle = 0;
    s_pending_auth_token[0] = '\0';
    set_state(BLE_PROV_STATE_OFF);
    xSemaphoreGive(s_lifecycle_mutex);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

#endif /* CONFIG_BT_NIMBLE_ENABLED */
