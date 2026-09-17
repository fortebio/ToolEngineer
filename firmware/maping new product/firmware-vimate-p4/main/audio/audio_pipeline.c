/**
 * audio_pipeline.c — orchestrate I2S + Opus + WS direction.
 *
 * Upstream (mic → ws):
 *   Task mic_task: i2s_input_read_pcm16 → opus_encode → ws_client_send_binary
 *   Chạy khi s_mic_active = true. Frame 60 ms, ưu tiên cao nhất trong app.
 *
 * Downstream (ws → speaker):
 *   ws callback → audio_pipeline_speaker_push() → push vào s_spk_queue
 *   Task spk_task: nhận từ queue → opus_decode → i2s_output_write_pcm16
 *   Tách task giải nén để không block ws receive task.
 */
#include "audio_pipeline.h"
#include "actuator/servo_emotion.h"
#include "i2s_input.h"
#include "i2s_output.h"
#include "opus_codec.h"
#include "barge_in.h"
#include "vimate_es8311.h"
#include "audio_afe.h"
#include "vimate.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "network/ws_client.h"
#include "esp_dsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <math.h>
#if CONFIG_VIMATE_WAKE_WORD_ENABLE
#include "model_path.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

/* ===== VAD (Voice Activity Detection) ===== *
 * Energy-based: tính RMS của PCM frame 60ms, so với threshold để phát hiện
 * speech vs silence. Khi user nói xong (silence ≥1.5s) tự động gửi
 * listen_stop → server STT process → reply. Bản deploy chưa có wake word
 * Alexa, nên VAD chỉ kết thúc lượt push-to-talk hiện tại; lượt mới cần tap/nút.
 *
 * Threshold calibrated cho ES8311 mic gain mặc định + môi trường nhà 25-40dB
 * background. Chỉnh `VAD_RMS_THRESHOLD` nếu quá nhạy / quá điếc.
 */
#define VAD_RMS_THRESHOLD_DEFAULT       160     /* int16 RMS — đủ nhạy cho giọng trẻ/ba mẹ ở 70-80% mic gain */
#ifndef BOARD_MIC_VAD_RMS_MIN
#define BOARD_MIC_VAD_RMS_MIN           50
#endif
#define VAD_SPEECH_FRAMES_TO_START      1       /* gửi từ frame speech đầu tiên, tránh mất âm đầu */
#define VAD_SILENCE_MS_TO_STOP_DEFAULT  550     /* 0.55s im lặng → end-of-turn; khớp default server (clampSavedVADSilence) cho phản hồi nhanh */
#define VAD_MIN_LISTEN_MS_BEFORE_EOT     1800    /* chừa thời gian nói câu hỏi sau wake word */
#define VAD_NO_SPEECH_MS_TO_STOP        6000    /* chờ giọng đầu lâu hơn sau câu hỏi/TTS */
#define VAD_FRAME_MS                    60      /* khớp OPUS_ENC_FRAME (1440 samples @24kHz) */
#define MIC_RENDER_DEFER_MAX_MS         2500    /* trần hoãn-gửi khi đang render ảnh; PHẢI < VAD_NO_SPEECH_MS_TO_STOP để không gây PCM=0 */
#define SPK_PREBUFFER_FRAMES            5       /* tích đủ 5 frame (~300ms) rồi mới phát → đệm jitter wifi + tranh CPU với AFE, hết underrun/giật */
#define SPK_PREBUFFER_MAX_MS            300     /* trần chờ đệm: câu ngắn (< prebuffer) không bị kẹt */
#define SPK_FIRST_FRAME_TIMEOUT_MS       50000   /* provider treo trước audio: tự trả WakeNet */
#define SPK_IDLE_TIMEOUT_MS              8000    /* server thiếu tts_stop sau frame cuối */
#define WAKE_DOWNSAMPLE_MAX_SAMPLES     ((OPUS_ENC_FRAME_SAMPLES * 2 / 3) + 4)
#ifndef BOARD_SPK_TTS_GAIN_PCT
#define BOARD_SPK_TTS_GAIN_PCT          100
#endif
#ifndef BOARD_SPK_TTS_LIMIT
#define BOARD_SPK_TTS_LIMIT             32767
#endif
#if CONFIG_MIC_HIGH_PASS_FILTER_ENABLE
#define MIC_HPF_CUTOFF_HZ               ((float)CONFIG_MIC_HIGH_PASS_FILTER_CUTOFF_HZ)
#define MIC_HPF_GAIN_SCALE              ((float)CONFIG_MIC_HIGH_PASS_FILTER_GAIN / 100.0f)
#define MIC_HPF_Q_BUTTERWORTH           0.70710678f
#define MIC_DSP_CHUNK_SAMPLES           240     /* 10ms @24kHz, giữ stack thấp trước Opus */
#endif

typedef struct {
    uint8_t *data;
    size_t   len;
    bool     is_pcm;
} opus_pkt_t;

static const uint8_t SPK_PCM_MAGIC[4] = {'P', 'C', 'M', '1'};

static QueueHandle_t s_spk_queue = NULL;
static volatile bool s_pipeline_ready = false;
static volatile bool s_mic_active = false;
/* s_paused: HARD-PAUSE từ app phụ huynh (lệnh "pause"). Khi true: dừng TTS + mic +
 * wake → thiết bị IM + ĐIẾC hoàn toàn (hết tự-nói do ồn nền) cho tới lệnh "resume".
 * Đường tắt TIN CẬY vì không phụ thuộc mic/touch on-device (touch chết, wake hỏng). */
static volatile bool s_paused = false;
static volatile bool s_spk_active = false;
#if CONFIG_VIMATE_BARGE_IN_ENABLE
static volatile bool s_barge_listen = false; /* mic nghe barge-in trong lúc phát */
#endif
static volatile bool s_wake_active = false;
static volatile bool s_spk_prebuffering = false; /* đầu lượt phát: chờ đệm trước khi rút queue */
static volatile int64_t s_spk_start_us = 0;      /* mốc bắt đầu lượt phát (trần prebuffer) */
static volatile int64_t s_spk_last_frame_us = 0;
static volatile uint32_t s_spk_received_frames = 0;
static volatile uint32_t s_mic_streamed_frames = 0;
/* Mốc thời gian (us) tới khi ảnh bài học còn đang tải/render — mic_task hoãn
 * gửi audio trước mốc này để tránh 2 phiên TLS tranh RAM. Auto-expiry. */
static volatile int64_t s_img_busy_until_us = 0;
/* Mute send mà KHÔNG stop i2s — discard PCM frame trong lúc TTS phát để
 * tránh echo bleed về server, đồng thời tránh restart i2s+opus codec
 * (silk burg LPC hang nếu encoder restart trên frame transient). */
static volatile bool s_mic_send_muted = false;
static volatile int s_vad_rms_threshold = VAD_RMS_THRESHOLD_DEFAULT;
static volatile int s_vad_silence_ms_to_stop = VAD_SILENCE_MS_TO_STOP_DEFAULT;
static volatile int s_vad_sensitivity = 80;
static volatile int s_audio_noise_level = 45;
static volatile int s_audio_speech_level = 65;
static volatile bool s_audio_aec_enabled = false;
static volatile bool s_audio_dual_mic_enabled = false;
static char s_audio_environment[16] = "normal";
static char s_audio_speaker_distance[16] = "normal";
#if CONFIG_MIC_HIGH_PASS_FILTER_ENABLE
static float s_mic_hpf_coeffs[5];
static float s_mic_hpf_w[2];
static bool s_mic_hpf_ready = false;
#endif

/* VAD state — mic_task only, no concurrent access. */
static bool s_vad_in_speech    = false;
static int  s_vad_speech_count = 0;
static int  s_vad_silence_ms   = 0;
static int  s_vad_listen_ms    = 0;
static uint32_t s_vad_peak_rms = 0;
static bool s_vad_stop_signaled = false;
/* s_turn_had_speech — đã từng có speech trong LƯỢT nghe này. Dùng để KHÔNG gửi
 * audio ambient TRƯỚC khi có giọng (chặn non-speech lên ASR → hết ảo giác), nhưng
 * SAU khi đã có giọng thì gửi cả frame im-lặng-xen-giữa-câu (không cắt cụt câu). */
static bool s_turn_had_speech = false;

#if CONFIG_VIMATE_WAKE_WORD_ENABLE
static srmodel_list_t *s_wake_models = NULL;
static esp_wn_iface_t *s_wake_iface = NULL;
static model_iface_data_t *s_wake_data = NULL;
static int16_t *s_wake_buf = NULL;
static uint32_t s_wake_det_count = 0;   /* tổng số lần WakeNet trả >0 (diag) */
static int s_wake_buf_len = 0;
static int s_wake_buf_cap = 0;
static int s_wake_chunk_samples = 0;
static bool s_wake_ready = false;
static int64_t s_wake_diag_next_us = 0;
#endif

static inline bool pipeline_ready(void) {
    return s_pipeline_ready && s_spk_queue != NULL;
}

static int clamp_percent(int v, int fallback) {
    if (v < 0) v = fallback;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

static int compute_vad_threshold(int sensitivity) {
    sensitivity = clamp_percent(sensitivity, 80);
    int threshold = 320 - (sensitivity * 3);

    int noise = s_audio_noise_level;
    int speech = s_audio_speech_level;
    if (noise < 0) noise = 45;
    if (noise > 100) noise = 100;
    if (speech < 0) speech = 65;
    if (speech > 100) speech = 100;

    /* Noise cao cần ngưỡng cao hơn để không bắt tiếng nền. Giọng yếu/xa cần
     * ngưỡng thấp hơn để không bị "điếc". Đây là lớp tuning runtime; HPF vẫn
     * xử lý tín hiệu ở tầng DSP trước khi VAD chạy. */
    threshold += (noise - 45) / 2;
    if (speech < 65) {
        threshold -= (65 - speech) / 2;
    } else if (speech > 82) {
        threshold += (speech - 82) / 3;
    }
    if (strcmp(s_audio_environment, "noisy") == 0) {
        threshold += 22;
    } else if (strcmp(s_audio_environment, "quiet") == 0) {
        threshold -= 10;
    } else if (strcmp(s_audio_environment, "child_soft") == 0) {
        threshold -= 26;
    }
    if (strcmp(s_audio_speaker_distance, "far") == 0) {
        threshold -= 24;
    } else if (strcmp(s_audio_speaker_distance, "near") == 0) {
        threshold += 14;
    }
    if (threshold < BOARD_MIC_VAD_RMS_MIN) threshold = BOARD_MIC_VAD_RMS_MIN;
    if (threshold < 50) threshold = 50;
    if (threshold > 520) threshold = 520;
    return threshold;
}

static void apply_speaker_tts_headroom(int16_t *pcm, int samples) {
#if BOARD_SPK_TTS_GAIN_PCT < 100 || BOARD_SPK_TTS_LIMIT < 32767
    if (!pcm || samples <= 0) return;
    int peak_in = 0;
    int peak_out = 0;
    int limited = 0;
    for (int i = 0; i < samples; i++) {
        int32_t v = pcm[i];
        int a = v < 0 ? (int)-v : (int)v;
        if (a > peak_in) peak_in = a;

        v = (v * BOARD_SPK_TTS_GAIN_PCT) / 100;
        if (v > BOARD_SPK_TTS_LIMIT) {
            v = BOARD_SPK_TTS_LIMIT;
            limited++;
        } else if (v < -BOARD_SPK_TTS_LIMIT) {
            v = -BOARD_SPK_TTS_LIMIT;
            limited++;
        }

        int b = v < 0 ? (int)-v : (int)v;
        if (b > peak_out) peak_out = b;
        pcm[i] = (int16_t)v;
    }

    static uint32_t log_count = 0;
    log_count++;
    if (limited > 0 || peak_in > 30000 || log_count <= 3 || (log_count % 100) == 0) {
        ESP_LOGI(TAG_AUDIO,
                 "TTS headroom gain=%d%% limit=%d peak=%d->%d limited=%d",
                 BOARD_SPK_TTS_GAIN_PCT, BOARD_SPK_TTS_LIMIT,
                 peak_in, peak_out, limited);
    }
#else
    (void)pcm;
    (void)samples;
#endif
}

void audio_pipeline_speaker_test_tone(void) {
    if (!pipeline_ready()) {
        ESP_LOGW(TAG_AUDIO, "Speaker test ignored: audio pipeline not ready");
        return;
    }
    enum {
        tone_hz = 880,
        sample_rate = BOARD_SPK_SAMPLE_RATE,
        chunk_samples = 240,
        total_ms = 650,
        amplitude = 16000,
    };
    int16_t pcm[chunk_samples];
    int phase = 0;
    const int chunks = (sample_rate * total_ms / 1000) / chunk_samples;
    const bool was_active = s_spk_active;

    audio_pipeline_wake_stop();
    i2s_output_start();
    for (int c = 0; c < chunks; c++) {
        for (int i = 0; i < chunk_samples; i++) {
            pcm[i] = (phase < sample_rate / 2) ? amplitude : -amplitude;
            phase += tone_hz;
            if (phase >= sample_rate) phase -= sample_rate;
        }
        int written = i2s_output_write_pcm16(pcm, chunk_samples, 500);
        if (written != chunk_samples) {
            ESP_LOGW(TAG_AUDIO, "Speaker test short write: %d/%d",
                     written, chunk_samples);
            break;
        }
    }
    if (!was_active) {
        i2s_output_stop();
        audio_pipeline_wake_start();
    }
    ESP_LOGI(TAG_AUDIO, "Speaker test tone done");
}

static void vad_reset(void) {
    s_vad_in_speech    = false;
    s_vad_speech_count = 0;
    s_vad_silence_ms   = 0;
    s_vad_listen_ms    = 0;
    s_vad_peak_rms     = 0;
    s_vad_stop_signaled = false;
    s_turn_had_speech  = false;
}

static uint32_t isqrt_u64(uint64_t x) {
    uint64_t op = x;
    uint64_t res = 0;
    uint64_t one = (uint64_t)1 << 62;
    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint32_t)res;
}

