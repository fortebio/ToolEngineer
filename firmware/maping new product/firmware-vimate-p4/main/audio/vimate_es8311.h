/**
 * es8311_codec.h — ES8311 audio codec init (I2C + DAC + PA enable).
 *
 * Without this, I2S TX → ES8311 → mute (codec defaults all DAC/ADC OFF).
 * MUST call BEFORE i2s_output_init() so codec is ready when audio frames arrive.
 *
 * Pin map (board ES3N28P-LCD-2.8):
 *   I2C SCL=15 SDA=16  (shared with FT6236G touch)
 *   I2S MCLK=4 BCLK=5 WS=7 DIN=6 DOUT=8
 *   PA enable=GPIO1 (active HIGH → bật loa)
 *   Codec I2C addr=0x18
 */
#pragma once
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t es8311_codec_init(void);
void es8311_codec_set_volume(int percent);   /* 0-100 */
void es8311_codec_set_mic_gain(int percent); /* 0-100 */
void es8311_codec_mute(bool mute);

/* Audio I/O qua esp_codec_dev wrapper — đúng pattern xiaozhi. */
int  es8311_codec_write_pcm16(const int16_t *pcm, size_t samples);
int  es8311_codec_read_pcm16(int16_t *dst, size_t samples);
/* Đọc thẳng 2 kênh mic [m0,m1,m0,m1,...] (KHÔNG downmix) cho AFE 2-mic.
 * dst chứa frames*2 mẫu int16. Chỉ board ES7210; board khác trả 0. */
int  es8311_codec_read_stereo_pcm16(int16_t *dst, size_t frames);
/* Số kênh int16 mỗi frame mà es8311_codec_read_frames() trả (2, hoặc 4 khi ES7210 TDM). */
int  es8311_codec_in_channels(void);
/* Đọc `frames` frame thô interleaved, mỗi frame es8311_codec_in_channels() int16. */
int  es8311_codec_read_frames(int16_t *dst, size_t frames);
void es8311_codec_enable_input(bool enable);
/* Tần số lấy mẫu THỰC TẾ của đường mic (ADC). Board ES8311 duplex dùng CHUNG clock
 * với DAC nên ADC = BOARD_SPK_SAMPLE_RATE (24kHz), KHÔNG phải BOARD_MIC_SAMPLE_RATE.
 * Bộ ghi âm phải ghi WAV header theo giá trị này, nếu không file phát sai tốc độ. */
int  es8311_codec_input_sample_rate(void);

/* Handles for legacy compat (i2s_input/output forward calls here). */
esp_codec_dev_handle_t es8311_get_dev(void);
i2s_chan_handle_t es8311_get_tx(void);
i2s_chan_handle_t es8311_get_rx(void);

#ifdef __cplusplus
}
#endif
