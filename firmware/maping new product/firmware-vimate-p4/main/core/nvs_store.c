#include "nvs_store.h"
#include "vimate.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define NVS_NS "vimate"

esp_err_t nvs_store_init(void) {
    /* nvs_flash_init() đã được gọi ở app_main; ở đây chỉ verify mở namespace OK. */
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_NVS, "open ns %s: %s", NVS_NS, esp_err_to_name(r));
        return r;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_store_set_str(const char *key, const char *value) {
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_set_str(h, key, value ? value : "");
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    return r;
}

esp_err_t nvs_store_get_str(const char *key, char *out, size_t maxlen) {
    if (!out || maxlen == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (r != ESP_OK) return r;
    size_t len = maxlen;
    r = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;   /* missing không phải lỗi */
    }
    return r;
}

esp_err_t nvs_store_set_u32(const char *key, uint32_t value) {
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_set_u32(h, key, value);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    return r;
}

esp_err_t nvs_store_get_u32(const char *key, uint32_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = 0;
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (r != ESP_OK) return r;
    r = nvs_get_u32(h, key, out);
    nvs_close(h);
    if (r == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    return r;
}

/* WiFi creds atomic: 1 blob entry. NVS ghi blob theo chunk rồi index-entry sau
 * cùng → cúp điện giữa chừng = blob mới vô hiệu, blob cũ còn nguyên (all-or-nothing).
 * Khác hẳn ghi 2 key wifi_ssid/wifi_pass riêng (2 commit, không atomic chéo). */
#define WIFI_BLOB_KEY "wifi_cfg"
#define WIFI_BLOB_VER 1

typedef struct {
    uint8_t version;
    char    ssid[33];   /* 32 + NUL */
    char    pass[65];   /* 64 + NUL */
} wifi_cfg_blob_t;

esp_err_t nvs_store_set_wifi(const char *ssid, const char *pass) {
    wifi_cfg_blob_t b = {0};
    b.version = WIFI_BLOB_VER;
    strlcpy(b.ssid, ssid ? ssid : "", sizeof(b.ssid));
    strlcpy(b.pass, pass ? pass : "", sizeof(b.pass));
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_set_blob(h, WIFI_BLOB_KEY, &b, sizeof(b));
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    return r;
}

esp_err_t nvs_store_get_wifi(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz) {
    if (ssid && ssid_sz) ssid[0] = '\0';
    if (pass && pass_sz) pass[0] = '\0';
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (r != ESP_OK) return (r == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : r;

    wifi_cfg_blob_t b = {0};
    size_t len = sizeof(b);
    esp_err_t rb = nvs_get_blob(h, WIFI_BLOB_KEY, &b, &len);
    if (rb == ESP_OK && len == sizeof(b) && b.version == WIFI_BLOB_VER) {
        if (ssid && ssid_sz) strlcpy(ssid, b.ssid, ssid_sz);
        if (pass && pass_sz) strlcpy(pass, b.pass, pass_sz);
        nvs_close(h);
        return ESP_OK;
    }

    /* Fallback: key cũ wifi_ssid/wifi_pass (thiết bị OTA chưa ghi blob lần nào). */
    if (ssid && ssid_sz) {
        size_t l = ssid_sz;
        esp_err_t r1 = nvs_get_str(h, "wifi_ssid", ssid, &l);
        if (r1 != ESP_OK && r1 != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return r1; }
        if (r1 == ESP_ERR_NVS_NOT_FOUND) ssid[0] = '\0';
    }
    if (pass && pass_sz) {
        size_t l = pass_sz;
        esp_err_t r2 = nvs_get_str(h, "wifi_pass", pass, &l);
        if (r2 != ESP_OK && r2 != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return r2; }
        if (r2 == ESP_ERR_NVS_NOT_FOUND) pass[0] = '\0';
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_store_erase(const char *key) {
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_erase_key(h, key);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    return r;
}

esp_err_t nvs_store_factory_reset(void) {
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_erase_all(h);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG_NVS, "factory reset done — token/cache cleared");
    return r;
}