static uint32_t mic_frame_rms(const int16_t *pcm, int n) {
    int64_t sum_sq = 0;
    for (int i = 0; i < n; i++) {
        int32_t s = pcm[i];
        sum_sq += (int64_t)s * s;
    }
    return isqrt_u64((uint64_t)(sum_sq / (n > 0 ? n : 1)));
}

static bool vad_frame_is_speech(const int16_t *pcm, int n) {
    uint32_t rms = mic_frame_rms(pcm, n);
    if (rms > s_vad_peak_rms) s_vad_peak_rms = rms;
    return rms > (uint32_t)s_vad_rms_threshold;
}

/* VOICE-ACTIVATED (đường mic thô): mở lượt nghe khi nghe trẻ nói GẦN/RÕ. Ngưỡng
 * CAO hơn VAD (×5) + cần vài frame liên tục → bỏ qua ồn nền/TV. Server còn RMS-gate
 * làm lưới chặn cuối nên đặt vừa phải, ưu tiên bé kích hoạt được. */
#define VOICE_ACTIVATED_ENABLE 0 /* 0 = CHỈ "Hi Lily" (WakeNet) mới dậy (user yêu
                                  * cầu 13/06: bất kỳ giọng cũng tự bật → phiền).
                                  * 1 = thêm tự-mở-lượt theo năng lượng giọng. */
#define VA_RMS_MULT          5   /* ngưỡng VA = 5× ngưỡng VAD (mặc định 160→800) */
#define VA_FRAMES_TO_START   4   /* ~0.24s nói liên tục mới mở lượt (4×60ms) */

#if CONFIG_MIC_HIGH_PASS_FILTER_ENABLE
static void mic_hpf_reset(void) {
    s_mic_hpf_w[0] = 0.0f;
    s_mic_hpf_w[1] = 0.0f;
}

static esp_err_t mic_hpf_init(void) {
    const float normalized = MIC_HPF_CUTOFF_HZ / (float)OPUS_ENC_SAMPLE_RATE;
    esp_err_t err = dsps_biquad_gen_hpf_f32(s_mic_hpf_coeffs, normalized,
                                            MIC_HPF_Q_BUTTERWORTH);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_AUDIO, "Mic HPF init failed: %s", esp_err_to_name(err));
        s_mic_hpf_ready = false;
        return err;
    }
    mic_hpf_reset();
    s_mic_hpf_ready = true;
    ESP_LOGI(TAG_AUDIO,
             "Mic HPF enabled: %.0fHz Butterworth biquad via ESP-DSP, gain=%d%%",
             MIC_HPF_CUTOFF_HZ, CONFIG_MIC_HIGH_PASS_FILTER_GAIN);
    return ESP_OK;
}

static inline int16_t f32_to_pcm16(float v) {
    if (v > 32767.0f) return INT16_MAX;
    if (v < -32768.0f) return INT16_MIN;
    return (int16_t)v;
}

/* ESP32-P4: esp-dsp 1.8.0 (va master upstream) chon dsps_biquad_f32_arp4 khi
 * CONFIG_DSP_OPTIMIZED. Vong loc trong .S dung, nhung epilogue tra ve
 * `mv a0, a6` — a6 chua bao gio duoc gan → esp_err_t la RAC. Ta kiem != ESP_OK
 * roi return TRUOC khi chep out→pcm, nen ca frame di qua nguyen ven: HPF thanh
 * passthrough dù log "Mic HPF enabled". Bat duoc 11/09/2026 bang self-test trong
 * mic_idle_probe (30Hz A=10000 ra nguyen 10000). Cac .S arp4 khac deu `li a0,0`,
 * chi hai file biquad (f32 + sf32) bi. Dung ban ANSI tren P4: 1440 mau/60ms la
 * khong dang ke, va khong phu thuoc esp-dsp sua khi nao. */
#if CONFIG_IDF_TARGET_ESP32P4
#define MIC_HPF_BIQUAD dsps_biquad_f32_ansi
#else
#define MIC_HPF_BIQUAD dsps_biquad_f32
#endif

static void mic_hpf_process_pcm16(int16_t *pcm, int n) {
    if (!s_mic_hpf_ready) return;
    float in[MIC_DSP_CHUNK_SAMPLES];
    float out[MIC_DSP_CHUNK_SAMPLES];
    for (int pos = 0; pos < n; pos += MIC_DSP_CHUNK_SAMPLES) {
        int chunk = n - pos;
        if (chunk > MIC_DSP_CHUNK_SAMPLES) chunk = MIC_DSP_CHUNK_SAMPLES;
        for (int i = 0; i < chunk; i++) {
            in[i] = (float)pcm[pos + i];
        }
        if (MIC_HPF_BIQUAD(in, out, chunk, s_mic_hpf_coeffs, s_mic_hpf_w) != ESP_OK) {
            return;
        }
        for (int i = 0; i < chunk; i++) {
            pcm[pos + i] = f32_to_pcm16(out[i] * MIC_HPF_GAIN_SCALE);
        }
    }
}
#else
static void mic_hpf_reset(void) {
}

static esp_err_t mic_hpf_init(void) {
    ESP_LOGI(TAG_AUDIO, "Mic HPF disabled");
    return ESP_OK;
}

static void mic_hpf_process_pcm16(int16_t *pcm, int n) {
    (void)pcm;
    (void)n;
}
#endif

#if CONFIG_VIMATE_WAKE_WORD_ENABLE
static void wake_reset(void) {
    s_wake_buf_len = 0;
}

static esp_err_t wake_init(void) {
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    s_wake_models = esp_srmodel_init("model");
    if (!s_wake_models || s_wake_models->num <= 0) {
        ESP_LOGW(TAG_AUDIO, "WakeNet disabled: no ESP-SR model partition found");
        return ESP_ERR_NOT_FOUND;
    }

    char *model_name = esp_srmodel_filter(s_wake_models, ESP_WN_PREFIX, NULL);
    if (!model_name) {
        ESP_LOGW(TAG_AUDIO, "WakeNet disabled: no WakeNet model selected");
        return ESP_ERR_NOT_FOUND;
    }

    s_wake_iface = (esp_wn_iface_t *)esp_wn_handle_from_name(model_name);
    if (!s_wake_iface) {
        ESP_LOGW(TAG_AUDIO, "WakeNet disabled: handle not found for %s", model_name);
        return ESP_ERR_NOT_FOUND;
    }

    det_mode_t wake_mode = BOARD_WAKE_DETECTION_AGGRESSIVE ? DET_MODE_95 : DET_MODE_90;
    s_wake_data = s_wake_iface->create(model_name, wake_mode);
    if (!s_wake_data) {
        ESP_LOGW(TAG_AUDIO, "WakeNet disabled: create failed for %s", model_name);
        return ESP_FAIL;
    }

    if (s_wake_iface->get_word_num && s_wake_iface->get_det_threshold) {
        int wn = s_wake_iface->get_word_num(s_wake_data);
        for (int wi = 1; wi <= wn; wi++) {
            ESP_LOGI(TAG_AUDIO, "WakeNet word=%d nguong mac dinh cua model=%.3f (BOARD_WAKE_DET_THRESHOLD=%.2f, 0=giu)",
                     wi, (double)s_wake_iface->get_det_threshold(s_wake_data, wi),
                     (double)BOARD_WAKE_DET_THRESHOLD);
        }
    }
    if (BOARD_WAKE_DET_THRESHOLD > 0.0f &&
        s_wake_iface->get_word_num &&
        s_wake_iface->get_det_threshold &&
        s_wake_iface->set_det_threshold) {
        int wake_word_count = s_wake_iface->get_word_num(s_wake_data);
        for (int word_index = 1; word_index <= wake_word_count; word_index++) {
            float before = s_wake_iface->get_det_threshold(s_wake_data, word_index);
            int applied = s_wake_iface->set_det_threshold(
                s_wake_data, BOARD_WAKE_DET_THRESHOLD, word_index);
            float after = s_wake_iface->get_det_threshold(s_wake_data, word_index);
            ESP_LOGI(TAG_AUDIO,
                     "WakeNet word=%d threshold %.3f -> %.3f applied=%d",
                     word_index, (double)before, (double)after, applied);
        }
    }

    s_wake_chunk_samples = s_wake_iface->get_samp_chunksize(s_wake_data);
    s_wake_buf_cap = s_wake_chunk_samples > WAKE_DOWNSAMPLE_MAX_SAMPLES
        ? s_wake_chunk_samples
        : WAKE_DOWNSAMPLE_MAX_SAMPLES;
    s_wake_buf_cap += 32;
    /* WakeNet reads this buffer from CPU code; prefer PSRAM to preserve
     * contiguous internal RAM for Wi-Fi/TLS and audio task stacks. */
    s_wake_buf = heap_caps_aligned_alloc(16, s_wake_buf_cap * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_wake_buf) {
        s_wake_buf = heap_caps_aligned_alloc(16, s_wake_buf_cap * sizeof(int16_t),
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_wake_buf) {
        ESP_LOGW(TAG_AUDIO, "WakeNet disabled: buffer alloc failed");
        s_wake_iface->destroy(s_wake_data);
        s_wake_data = NULL;
        return ESP_ERR_NO_MEM;
    }

    int sr = s_wake_iface->get_samp_rate(s_wake_data);
    char *words = esp_srmodel_get_wake_words(s_wake_models, model_name);
    ESP_LOGI(TAG_AUDIO,
             "WakeNet ready model=%s words=%s sr=%d chunk=%d mode=%s gain=%d%%",
             model_name, words ? words : "?", sr, s_wake_chunk_samples,
             BOARD_WAKE_DETECTION_AGGRESSIVE ? "aggressive-95" : "normal-90",
             BOARD_WAKE_GAIN_PCT);
    ESP_LOGI(TAG_AUDIO,
             "WakeNet heap: internal=%u->%u largest=%u->%u PSRAM=%u->%u",
             (unsigned)internal_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)largest_before,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)psram_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_wake_ready = true;
    return ESP_OK;
}

