/**
 * opus_codec.c — libopus wrapper. Mic uses CELT low-delay mode to avoid the
 * SILK VOIP encoder path stalling CPU1 on ESP32-S3.
 */
#include "opus_codec.h"
#include "vimate.h"
#include "opus.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static OpusEncoder *s_enc = NULL;
static OpusDecoder *s_dec = NULL;
static uint32_t s_dec_packet_seq = 0;
static uint32_t s_dec_drop_seq = 0;

#define OPUS_DEC_SILENCE_PACKET_MAX_BYTES 3

static const char *opus_downlink_mode_name(uint8_t toc) {
    if (toc & 0x80) return "celt";
    if ((toc & 0x60) == 0x60) return "hybrid";
    return "silk";
}

static const char *opus_bandwidth_name(int bw) {
    switch (bw) {
    case OPUS_BANDWIDTH_NARROWBAND: return "nb";
    case OPUS_BANDWIDTH_MEDIUMBAND: return "mb";
    case OPUS_BANDWIDTH_WIDEBAND: return "wb";
    case OPUS_BANDWIDTH_SUPERWIDEBAND: return "swb";
    case OPUS_BANDWIDTH_FULLBAND: return "fb";
    default: return "?";
    }
}

esp_err_t opus_codec_init(void) {
    int err = 0;
    s_enc = opus_encoder_create(OPUS_ENC_SAMPLE_RATE, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err != OPUS_OK || !s_enc) {
        ESP_LOGE(TAG_AUDIO, "opus enc create fail %d", err);
        return ESP_FAIL;
    }
    /* 48 kbps + complexity 3: mic nén rõ hơn (32k/cx1 cũ làm méo nhẹ → ASR dễ
     * nghe nhầm). Vẫn CELT low-delay; cx3 chỉ thêm ~vài ms/frame trên CPU1 S3. */
    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(48000));
    opus_encoder_ctl(s_enc, OPUS_SET_COMPLEXITY(3));
    /* VOICE thay vì MUSIC: bộ nén ưu tiên cấu trúc formant/thanh điệu tiếng Việt
     * cho ASR nghe rõ hơn (MUSIC làm mờ phụ âm). CELT low-delay nên tác dụng có
     * hạn nhưng đúng ngữ nghĩa, không hại. */
    opus_encoder_ctl(s_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(s_enc, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
    opus_encoder_ctl(s_enc, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(s_enc, OPUS_SET_PACKET_LOSS_PERC(0));
    opus_encoder_ctl(s_enc, OPUS_SET_DTX(0));

    s_dec = opus_decoder_create(OPUS_DEC_SAMPLE_RATE, 1, &err);
    if (err != OPUS_OK || !s_dec) {
        ESP_LOGE(TAG_AUDIO, "opus dec create fail %d", err);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG_AUDIO, "Opus codec ready (enc %dHz, dec %dHz)",
             OPUS_ENC_SAMPLE_RATE, OPUS_DEC_SAMPLE_RATE);
    return ESP_OK;
}

void opus_codec_reset_decoder(void) {
    s_dec_packet_seq = 0;
    s_dec_drop_seq = 0;
    if (!s_dec) return;
    int ret = opus_decoder_ctl(s_dec, OPUS_RESET_STATE);
    if (ret != OPUS_OK) {
        ESP_LOGW(TAG_AUDIO, "opus decoder reset failed %d", ret);
    }
}

int opus_codec_encode(const int16_t *pcm_in, uint8_t *opus_out, size_t out_cap) {
    if (!s_enc) return -1;
    int n = opus_encode(s_enc, pcm_in, OPUS_ENC_FRAME_SAMPLES, opus_out,
                        out_cap > 0x7fffffff ? 0x7fffffff : (opus_int32)out_cap);
    return n;
}

int opus_codec_decode(const uint8_t *opus_in, size_t in_len,
                      int16_t *pcm_out, size_t out_cap_samples) {
    if (!s_dec) return -1;
    if (!opus_in || !pcm_out || in_len == 0 || in_len > OPUS_MAX_PACKET_BYTES) {
        ESP_LOGW(TAG_AUDIO, "drop invalid opus packet len=%u", (unsigned)in_len);
        return -1;
    }
    int samples = opus_packet_get_nb_samples(opus_in, (opus_int32)in_len,
                                             OPUS_DEC_SAMPLE_RATE);
    int frames = opus_packet_get_nb_frames(opus_in, (opus_int32)in_len);
    int channels = opus_packet_get_nb_channels(opus_in);
    int bandwidth = opus_packet_get_bandwidth(opus_in);
    uint8_t toc = opus_in[0];
    uint32_t seq = ++s_dec_packet_seq;
    bool celt_only = (toc & 0x80) != 0;
    /* 1 dòng/lượt (gói đầu) hoặc khi gặp gói lạ; 8 dòng/lượt cũ chỉ là nhiễu UART. */
    if (seq == 1 || !celt_only) {
        ESP_LOGI(TAG_AUDIO,
                 "opus downlink #%u len=%u toc=0x%02x mode=%s bw=%s frames=%d samples=%d ch=%d",
                 (unsigned)seq, (unsigned)in_len, toc,
                 opus_downlink_mode_name(toc), opus_bandwidth_name(bandwidth),
                 frames, samples, channels);
    }
    if (samples <= 0 || (size_t)samples > out_cap_samples) {
        ESP_LOGW(TAG_AUDIO, "drop unsupported opus packet len=%u samples=%d cap=%u",
                 (unsigned)in_len, samples, (unsigned)out_cap_samples);
        return -1;
    }
    if (frames <= 0 || channels != 1 || !celt_only) {
        if ((s_dec_drop_seq++ % 20) == 0) {
            ESP_LOGW(TAG_AUDIO,
                     "drop non-CELT/invalid downlink opus len=%u toc=0x%02x mode=%s frames=%d ch=%d",
                     (unsigned)in_len, toc, opus_downlink_mode_name(toc), frames, channels);
        }
        return -1;
    }
    /* KHÔNG chặn theo frames/bandwidth/20ms: server EDU gửi gói CELT SWB 60ms
     * (frames=3, 1440 mẫu) — guard cũ (thêm ở 070570c7 cho watch) vứt SẠCH TTS
     * → loa im trên FW mới trong khi 1.0.78 prod phát tốt. Tràn buffer đã chặn
     * ở trên (samples > out_cap); opus_decode giải gói đa-frame bình thường. */
    if (in_len <= OPUS_DEC_SILENCE_PACKET_MAX_BYTES) {
        memset(pcm_out, 0, (size_t)samples * sizeof(int16_t));
        if (seq == 1) {
            ESP_LOGI(TAG_AUDIO, "opus downlink #%u short silence -> pcm zeros samples=%d",
                     (unsigned)seq, samples);
        }
        return samples;
    }
    int n = opus_decode(s_dec, opus_in, (opus_int32)in_len, pcm_out,
                        (int)out_cap_samples, 0);
    return n;
}

int opus_codec_decode_selftest(void)
{
    if (!s_dec) return -1;
    /* TOC 0xb8 = CELT-only, WB, 20ms, 1 frame (đúng gói server gửi); payload 4 byte
     * bất kỳ để KHÔNG đi nhánh "short silence" của opus_codec_decode mà vào thẳng
     * opus_decode → opus_decode_frame → ALLOC_STACK. Kết quả PCM không quan trọng. */
    static const uint8_t pkt[5] = { 0xb8, 0x00, 0x00, 0x00, 0x00 };
    static int16_t pcm[480];
    return opus_decode(s_dec, pkt, (opus_int32)sizeof(pkt), pcm, 480, 0);
}
