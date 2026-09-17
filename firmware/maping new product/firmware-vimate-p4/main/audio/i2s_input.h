/**
 * i2s_input.h — ES8311 mic capture qua I2S std mode.
 * Trả về PCM16 mono 16 kHz vào ring buffer cho encoder.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2s_input_init(void);
/* Đọc tối đa max_samples PCM16 vào dst; trả về số sample thực đọc.
 * Block tối đa timeout_ms. */
int i2s_input_read_pcm16(int16_t *dst, size_t max_samples, uint32_t timeout_ms);
void i2s_input_start(void);
void i2s_input_stop(void);
/* Tần số lấy mẫu thực tế của mic (Hz) — bộ ghi âm dùng cho WAV header. */
int i2s_input_sample_rate(void);

#ifdef __cplusplus
}
#endif
