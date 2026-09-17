/**
 * tcs34725.h — cảm biến màu TCS34725 + auto-gain + tính lux DN40.
 *
 * Port từ FBT-ReaderPlus-1.0/src/tcs.h (lớp tcs34725 bọc Adafruit_TCS34725 1.4.2):
 *   - bảng AGC 5 bậc dim→bright: 60x/614 ms, 60x/154, 16x/154, 4x/154, 1x/154 với
 *     ngưỡng min/max của kênh CLEAR (trễ chống nhảy);
 *   - mỗi lần getData chỉnh TỐI ĐA MỘT bậc rồi đọc lại (đúng như bản gốc);
 *   - lux = (0.146·R' + 1.000·G' − 0.454·B') / cpl, cpl = atime_ms·gain/(GA·DF), GA=1,
 *     DF=320, R'/G'/B' đã trừ IR = (R+G+B−C)/2.
 *
 * KHÁC bản gốc (lỗi tiềm ẩn ở ReaderPlus, MAPPING §3.5): bản gốc dùng MỘT object cho
 * cả 4 slot nên trạng thái AGC (gain/atime đang tin) lệch với thanh ghi thật của từng
 * sensor sau mux → cpl sai bậc. Ở đây mỗi slot có tcs34725_t riêng, caller chọn kênh
 * mux trước rồi gọi hàm với đúng struct của slot đó.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int agc;                 /* bậc AGC hiện tại (0..4) */
    uint8_t atime;           /* thanh ghi ATIME đang đặt */
    uint16_t atime_ms;       /* (256 - atime) * 2.4 */
    uint16_t againx;         /* 1/4/16/60 */
    uint16_t r, g, b, c;     /* raw lần đọc cuối */
    uint16_t ir;
    float lux;
    bool saturated;
    bool available;          /* begin() thấy chip */
} tcs34725_t;

esp_err_t tcs34725_read_id(uint8_t *id);
/* Đặt AGC bậc 0 + bật chip. Mux PHẢI đã chọn đúng kênh. */
esp_err_t tcs34725_begin(tcs34725_t *t);
/* Đọc raw (chờ đủ 1 chu kỳ tích phân), tự chỉnh AGC 1 bậc, tính lux. */
esp_err_t tcs34725_get_data(tcs34725_t *t);
esp_err_t tcs34725_read_raw(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c);

#ifdef __cplusplus
}
#endif
