#include "tcs34725.h"
#include "sensor_bus.h"
#include "rapid4p.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Thanh ghi (datasheet TCS34725 / Adafruit_TCS34725.h) */
#define TCS_CMD_BIT      0x80
#define TCS_REG_ENABLE   0x00
#define TCS_REG_ATIME    0x01
#define TCS_REG_CONTROL  0x0F
#define TCS_REG_ID       0x12
#define TCS_REG_STATUS   0x13
#define TCS_REG_CDATAL   0x14
#define TCS_EN_PON       0x01
#define TCS_EN_AEN       0x02

/* Hằng DN40 (tcs.h ReaderPlus) */
#define TCS_R_COEF   0.146f
#define TCS_G_COEF   1.000f
#define TCS_B_COEF  -0.454f
#define TCS_GA       1.0f
#define TCS_DF       320.0f

typedef struct { uint8_t gain; uint8_t atime; uint16_t mincnt; uint16_t maxcnt; } tcs_agc_t;
/* gain: 0=1x 1=4x 2=16x 3=60x; atime: 0x00=614 ms, 0xC0=154 ms */
static const tcs_agc_t s_agc[] = {
    { 3, 0x00, 0,     20000 },
    { 3, 0xC0, 4990,  63000 },
    { 2, 0xC0, 16790, 63000 },
    { 1, 0xC0, 15840, 63000 },
    { 0, 0xC0, 15840, 0     },
};
#define AGC_N ((int)(sizeof(s_agc) / sizeof(s_agc[0])))

static esp_err_t wr8(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev = sensor_bus_tcs_dev();
    if (!dev) return ESP_ERR_INVALID_STATE;
    uint8_t b[2] = { (uint8_t)(TCS_CMD_BIT | reg), val };
    return i2c_master_transmit(dev, b, 2, pdMS_TO_TICKS(50));
}

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n)
{
    i2c_master_dev_handle_t dev = sensor_bus_tcs_dev();
    if (!dev) return ESP_ERR_INVALID_STATE;
    uint8_t c = (uint8_t)(TCS_CMD_BIT | reg);
    return i2c_master_transmit_receive(dev, &c, 1, out, n, pdMS_TO_TICKS(50));
}

esp_err_t tcs34725_read_id(uint8_t *id) { return rd(TCS_REG_ID, id, 1); }

static uint16_t gain_x(uint8_t g)
{
    switch (g) { case 0: return 1; case 1: return 4; case 2: return 16; default: return 60; }
}

static esp_err_t set_gain_time(tcs34725_t *t)
{
    const tcs_agc_t *a = &s_agc[t->agc];
    ESP_RETURN_ON_ERROR(wr8(TCS_REG_CONTROL, a->gain), TAG_SENSOR, "control");
    ESP_RETURN_ON_ERROR(wr8(TCS_REG_ATIME, a->atime), TAG_SENSOR, "atime");
    t->atime = a->atime;
    t->atime_ms = (uint16_t)((256 - a->atime) * 2.4f);
    t->againx = gain_x(a->gain);
    return ESP_OK;
}

esp_err_t tcs34725_begin(tcs34725_t *t)
{
    uint8_t id = 0;
    esp_err_t r = rd(TCS_REG_ID, &id, 1);
    t->available = (r == ESP_OK) && (id == 0x44 || id == 0x4D || id == 0x10);
    if (!t->available) return r == ESP_OK ? ESP_ERR_NOT_FOUND : r;
    t->agc = 0;
    ESP_RETURN_ON_ERROR(set_gain_time(t), TAG_SENSOR, "gain/time");
    ESP_RETURN_ON_ERROR(wr8(TCS_REG_ENABLE, TCS_EN_PON), TAG_SENSOR, "PON");
    vTaskDelay(pdMS_TO_TICKS(3));
    ESP_RETURN_ON_ERROR(wr8(TCS_REG_ENABLE, TCS_EN_PON | TCS_EN_AEN), TAG_SENSOR, "AEN");
    return ESP_OK;
}

esp_err_t tcs34725_read_raw(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
    uint8_t d[8];
    ESP_RETURN_ON_ERROR(rd(TCS_REG_CDATAL, d, 8), TAG_SENSOR, "rgbc");
    if (c) *c = (uint16_t)(d[0] | (d[1] << 8));
    if (r) *r = (uint16_t)(d[2] | (d[3] << 8));
    if (g) *g = (uint16_t)(d[4] | (d[5] << 8));
    if (b) *b = (uint16_t)(d[6] | (d[7] << 8));
    return ESP_OK;
}

/* Adafruit getRawData: đọc rồi delay(atime_ms). Ở đây delay TRƯỚC khi đọc để mẫu đầu
 * sau khi bật LED / đổi gain đã tích phân đủ (ReaderPlus bù bằng delay(1000) sau LED_on). */
static esp_err_t read_fresh(tcs34725_t *t)
{
    vTaskDelay(pdMS_TO_TICKS(t->atime_ms + 3));
    return tcs34725_read_raw(&t->r, &t->g, &t->b, &t->c);
}

esp_err_t tcs34725_get_data(tcs34725_t *t)
{
    ESP_RETURN_ON_ERROR(read_fresh(t), TAG_SENSOR, "read");
    const tcs_agc_t *a = &s_agc[t->agc];
    int next = t->agc;
    if (a->maxcnt && t->c > a->maxcnt && t->agc < AGC_N - 1) next = t->agc + 1;
    else if (a->mincnt && t->c < a->mincnt && t->agc > 0) next = t->agc - 1;
    if (next != t->agc) {
        t->agc = next;
        ESP_RETURN_ON_ERROR(set_gain_time(t), TAG_SENSOR, "agc");
        vTaskDelay(pdMS_TO_TICKS(t->atime_ms * 2));   /* "shock absorber" như bản gốc */
        ESP_RETURN_ON_ERROR(tcs34725_read_raw(&t->r, &t->g, &t->b, &t->c), TAG_SENSOR, "read2");
    }
    /* DN40 */
    const uint32_t sum = (uint32_t)t->r + t->g + t->b;
    t->ir = (sum > t->c) ? (uint16_t)((sum - t->c) / 2) : 0;
    const float r_comp = (float)t->r - t->ir;
    const float g_comp = (float)t->g - t->ir;
    const float b_comp = (float)t->b - t->ir;
    const uint32_t saturation = ((256 - t->atime) > 63) ? 65535u : 1024u * (256 - t->atime);
    const uint32_t saturation75 = (t->atime_ms < 150) ? (saturation - saturation / 4) : saturation;
    t->saturated = (t->atime_ms < 150 && t->c > saturation75);
    const float cpl = ((float)t->atime_ms * (float)t->againx) / (TCS_GA * TCS_DF);
    t->lux = (TCS_R_COEF * r_comp + TCS_G_COEF * g_comp + TCS_B_COEF * b_comp) / cpl;
    return ESP_OK;
}
