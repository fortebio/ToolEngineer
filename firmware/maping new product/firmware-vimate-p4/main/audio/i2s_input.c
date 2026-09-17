/**
 * i2s_input.c — Thin wrapper, delegates đến vimate_es8311 esp_codec_dev wrapper.
 */
#include "i2s_input.h"
#include "vimate_es8311.h"
#include "vimate.h"
#include "boards/board.h"

esp_err_t i2s_input_init(void) {
    /* No-op — es8311_codec_init() đã setup. */
    return ESP_OK;
}

void i2s_input_start(void) {
    es8311_codec_enable_input(true);
}

void i2s_input_stop(void) {
    es8311_codec_enable_input(false);
}

int i2s_input_read_pcm16(int16_t *dst, size_t max_samples, uint32_t timeout_ms) {
    (void)timeout_ms;
    return es8311_codec_read_pcm16(dst, max_samples);
}

int i2s_input_sample_rate(void) {
    return es8311_codec_input_sample_rate();
}
