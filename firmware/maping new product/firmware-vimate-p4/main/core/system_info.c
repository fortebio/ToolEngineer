#include "system_info.h"
#include "vimate.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

static char s_mac_str[18] = {0};
static char s_chip_model[24] = {0};

esp_err_t system_info_init(void) {
    uint8_t mac[6] = {0};
#if CONFIG_IDF_TARGET_ESP32P4
    /* P4 không có radio: ESP_MAC_WIFI_STA in "E system_api: 0 mac type is incorrect" rồi
     * mới rơi về base MAC. Đọc base MAC thẳng — cùng giá trị (MAC đã đăng ký server). */
    esp_err_t err = esp_efuse_mac_get_default(mac);
#else
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        err = esp_efuse_mac_get_default(mac);
    }
#endif
    if (err != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "MAC read failed (%s), using fallback", esp_err_to_name(err));
        memset(mac, 0xA5, sizeof(mac));
    }
    snprintf(s_mac_str, sizeof(s_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strlcpy(g_vimate_server.mac_id, s_mac_str, sizeof(g_vimate_server.mac_id));

    esp_chip_info_t info;
    esp_chip_info(&info);
    const char *name = "ESP32";
    switch (info.model) {
        case CHIP_ESP32:    name = "ESP32"; break;
        case CHIP_ESP32S2:  name = "ESP32-S2"; break;
        case CHIP_ESP32S3:  name = "ESP32-S3"; break;
        case CHIP_ESP32C3:  name = "ESP32-C3"; break;
        case CHIP_ESP32C6:  name = "ESP32-C6"; break;
        case CHIP_ESP32P4:  name = "ESP32-P4"; break;
        default: break;
    }
    snprintf(s_chip_model, sizeof(s_chip_model), "%s rev%d", name, info.revision);

    ESP_LOGI(TAG_MAIN, "Chip=%s MAC=%s Heap=%u",
             s_chip_model, s_mac_str, (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

const char *system_info_get_mac_str(void)   { return s_mac_str; }
const char *system_info_get_chip_model(void) { return s_chip_model; }
size_t system_info_free_heap(void) { return esp_get_free_heap_size(); }