static int wake_downsample_24k_to_16k(const int16_t *in, int n,
                                      int16_t *out, int out_cap) {
    int o = 0;
    for (int i = 0; i + 2 < n && o + 1 < out_cap; i += 3) {
        out[o++] = in[i];
        out[o++] = (int16_t)(((int32_t)in[i + 1] + (int32_t)in[i + 2]) / 2);
    }
    return o;
}

/* Tự kích ghi PCM: TẮT mặc định từ 13/09 — việc phân tích mic đã xong (§4.7), còn
 * mỗi lần kích là ~250 KB base64 = ~22 s UART 115200 kín, log khác xếp hàng (§9.5).
 * Bật lúc chạy bằng audio_pipeline_capture_enable(true) trước khi cần ghi. */
static bool s_wcap_enabled = false;
void audio_pipeline_capture_enable(bool on) { s_wcap_enabled = on; }

#if CONFIG_VIMATE_DIAG_ENABLE
/* ===== Bộ ghi PCM chẩn đoán (bản diag), dump base64 qua UART =====
 * Ba bản ghi mỗi lần boot, theo thứ tự:
 *   #1 "idle"   : 2 s đầu vào WakeNet (16 kHz, sau gain) lúc rảnh, 15 s sau WakeNet ON
 *                 → phổ nền mic (ù, đơn âm, sàn nhiễu).
 *   #2 "wake"   : 2,5 s đầu vào WakeNet khi có tiếng to BỀN (≥3 khung liên tiếp rms ≥ 600,
 *                 ~100 ms — bỏ qua tiếng gõ màn 1 khung), pre-roll 0,5 s → "Hi Lily".
 *   #3 "uplink" : 3 s mono 24 kHz SAU HPF đúng thứ đang opus-encode gửi server, từ lúc
 *                 Mic ON → cái STT nghe.
 * Dump: `WD:BEGIN tag=<tag> sr=<sr> samples=<n>` + dòng `WD:<base64>` + `WD:END`, do diag
 * task in (UART ~10 s/bản, poll 5 s). PC: tools/wake_dump_to_wav.py <log> <out.wav> [tag]. */
typedef struct {
    const char *tag;
    int sr, total, pre;          /* mẫu */
    int16_t *buf, *pre_ring;     /* PSRAM */
    int pre_w; bool pre_full;
    int len;                     /* -1 chưa kích; >=0 đang ghi; >= total đầy */
    bool dumped;
    int loud_run;
} wcap_t;
static wcap_t s_cap_idle   = { "idle",   16000, 16000 * 2,       0,        NULL, NULL, 0, false, -1, false, 0 };
static wcap_t s_cap_wake   = { "wake",   16000, 16000 * 5 / 2,   16000 / 2, NULL, NULL, 0, false, -1, false, 0 };
static wcap_t s_cap_uplink = { "uplink", 24000, 24000 * 3,       0,        NULL, NULL, 0, false, -1, false, 0 };
static wcap_t s_cap_afe    = { "afe",    16000, 16000 * 3,       16000 / 2, NULL, NULL, 0, false, -1, false, 0 };
static int64_t s_cap_idle_due_us = 0;

static bool wcap_alloc(wcap_t *c)
{
    if (c->buf) return true;
    if (!s_wcap_enabled) return false;
    c->buf = heap_caps_malloc((size_t)c->total * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (c->pre > 0) c->pre_ring = heap_caps_malloc((size_t)c->pre * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c->buf || (c->pre > 0 && !c->pre_ring)) { c->dumped = true; return false; }
    return true;
}

static void wcap_trigger(wcap_t *c, const char *why)
{
    int avail = 0;
    if (c->pre > 0) {
        avail = c->pre_full ? c->pre : c->pre_w;
        int start = c->pre_full ? c->pre_w : 0;
        for (int i = 0; i < avail; i++) c->buf[i] = c->pre_ring[(start + i) % c->pre];
    }
    c->len = avail;
    ESP_LOGI(TAG_AUDIO, "capture[%s]: kich (%s), pre-roll %d mau", c->tag, why, avail);
}

static void wcap_feed(wcap_t *c, const int16_t *pcm, int n)
{
    if (c->dumped || c->len < 0 || !c->buf) return;
    if (c->len >= c->total) return;
    int take = c->total - c->len;
    if (take > n) take = n;
    memcpy(&c->buf[c->len], pcm, (size_t)take * sizeof(int16_t));
    c->len += take;
    if (c->len >= c->total) ESP_LOGI(TAG_AUDIO, "capture[%s]: du %d mau, cho dump", c->tag, c->total);
}

/* Đầu vào WakeNet 16 kHz: nuôi idle + wake. */
static void wcap_feed_wake16(const int16_t *pcm, int n, uint32_t rms)
{
    /* idle: kích theo thời gian */
    if (!s_cap_idle.dumped && s_cap_idle.len < 0 && wcap_alloc(&s_cap_idle)) {
        int64_t now = esp_timer_get_time();
        if (!s_cap_idle_due_us) s_cap_idle_due_us = now + 15 * 1000000LL;
        if (now >= s_cap_idle_due_us) wcap_trigger(&s_cap_idle, "15s sau WakeNet ON");
    }
    wcap_feed(&s_cap_idle, pcm, n);
    /* wake: pre-roll + kích bền vững; CHỈ khi idle đã ghi xong (tránh 2 bản ghi trùng) */
    if (!s_cap_wake.dumped && wcap_alloc(&s_cap_wake)) {
        if (s_cap_wake.len < 0) {
            for (int i = 0; i < n; i++) {
                s_cap_wake.pre_ring[s_cap_wake.pre_w] = pcm[i];
                if (++s_cap_wake.pre_w >= s_cap_wake.pre) { s_cap_wake.pre_w = 0; s_cap_wake.pre_full = true; }
            }
            s_cap_wake.loud_run = (rms >= 600) ? s_cap_wake.loud_run + 1 : 0;
            if (s_cap_wake.loud_run >= 3 && s_cap_idle.len >= s_cap_idle.total) wcap_trigger(&s_cap_wake, "3 khung rms>=600");
        } else {
            wcap_feed(&s_cap_wake, pcm, n);
        }
    }
}

/* Đầu ra AFE 16 kHz (đường P4): #A "afe" — kích khi tiếng to bền (như wake) HOẶC khi
 * Mic ON (lượt nghe), pre-roll 0,5 s. Đúng thứ ASR nhận khi BOARD_AUDIO_ASR_UPLINK_RAW=0. */
static void wcap_feed_afe16(const int16_t *pcm, int n, uint32_t rms, bool mic_on)
{
    wcap_t *c = &s_cap_afe;
    if (c->dumped || !wcap_alloc(c)) return;
    if (c->len < 0) {
        for (int i = 0; i < n; i++) {
            c->pre_ring[c->pre_w] = pcm[i];
            if (++c->pre_w >= c->pre) { c->pre_w = 0; c->pre_full = true; }
        }
        c->loud_run = (rms >= 600) ? c->loud_run + 1 : 0;
        if (c->loud_run >= 3) wcap_trigger(c, "3 khung rms>=600 (AFE out)");
        else if (mic_on) wcap_trigger(c, "Mic ON (AFE out)");
    } else {
        wcap_feed(c, pcm, n);
    }
}

/* Uplink 24 kHz sau HPF, gọi trong mic_task khi mic_streaming (trước encode). */
static void wcap_feed_uplink24(const int16_t *pcm, int n)
{
    if (s_cap_uplink.dumped || !wcap_alloc(&s_cap_uplink)) return;
    if (s_cap_uplink.len < 0) wcap_trigger(&s_cap_uplink, "Mic ON");
    wcap_feed(&s_cap_uplink, pcm, n);
}

static bool wcap_dump_one(wcap_t *c)
{
    if (c->dumped || !c->buf || c->len < c->total) return false;
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *src = (const uint8_t *)c->buf;
    size_t len = (size_t)c->total * sizeof(int16_t);
    char line[80];
    int col = 0;
    printf("WD:BEGIN tag=%s sr=%d samples=%d\n", c->tag, c->sr, c->total);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < len) v |= (uint32_t)src[i + 1] << 8;
        if (i + 2 < len) v |= src[i + 2];
        line[col++] = b64[(v >> 18) & 63];
        line[col++] = b64[(v >> 12) & 63];
        line[col++] = (i + 1 < len) ? b64[(v >> 6) & 63] : '=';
        line[col++] = (i + 2 < len) ? b64[v & 63] : '=';
        if (col >= 76) { line[col] = 0; printf("WD:%s\n", line); col = 0; vTaskDelay(1); }
    }
    if (col) { line[col] = 0; printf("WD:%s\n", line); }
    printf("WD:END tag=%s\n", c->tag);
    c->dumped = true;
    return true;
}

/* Gọi từ diag task (poll 5 s). Trả true nếu vừa dump một bản. */
bool audio_pipeline_wake_capture_dump(void)
{
    return wcap_dump_one(&s_cap_idle) || wcap_dump_one(&s_cap_wake) || wcap_dump_one(&s_cap_uplink)
        || wcap_dump_one(&s_cap_afe);
}
#endif

static bool wake_feed_16k(const int16_t *pcm, int samples) {
    if (!s_wake_ready || !s_wake_active || !s_wake_iface || !s_wake_data) {
        return false;
    }

    int pos = 0;
    while (pos < samples) {
        int need = s_wake_chunk_samples - s_wake_buf_len;
        int take = samples - pos;
        if (take > need) take = need;
        memcpy(&s_wake_buf[s_wake_buf_len], &pcm[pos], take * sizeof(int16_t));
        s_wake_buf_len += take;
        pos += take;

        if (s_wake_buf_len >= s_wake_chunk_samples) {
            int res = s_wake_iface->detect(s_wake_data, s_wake_buf);
            s_wake_buf_len = 0;
            if (res > 0) {
                s_wake_det_count++;
                const char *word = s_wake_iface->get_word_name(s_wake_data, res);
                ESP_LOGI(TAG_AUDIO, "WakeNet detected: %s", word ? word : "Hi Lily");
                s_wake_active = false;
                if (g_vimate_events) {
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WAKE_WORD);
                }
                return true;
            }
        }
    }
    return false;
}

