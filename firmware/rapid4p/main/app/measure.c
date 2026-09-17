#include "measure.h"
#include "calib_store.h"
#include "sensor/sensor_bus.h"
#include "sensor/tca9548.h"
#include "sensor/tcs34725.h"
#include "sensor/slot_led.h"
#include "core/task_profile.h"
#include "boards/board.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

/* Thời gian như bản gốc (sensor.cpp) */
#define LED_SETTLE_MS      1000   /* read_Sensor: LED_on → delay(1000) */
#define CALIB_SETTLE_MS    620    /* calib_Sensor: LED_on → delay(620) */
#define CALIB_SAMPLES      5

typedef enum { CMD_MEASURE, CMD_CALIB } cmd_kind_t;
typedef struct { cmd_kind_t kind; r4p_sick_t sick; r4p_sample_t sample; int slot; } cmd_t;

static const uint8_t s_mux_ch[BOARD_SENSOR_SLOTS] = BOARD_SENSOR_MUX_CHANNELS;
static tcs34725_t s_tcs[R4P_SLOTS];
static measure_result_t s_res;
static QueueHandle_t s_q;
static volatile bool s_busy, s_abort;
static measure_progress_cb_t s_cb;
static void *s_cb_ctx;
static int s_alive;

static void notify(measure_phase_t ph)
{
    if (s_cb) s_cb(&s_res, ph, s_cb_ctx);
}

/* Bật LED, chọn mux, đọc n mẫu lux, tắt LED. Trả ESP_OK và mean lux qua *out. */
static esp_err_t read_slot_lux(int slot, int settle_ms, int n, float *out_mean)
{
    if (slot < 0 || slot >= R4P_SLOTS) return ESP_ERR_INVALID_ARG;
    slot_led_on(slot);
    vTaskDelay(pdMS_TO_TICKS(settle_ms));
    esp_err_t r = tca9548_select(s_mux_ch[slot]);
    float sum = 0;
    int got = 0;
    if (r == ESP_OK && s_tcs[slot].available) {
        for (int i = 0; i < n && !s_abort; i++) {
            r = tcs34725_get_data(&s_tcs[slot]);
            if (r != ESP_OK) break;
            sum += s_tcs[slot].lux;
            got++;
        }
    } else if (r == ESP_OK) {
        r = ESP_ERR_NOT_FOUND;
    }
    slot_led_off(slot);
    if (r == ESP_OK && got == n) {
        *out_mean = sum / (float)n;
        s_res.lux_last[slot] = *out_mean;
    }
    return r;
}

/* handle_Sensor(): map giá trị vào 0..3000 theo calib slot. */
static uint32_t map_result(int slot, uint32_t value)
{
    const r4p_settings_t *c = calib_store_get();
    const uint32_t lo = c->cal_min[slot], hi = c->cal_max[slot];
    if (hi <= lo) return value < lo ? R4P_RESULT_MIN : R4P_RESULT_MAX;   /* chưa calib */
    if (value < lo) return R4P_RESULT_MIN;
    if (value > hi) return R4P_RESULT_MAX;
    return (uint32_t)(((uint64_t)(value - lo) * (R4P_RESULT_MAX - R4P_RESULT_MIN)) / (hi - lo)) + R4P_RESULT_MIN;
}

static void do_measure(const cmd_t *cmd)
{
    memset(&s_res, 0, sizeof(s_res));
    s_res.sick = cmd->sick;
    s_res.sample = cmd->sample;
    s_res.started_us = esp_timer_get_time();
    for (int i = 0; i < R4P_SLOTS; i++) s_res.sensor_ok[i] = s_tcs[i].available;
    ESP_LOGI(TAG_MEASURE, "bat dau do %s / %s (%d vong x %d slot x %d mau)",
             r4p_sick_name(cmd->sick), r4p_sample_name(cmd->sample), R4P_ROUNDS, R4P_SLOTS, R4P_SAMPLES);

    for (int round = 1; round <= R4P_ROUNDS && !s_abort; round++) {
        s_res.round = round;
        for (int slot = 0; slot < R4P_SLOTS && !s_abort; slot++) {
            s_res.slot = slot;
            notify(MEASURE_PHASE_SLOT_START);
            float mean = 0;
            esp_err_t r = read_slot_lux(slot, LED_SETTLE_MS, R4P_SAMPLES, &mean);
            if (s_abort) break;
            if (r != ESP_OK) {
                s_res.err = r;
                s_res.sensor_ok[slot] = false;
                ESP_LOGE(TAG_MEASURE, "slot %d vong %d loi %s", slot + 1, round, esp_err_to_name(r));
                slot_led_off_all();
                notify(MEASURE_PHASE_ERROR);
                return;
            }
            const uint32_t value = (uint32_t)lroundf(mean * 1000.0f);
            s_res.value_raw[slot] = value;
            s_res.result[slot][round - 1] = map_result(slot, value);
            ESP_LOGI(TAG_MEASURE, "vong %d slot %d: lux=%.2f value=%lu result=%lu (agc %d)",
                     round, slot + 1, mean, (unsigned long)value,
                     (unsigned long)s_res.result[slot][round - 1], s_tcs[slot].agc);
            notify(MEASURE_PHASE_SLOT_DONE);
        }
    }
    slot_led_off_all();
    if (s_abort) {
        notify(MEASURE_PHASE_ABORTED);
        return;
    }
    const r4p_settings_t *c = calib_store_get();
    for (int slot = 0; slot < R4P_SLOTS; slot++) {
        uint32_t sum = 0;
        for (int k = 0; k < R4P_ROUNDS; k++) sum += s_res.result[slot][k];
        s_res.average[slot] = sum / R4P_ROUNDS;
        s_res.positive[slot] = s_res.average[slot] >= c->threshold[cmd->sick];
    }
    s_res.finished_us = esp_timer_get_time();
    ESP_LOGI(TAG_MEASURE, "xong %lld ms: %lu%s %lu%s %lu%s %lu%s (nguong %lu)",
             (long long)((s_res.finished_us - s_res.started_us) / 1000),
             (unsigned long)s_res.average[0], s_res.positive[0] ? "+" : "-",
             (unsigned long)s_res.average[1], s_res.positive[1] ? "+" : "-",
             (unsigned long)s_res.average[2], s_res.positive[2] ? "+" : "-",
             (unsigned long)s_res.average[3], s_res.positive[3] ? "+" : "-",
             (unsigned long)c->threshold[cmd->sick]);
    notify(MEASURE_PHASE_DONE);
    xEventGroupSetBits(g_r4p_events, R4P_EVT_MEASURE_DONE);
}

