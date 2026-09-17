/**
 * opus_codec.h — wrap libopus encoder (mic) + decoder (speaker).
 *
 * Frame layout khớp envelope_send_hello():
 *   - Encoder: 24 kHz mono, 60 ms mic uplink = 1440 samples.
 *   - Decoder buffer: 24 kHz mono, tối đa 60 ms = 1440 samples.
 *   - Server TTS downlink phải là CELT wideband 20 ms / 1 frame / packet để tránh
 *     micro-opus overflow trên ESP32-P4.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Server hello: sample_rate=24000 mono — both encode + decode tại 24kHz.
 * Lý do: ES8311 + I2S full-duplex chạy 1 clock duy nhất 24kHz, không thể
 * mic 16kHz + spk 24kHz riêng. */
#define OPUS_ENC_SAMPLE_RATE   24000
#define OPUS_ENC_FRAME_SAMPLES 1440  /* 60 ms @ 24 kHz */
#define OPUS_DEC_SAMPLE_RATE   24000
#define OPUS_DEC_FRAME_SAMPLES 1440  /* max 60 ms buffer; server sends 20 ms packets */
#define OPUS_MAX_PACKET_BYTES  1275  /* max Opus packet bytes */

esp_err_t opus_codec_init(void);
/* Giải mã 1 gói CELT im lặng ngay trong task gọi — đi qua ALLOC_STACK/PUSH của
 * pseudostack TLS. Trả số mẫu (>0) nếu OK. Dùng để bắt sớm lỗi TLS trên P4
 * (xem app_main.c g_vimate_tls_anchor) thay vì chờ gói TTS thật rồi panic. */
int opus_codec_decode_selftest(void);
void opus_codec_reset_decoder(void);

/* Encode 1440-sample PCM16 → opus bytes (max OPUS_MAX_PACKET_BYTES).
 * Trả về số bytes encoded hoặc -1 nếu lỗi. */
int opus_codec_encode(const int16_t *pcm_in, uint8_t *opus_out, size_t out_cap);

/* Decode opus packet → tối đa OPUS_DEC_FRAME_SAMPLES PCM16.
 * Trả về số sample decoded hoặc -1 nếu lỗi. */
int opus_codec_decode(const uint8_t *opus_in, size_t in_len,
                      int16_t *pcm_out, size_t out_cap_samples);

#ifdef __cplusplus
}
#endif
