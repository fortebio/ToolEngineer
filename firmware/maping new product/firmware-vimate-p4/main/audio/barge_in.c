/**
 * barge_in.c — Voice barge-in detection (AEC + VAD). MẶC ĐỊNH TẮT.
 * Xem barge_in.h cho tổng quan. Toàn bộ file chỉ biên dịch khi
 * CONFIG_VIMATE_BARGE_IN_ENABLE=y (build verify) — runtime mặc định OFF.
 */
#include "audio/barge_in.h"

#if CONFIG_VIMATE_BARGE_IN_ENABLE

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_aec.h"

static const char *TAG_BI = "barge_in";

/* AEC: chỉ hỗ trợ 16kHz, frame 32ms = 512 mẫu. Audio app @24kHz → resample 3:2. */
#define BI_AEC_RATE   16000
#define BI_AEC_FRAME  512                 /* 32 ms @ 16 kHz */
#define BI_SRC_FRAME  768                 /* 32 ms @ 24 kHz (768*2/3 = 512)      */
#define BI_REF_RING   (BI_AEC_FRAME * 8)  /* đệm ref để căn theo độ trễ echo     */

static aec_handle_t   *s_aec;
static SemaphoreHandle_t s_lock;

/* Gom mẫu 24k thành block BI_SRC_FRAME trước khi resample. */
static int16_t s_mic_acc[BI_SRC_FRAME];
static int     s_mic_n;
static int16_t s_ref_acc[BI_SRC_FRAME];
static int     s_ref_n;

/* Ring chứa ref đã resample @16k. */
static int16_t *s_ref_ring;
static int      s_ref_w;

static int  s_speech_frames;
static volatile bool s_triggered;

/* Resample 24k→16k thô (linear 3 vào → 2 ra). Đủ cho mục đích VAD/AEC; có thể
 * thay bằng esp-dsp resampler chất lượng cao hơn nếu cần. */
static void bi_resample_24_16(const int16_t *in, int16_t *out) {
    int o = 0;
    for (int i = 0; i + 2 < BI_SRC_FRAME; i += 3) {
        int a = in[i], b = in[i + 1], c = in[i + 2];
        out[o++] = (int16_t)((2 * a + b) / 3);
        out[o++] = (int16_t)((b + 2 * c) / 3);
    }
}

void barge_in_init(void) {
    if (s_aec) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_ref_ring) {
        s_ref_ring = heap_caps_calloc(BI_REF_RING, sizeof(int16_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ref_ring) s_ref_ring = calloc(BI_REF_RING, sizeof(int16_t));
    }
    if (!s_lock || !s_ref_ring) {
        ESP_LOGE(TAG_BI, "barge-in alloc fail → disabled");
        return;
    }
    /* filter_length 4 (khuyến nghị), mode low-cost cho ASR. */
    s_aec = aec_create(BI_AEC_RATE, CONFIG_VIMATE_BARGE_IN_AEC_FILTER_LEN, 1,
                       AEC_MODE_SR_LOW_COST);
    if (!s_aec) {
        ESP_LOGE(TAG_BI, "aec_create fail → barge-in disabled");
        return;
    }
    ESP_LOGI(TAG_BI, "barge-in AEC ready (filter=%d rms_thr=%d frames=%d)",
             CONFIG_VIMATE_BARGE_IN_AEC_FILTER_LEN,
             CONFIG_VIMATE_BARGE_IN_RMS_THRESHOLD,
             CONFIG_VIMATE_BARGE_IN_SPEECH_FRAMES);
}

void barge_in_reset(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_mic_n = 0;
    s_ref_n = 0;
    s_ref_w = 0;
    s_speech_frames = 0;
    s_triggered = false;
    if (s_ref_ring) memset(s_ref_ring, 0, BI_REF_RING * sizeof(int16_t));
    xSemaphoreGive(s_lock);
}

void barge_in_feed_ref(const int16_t *pcm24, int n) {
    if (!s_aec || !s_lock || !pcm24) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < n; i++) {
        s_ref_acc[s_ref_n++] = pcm24[i];
        if (s_ref_n == BI_SRC_FRAME) {
            int16_t r16[BI_AEC_FRAME];
            bi_resample_24_16(s_ref_acc, r16);
            for (int j = 0; j < BI_AEC_FRAME; j++) {
                s_ref_ring[s_ref_w] = r16[j];
                s_ref_w = (s_ref_w + 1) % BI_REF_RING;
            }
            s_ref_n = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

void barge_in_feed_mic(const int16_t *pcm24, int n) {
    if (!s_aec || !s_lock || !pcm24 || s_triggered) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < n && !s_triggered; i++) {
        s_mic_acc[s_mic_n++] = pcm24[i];
        if (s_mic_n < BI_SRC_FRAME) continue;
        s_mic_n = 0;

        int16_t m16[BI_AEC_FRAME], ref[BI_AEC_FRAME], out[BI_AEC_FRAME];
        bi_resample_24_16(s_mic_acc, m16);

        /* Lấy frame ref căn theo độ trễ echo (tunable). delay=0 → frame ref mới
         * nhất; tăng nếu loa→mic có trễ. */
        int delay = (CONFIG_VIMATE_BARGE_IN_REF_DELAY_FRAMES + 1) * BI_AEC_FRAME;
        int start = ((s_ref_w - delay) % BI_REF_RING + BI_REF_RING) % BI_REF_RING;
        for (int j = 0; j < BI_AEC_FRAME; j++) {
            ref[j] = s_ref_ring[(start + j) % BI_REF_RING];
        }

        aec_process(s_aec, m16, ref, out);

        /* RMS tín hiệu đã khử echo: phần còn lại chủ yếu là giọng trẻ (nếu có). */
        int64_t sum = 0;
        for (int j = 0; j < BI_AEC_FRAME; j++) sum += (int32_t)out[j] * out[j];
        int rms = (int)sqrt((double)sum / BI_AEC_FRAME);

        if (rms >= CONFIG_VIMATE_BARGE_IN_RMS_THRESHOLD) {
            if (++s_speech_frames >= CONFIG_VIMATE_BARGE_IN_SPEECH_FRAMES) {
                s_triggered = true;
                ESP_LOGI(TAG_BI, "BARGE-IN: trẻ nói khi đang phát (rms=%d)", rms);
            }
        } else if (s_speech_frames > 0) {
            s_speech_frames--;
        }
    }
    xSemaphoreGive(s_lock);
}

bool barge_in_triggered(void) {
    return s_triggered;
}

#endif /* CONFIG_VIMATE_BARGE_IN_ENABLE */