static void wake_process_24k(int16_t *pcm, int samples) {
    if (!s_wake_ready || !s_wake_active || s_spk_active || s_mic_active) {
        return;
    }
    int16_t down[WAKE_DOWNSAMPLE_MAX_SAMPLES];
    int n16 = wake_downsample_24k_to_16k(pcm, samples, down,
                                         WAKE_DOWNSAMPLE_MAX_SAMPLES);
    if (n16 > 0) {
#if BOARD_WAKE_GAIN_PCT != 100
        for (int i = 0; i < n16; i++) {
            int32_t value = ((int32_t)down[i] * BOARD_WAKE_GAIN_PCT) / 100;
            if (value > 32767) value = 32767;
            if (value < -32768) value = -32768;
            down[i] = (int16_t)value;
        }
#endif
        /* Thống kê CẢ cửa sổ 10 s (không phải 1 khung/10 s như trước — snapshot bỏ
         * lỡ lúc người dùng nói): rms min/max, peak, số khung "to" (rms ≥ 600, cỡ
         * giọng nói ở 0,5 m), số lần WakeNet trả >0. Đọc log: nói "Hi Lily" mà
         * loud=0 → tín hiệu tới WakeNet quá nhỏ (gain/mic); loud>0 mà det=0 → model
         * không nhận (ngưỡng/giọng/nhiễu). */
        static uint32_t s_wd_frames, s_wd_loud, s_wd_rms_min = UINT32_MAX, s_wd_rms_max;
        static int s_wd_peak;
        {
            int peak = 0;
            for (int i = 0; i < n16; i++) {
                int value = down[i] < 0 ? -(int)down[i] : (int)down[i];
                if (value > peak) peak = value;
            }
            uint32_t rms = mic_frame_rms(down, n16);
            wcap_feed_wake16(down, n16, rms);
            s_wd_frames++;
            if (rms >= 600) s_wd_loud++;
            if (rms < s_wd_rms_min) s_wd_rms_min = rms;
            if (rms > s_wd_rms_max) s_wd_rms_max = rms;
            if (peak > s_wd_peak) s_wd_peak = peak;
        }
        int64_t now = esp_timer_get_time();
        if (now >= s_wake_diag_next_us) {
            ESP_LOGI(TAG_AUDIO, "WakeNet input 10s: frames=%u rms=%u..%u peak=%d loud=%u det=%u",
                     (unsigned)s_wd_frames,
                     (unsigned)(s_wd_rms_min == UINT32_MAX ? 0 : s_wd_rms_min), (unsigned)s_wd_rms_max,
                     s_wd_peak, (unsigned)s_wd_loud, (unsigned)s_wake_det_count);
            s_wd_frames = s_wd_loud = s_wd_rms_max = 0; s_wd_rms_min = UINT32_MAX; s_wd_peak = 0;
            s_wake_diag_next_us = now + 10000000;
        }
        (void)wake_feed_16k(down, n16);
    }
}
#else
static esp_err_t wake_init(void) {
    ESP_LOGI(TAG_AUDIO, "WakeNet disabled by config");
    return ESP_OK;
}

static void wake_reset(void) {
}

static void wake_process_24k(int16_t *pcm, int samples) {
    (void)pcm;
    (void)samples;
}
#endif

void audio_pipeline_note_image_busy(int window_ms) {
    if (window_ms <= 0) {
        s_img_busy_until_us = 0;
        return;
    }
    s_img_busy_until_us = esp_timer_get_time() + (int64_t)window_ms * 1000;
}

#if CONFIG_VIMATE_DIAG_ENABLE
/* Mic idle probe — CHỈ bản diag. Chưa READY + WS thì mic_task ngủ hoàn toàn, nên
 * qua UART không có cách nào biết mic có thu hay không (README-P4 §4 "chưa kiểm
 * được"). Khi rảnh: 20s đầu đọc liên tục (bắt được beep BOARD_AUDIO_BOOT_TEST_BEEP
 * ở ~4.6s nếu bật → tự xác nhận cả loa lẫn mic qua không khí), sau đó 1 frame/s.
 * In mỗi 2s (20s đầu) rồi mỗi 10s: rms min..max, peak, DC. Cách đọc:
 *   rms cố định ~0, peak 0        → mic câm (không có data / codec không xuất)
 *   rms vài chục..trăm, dao động   → mic sống, đang nghe tiếng ồn phòng
 *   rms nhảy vọt đúng lúc beep     → loa + amp + mic đều chạy
 *   DC lớn (> vài trăm) cố định    → lệch DC, HPF 120Hz sẽ lo
 * i2s_input_start() chỉ bật cờ đọc (không đụng DMA), để nguyên không stop: cờ
 * true lúc rảnh vô hại, và không có cửa sổ đua với mic_start/wake_start. */
static void mic_idle_probe(int16_t *pcm)
{
    static int64_t s_next_read_us = 0, s_next_log_us = 0, s_first_us = 0;
    static uint32_t s_rms_min = UINT32_MAX, s_rms_max = 0;
    static uint32_t s_hrms_min = UINT32_MAX, s_hrms_max = 0;
    static int s_peak = 0, s_frames = 0, s_zero_reads = 0;
    static int32_t s_dc = 0;
    int64_t now = esp_timer_get_time();
    if (!s_first_us) {
        s_first_us = now;
        /* HPF self-test: ESP-DSP moi co ban P4, phai chac dsps_biquad_f32 loc that.
         * 30Hz A=10000 (phai bi cat ~24dB o HPF 120Hz bac 2: |H|=(f/fc)^2/sqrt(1+(f/fc)^4)
         * = 0.0624) + 1kHz A=1000 (di qua). rms_in ~7106, rms_out ~830. rms_out == rms_in
         * nghia la bo loc passthrough (bug). */
        const int n = OPUS_ENC_FRAME_SAMPLES;
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)OPUS_ENC_SAMPLE_RATE;
            pcm[i] = (int16_t)(10000.0f * sinf(6.2831853f * 30.0f * t)
                             + 1000.0f * sinf(6.2831853f * 1000.0f * t));
        }
        uint32_t rin = mic_frame_rms(pcm, n);
        mic_hpf_reset();
        mic_hpf_process_pcm16(pcm, n);
        uint32_t rout = mic_frame_rms(pcm, n);
        mic_hpf_reset();
        ESP_LOGI(TAG_AUDIO, "mic HPF self-test: rms_in=%u rms_out=%u (mong ~7106 -> ~830)",
                 (unsigned)rin, (unsigned)rout);
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_DSP_OPTIMIZED
        {
            /* Bang chung cho MIC_HPF_BIQUAD: ban arp4 tra ve gi? (phai la 0) */
            float fi[8] = {0}, fo[8] = {0}, fw[2] = {0};
            esp_err_t r = dsps_biquad_f32_arp4(fi, fo, 8, s_mic_hpf_coeffs, fw);
            ESP_LOGI(TAG_AUDIO, "mic HPF self-test: dsps_biquad_f32_arp4 ret=0x%x (%s)",
                     (unsigned)r, r == ESP_OK ? "OK" : "RAC — vi the P4 dung ban ansi");
        }
#endif
    }
    const bool burst = (now - s_first_us) < 20 * 1000000LL;
    if (now < s_next_read_us) return;
    s_next_read_us = burst ? 0 : now + 1000000;
    i2s_input_start();
    int n = i2s_input_read_pcm16(pcm, OPUS_ENC_FRAME_SAMPLES, 200);
    if (n <= 0) { s_zero_reads++; }
    else {
        int64_t sum = 0, sum_sq = 0; int peak = 0;
        for (int i = 0; i < n; i++) {
            int32_t s = pcm[i];
            sum += s; sum_sq += (int64_t)s * s;
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
        }
        uint32_t rms = isqrt_u64((uint64_t)(sum_sq / n));
        if (rms < s_rms_min) s_rms_min = rms;
        if (rms > s_rms_max) s_rms_max = rms;
        if (peak > s_peak) s_peak = peak;
        s_dc = (int32_t)(sum / n);
        /* Nền SAU HPF 120Hz là cái VAD thật sự nhìn thấy (mic_start reset lại
         * trạng thái biquad nên dùng chung ở đây vô hại). */
        mic_hpf_process_pcm16(pcm, n);
        uint32_t rms_h = mic_frame_rms(pcm, n);
        if (rms_h < s_hrms_min) s_hrms_min = rms_h;
        if (rms_h > s_hrms_max) s_hrms_max = rms_h;
        s_frames++;
    }
    if (now >= s_next_log_us) {
        ESP_LOGI(TAG_AUDIO, "mic idle probe: frames=%d zero_reads=%d rms=%u..%u peak=%d dc=%ld hpf_rms=%u..%u",
                 s_frames, s_zero_reads,
                 (unsigned)(s_rms_min == UINT32_MAX ? 0 : s_rms_min), (unsigned)s_rms_max,
                 s_peak, (long)s_dc,
                 (unsigned)(s_hrms_min == UINT32_MAX ? 0 : s_hrms_min), (unsigned)s_hrms_max);
        s_rms_min = UINT32_MAX; s_rms_max = 0; s_peak = 0; s_frames = 0; s_zero_reads = 0;
        s_hrms_min = UINT32_MAX; s_hrms_max = 0;
        s_next_log_us = now + (burst ? 2 : 10) * 1000000LL;
    }
}
#endif

