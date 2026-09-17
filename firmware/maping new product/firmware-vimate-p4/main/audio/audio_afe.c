/**
 * audio_afe.c — ESP-SR AFE lifecycle. Bước 2: tạo handle + đo RAM gate.
 * Xem audio_afe.h.
 */
#include "audio/audio_afe.h"
#include "boards/board.h"   /* BOARD_AUDIO_AFE_* knobs */

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"   /* ESP_WN_PREFIX, DET_MODE_90 */
#include "model_path.h"     /* esp_srmodel_init, esp_srmodel_filter */

static const char *TAG = "vimate.afe";

#ifndef BOARD_AUDIO_AFE_FORMAT
#define BOARD_AUDIO_AFE_FORMAT "MMR"
#endif
#ifndef BOARD_AUDIO_AFE_HIGH_PERF
#define BOARD_AUDIO_AFE_HIGH_PERF 0
#endif
#ifndef BOARD_AUDIO_AFE_LINEAR_GAIN
#define BOARD_AUDIO_AFE_LINEAR_GAIN 1.0f
#endif
static int s_in_nch = 2;           /* số kênh của frame feed vào (2: mềm ref; 4: TDM) */

static const esp_afe_sr_iface_t *s_h;
static esp_afe_sr_data_t        *s_afe;
static int s_feed_chunk, s_feed_ch, s_fetch_chunk;

/* ===== Feed/ref buffers (cấp trong init theo feed_chunk) ===== *
 * Mic 24k (board share I2S WS nên không hạ 16k được) → resample 3:2 sang 16k,
 * gom đủ feed_chunk mẫu/kênh rồi interleave [m0,m1,ref] feed AFE. Ref = PCM loa
 * 24k (cũng 3:2 →16k) đẩy vào ring; rỗng khi không phát → AEC vẫn hội tụ. */
static int16_t *s_il;          /* interleaved feed buffer: feed_chunk*feed_ch */
static int16_t *s_acc0, *s_acc1; /* accumulator mic0/mic1 @16k */
static int      s_acc_n;       /* số mẫu/kênh đã gom */
static int      s_acc_cap;     /* = feed_chunk + 1 frame dự phòng */
static int16_t *s_refring;     /* ring ref @16k (PSRAM) */
static int      s_ref_cap, s_ref_w, s_ref_r;
static SemaphoreHandle_t s_afe_lock;

bool audio_afe_ready(void)       { return s_afe != NULL; }
int  audio_afe_feed_chunk(void)   { return s_feed_chunk; }
int  audio_afe_feed_channels(void){ return s_feed_ch; }
int  audio_afe_fetch_chunk(void)  { return s_fetch_chunk; }

esp_err_t audio_afe_init(void) {
    if (s_afe) return ESP_OK;

    uint32_t i0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t p0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num <= 0) {
        ESP_LOGE(TAG, "esp_srmodel_init('model') trống → AFE off");
        return ESP_ERR_NOT_FOUND;
    }

    /* "MMR" = mic0, mic1, ref loa (ref là kênh CUỐI theo yêu cầu feed). P4 TDM:
     * "MMNR" — 4 kênh thẳng từ ES7210 [MIC2, MIC1, MIC4, MIC3=ref cứng]. */
    afe_config_t *cfg = afe_config_init(BOARD_AUDIO_AFE_FORMAT, models, AFE_TYPE_SR,
                                        BOARD_AUDIO_AFE_HIGH_PERF ? AFE_MODE_HIGH_PERF : AFE_MODE_LOW_COST);
    if (!cfg) {
        ESP_LOGE(TAG, "afe_config_init NULL");
        return ESP_FAIL;
    }

    /* CHẨN ĐOÁN 12/06: mọi tổ hợp override (ringbuf=4, aec_mode, fixed_output_
     * channel, prio...) đều cho feed REJECTED ret=0 + fetch ret=-1 vĩnh viễn.
     * Task list xác nhận lib KHÔNG spawn worker riêng → override có thể phá flow
     * nội bộ. Thử CONFIG NGUYÊN BẢN từ afe_config_init (auto-enable theo "MMR"
     * + chip) — CHỈ ép MORE_PSRAM (bắt buộc, RAM nội căng). */
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_linear_gain = BOARD_AUDIO_AFE_LINEAR_GAIN;
    /* Task xử lý của AFE (WakeNet/VAD/AGC) mặc định core 0 prio 5 — chung core với
     * taskLVGL (6)/touch (7)/main+WS (5). Log 13/09 17:00: fetch 276–308/313 lúc loa
     * phát + decode ảnh, và UI chờ lock 95–109 ms. Core 1 chỉ có afe_feed (6),
     * spk_dec (7), afe_fetch, img_worker → chuyển sang đó (README-P4 §9.3). */
    cfg->afe_perferred_core = BOARD_AUDIO_AFE_CORE;
    cfg->afe_perferred_priority = BOARD_AUDIO_AFE_PRIO;
