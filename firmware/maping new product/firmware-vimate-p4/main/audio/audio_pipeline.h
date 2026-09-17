/**
 * audio_pipeline.h — Mic → Opus encode → WS upstream
 *                    WS downstream → Opus decode → Speaker
 *
 * Lifecycle:
 *   audio_pipeline_init()         — setup I2S in + out, opus enc/dec, queues
 *   audio_pipeline_mic_start()    — bật mic capture task (gọi khi listen=start)
 *   audio_pipeline_mic_stop()     — pause capture
 *   audio_pipeline_speaker_start()— bật decoder task (gọi khi tts=start)
 *   audio_pipeline_speaker_stop() — drain + pause decoder
 *   audio_pipeline_speaker_push(data,len) — feed opus frame từ WS binary
 *
 * Format khớp envelope_send_hello():
 *   - Upstream: 24 kHz mono Opus, 60 ms frame (~1440 samples → ~120-160 bytes encoded)
 *   - Downstream: 24 kHz mono Opus, 60 ms frame
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_pipeline_init(void);

void audio_pipeline_mic_start(void);
void audio_pipeline_mic_stop(void);
/* HARD-PAUSE từ app phụ huynh: paused=true → dừng TTS+mic+wake (im+điếc); false →
 * bật lại wake. Đường điều khiển tin cậy không phụ thuộc mic/touch on-device. */
void audio_pipeline_set_paused(bool paused);
bool audio_pipeline_is_paused(void);
uint32_t audio_pipeline_mic_streamed_frames(void);
/* Mute SEND (discard PCM, vẫn drain i2s) — dùng trong lúc TTS phát để
 * tránh echo bleed mà ko phải restart opus encoder. */
void audio_pipeline_mic_mute(bool mute);

/* Báo ảnh bài học đang tải/render trong ~window_ms tới. mic_task sẽ HOÃN gửi
 * audio (chỉ ở đầu cửa sổ nghe, khi chưa có speech) để 2 phiên TLS (audio +
 * image download) không tranh RAM gây rớt WS/OOM. Cờ tự hết hạn (timestamp) nên
 * không bao giờ kẹt; mic luôn stream lại trước ngưỡng no-speech → không gây PCM=0.
 * window_ms <= 0 = xoá cờ ngay. */
void audio_pipeline_note_image_busy(int window_ms);

/* WakeNet idle detection ("Hi Lily"). Wake mode keeps the mic input running
 * locally but does not stream audio to the server until a wake word fires. */
void audio_pipeline_wake_start(void);
void audio_pipeline_wake_stop(void);
bool audio_pipeline_wake_available(void);

void audio_pipeline_speaker_start(void);
void audio_pipeline_speaker_stop(void);
bool audio_pipeline_speaker_is_active(void);
/* Còn audio CHƯA PHÁT trong queue loa? "tts stop" bình thường phải đợi hết
 * (server burst TTS cache → stop tới sớm; stop ngay = xả queue = cụt đuôi câu).
 * Abort/barge-in vẫn gọi speaker_stop() thẳng (xả ngay — chủ đích). */
bool audio_pipeline_speaker_pending(void);
void audio_pipeline_speaker_push(const uint8_t *opus_frame, size_t len);

void audio_pipeline_set_volume(int percent); /* 0-100, áp vào digital gain trước I2S */
void audio_pipeline_set_mic_gain(int percent); /* 0-100, map sang ES8311 input gain */
void audio_pipeline_set_vad_config(int sensitivity, int silence_ms); /* sensitivity 0-100 */
void audio_pipeline_set_noise_profile(const char *environment,
                                      const char *speaker_distance,
                                      int noise_level,
                                      int speech_level,
                                      bool aec_enabled,
                                      bool dual_mic_enabled);
void audio_pipeline_apply_audio_config(int mic_gain_percent,
                                       int vad_sensitivity,
                                       int vad_silence_ms,
                                       const char *environment,
                                       const char *speaker_distance,
                                       int noise_level,
                                       int speech_level,
                                       bool aec_enabled,
                                       bool dual_mic_enabled);
void audio_pipeline_speaker_test_tone(void);
/* Bật/tắt bộ ghi PCM chẩn đoán (WD: dump qua UART, chỉ bản diag). Mặc định TẮT — mỗi
 * lần dump ~22 s UART kín. Bật trước khi cần ghi; tự kích theo rms/Mic ON như trước. */
void audio_pipeline_capture_enable(bool on);

#ifdef __cplusplus
}
#endif