static void mic_task(void *arg) {
    static int16_t pcm[OPUS_ENC_FRAME_SAMPLES];
    static uint8_t opus_buf[OPUS_MAX_PACKET_BYTES];
    while (1) {
        bool mic_streaming = s_mic_active && ws_client_is_connected();
        bool wake_listening = s_wake_active && ws_client_is_connected();
        bool barge_listening = false;
#if CONFIG_VIMATE_BARGE_IN_ENABLE
        barge_listening = s_barge_listen && s_spk_active;
#endif
        if (!mic_streaming && !wake_listening && !barge_listening) {
#if CONFIG_VIMATE_DIAG_ENABLE
            mic_idle_probe(pcm);
#endif
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        int n = i2s_input_read_pcm16(pcm, OPUS_ENC_FRAME_SAMPLES, 200);
        if (n < OPUS_ENC_FRAME_SAMPLES) {
            /* Không có frame trong 200ms — yield CPU rồi loop tiếp.
             * Tránh tight loop làm watchdog Core 1 fire. */
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!mic_streaming) {
#if CONFIG_VIMATE_BARGE_IN_ENABLE
            /* Barge-in: feed mic RAW (ref cũng raw) → AEC + VAD trên giọng đã khử
             * echo. Trigger → báo app_main ngắt TTS + chuyển sang nghe. */
            if (barge_listening) {
                barge_in_feed_mic(pcm, n);
                if (barge_in_triggered() && g_vimate_events) {
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BARGE_IN);
                    s_barge_listen = false; /* tránh set lặp; app_main tiếp quản */
                }
            }
#endif
            mic_hpf_process_pcm16(pcm, n);
            if (wake_listening) {
                wake_process_24k(pcm, n);
                /* VOICE-ACTIVATED tắt mặc định (user 13/06: "cứ có voice là tự bật"
                 * — phiền). CHỈ "Hi Lily" (WakeNet) mới mở lượt. Bật lại =1 nếu wake
                 * không tin cậy + chấp nhận nhạy theo giọng. */
#if VOICE_ACTIVATED_ENABLE
                if (!s_paused && !s_spk_active && g_vimate_events) {
                    static int va_cnt = 0;
                    if (mic_frame_rms(pcm, n) >=
                        (uint32_t)(s_vad_rms_threshold * VA_RMS_MULT)) {
                        if (++va_cnt >= VA_FRAMES_TO_START) {
                            va_cnt = 0;
                            ESP_LOGI(TAG_AUDIO, "voice-activated (raw) → listen start");
                            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BTN_PRESS);
                        }
                    } else if (va_cnt > 0) {
                        va_cnt--;
                    }
                }
#endif /* VOICE_ACTIVATED_ENABLE */
            }
            vTaskDelay(1);
            continue;
        }

        /* Mute SEND khi TTS đang phát — vẫn drain i2s buffer, ko encode,
         * reset VAD state để turn tiếp theo bắt đầu sạch. */
        if (s_mic_send_muted) {
            if (s_vad_in_speech || s_vad_speech_count > 0) vad_reset();
            continue;
        }

        mic_hpf_process_pcm16(pcm, n);
#if CONFIG_VIMATE_DIAG_ENABLE
        wcap_feed_uplink24(pcm, n);
#endif

        /* Stream cả lượt nghe thay vì chỉ stream sau khi VAD mở speech.
         * VAD vẫn quyết định EOT, nhưng ASR phải nhận được audio kể cả khi
         * giọng nhỏ/ngưỡng VAD chưa đúng. Opus 32kbps nên 6s no-speech chỉ
         * khoảng vài chục KB, đủ an toàn hơn lỗi PCM=0. */
        bool is_speech = vad_frame_is_speech(pcm, n);
        s_vad_listen_ms += VAD_FRAME_MS;
        if (s_vad_stop_signaled) {
            vTaskDelay(1);
            continue;
        }
        if (s_vad_in_speech) {
            if (is_speech) {
                s_vad_silence_ms = 0;
            } else {
                s_vad_silence_ms += VAD_FRAME_MS;
                if (s_vad_listen_ms >= VAD_MIN_LISTEN_MS_BEFORE_EOT &&
                    s_vad_silence_ms >= s_vad_silence_ms_to_stop) {
                    ESP_LOGI(TAG_AUDIO, "VAD EOT silence %dms → signal main",
                             s_vad_silence_ms);
                    s_vad_stop_signaled = true;
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_VAD_EOT);
                }
            }
        } else if (is_speech) {
            s_vad_speech_count++;
            if (s_vad_speech_count >= VAD_SPEECH_FRAMES_TO_START) {
                s_vad_in_speech   = true;
                s_turn_had_speech = true;
                s_vad_silence_ms  = 0;
                ESP_LOGI(TAG_AUDIO, "VAD speech start");
            }
        } else {
            s_vad_speech_count = 0;
            if (s_vad_listen_ms >= VAD_NO_SPEECH_MS_TO_STOP) {
                ESP_LOGW(TAG_AUDIO,
                         "VAD no-speech fallback %dms frames=%u threshold=%d peak_rms=%u → signal main",
                         s_vad_listen_ms,
                         (unsigned)s_mic_streamed_frames,
                         s_vad_rms_threshold,
                         (unsigned)s_vad_peak_rms);
                s_vad_stop_signaled = true;
                xEventGroupSetBits(g_vimate_events, VIMATE_EVT_VAD_EOT);
            }
        }

        /* Hoãn GỬI khi ảnh bài học đang tải/render: gửi audio (TLS/AES) cùng lúc
         * image download (TLS riêng) làm đỉnh RAM → rớt WS/OOM. Chỉ hoãn ở ĐẦU
         * cửa sổ nghe khi CHƯA có speech, có TRẦN MIC_RENDER_DEFER_MAX_MS và tự
         * nhả khi có speech → KHÔNG tái phát lỗi PCM=0. Luồng bài học bình thường
         * render xong trước khi listen nên nhánh này gần như không kích hoạt. */
        bool defer_send = !s_vad_in_speech &&
                          s_vad_listen_ms <= MIC_RENDER_DEFER_MAX_MS &&
                          esp_timer_get_time() < s_img_busy_until_us;
        /* CHỈ gửi khi đã/đang có speech trong lượt: chặn ambient TRƯỚC giọng (clip
         * gần-im → ASR ảo giác). is_speech cho gửi ngay frame onset; s_turn_had_speech
         * giữ gửi tới EOT (cả khoảng lặng giữa câu). Nếu CẢ lượt không có speech →
         * nhánh no-speech fallback (line ~595) vẫn chốt EOT, server prompt nói lại. */
        if (!s_vad_stop_signaled && !defer_send &&
            (s_vad_in_speech || is_speech || s_turn_had_speech)) {
            int enc = opus_codec_encode(pcm, opus_buf, sizeof(opus_buf));
            if (enc > 0) {
                if (ws_client_send_binary(opus_buf, enc) == ESP_OK) {
                    s_mic_streamed_frames++;
                }
            }
        }

        /* Opus encode có thể giữ CPU1 liên tục trong lúc trẻ nói; nhường 1 tick
         * để IDLE1 feed task watchdog nhưng vẫn giữ latency dưới mức nghe được. */
        vTaskDelay(1);
    }
}

/* ====== AFE 2-mic path (AEC + SE/BSS beamforming + VAD + WakeNet) ====== *
 * Thay mic_task khi AFE init OK. QUAN TRỌNG: init AFE SAU khi WS/WiFi lên —
 * init trước WiFi làm WiFi thiếu static RX buffer DMA nội → abort/boot-loop
 * (bài học 12/06). afe_feed_task (stack nhỏ) chờ WS, init AFE, spawn fetch
 * task, rồi đọc 2 mic interleaved feed AFE. AFE init fail → fallback wake_init
 * + mic_task cũ (đường mono cũ, không hồi quy). */
static volatile bool s_afe_failed = false;

/* Upsample 16k→24k mono (2:3 linear) cho uplink Opus 24k. out cần 3*(n/2). */
/* Upsample 16k→24k mono — nội suy tuyến tính ĐÚNG vị trí thời gian: out[i] ở
 * vị trí nguồn i*2/3 (16k). Bản cũ đặt sai vị trí (0,0.667,1.0 thay vì
 * 0,0.667,1.333) → méo + spike hại thanh điệu VN → ASR sai. out cần n*3/2. */
__attribute__((unused))
static int up_16_24_mono(const int16_t *in, int n, int16_t *out) {
    int out_n = n * 3 / 2;
    for (int i = 0; i < out_n; i++) {
        int num = i * 2;          /* vị trí nguồn = num/3 (đơn vị mẫu 16k) */
        int i0  = num / 3;
        int rem = num - i0 * 3;   /* 0,1,2 → frac 0, 1/3, 2/3 */
        int a = in[i0];
        int b = (i0 + 1 < n) ? in[i0 + 1] : in[i0];
        out[i] = (int16_t)(a + (b - a) * rem / 3);
    }
    return out_n;
}

/* ASR_UPLINK_RAW_MIC (13/06): bằng chứng dump cho thấy output AFE (AEC+BSS+NS)
 * BĂM NÁT speech (centroid ~1500Hz, transcript rác "con nhé là bé bé" ở MỌI
 * sample-rate) trong khi Qwen3-ASR-1.7B KHỎE với nhiễu/quiet/clip (đã đo). Nên
 * cho ASR ăn MIC THÔ (downmix 2 mic → mono24 → HPF → opus), AFE chỉ còn lo
 * VAD/wake/barge-in. =0 để quay lại gửi output AFE đã xử lý. */
#ifndef BOARD_AUDIO_ASR_UPLINK_RAW
#define BOARD_AUDIO_ASR_UPLINK_RAW 1
#endif
#ifndef BOARD_AUDIO_TDM_MIC_A
#define BOARD_AUDIO_TDM_MIC_A 0
#define BOARD_AUDIO_TDM_MIC_B 1
#endif
#define ASR_UPLINK_RAW_MIC BOARD_AUDIO_ASR_UPLINK_RAW

#if ASR_UPLINK_RAW_MIC
/* Ring SPSC mono24 cho mic THÔ: writer = afe_feed_task (prio 6, chỉ downmix+push,
 * nhẹ); reader = afe_fetch_task (prio 3, encode+gửi). Tách capture↔encode để
 * KHÔNG bỏ đói IDLE1 — đưa HPF+opus vào feed (prio 6, Core1) gây task_wdt +
 * reboot ~75s (13/06). */
#define RAW_RING_SAMPLES (OPUS_ENC_FRAME_SAMPLES * 6)  /* ~360ms @24k */
static int16_t      *s_raw_ring;
static volatile int  s_raw_w, s_raw_r;
static void raw_ring_push(const int16_t *mono, int n) {
    if (!s_raw_ring) return;
    for (int i = 0; i < n; i++) {
        int nxt = (s_raw_w + 1) % RAW_RING_SAMPLES;
        if (nxt == s_raw_r) return;   /* đầy → bỏ phần dư (reader chậm) */
        s_raw_ring[s_raw_w] = mono[i];
        s_raw_w = nxt;
    }
}
static int raw_ring_avail(void) {
    return (s_raw_w - s_raw_r + RAW_RING_SAMPLES) % RAW_RING_SAMPLES;
}
static int raw_ring_pop(int16_t *out, int want) {
    int got = 0;
    while (got < want && s_raw_r != s_raw_w) {
        out[got++] = s_raw_ring[s_raw_r];
        s_raw_r = (s_raw_r + 1) % RAW_RING_SAMPLES;
    }
    return got;
}
static void raw_ring_reset(void) { s_raw_r = s_raw_w; }
#endif

#if CONFIG_VIMATE_DIAG_ENABLE
/* Đếm phía FEED để đối chiếu với fetch (AFE out 10s): 10 s = 167 lần đọc 1440 khung
 * (60 ms @24 k) = 313 khung AFE 512 @16 k. feed thiếu = I2S RX tràn vì afe_feed về
 * đọc trễ (bị chiếm CPU); feed đủ mà fetch thiếu = AFE/fetch trễ. Cả hai đều 0 khi
 * pipeline nghỉ (không mic/wake/spk) — không phải mất CPU. */
static volatile uint32_t s_st_feed = 0, s_st_feed_short = 0;
#endif

/* FETCH: mono 16k ĐÃ SẠCH (AEC+BSS) + cờ wake/vad. Wake → app_main. Nghe: VAD
 * EOT theo AFE + upsample 16→24 → opus → WS. Loa đang phát: VAD speech (đã khử
 * echo) → barge-in (debounce 3 fetch ≈ 96ms + bỏ 500ms đầu lượt phát). */