#if defined(BOARD_AUDIO_AFE_AGC) && BOARD_AUDIO_AFE_AGC
    cfg->agc_init = true;
    cfg->agc_mode = AFE_AGC_MODE_WEBRTC;
    cfg->agc_compression_gain_db = 9;
    cfg->agc_target_level_dbfs = 3;
#elif defined(BOARD_AUDIO_AFE_AGC)
    cfg->agc_init = false;
#endif
#ifdef BOARD_AUDIO_AFE_VAD_MODE
    cfg->vad_mode = (vad_mode_t)BOARD_AUDIO_AFE_VAD_MODE;
#endif

    cfg = afe_config_check(cfg);
    afe_config_print(cfg);

    s_h = esp_afe_handle_from_config(cfg);
    if (!s_h) {
        ESP_LOGE(TAG, "esp_afe_handle_from_config NULL");
        afe_config_free(cfg);
        return ESP_FAIL;
    }
    s_afe = s_h->create_from_config(cfg);
    /* KHÔNG afe_config_free(cfg): lib giữ con trỏ vào config (mic_ids/ref_ids/
     * model names) — free xong internal task đọc vùng đã free → feed REJECTED
     * ret=0 + fetch ret=-1 vĩnh viễn (12/06). Giữ cfg sống suốt đời AFE
     * (singleton, không leak lặp). */
    if (!s_afe) {
        ESP_LOGE(TAG, "create_from_config NULL — thiếu RAM? thử hạ cấp (filter 2 / bỏ se)");
        return ESP_ERR_NO_MEM;
    }

    s_feed_chunk  = s_h->get_feed_chunksize(s_afe);
    s_feed_ch     = s_h->get_feed_channel_num(s_afe);
    s_fetch_chunk = s_h->get_fetch_chunksize(s_afe);
    /* (Lưu ý 12/06: disable_se RUNTIME trên format MMR làm output rail 32768 —
     * đường 2-mic-không-SE của lib hỏng, khớp vụ se_init=false panic. GIỮ AEC+SE
     * luôn bật.) Hạ ngưỡng WakeNet ~0.6 (mặc định DET_MODE_90 ≈ 0.9): model
     * wn9_hilili_tts vốn thất thường với giọng Việt — tăng nhạy, đổi chút
     * false-accept (chấp nhận cho thiết bị trẻ em có chạm-nói làm chính). */
    s_h->set_wakenet_threshold(s_afe, 1, 0.6f);
    s_h->print_pipeline(s_afe);

    /* Buffer feed: interleave + accumulator + ref ring. Mic frame 24k 60ms=1440
     * → 960 mẫu/kênh @16k; accumulator chứa feed_chunk + 1 frame dự phòng. */
    s_acc_cap = s_feed_chunk + 1024;
    s_il      = heap_caps_malloc((size_t)s_feed_chunk * s_feed_ch * sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* PSRAM: RAM nội phải chừa cho WiFi RX buffer (bài học boot-loop 12/06). */
    s_acc0    = heap_caps_malloc((size_t)s_acc_cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_acc1    = heap_caps_malloc((size_t)s_acc_cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ref_cap = s_feed_chunk * 8;
    s_refring = heap_caps_calloc(s_ref_cap, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_afe_lock = xSemaphoreCreateMutex();
    if (!s_il || !s_acc0 || !s_acc1 || !s_refring || !s_afe_lock) {
        ESP_LOGE(TAG, "AFE buffer alloc fail → AFE off");
        return ESP_ERR_NO_MEM;
    }


    uint32_t i1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t p1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGW(TAG, "AFE created: feed_chunk=%d feed_ch=%d fetch_chunk=%d samp=%d",
             s_feed_chunk, s_feed_ch, s_fetch_chunk, s_h->get_samp_rate(s_afe));
    ESP_LOGW(TAG, "AFE RAM gate: internal %u->%u (cost=%d B) | psram %u->%u (cost=%d B)",
             (unsigned)i0, (unsigned)i1, (int)(i0 - i1),
             (unsigned)p0, (unsigned)p1, (int)(p0 - p1));
    return ESP_OK;
}

void audio_afe_push_ref24(const int16_t *mono24, int n) {
    if (!s_afe || !s_refring || !mono24 || n < 3) return;
    if (s_in_nch == 4) return;   /* ref cứng từ ES7210 MIC3 — bỏ qua ref mềm */
    /* SPSC ring: chỉ writer (spk task) động w; reader (feed task) động r.
     * Đầy → bỏ mẫu mới (giữ căn chỉnh, AEC chịu được). */
    for (int i = 0; i + 2 < n; i += 3) {
        int16_t s2[2] = {
            mono24[i],
            (int16_t)(((int32_t)mono24[i + 1] + mono24[i + 2]) / 2),
        };
        for (int j = 0; j < 2; j++) {
            int nxt = (s_ref_w + 1) % s_ref_cap;
            if (nxt == s_ref_r) return; /* full → drop phần còn lại */
            s_refring[s_ref_w] = s2[j];
            s_ref_w = nxt;
        }
    }
}

void audio_afe_feed_frames24(const int16_t *pcm24, int frames, int nch)
{
    if (!s_afe || !s_il || !pcm24 || frames < 3) return;
    if (nch == 2) { audio_afe_feed_mic24(pcm24, frames); return; }
    if (nch != s_feed_ch) {
        static bool warned = false;
        if (!warned) { warned = true; ESP_LOGE(TAG, "feed nch=%d != AFE feed_ch=%d (format %s)", nch, s_feed_ch, BOARD_AUDIO_AFE_FORMAT); }
        return;
    }
    s_in_nch = nch;
    /* Ref CỨNG nằm sẵn trong frame → decimate 3:2 từng kênh, interleave y nguyên,
     * feed khi đủ feed_chunk. Accumulator dùng chung s_il làm kho (feed_chunk*nch)
     * + phần dư tối đa 1 frame ở s_acc0 (nch*1024 mẫu). */
    static int acc_n = 0;                 /* mẫu/kênh đã gom trong s_il */
    for (int i = 0; i + 2 < frames; i += 3) {
        const int16_t *f0 = pcm24 + (size_t)i * nch, *f1 = f0 + nch, *f2 = f1 + nch;
        /* mẫu 16k thứ nhất = f0; thứ hai = trung bình f1,f2 (cùng phép 3:2 như đường 2 kênh) */
        int16_t *o = s_il + (size_t)acc_n * nch;
        for (int c = 0; c < nch; c++) o[c] = f0[c];
        if (++acc_n >= s_feed_chunk) { s_h->feed(s_afe, s_il); acc_n = 0; }
        o = s_il + (size_t)acc_n * nch;
        for (int c = 0; c < nch; c++) o[c] = (int16_t)(((int32_t)f1[c] + f2[c]) / 2);
        if (++acc_n >= s_feed_chunk) { s_h->feed(s_afe, s_il); acc_n = 0; }
    }
}

void audio_afe_feed_mic24(const int16_t *stereo24, int frames) {
    if (!s_afe || !s_il || !stereo24 || frames < 3) return;
    if (s_feed_ch == 1) {
        /* Format "M": tổng bão hoà 2 mic → mono 24k → decimate 3:2 → feed. */
        static int acc_n = 0;
        for (int i = 0; i + 2 < frames; i += 3) {
            int32_t a = (int32_t)stereo24[2 * i] + stereo24[2 * i + 1];
            int32_t b = (int32_t)stereo24[2 * (i + 1)] + stereo24[2 * (i + 1) + 1];
            int32_t c = (int32_t)stereo24[2 * (i + 2)] + stereo24[2 * (i + 2) + 1];
            if (a > 32767) a = 32767; else if (a < -32768) a = -32768;
            int32_t bc = (b + c) / 2;
            if (bc > 32767) bc = 32767; else if (bc < -32768) bc = -32768;
            s_il[acc_n++] = (int16_t)a;
            if (acc_n >= s_feed_chunk) { s_h->feed(s_afe, s_il); acc_n = 0; }
            s_il[acc_n++] = (int16_t)bc;
            if (acc_n >= s_feed_chunk) { s_h->feed(s_afe, s_il); acc_n = 0; }
        }
        return;
    }
    /* deinterleave + decimate 3:2 mỗi kênh → gom accumulator @16k. */
    for (int i = 0; i + 2 < frames; i += 3) {
        int i0 = i * 2, i1 = (i + 1) * 2, i2 = (i + 2) * 2;
        if (s_acc_n + 2 > s_acc_cap) break; /* tràn (không nên xảy ra) */
        s_acc0[s_acc_n] = stereo24[i0];
        s_acc1[s_acc_n] = stereo24[i0 + 1];
        s_acc_n++;
        s_acc0[s_acc_n] = (int16_t)(((int32_t)stereo24[i1] + stereo24[i2]) / 2);
        s_acc1[s_acc_n] = (int16_t)(((int32_t)stereo24[i1 + 1] + stereo24[i2 + 1]) / 2);
        s_acc_n++;
    }
    /* Feed mỗi khi đủ feed_chunk mẫu/kênh: interleave [m0,m1,ref].
     * Gain số ×3 cho mic (kẹp chống clip): mức raw nói thường chỉ ~1.6k/32k
     * (-26dBFS) — quá nhỏ cho WakeNet (VAD bắt được nhưng wake không detect,
     * 12/06). Đường cũ WakeNet nhận audio đã qua HPF gain nên từng wake được. */
    while (s_acc_n >= s_feed_chunk) {
        for (int k = 0; k < s_feed_chunk; k++) {
            int b = k * s_feed_ch;
            /* KHÔNG gain đầu vào: ×3 đẩy AEC vào bão hòa → output rail DC 32768
             * → Opus loại DC → server rms=0 "chưa nghe rõ" (13/06). AFE có AGC
             * (agc_mode=WAKENET) tự nâng mức. Đưa raw mic thẳng vào. */
            s_il[b + 0] = s_acc0[k];
            s_il[b + 1] = s_acc1[k];
            int16_t ref = 0;
            if (s_ref_r != s_ref_w) {           /* pull 1 ref/mẫu (SPSC reader) */
                ref = s_refring[s_ref_r];
                s_ref_r = (s_ref_r + 1) % s_ref_cap;
            }
            s_il[b + 2] = ref;
        }
        s_h->feed(s_afe, s_il);
        int rem = s_acc_n - s_feed_chunk;
        if (rem > 0) {
            memmove(s_acc0, s_acc0 + s_feed_chunk, (size_t)rem * sizeof(int16_t));
            memmove(s_acc1, s_acc1 + s_feed_chunk, (size_t)rem * sizeof(int16_t));
        }
        s_acc_n = rem;
    }
}

void audio_afe_set_aec(bool on) {
    if (!s_afe || !s_h) return;
    if (!strchr(BOARD_AUDIO_AFE_FORMAT, 'R')) return;   /* không có ref → không có AEC */
    int r = on ? (s_h->enable_aec ? s_h->enable_aec(s_afe) : -1)
               : (s_h->disable_aec ? s_h->disable_aec(s_afe) : -1);
    ESP_LOGI(TAG, "AEC %s (ret=%d)", on ? "ON" : "OFF", r);
}

bool audio_afe_fetch(audio_afe_result_t *o) {
    if (!s_afe || !o) return false;
    afe_fetch_result_t *r = s_h->fetch(s_afe);
    if (!r || r->ret_value < 0) return false;
    o->pcm16      = r->data;
    o->samples    = r->data_size / (int)sizeof(int16_t);
    o->wake       = (r->wakeup_state == WAKENET_DETECTED);
    o->vad_speech = (r->vad_state == VAD_SPEECH);
    return true;
}
