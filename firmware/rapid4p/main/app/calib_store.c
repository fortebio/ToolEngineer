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

/* Blob theo-khe (cal_min/cal_max/led_pwm) có kích thước = N × elem. Khi số khe đổi (4 → 5,
 * docs/plan/rapid4p-5-slot.md) blob cũ ngắn hơn → KHÔNG xoá calib: đọc blob cũ theo mọi
 * kích thước 1..R4P_SLOTS-1 khe, giữ phần trùng, khe mới = `fill`, rồi ghi lại đúng kích thước. */
static void load_slot_blob(const char *key, void *dst, size_t elem, uint8_t fill)
{
    uint8_t *d = dst;
    const size_t want = elem * R4P_SLOTS;
    if (nvs_store_get_blob(key, d, want) == ESP_OK) return;
    memset(d, fill, want);
    for (int n = R4P_SLOTS - 1; n >= 1; n--) {
        if (nvs_store_get_blob(key, d, elem * n) == ESP_OK) {
            ESP_LOGW(TAG_NVS, "%s: blob %d khe -> mo rong %d khe (giu calib cu)", key, n, R4P_SLOTS);
            break;
        }
    }
    nvs_store_set_blob(key, d, want);
}

esp_err_t calib_store_load(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    load_slot_blob("cal_min", s_cfg.cal_min, sizeof(s_cfg.cal_min[0]), 0);
    load_slot_blob("cal_max", s_cfg.cal_max, sizeof(s_cfg.cal_max[0]), 0);
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
    /* Ngôn ngữ: mặc định TIẾNG VIỆT (người dùng cuối là nông dân VN; bản gốc mặc định EN).
     * Khoá chưa có → get trả OK + 0 = VI. Chưa có font CJK thì ZH/TW không hiện được (rơi về EN
     * trong ui_strings) → coi là không hợp lệ, về VI (máy thử 2026-09-20 kẹt ở lang=3 do chọn
     * nhầm lúc chạm còn ngược); khi có font, R4P_HAVE_CJK_FONT=1 mở lại. */
    uint32_t lang = R4P_LANG_VI;
    nvs_store_get_u32("lang", &lang);
    if (lang >= R4P_LANG_COUNT) lang = R4P_LANG_VI;
#if !R4P_HAVE_CJK_FONT
    if (lang >= R4P_LANG_ZH) {
        ESP_LOGW(TAG_NVS, "lang=%lu (CJK) chua co font -> ve VI", (unsigned long)lang);
        lang = R4P_LANG_VI;
        nvs_store_set_u32("lang", lang);
    }
#endif
    s_cfg.lang = (r4p_lang_t)lang;
    load_slot_blob("led_pwm", s_cfg.led_pwm, sizeof(s_cfg.led_pwm[0]), 127);
    /* Lần đo trước: lưu giá trị + 1 để 0 = chưa có (get_u32 trả OK + 0 khi thiếu khoá). */
    uint32_t ls = 0, lp = 0;
    nvs_store_get_u32("last_sick", &ls);
    nvs_store_get_u32("last_sample", &lp);
    s_cfg.last_valid = ls >= 1 && ls <= R4P_SICK_COUNT && lp >= 1 && lp <= R4P_SAMPLE_COUNT;
    s_cfg.last_sick = s_cfg.last_valid ? (r4p_sick_t)(ls - 1) : R4P_SICK_PC;
    s_cfg.last_sample = s_cfg.last_valid ? (r4p_sample_t)(lp - 1) : R4P_SAMPLE_VANNAMEI;
    for (int i = 0; i < R4P_SLOTS; i++)
        ESP_LOGI(TAG_NVS, "settings: khe %d cal min=%u max=%u led=%u", i + 1, s_cfg.cal_min[i], s_cfg.cal_max[i], s_cfg.led_pwm[i]);
    ESP_LOGI(TAG_NVS, "settings: thr=%lu/%lu/%lu/%lu/%lu lang=%d last=%s",
             (unsigned long)s_cfg.threshold[0], (unsigned long)s_cfg.threshold[1],
             (unsigned long)s_cfg.threshold[2], (unsigned long)s_cfg.threshold[3],
             (unsigned long)s_cfg.threshold[4], (int)s_cfg.lang,
             s_cfg.last_valid ? r4p_sick_name(s_cfg.last_sick) : "-");
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

esp_err_t calib_store_set_last(r4p_sick_t sick, r4p_sample_t sample)
{
    if (sick >= R4P_SICK_COUNT || sample >= R4P_SAMPLE_COUNT) return ESP_ERR_INVALID_ARG;
    /* Không ghi lại NVS nếu không đổi (tiết kiệm chu kỳ flash: nông dân đo cùng loại cả ngày). */
    if (s_cfg.last_valid && s_cfg.last_sick == sick && s_cfg.last_sample == sample) return ESP_OK;
    s_cfg.last_sick = sick;
    s_cfg.last_sample = sample;
    s_cfg.last_valid = true;
    esp_err_t r = nvs_store_set_u32("last_sick", (uint32_t)sick + 1);
    if (r == ESP_OK) r = nvs_store_set_u32("last_sample", (uint32_t)sample + 1);
    return r;
}