static void afe_fetch_task(void *arg) {
    /* enc(input)+opus_buf(output) INTERNAL: opus đọc/ghi PSRAM ra gói 2-byte câm
     * (13/06). Dùng cho CẢ hai đường (raw rút ring / AFE upsample). */
    int16_t *enc = heap_caps_malloc(OPUS_ENC_FRAME_SAMPLES * sizeof(int16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *opus_buf = heap_caps_malloc(OPUS_MAX_PACKET_BYTES,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!enc || !opus_buf) {
        ESP_LOGE(TAG_AUDIO, "afe_fetch buffer alloc fail");
        vTaskDelete(NULL);
    }
#if !ASR_UPLINK_RAW_MIC
    /* up24 PSRAM: chỉ đường AFE cần (upsample 16→24). */
    int16_t *up24 = heap_caps_malloc(((512 * 3) / 2 + 16) * sizeof(int16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!up24) {
        ESP_LOGE(TAG_AUDIO, "afe_fetch up24 alloc fail");
        vTaskDelete(NULL);
    }
    int encn = 0;
#endif
    int barge_speech = 0;
    audio_afe_result_t r;
#if CONFIG_VIMATE_DIAG_ENABLE
    uint32_t st_fetch = 0, st_speech = 0, st_wake = 0, st_rms_max = 0, st_loud = 0;
    int64_t st_next_us = 0;
#endif
    while (1) {
        if (!audio_afe_fetch(&r)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        bool ws = ws_client_is_connected();
#if CONFIG_VIMATE_DIAG_ENABLE
        {
            /* Thống kê 10 s đầu ra AFE: số fetch, khung VAD=speech, wake, rms max. */
            uint32_t rms = (r.pcm16 && r.samples > 0) ? mic_frame_rms(r.pcm16, r.samples) : 0;
            st_fetch++; if (r.vad_speech) st_speech++; if (r.wake) st_wake++;
            if (rms > st_rms_max) st_rms_max = rms;
            if (rms >= 600) st_loud++;
            int64_t now = esp_timer_get_time();
            if (!st_next_us) st_next_us = now + 10000000LL;
            if (now >= st_next_us) {
                ESP_LOGI(TAG_AUDIO, "AFE out 10s: fetch=%u/313 feed=%u/167 short=%u speech=%u loud=%u rms_max=%u wake=%u mic=%d spk=%d",
                         (unsigned)st_fetch, (unsigned)s_st_feed, (unsigned)s_st_feed_short,
                         (unsigned)st_speech, (unsigned)st_loud, (unsigned)st_rms_max,
                         (unsigned)st_wake, (int)s_mic_active, (int)s_spk_active);
                st_fetch = st_speech = st_wake = st_rms_max = st_loud = 0;
                s_st_feed = s_st_feed_short = 0;
                st_next_us = now + 10000000LL;
            }
            if (r.pcm16 && r.samples > 0) wcap_feed_afe16(r.pcm16, r.samples, rms, s_mic_active);
        }
#endif
        /* Wake word "Hi Lily" → mở lượt TRỢ LÝ. WAKE_WORD KHÔNG toggle
         * gì khác — app_main xử lý riêng. */
        if (r.wake && ws && !s_paused && g_vimate_events) {
            ESP_LOGI(TAG_AUDIO, "AFE WakeNet detected");
            xEventGroupSetBits(g_vimate_events, VIMATE_EVT_WAKE_WORD);
        }
        /* VOICE-ACTIVATED (tự nói là nghe) → BTN_PRESS. Cùng knob với đường thô
         * (VOICE_ACTIVATED_ENABLE=0: người dùng 13/06 "cứ có voice là tự bật — phiền";
         * trên P4 12/09 VAD còn báo speech theo ồn nền → tự mở lượt liên tục). */
        if (VOICE_ACTIVATED_ENABLE && ws && !s_paused && !s_mic_active && !s_spk_active && g_vimate_events) {
            static int va_cnt = 0;
            if (r.vad_speech) {
                if (++va_cnt >= 6) {
                    va_cnt = 0;
                    ESP_LOGI(TAG_AUDIO, "AFE voice-activated → listen start");
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BTN_PRESS);
                }
            } else if (va_cnt > 0) {
                va_cnt--;
            }
        }
        /* Barge-in bằng giọng TẠM TẮT (12/06): AEC (NLP_OFF) chưa khử đủ tiếng
         * loa → VAD báo speech ~100% từ echo → barge fired liên tục → TTS bị
         * CẮT giữa câu ("chào không hết câu") + mở lượt nghe ma → "chưa nghe
         * rõ". Bật lại sau khi tune AEC (NLP/AGGR + test echo 85-90dB). Chạm
         * màn để ngắt vẫn hoạt động bình thường. */
#if 0
        if (s_spk_active && ws && g_vimate_events) {
            bool warmed = (esp_timer_get_time() - s_spk_start_us) > 500000;
            if (r.vad_speech && warmed) {
                if (++barge_speech >= 3) {
                    barge_speech = 0;
                    ESP_LOGW(TAG_AUDIO, "AFE barge-in fired");
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BARGE_IN);
                }
            } else {
                barge_speech = 0;
            }
        } else {
            barge_speech = 0;
        }
#else
        (void)barge_speech;
#endif

        bool listening = s_mic_active && ws;
        if (!listening || s_mic_send_muted) {
            if (s_vad_in_speech || s_vad_speech_count > 0) vad_reset();
#if !ASR_UPLINK_RAW_MIC
            encn = 0;
#endif
            continue;
        }

        /* VAD EOT theo AFE vad_state; mỗi fetch = r.samples @16k (~32ms). */
        int frame_ms = r.samples > 0 ? r.samples * 1000 / 16000 : 32;
        s_vad_listen_ms += frame_ms;
        bool is_speech = r.vad_speech;
        if (!s_vad_stop_signaled) {
            if (s_vad_in_speech) {
                if (is_speech) {
                    s_vad_silence_ms = 0;
                } else {
                    s_vad_silence_ms += frame_ms;
                    if (s_vad_silence_ms >= s_vad_silence_ms_to_stop) {
                        ESP_LOGI(TAG_AUDIO, "AFE VAD EOT silence %dms → signal main",
                                 s_vad_silence_ms);
                        s_vad_stop_signaled = true;
                        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_VAD_EOT);
                    }
                }
            } else if (is_speech) {
                s_vad_speech_count++;
                if (s_vad_speech_count >= VAD_SPEECH_FRAMES_TO_START) {
                    s_vad_in_speech  = true;
                    s_vad_silence_ms = 0;
                    ESP_LOGI(TAG_AUDIO, "AFE VAD speech start");
                }
            } else {
                s_vad_speech_count = 0;
                if (s_vad_listen_ms >= VAD_NO_SPEECH_MS_TO_STOP) {
                    ESP_LOGW(TAG_AUDIO, "AFE VAD no-speech %dms → signal main",
                             s_vad_listen_ms);
                    s_vad_stop_signaled = true;
                    xEventGroupSetBits(g_vimate_events, VIMATE_EVT_VAD_EOT);
                }
            }
        }

        /* Gửi cả lượt nghe (ASR cần audio kể cả giọng nhỏ). */
#if ASR_UPLINK_RAW_MIC
        /* RAW uplink: rút mic THÔ từ ring (feed_task đẩy) → HPF → opus → WS.
         * Encode Ở ĐÂY (prio 3) thay vì feed (prio 6) để không bỏ đói IDLE1. */
        if (s_mic_active && ws && !s_mic_send_muted && !s_vad_stop_signaled) {
            while (raw_ring_avail() >= OPUS_ENC_FRAME_SAMPLES) {
                raw_ring_pop(enc, OPUS_ENC_FRAME_SAMPLES);
                mic_hpf_process_pcm16(enc, OPUS_ENC_FRAME_SAMPLES);
                int e = opus_codec_encode(enc, opus_buf, OPUS_MAX_PACKET_BYTES);
                if (e > 0 && ws_client_send_binary(opus_buf, e) == ESP_OK)
                    s_mic_streamed_frames++;
            }
        } else {
            raw_ring_reset();
        }
#else
        /* 16→24 → gom đủ frame Opus → encode → WS. */
        if (!s_vad_stop_signaled && r.pcm16 && r.samples > 0) {
            int u = up_16_24_mono(r.pcm16, r.samples, up24);
            for (int i = 0; i < u; i++) {
                enc[encn++] = up24[i];
                if (encn >= OPUS_ENC_FRAME_SAMPLES) {
                    /* DC-blocker (y=x-x1+0.995*y1): AFE output lệch DC → Opus loại
                     * DC → gói 2-byte câm → server rms=0. Khử DC, KHÔNG gain. */
                    {
                        static int32_t dcx1 = 0, dcy1 = 0;
                        for (int k = 0; k < OPUS_ENC_FRAME_SAMPLES; k++) {
                            int32_t x = enc[k];
                            int32_t y = x - dcx1 + (dcy1 * 995) / 1000;
                            dcx1 = x;
                            if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
                            enc[k] = (int16_t)y; dcy1 = y;
                        }
                    }
                    /* OPUS_MAX_PACKET_BYTES (KHÔNG sizeof(opus_buf) — opus_buf là
                     * con trỏ heap, sizeof=4 → opus capped 4B → gói 2-byte câm →
                     * server rms=0 "chưa nghe rõ". BUG GỐC, 13/06). */
                    int e = opus_codec_encode(enc, opus_buf, OPUS_MAX_PACKET_BYTES);
                    if (e > 0 && ws_client_send_binary(opus_buf, e) == ESP_OK) {
                        s_mic_streamed_frames++;
                    }
                    encn = 0;
                }
            }
        }
#endif /* !ASR_UPLINK_RAW_MIC */
    }
}

static void afe_feed_task(void *arg) {
    const int nch = es8311_codec_in_channels();   /* 2, hoặc 4 khi ES7210 TDM */
    int16_t *stereo = heap_caps_malloc((size_t)OPUS_ENC_FRAME_SAMPLES * nch * sizeof(int16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stereo) {
        ESP_LOGE(TAG_AUDIO, "afe_feed buffer alloc fail");
        vTaskDelete(NULL);
    }
#if ASR_UPLINK_RAW_MIC
    /* Ring mic THÔ (PSRAM, chỉ là kho chứa). feed downmix→push (nhẹ); fetch
     * encode+gửi. AFE vẫn chạy song song lo VAD/wake/barge. */
    s_raw_ring = heap_caps_malloc(RAW_RING_SAMPLES * sizeof(int16_t),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_raw_ring) {
        ESP_LOGE(TAG_AUDIO, "raw ring alloc fail");
        vTaskDelete(NULL);
    }
    s_raw_w = s_raw_r = 0;
#endif
    while (1) {
        if (!audio_afe_ready()) {
            if (!ws_client_is_connected()) {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            /* WS lên ⇒ WiFi đã init + giữ đủ RX buffer. Đợi thêm 3s cho TLS OTA
             * check + emo spiffs mount xong (AFE create chiếm peak RAM tạm —
             * init chồng lấn từng làm emo mount NO_MEM, 12/06). */
            vTaskDelay(pdMS_TO_TICKS(3000));
            if (audio_afe_init() != ESP_OK) {
                s_afe_failed = true;
                ESP_LOGW(TAG_AUDIO, "AFE init fail → fallback wake_init + mic_task");
                (void)wake_init();
                xTaskCreatePinnedToCore(mic_task, "mic_enc", VIMATE_TASK_STACK_AUDIO_MIC,
                                        NULL, VIMATE_TASK_PRIO_AUDIO, NULL,
                                        VIMATE_TASK_CORE_IO);
                audio_pipeline_wake_start();
                vTaskDelete(NULL);
            }
            /* Prio 3 — DƯỚI spk_task (7) VÀ websocket_task (5): fetch chạy cả
             * AEC+BSS+WakeNet bên trong (nặng CPU); để ngang WS làm WS nhận
             * packet TTS chậm → loa đói data → chập chờn (12/06). Mic path có
             * ring đệm nên fetch trễ vài chục ms vô hại. */
            /* prio 4: trên img_worker (3) — decode JPEG 800×480 từng cướp slot fetch
             * (fetch=276/313, README-P4 §9.3); dưới afe_feed (6) và spk_dec (7). */
            xTaskCreatePinnedToCore(afe_fetch_task, "afe_fetch",
                                    VIMATE_TASK_STACK_AUDIO_MIC, NULL,
                                    4, NULL, VIMATE_TASK_CORE_IO);
            /* Boot-flow: wake_start lúc WS connect đã no-op vì AFE chưa ready —
             * bật lại ngay khi AFE sẵn sàng. */
            audio_pipeline_wake_start();
            continue;
        }
        if (!(s_mic_active || s_wake_active || s_spk_active)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        /* AEC chỉ khi loa phát: là khối nặng nhất của AFE; bật liên tục làm core 1
         * không còn IDLE (task_wdt 12/09). Không phát thì không có gì để khử. */
        {
            static int aec_on = -1;
            int want = s_spk_active ? 1 : 0;
            if (want != aec_on) {
                audio_afe_set_aec(want != 0);
                aec_on = want;
            }
        }
        int n = es8311_codec_read_frames(stereo, OPUS_ENC_FRAME_SAMPLES);
        if (n < OPUS_ENC_FRAME_SAMPLES) {
#if CONFIG_VIMATE_DIAG_ENABLE
            s_st_feed_short++;
#endif
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
#if CONFIG_VIMATE_DIAG_ENABLE
        s_st_feed++;
#endif
#if CONFIG_VIMATE_DIAG_ENABLE
        /* Ghi mic THÔ (tổng 2 mic, 24k) song song với capture "afe" để biết rác sinh
         * trước hay sau AFE: kích cùng lúc capture afe bắt đầu (cùng cửa sổ thời gian). */
        if (!s_cap_uplink.dumped && wcap_alloc(&s_cap_uplink)) {
            if (s_cap_uplink.len < 0 && s_cap_afe.len >= 0) wcap_trigger(&s_cap_uplink, "cung luc capture afe");
            if (s_cap_uplink.len >= 0 && s_cap_uplink.len < s_cap_uplink.total) {
                static int16_t mono[OPUS_ENC_FRAME_SAMPLES];
                for (int i = 0; i < n; i++) {
                    int32_t v = (int32_t)stereo[nch * i + BOARD_AUDIO_TDM_MIC_A] + stereo[nch * i + BOARD_AUDIO_TDM_MIC_B];
                    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                    mono[i] = (int16_t)v;
                }
                wcap_feed(&s_cap_uplink, mono, n);
            }
        }
#endif
        audio_afe_feed_frames24(stereo, n, nch);
#if ASR_UPLINK_RAW_MIC
        /* Uplink ASR = mic THÔ (bypass méo AFE). CHỈ downmix in-place + đẩy ring
         * (nhẹ, không chiếm Core1); encode+gửi do fetch_task (prio 3). VAD EOT vẫn
         * do fetch quyết (s_vad_stop_signaled). Tắt khi loa phát để không vọng. */
        if (s_mic_active && !s_mic_send_muted && !s_vad_stop_signaled) {
            for (int i = 0; i < n; i++) {
                int32_t s = (int32_t)stereo[nch * i + BOARD_AUDIO_TDM_MIC_A]
                          + stereo[nch * i + BOARD_AUDIO_TDM_MIC_B];
                if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                stereo[i] = (int16_t)s;   /* downmix vào đầu buffer (i <= nch*i, an toàn) */
            }
            raw_ring_push(stereo, n);
        }
#endif
        vTaskDelay(1);
    }
}

static void spk_task(void *arg) {
    static int16_t pcm[OPUS_DEC_FRAME_SAMPLES];
    opus_pkt_t pkt;
#if CONFIG_VIMATE_DIAG_ENABLE
    /* Kiểm pseudostack TLS của Opus ngay trong task này (mỗi task một TLS riêng).
     * Trước 12/09 trên P4 dòng này sẽ panic ngay — xem app_main.c g_vimate_tls_anchor. */
    {
        extern _Thread_local int g_vimate_tls_anchor;
        int n = opus_codec_decode_selftest();
        ESP_LOGI(TAG_AUDIO, "opus decode self-test trong spk_task: %d mau (tls_anchor=%d, mong 480 / 1)",
                 n, g_vimate_tls_anchor);
    }
#endif
    while (1) {
        /* Prebuffer: đầu mỗi lượt phát, chờ tích đủ SPK_PREBUFFER_FRAMES frame
         * (hoặc hết SPK_PREBUFFER_MAX_MS) rồi mới rút queue → dựng lớp đệm hấp
         * thụ jitter wifi, hết underrun/giật giữa câu. Trần thời gian để câu ngắn
         * (ít hơn prebuffer) không bị kẹt chờ. */
        if (s_spk_prebuffering) {
            UBaseType_t depth = uxQueueMessagesWaiting(s_spk_queue);
            int64_t elapsed_ms = (esp_timer_get_time() - s_spk_start_us) / 1000;
            if (depth >= SPK_PREBUFFER_FRAMES || elapsed_ms >= SPK_PREBUFFER_MAX_MS) {
                s_spk_prebuffering = false;
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }
        if (xQueueReceive(s_spk_queue, &pkt, pdMS_TO_TICKS(250)) != pdTRUE) {
            if (s_spk_active) {
                int64_t now_us = esp_timer_get_time();
                int64_t idle_ms = (now_us - s_spk_last_frame_us) / 1000;
                int timeout_ms = s_spk_received_frames > 0
                    ? SPK_IDLE_TIMEOUT_MS
                    : SPK_FIRST_FRAME_TIMEOUT_MS;
                if (idle_ms >= timeout_ms) {
                    ESP_LOGE(TAG_AUDIO,
                             "TTS watchdog timeout idle=%lldms frames=%u -> recover",
                             (long long)idle_ms,
                             (unsigned)s_spk_received_frames);
                    audio_pipeline_speaker_stop();
                    if (g_vimate_events) {
                        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_TTS_TIMEOUT);
                    }
                }
            }
            continue;
        }
        if (!s_spk_active) {
            heap_caps_free(pkt.data);
            continue;
        }
        int n = -1;
        if (pkt.is_pcm) {
            if ((pkt.len & 1U) == 0 && pkt.len <= sizeof(pcm)) {
                n = (int)(pkt.len / sizeof(int16_t));
                memcpy(pcm, pkt.data, pkt.len);
                static uint32_t s_pcm_downlink_seq;
                if (++s_pcm_downlink_seq <= 8) {
                    ESP_LOGI(TAG_AUDIO, "PCM downlink #%u samples=%d bytes=%u",
                             (unsigned)s_pcm_downlink_seq, n, (unsigned)pkt.len);
                }
            } else {
                ESP_LOGW(TAG_AUDIO, "drop invalid PCM downlink bytes=%u", (unsigned)pkt.len);
            }
        } else {
            n = opus_codec_decode(pkt.data, pkt.len, pcm, OPUS_DEC_FRAME_SAMPLES);
        }
        heap_caps_free(pkt.data);
        if (n > 0) {
            apply_speaker_tts_headroom(pcm, n);
            i2s_output_write_pcm16(pcm, (size_t)n, 500);
            audio_afe_push_ref24(pcm, n); /* PCM loa → ref AEC cho AFE */
            barge_in_feed_ref(pcm, n); /* ref AEC standalone; no-op khi tắt */
        }
    }
}

esp_err_t audio_pipeline_init(void) {
    s_pipeline_ready = false;
    s_mic_active = false;
    s_spk_active = false;
    s_wake_active = false;

    /* QUAN TRỌNG (refactor theo xiaozhi): es8311_codec_init() phải gọi
     * TRƯỚC. Nó OWN I2S full-duplex + esp_codec_dev wrapper. i2s_input/output
     * giờ chỉ là thin wrapper delegate. */
    if (es8311_codec_init() != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "ES8311 init FAIL — audio chain sẽ silent!");
        return ESP_FAIL;
    }
    /* Boot-safety: lỗi audio → trả ESP_FAIL (KHÔNG abort) để thiết bị vẫn boot
     * + giữ WS/OTA recovery thay vì boot-loop. */
    esp_err_t aerr;
    if ((aerr = i2s_output_init()) != ESP_OK) { ESP_LOGE(TAG_AUDIO, "i2s_output_init lỗi"); return aerr; }
    if ((aerr = i2s_input_init()) != ESP_OK) { ESP_LOGE(TAG_AUDIO, "i2s_input_init lỗi"); return aerr; }
    if ((aerr = opus_codec_init()) != ESP_OK) { ESP_LOGE(TAG_AUDIO, "opus_codec_init lỗi"); return aerr; }
    (void)mic_hpf_init();
    /* AFE 2-mic TẠM TẮT runtime (12/06 đêm): pipeline AFE chạy nhưng còn 2 lỗi
     * chưa giải — (1) PCM lên server toàn 0 trong lượt nghe (enc_peak diag đã
     * cài, chưa bắt được vì lệch nhịp test), (2) loa còn chập chờn nhẹ. Trả về
     * đường mic_task/WakeNet cũ (bản "nghe rõ hơn" user xác nhận) để máy DÙNG
     * ĐƯỢC. Bật lại AFE: đổi AFE_RUNTIME_ENABLE=1 (mọi code giữ nguyên). */
/* AFE_RUNTIME_ENABLE 0 (13/06): ESP-SR AFE (AEC/BSS/NS) treo Core 1 → task_wdt +
 * rớt WS mỗi ~75s, ĐỒNG THỜI băm nát speech (ASR rác) + "Hi Lily" không chạy.
 * Hại hơn lợi → TẮT. Dùng đường mic_task: mic THÔ → opus (ASR tốt, đã chứng minh)
 * + WakeNet + voice-activated năng lượng. Server RMS-gate chặn ồn nền. */
#ifndef BOARD_AUDIO_AFE_RUNTIME
#define BOARD_AUDIO_AFE_RUNTIME 0
#endif
#define AFE_RUNTIME_ENABLE BOARD_AUDIO_AFE_RUNTIME   /* P4 4.3": 1 (board header) */
#if !AFE_RUNTIME_ENABLE
    (void)wake_init();
#endif
    barge_in_init(); /* no-op khi CONFIG_VIMATE_BARGE_IN_ENABLE tắt */

    /* Queue 128 frames × 60ms = 7.7s buffer. Server hay burst-send full
     * sentence (~100 frames trong vài trăm ms) trước khi I2S kịp drain real-time.
     * 16 frames original drop ngay khi >960ms audio arrived. */
    s_spk_queue = xQueueCreate(128, sizeof(opus_pkt_t));
    configASSERT(s_spk_queue);

    /* Stack 20KB — silk_P_Ana_calc_corr_st3 (pitch analysis) peak ~15KB stack.
     * Trước 16KB crash IllegalInstruction (return addr corrupt). Sau khi đổi
     * LVGL pool 64KB → CLIB heap, có thêm DRAM nên bump an toàn.
     * 24KB trước đây fail WS spawn vì còn LV pool ăn 64KB DRAM. */
#if AFE_RUNTIME_ENABLE
    /* AFE feed task stack NHỎ (không opus): RAM nội trước WiFi phải tối thiểu.
     * Fetch task (16KB, có opus) do feed task spawn SAU khi AFE init OK. */
    /* P4 (BOARD_AUDIO_FEED_TASK_PRIO 7): RX DMA chỉ 60 ms = đúng 1 lần đọc, không có
     * lề; feed ở 6 ngang task AFE (6) và dưới spk_dec (7) → lúc loa phát mỗi 10 s mất
     * 3–5 lần đọc (feed=162–164/167, log 13/09 18:xx). Feed chỉ downsample + đẩy ring
     * (~1 ms) nên đặt 7 là an toàn; S3 giữ 6. */
    xTaskCreatePinnedToCore(afe_feed_task, "afe_feed", 4096, NULL,
                            BOARD_AUDIO_FEED_TASK_PRIO, NULL, VIMATE_TASK_CORE_IO);
#else
    (void)afe_feed_task; /* giữ code AFE compile, tắt runtime */
    xTaskCreatePinnedToCore(mic_task, "mic_enc", VIMATE_TASK_STACK_AUDIO_MIC, NULL,
                            VIMATE_TASK_PRIO_AUDIO, NULL, VIMATE_TASK_CORE_IO);
#endif
    /* opus_decode nhẹ ~6KB. Audio prio cao nhất trong app để I2S không giật;
     * worker ảnh/OTA đã hạ dưới audio nên không tranh CPU realtime. */
    xTaskCreatePinnedToCore(spk_task, "spk_dec", VIMATE_TASK_STACK_AUDIO_SPK, NULL,
                            VIMATE_TASK_PRIO_AUDIO, NULL, VIMATE_TASK_CORE_IO);

    i2s_output_set_volume(BOARD_SPK_VOLUME_DEFAULT);
    s_pipeline_ready = true;
    ESP_LOGI(TAG_AUDIO, "Audio pipeline ready");
    return ESP_OK;
}

void audio_pipeline_set_paused(bool paused) {
    s_paused = paused;
    if (paused) {
        /* IM + ĐIẾC: dừng TTS đang phát + ngừng nghe + ngừng cả wake → feed_task
         * idle, không còn tự mở lượt do ồn nền. Chỉ "resume" mới bật lại. */
        audio_pipeline_speaker_stop();
        audio_pipeline_mic_stop();
        audio_pipeline_wake_stop();
        ESP_LOGW(TAG_AUDIO, "PAUSED (app) — TTS+mic+wake off");
    } else {
        audio_pipeline_wake_start();
        ESP_LOGW(TAG_AUDIO, "RESUMED (app) — wake on");
    }
}

bool audio_pipeline_is_paused(void) { return s_paused; }

void audio_pipeline_mic_start(void) {
    if (!pipeline_ready()) {
        ESP_LOGW(TAG_AUDIO, "Mic start ignored: audio pipeline not ready");
        s_mic_active = false;
        s_wake_active = false;
        return;
    }
    i2s_input_start();
    mic_hpf_reset();
    vad_reset();
    s_mic_streamed_frames = 0;
    s_mic_active = true;
    s_wake_active = false;
    wake_reset();
    ESP_LOGI(TAG_AUDIO, "Mic ON");
}

void audio_pipeline_mic_stop(void) {
    s_mic_active = false;
    if (!pipeline_ready()) return;
    if (!s_wake_active) {
        i2s_input_stop();
    }
    ESP_LOGI(TAG_AUDIO, "Mic OFF");
}

uint32_t audio_pipeline_mic_streamed_frames(void) {
    return s_mic_streamed_frames;
}

void audio_pipeline_mic_mute(bool mute) {
    if (!mute) {
        mic_hpf_reset();
        vad_reset();
        s_mic_streamed_frames = 0;
    }
    s_mic_send_muted = mute;
    /* Ko log mỗi turn để tránh UART mutex contention. */
}

void audio_pipeline_wake_start(void) {
    if (!pipeline_ready() || s_paused) return;
    if (audio_afe_ready()) {
        /* AFE bundle WakeNet: chỉ cần bật input + cờ wake; detect ở fetch task. */
        if (!ws_client_is_connected()) return;
        if (!s_mic_active) {
            i2s_input_start();
        }
        mic_hpf_reset();
        s_wake_active = true;
        ESP_LOGI(TAG_AUDIO, "WakeNet ON (AFE: Hi Lily)");
        return;
    }
#if CONFIG_VIMATE_WAKE_WORD_ENABLE
    if (!s_wake_ready || !ws_client_is_connected()) return;
    if (!s_mic_active) {
        i2s_input_start();
    }
    wake_reset();
    mic_hpf_reset();
    s_wake_active = true;
    ESP_LOGI(TAG_AUDIO, "WakeNet ON (Hi Lily)");
#endif
}

void audio_pipeline_wake_stop(void) {
    if (!s_wake_active) return;
    s_wake_active = false;
    wake_reset();
    if (!pipeline_ready()) return;
    if (!s_mic_active) {
        i2s_input_stop();
    }
    ESP_LOGI(TAG_AUDIO, "WakeNet OFF");
}

bool audio_pipeline_wake_available(void) {
    if (pipeline_ready() && audio_afe_ready()) return true;
#if CONFIG_VIMATE_WAKE_WORD_ENABLE
    return pipeline_ready() && s_wake_ready;
#else
    return false;
#endif
}

void audio_pipeline_speaker_start(void) {
    if (s_paused) {
        /* PAUSED: từ chối mở lượt phát mới (server gửi TTS câu kế sau pause) →
         * im tức thì, không lố câu. Chỉ "resume" mới cho phát lại. */
        return;
    }
    if (!pipeline_ready()) {
        ESP_LOGW(TAG_AUDIO, "Speaker start ignored: audio pipeline not ready");
        s_spk_active = false;
        return;
    }
    audio_pipeline_wake_stop();
    opus_codec_reset_decoder();
    i2s_output_start();
    /* Bật prebuffer cho lượt phát này: spk_task chờ tích đủ đệm trước khi rút. */
    s_spk_start_us = esp_timer_get_time();
    s_spk_last_frame_us = s_spk_start_us;
    s_spk_received_frames = 0;
    s_spk_prebuffering = true;
    s_spk_active = true;
    servo_emotion_set_speaking(true);
#if CONFIG_VIMATE_BARGE_IN_ENABLE
    /* Barge-in: giữ mic chạy trong lúc phát (full-duplex) để nghe trẻ ngắt lời. */
    i2s_input_start();
    barge_in_reset();
    s_barge_listen = true;
#endif
    if (audio_afe_ready()) {
        /* AFE: mic phải chạy trong lúc phát — AEC cần mic+ref đồng thời để khử
         * vọng + barge-in nghe trẻ chen ngang trên giọng đã khử echo. */
        i2s_input_start();
    }
    ESP_LOGI(TAG_AUDIO, "Speaker ON");
}

void audio_pipeline_speaker_stop(void) {
    s_spk_active = false;
    s_spk_prebuffering = false;
    servo_emotion_set_speaking(false);
#if CONFIG_VIMATE_BARGE_IN_ENABLE
    s_barge_listen = false;
#endif
    if (!s_spk_queue) return;
    /* Drain queue */
    opus_pkt_t pkt;
    while (xQueueReceive(s_spk_queue, &pkt, 0) == pdTRUE) {
        heap_caps_free(pkt.data);
    }
    /* KHÔNG log ở đây — gọi từ WS task context có thể trigger UART mutex
     * assert khi scheduler suspended. */
    if (s_pipeline_ready) {
        i2s_output_stop();
    }
}

bool audio_pipeline_speaker_is_active(void) {
    return pipeline_ready() && s_spk_active;
}

bool audio_pipeline_speaker_pending(void) {
    if (!pipeline_ready() || !s_spk_active || !s_spk_queue) return false;
    return uxQueueMessagesWaiting(s_spk_queue) > 0;
}

void audio_pipeline_speaker_push(const uint8_t *opus_frame, size_t len) {
    /* PAUSED: bỏ MỌI frame TTS (server có thể còn gửi sau lệnh pause do abort trễ)
     * → không phát lố câu nào. */
    if (s_paused) return;
    if (!pipeline_ready() || !s_spk_active || len == 0) return;
    s_spk_last_frame_us = esp_timer_get_time();
    s_spk_received_frames++;
    bool is_pcm = len > sizeof(SPK_PCM_MAGIC) &&
                  memcmp(opus_frame, SPK_PCM_MAGIC, sizeof(SPK_PCM_MAGIC)) == 0;
    const uint8_t *payload = is_pcm ? opus_frame + sizeof(SPK_PCM_MAGIC) : opus_frame;
    size_t payload_len = is_pcm ? len - sizeof(SPK_PCM_MAGIC) : len;
    if (payload_len == 0) return;
    opus_pkt_t pkt = {
        .data = heap_caps_malloc(payload_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .len = payload_len,
        .is_pcm = is_pcm,
    };
    if (!pkt.data) {
        pkt.data = heap_caps_malloc(payload_len, MALLOC_CAP_8BIT);
    }
    if (!pkt.data) return;
    memcpy(pkt.data, payload, payload_len);
    if (xQueueSend(s_spk_queue, &pkt, 0) != pdTRUE) {
        /* Throttle log + đếm: khi WiFi/loa nghẽn có thể drop hàng chục frame/giây,
         * log từng cái sẽ spam (và tự làm chậm thêm). 1 dòng mỗi 20 lần cho thấy rate. */
        static uint32_t s_spk_drop;
        if ((s_spk_drop++ % 20) == 0) {
            ESP_LOGW(TAG_AUDIO, "spk queue full — drop frame (tổng %u; WiFi/loa nghẽn?)",
                     (unsigned)s_spk_drop);
        }
        heap_caps_free(pkt.data);
    }
}

void audio_pipeline_set_volume(int percent) {
    if (!pipeline_ready()) return;
    i2s_output_set_volume(percent);
}

void audio_pipeline_set_mic_gain(int percent) {
    if (!pipeline_ready()) return;
    es8311_codec_set_mic_gain(percent);
    vad_reset();
}

void audio_pipeline_set_noise_profile(const char *environment,
                                      const char *speaker_distance,
                                      int noise_level,
                                      int speech_level,
                                      bool aec_enabled,
                                      bool dual_mic_enabled) {
    const char *env = environment && environment[0] ? environment : "normal";
    const char *dist = speaker_distance && speaker_distance[0] ? speaker_distance : "normal";
    if (strcmp(env, "quiet") != 0 && strcmp(env, "noisy") != 0 &&
        strcmp(env, "child_soft") != 0) {
        env = "normal";
    }
    if (strcmp(dist, "near") != 0 && strcmp(dist, "far") != 0) {
        dist = "normal";
    }
    strlcpy(s_audio_environment, env, sizeof(s_audio_environment));
    strlcpy(s_audio_speaker_distance, dist, sizeof(s_audio_speaker_distance));
    s_audio_noise_level = clamp_percent(noise_level, 45);
    s_audio_speech_level = clamp_percent(speech_level, 65);
    s_audio_aec_enabled = aec_enabled;
    s_audio_dual_mic_enabled = dual_mic_enabled;
    s_vad_rms_threshold = compute_vad_threshold(s_vad_sensitivity);
    vad_reset();
    ESP_LOGI(TAG_AUDIO,
             "Audio profile env=%s distance=%s noise=%d speech=%d dual_mic=%d aec=%d threshold=%d",
             s_audio_environment,
             s_audio_speaker_distance,
             (int)s_audio_noise_level,
             (int)s_audio_speech_level,
             s_audio_dual_mic_enabled ? 1 : 0,
             s_audio_aec_enabled ? 1 : 0,
             (int)s_vad_rms_threshold);
}

void audio_pipeline_set_vad_config(int sensitivity, int silence_ms) {
    if (sensitivity < 0) sensitivity = 0;
    if (sensitivity > 100) sensitivity = 100;
    if (silence_ms <= 0) silence_ms = VAD_SILENCE_MS_TO_STOP_DEFAULT;
    if (silence_ms < 300) silence_ms = 300;
    if (silence_ms > 3000) silence_ms = 3000;
    s_vad_sensitivity = sensitivity;
    int threshold = compute_vad_threshold(sensitivity);
    s_vad_rms_threshold = threshold;
    s_vad_silence_ms_to_stop = silence_ms;
    vad_reset();
    ESP_LOGI(TAG_AUDIO, "VAD sensitivity=%d threshold=%d silence=%dms env=%s distance=%s",
             sensitivity, threshold, silence_ms,
             s_audio_environment, s_audio_speaker_distance);
}

void audio_pipeline_apply_audio_config(int mic_gain_percent,
                                       int vad_sensitivity,
                                       int vad_silence_ms,
                                       const char *environment,
                                       const char *speaker_distance,
                                       int noise_level,
                                       int speech_level,
                                       bool aec_enabled,
                                       bool dual_mic_enabled) {
    if (mic_gain_percent >= 0) {
        audio_pipeline_set_mic_gain(mic_gain_percent);
    }
    audio_pipeline_set_noise_profile(environment, speaker_distance, noise_level,
                                     speech_level, aec_enabled, dual_mic_enabled);
    audio_pipeline_set_vad_config(vad_sensitivity >= 0 ? vad_sensitivity : s_vad_sensitivity,
                                  vad_silence_ms >= 0 ? vad_silence_ms : s_vad_silence_ms_to_stop);
}
