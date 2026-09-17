#include "calib_store.h"
#include "core/nvs_store.h"
#include <string.h>
#include <stdio.h>

static r4p_settings_t s_cfg;

static const char *thr_key(r4p_sick_t s, char *buf, size_t n)
{
    snprintf(buf, n, "thr_%s", r4p_sick_name(s));   /* thr_PC, thr_EHP, ... (≤ 15 ký tự) */
    return buf;
}

esp_err_t calib_store_load(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    if (nvs_store_get_blob("cal_min", s_cfg.cal_min, sizeof(s_cfg.cal_min)) != ESP_OK) {
        memset(s_cfg.cal_min, 0, sizeof(s_cfg.cal_min));
        nvs_store_set_blob("cal_min", s_cfg.cal_min, sizeof(s_cfg.cal_min));
    }
    if (nvs_store_get_blob("cal_max", s_cfg.cal_max, sizeof(s_cfg.cal_max)) != ESP_OK) {
        memset(s_cfg.cal_max, 0, sizeof(s_cfg.cal_max));
        nvs_store_set_blob("cal_max", s_cfg.cal_max, sizeof(s_cfg.cal_max));
    }
    for (int i = 0; i < R4P_SICK_COUNT; i++) {
        char k[16];
        uint32_t v = 0;
        thr_key((r4p_sick_t)i, k, sizeof(k));
        /* nvs_store_get_u32 trả OK + 0 khi chưa có → 0 coi như chưa đặt (ReaderPlus
         * cũng dùng cờ ADDR_CHECK_THRESHOLD để ghi 600 lần đầu). */
        if (nvs_store_get_u32(k, &v) != ESP_OK || v == 0) {
            v = R4P_THRESHOLD_DEFAULT;
            nvs_store_set_u32(k, v);
        }
        s_cfg.threshold[i] = v;
    }
    uint32_t lang = 0;
    nvs_store_get_u32("lang", &lang);
    if (lang >= R4P_LANG_COUNT) lang = R4P_LANG_EN;   /* mặc định EN như bản gốc */
    s_cfg.lang = (r4p_lang_t)lang;
    if (nvs_store_get_blob("led_pwm", s_cfg.led_pwm, sizeof(s_cfg.led_pwm)) != ESP_OK) {
        memset(s_cfg.led_pwm, 127, sizeof(s_cfg.led_pwm));
    }
    ESP_LOGI(TAG_NVS, "settings: cal min=%u/%u/%u/%u max=%u/%u/%u/%u thr=%lu/%lu/%lu/%lu/%lu lang=%d",
             s_cfg.cal_min[0], s_cfg.cal_min[1], s_cfg.cal_min[2], s_cfg.cal_min[3],
             s_cfg.cal_max[0], s_cfg.cal_max[1], s_cfg.cal_max[2], s_cfg.cal_max[3],
             (unsigned long)s_cfg.threshold[0], (unsigned long)s_cfg.threshold[1],
             (unsigned long)s_cfg.threshold[2], (unsigned long)s_cfg.threshold[3],
             (unsigned long)s_cfg.threshold[4], (int)s_cfg.lang);
    return ESP_OK;
}

const r4p_settings_t *calib_store_get(void) { return &s_cfg; }

esp_err_t calib_store_set_calib(int slot, uint16_t cal_max, uint16_t cal_min)
{
    if (slot < 0 || slot >= R4P_SLOTS) return ESP_ERR_INVALID_ARG;
    s_cfg.cal_max[slot] = cal_max;
    s_cfg.cal_min[slot] = cal_min;
    esp_err_t r = nvs_store_set_blob("cal_max", s_cfg.cal_max, sizeof(s_cfg.cal_max));
    if (r == ESP_OK) r = nvs_store_set_blob("cal_min", s_cfg.cal_min, sizeof(s_cfg.cal_min));
    ESP_LOGI(TAG_NVS, "calib slot %d: max=%u min=%u (%s)", slot + 1, cal_max, cal_min, esp_err_to_name(r));
    return r;
}

esp_err_t calib_store_format_calib(void)
{
    memset(s_cfg.cal_max, 0, sizeof(s_cfg.cal_max));
    memset(s_cfg.cal_min, 0, sizeof(s_cfg.cal_min));
    esp_err_t r = nvs_store_set_blob("cal_max", s_cfg.cal_max, sizeof(s_cfg.cal_max));
    if (r == ESP_OK) r = nvs_store_set_blob("cal_min", s_cfg.cal_min, sizeof(s_cfg.cal_min));
    ESP_LOGW(TAG_NVS, "format calib: %s", esp_err_to_name(r));
    return r;
}

bool calib_store_slot_calibrated(int slot)
{
    if (slot < 0 || slot >= R4P_SLOTS) return false;
    return s_cfg.cal_max[slot] > s_cfg.cal_min[slot] && s_cfg.cal_max[slot] > 0;
}

esp_err_t calib_store_set_threshold(r4p_sick_t sick, uint32_t value)
{
    if (sick >= R4P_SICK_COUNT) return ESP_ERR_INVALID_ARG;
    char k[16];
    s_cfg.threshold[sick] = value;
    return nvs_store_set_u32(thr_key(sick, k, sizeof(k)), value);
}

esp_err_t calib_store_set_lang(r4p_lang_t lang)
{
    if (lang >= R4P_LANG_COUNT) return ESP_ERR_INVALID_ARG;
    s_cfg.lang = lang;
    return nvs_store_set_u32("lang", (uint32_t)lang);
}

esp_err_t calib_store_set_led_pwm(int slot, uint8_t pwm)
{
    if (slot < 0 || slot >= R4P_SLOTS) return ESP_ERR_INVALID_ARG;
    s_cfg.led_pwm[slot] = pwm;
    return nvs_store_set_blob("led_pwm", s_cfg.led_pwm, sizeof(s_cfg.led_pwm));
}
