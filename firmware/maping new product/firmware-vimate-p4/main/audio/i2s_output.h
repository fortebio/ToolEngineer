/**
 * i2s_output.h — ES8311 speaker output qua I2S std mode.
 * Nhận PCM16 mono 24 kHz và write trực tiếp lên I2S TX.
 */
#pragma once
#include "esp_err.h"
#include "driver/i2s_std.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2s_output_init(void);
int i2s_output_write_pcm16(const int16_t *src, size_t samples, uint32_t timeout_ms);
void i2s_output_start(void);
void i2s_output_stop(void);
void i2s_output_set_volume(int percent); /* 0-100 */
/* Returns RX channel của full-duplex I2S — i2s_input.c dùng để read mic. */
i2s_chan_handle_t i2s_output_get_rx(void);

#ifdef __cplusplus
}
#endif
