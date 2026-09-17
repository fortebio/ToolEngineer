/**
 * measure.h — task đo 4 slot + hiệu chuẩn. Giữ đúng ngữ nghĩa FBT-ReaderPlus-1.0
 * (sensor.cpp), MAPPING-Rapid4P.md §3.5:
 *
 *   Đo:   R4P_ROUNDS (3) vòng × { mỗi slot: LED on → 1000 ms → chọn mux → R4P_SAMPLES (3)
 *         mẫu lux (auto-gain) → LED off }
 *         value = round(mean(lux) × 1000)
 *         result = clamp-map(value, cal_min..cal_max → 0..3000)
 *         average = mean(3 vòng); dương tính khi average ≥ threshold[sick]
 *         ≈ 34 s cho 4 slot → chạy TRONG TASK RIÊNG (core 1, prio 4), UI nhận tiến độ qua
 *         callback (gọi từ task đo → UI phải bọc display_schedule, KHÔNG gọi lv_* thẳng).
 *   Calib: LED on → 620 ms → 5 mẫu lux → value = mean × 1000 (uint16). UI quyết định đó
 *         là MAX hay MIN và lưu qua calib_store_set_calib().
 */
#pragma once
#include "esp_err.h"
#include "rapid4p.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEASURE_PHASE_IDLE = 0,
    MEASURE_PHASE_SLOT_START,   /* bắt đầu đo một slot (r->round, r->slot) */
    MEASURE_PHASE_SLOT_DONE,    /* xong một slot: value_raw/result cập nhật */
    MEASURE_PHASE_DONE,         /* xong chu trình: average/positive hợp lệ */
    MEASURE_PHASE_ABORTED,
    MEASURE_PHASE_ERROR,        /* I2C lỗi giữa chừng (r->err) */
    MEASURE_PHASE_CALIB_DONE,   /* r->calib_value hợp lệ */
} measure_phase_t;

typedef struct {
    r4p_sick_t sick;
    r4p_sample_t sample;
    int round;                              /* 1..R4P_ROUNDS */
    int slot;                               /* 0..R4P_SLOTS-1 */
    float lux_last[R4P_SLOTS];
    uint32_t value_raw[R4P_SLOTS];          /* lux × 1000 của vòng gần nhất */
    uint32_t result[R4P_SLOTS][R4P_ROUNDS]; /* 0..3000 */
    uint32_t average[R4P_SLOTS];
    bool positive[R4P_SLOTS];
    bool sensor_ok[R4P_SLOTS];
    uint16_t calib_value;
    int calib_slot;
    esp_err_t err;
    int64_t started_us, finished_us;
} measure_result_t;

typedef void (*measure_progress_cb_t)(const measure_result_t *r, measure_phase_t phase, void *ctx);

/* sensor_bus + LED + begin() từng TCS. Lỗi bus → trả lỗi nhưng module vẫn sống (đo sẽ
 * báo MEASURE_PHASE_ERROR) để UI/OTA còn chạy. */
esp_err_t measure_init(void);
int measure_sensors_alive(void);
void measure_set_progress_cb(measure_progress_cb_t cb, void *ctx);

esp_err_t measure_start(r4p_sick_t sick, r4p_sample_t sample);
esp_err_t measure_calib_start(int slot);
esp_err_t measure_abort(void);
bool measure_busy(void);
const measure_result_t *measure_last(void);

#ifdef __cplusplus
}
#endif
