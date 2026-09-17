/**
 * i2s_output.c — Thin wrapper, delegates đến vimate_es8311 esp_codec_dev wrapper.
 *
 * Lý do thay vì gọi i2s_channel_write trực tiếp:
 * ES8311 codec cần esp_codec_dev_open() set sample rate + esp_codec_dev_set_out_vol().
 * Nếu chỉ write I2S, codec chưa configure → DAC silent. Đây là pattern xiaozhi
 * verified work (firmware/main/audio/codecs/es8311_audio_codec.cc).
 */
#include "i2s_output.h"
#include "vimate_es8311.h"
#include "vimate.h"
#include "boards/board.h"

esp_err_t i2s_output_init(void) {
    /* No-op — es8311_codec_init() đã setup full-duplex I2S + codec_dev. */
    return ESP_OK;
}

void i2s_output_start(void) {
    es8311_codec_mute(false);   /* bật PA */
}

void i2s_output_stop(void) {
    es8311_codec_mute(true);
}

void i2s_output_set_volume(int percent) {
    es8311_codec_set_volume(percent);
}

int i2s_output_write_pcm16(const int16_t *src, size_t samples, uint32_t timeout_ms) {
    (void)timeout_ms;   /* esp_codec_dev_write tự manage timeout internally */
    return es8311_codec_write_pcm16(src, samples);
}

i2s_chan_handle_t i2s_output_get_rx(void) {
    return es8311_get_rx();
}
