/**
 * audio_afe.h — Espressif ESP-SR AFE (Audio Front End) cho genu-v6 2-mic.
 *
 * Pipeline: [mic0, mic1, ref_loa] interleaved 16kHz → AFE feed → fetch ra MONO
 * đã AEC (khử vọng) + SE/BSS (beamforming 2 mic) + NS (khử ồn) + VAD + WakeNet.
 * Thay đường WakeNet/RMS-VAD/downmix thủ công.
 *
 * Bước 2 (hiện tại): chỉ TẠO handle + đo RAM gate (ép PSRAM). Feed/fetch nối ở
 * bước sau. RAM nội board này rất căng → audio_afe_init log cost để go/no-go.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tạo AFE (AEC+SE+NS+VAD+WakeNet, memory MORE_PSRAM). Log feed/fetch chunk +
 * cost RAM nội/psram. Trả ESP_OK nếu tạo được, ESP_ERR_NO_MEM nếu thiếu RAM. */
esp_err_t audio_afe_init(void);
bool audio_afe_ready(void);

/* Kích thước để bên gọi cấp buffer feed/fetch. */
int audio_afe_feed_chunk(void);     /* mẫu/kênh mỗi lần feed */
int audio_afe_feed_channels(void);  /* số kênh feed (=3 cho "MMR") */
int audio_afe_fetch_chunk(void);    /* mẫu mỗi lần fetch (mono 16k) */

typedef struct {
    const int16_t *pcm16;  /* mono 16k ĐÃ SẠCH (AEC+BSS+NS) — AFE sở hữu, dùng ngay */
    int  samples;          /* số mẫu trong pcm16 */
    bool wake;             /* wake word phát hiện ở fetch này */
    bool vad_speech;       /* VAD = speech */
} audio_afe_result_t;

/* Feed 1 frame 2 mic 24k interleaved [m0,m1,...] (frames mẫu/kênh). Tự resample
 * 24→16, gom + feed AFE. Gọi từ feed task. */
void audio_afe_feed_mic24(const int16_t *stereo24, int frames);
/* Feed 1 frame `nch` kênh 24k interleaved (2 = [m0,m1] + ref mềm từ ring; 4 = TDM
 * [.. theo BOARD_AUDIO_AFE_FORMAT ..] có ref CỨNG, ring bị bỏ qua). */
void audio_afe_feed_frames24(const int16_t *pcm24, int frames, int nch);
/* Đẩy PCM loa 24k mono làm tham chiếu AEC (resample 24→16 vào ring). Gọi từ
 * spk task. Rỗng (không phát) → AEC tự nuốt zeros. */
void audio_afe_push_ref24(const int16_t *mono24, int n);
/* Drain 1 kết quả fetch (block tới ~2s nội bộ). true nếu có. Gọi từ fetch task. */
bool audio_afe_fetch(audio_afe_result_t *o);
/* Bật/tắt AEC lúc chạy (khối nặng nhất) — tắt khi loa không phát. */
void audio_afe_set_aec(bool on);

#ifdef __cplusplus
}
#endif
