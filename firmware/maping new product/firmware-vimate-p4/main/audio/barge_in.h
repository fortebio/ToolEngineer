/**
 * barge_in.h — Voice barge-in (ngắt lời) scaffold. MẶC ĐỊNH TẮT.
 *
 * Mục tiêu: phát hiện TRẺ NÓI trong lúc robot đang phát TTS để ngắt lượt nói của
 * robot (giống trợ lý thật). Vì audio chạy full-duplex (mic nghe cả tiếng loa),
 * phải khử echo loa khỏi mic TRƯỚC khi VAD — nếu không VAD sẽ trigger trên CHÍNH
 * tiếng robot. Dùng ESP-SR AEC (Acoustic Echo Cancellation).
 *
 * Pipeline khi bật:
 *   spk_task  → barge_in_feed_ref(pcm_loa@24k)   (tín hiệu tham chiếu = đang phát)
 *   mic_task  → barge_in_feed_mic(pcm_mic@24k)    (đọc mic TRONG lúc phát)
 *               → resample 24k→16k → AEC(mic, ref) → RMS → đủ frame liên tục → trigger
 *   barge_in_triggered() == true → app_main: envelope_send_abort()+speaker_stop()+listen
 *
 * LƯU Ý QUAN TRỌNG (cần tinh chỉnh TRÊN THIẾT BỊ THẬT, có loa + mic + thẻ):
 *   - Ngưỡng RMS, số frame liên tục, độ trễ ref (echo delay) đều là tham số Kconfig.
 *   - AEC chỉ hỗ trợ 16kHz nên có bước resample 24→16k (thô, có thể thay bằng
 *     esp-dsp resampler chất lượng cao hơn nếu cần).
 *   - Khi CONFIG tắt: toàn bộ API là no-op inline → KHÔNG ảnh hưởng đường chạy
 *     hiện tại (không hồi quy), không tốn RAM/CPU.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_VIMATE_BARGE_IN_ENABLE

/* Khởi tạo AEC + buffer (gọi 1 lần lúc audio_pipeline_init). An toàn nếu fail
 * (chỉ log; barge-in sẽ no-op). */
void barge_in_init(void);

/* Reset trạng thái phát hiện cho một lượt phát mới (gọi ở speaker_start). */
void barge_in_reset(void);

/* spk_task gọi với PCM @24kHz vừa ghi ra loa — làm tín hiệu tham chiếu cho AEC. */
void barge_in_feed_ref(const int16_t *pcm24, int n);

/* mic_task gọi (khi đang phát) với PCM mic @24kHz — chạy AEC + VAD. */
void barge_in_feed_mic(const int16_t *pcm24, int n);

/* true khi đã phát hiện trẻ nói liên tục đủ ngưỡng → cần ngắt TTS. */
bool barge_in_triggered(void);

#else /* CONFIG tắt → no-op, zero overhead */

static inline void barge_in_init(void) {}
static inline void barge_in_reset(void) {}
static inline void barge_in_feed_ref(const int16_t *pcm24, int n) { (void)pcm24; (void)n; }
static inline void barge_in_feed_mic(const int16_t *pcm24, int n) { (void)pcm24; (void)n; }
static inline bool barge_in_triggered(void) { return false; }

#endif /* CONFIG_VIMATE_BARGE_IN_ENABLE */

#ifdef __cplusplus
}
#endif