static void do_calib(const cmd_t *cmd)
{
    s_res.calib_slot = cmd->slot;
    float mean = 0;
    esp_err_t r = read_slot_lux(cmd->slot, CALIB_SETTLE_MS, CALIB_SAMPLES, &mean);
    if (r != ESP_OK) {
        s_res.err = r;
        ESP_LOGE(TAG_MEASURE, "calib slot %d loi %s", cmd->slot + 1, esp_err_to_name(r));
        notify(MEASURE_PHASE_ERROR);
        return;
    }
    float v = mean * 1000.0f;
    if (v < 0) v = 0;
    if (v > 65535.0f) v = 65535.0f;
    s_res.calib_value = (uint16_t)v;
    ESP_LOGI(TAG_MEASURE, "calib slot %d: lux=%.2f value=%u", cmd->slot + 1, mean, s_res.calib_value);
    notify(MEASURE_PHASE_CALIB_DONE);
}

static void measure_task(void *arg)
{
    (void)arg;
    cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        s_busy = true;
        s_abort = false;
        if (cmd.kind == CMD_MEASURE) do_measure(&cmd);
        else do_calib(&cmd);
        s_busy = false;
    }
}

esp_err_t measure_init(void)
{
    slot_led_init();
    s_q = xQueueCreate(2, sizeof(cmd_t));
    configASSERT(s_q);
    esp_err_t r = sensor_bus_init();
    if (r == ESP_OK) {
        s_alive = 0;
        for (int i = 0; i < R4P_SLOTS; i++) {
            if (tca9548_select(s_mux_ch[i]) != ESP_OK) continue;
            esp_err_t e = tcs34725_begin(&s_tcs[i]);
            ESP_LOGI(TAG_SENSOR, "slot %d (mux ch %d): %s", i + 1, s_mux_ch[i],
                     e == ESP_OK ? "TCS34725 OK" : esp_err_to_name(e));
            if (e == ESP_OK) s_alive++;
        }
        tca9548_close();
    } else {
        ESP_LOGE(TAG_SENSOR, "sensor bus loi %s - do se bao loi, UI/OTA van chay", esp_err_to_name(r));
    }
    const r4p_settings_t *c = calib_store_get();
    for (int i = 0; i < R4P_SLOTS; i++) slot_led_set_brightness(i, c->led_pwm[i]);
    xTaskCreatePinnedToCore(measure_task, "measure", R4P_TASK_STACK_MEASURE, NULL,
                            R4P_TASK_PRIO_MEASURE, NULL, R4P_TASK_CORE_IO);
    ESP_LOGI(TAG_MEASURE, "measure task san sang, %d/%d cam bien song", s_alive, R4P_SLOTS);
    return r;
}

int measure_sensors_alive(void) { return s_alive; }

void measure_set_progress_cb(measure_progress_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
}

esp_err_t measure_start(r4p_sick_t sick, r4p_sample_t sample)
{
    if (!s_q) return ESP_ERR_INVALID_STATE;
    if (s_busy) return ESP_ERR_INVALID_STATE;
    cmd_t c = { .kind = CMD_MEASURE, .sick = sick, .sample = sample };
    return xQueueSend(s_q, &c, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t measure_calib_start(int slot)
{
    if (!s_q) return ESP_ERR_INVALID_STATE;
    if (s_busy) return ESP_ERR_INVALID_STATE;
    if (slot < 0 || slot >= R4P_SLOTS) return ESP_ERR_INVALID_ARG;
    cmd_t c = { .kind = CMD_CALIB, .slot = slot };
    return xQueueSend(s_q, &c, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t measure_abort(void)
{
    s_abort = true;
    return ESP_OK;
}

bool measure_busy(void) { return s_busy; }
const measure_result_t *measure_last(void) { return &s_res; }
