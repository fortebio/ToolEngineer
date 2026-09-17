#include "media/mp4_player.h"

#include "audio/i2s_output.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "ui/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "audio_render.h"
#include "av_render.h"
#include "esp_audio_dec_default.h"
#include "esp_extractor.h"
#include "esp_extractor_defaults.h"
#include "esp_extractor_types.h"
#include "esp_video_dec_default.h"
#include "video_render.h"
}

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static const char *TAG = "VimateMP4";

namespace {

constexpr uint32_t kAudioRawFifoSize = 64 * 1024;
constexpr uint32_t kAudioRenderFifoSize = 16 * 1024;
constexpr uint32_t kVideoRawFifoSize = 512 * 1024;
constexpr uint32_t kExtractorPoolSize = 256 * 1024;
constexpr uint32_t kTaskStackSize = 14 * 1024;
constexpr uint32_t kTaskPriority = VIMATE_TASK_PRIO_BACKGROUND + 1;
constexpr uint32_t kBufferLines = 32;

constexpr uint32_t kAacSampleRateTable[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000,
    12000, 11025, 8000, 7350,
};

bool parseAacAsc(const uint8_t *data, size_t len, uint32_t &sample_rate, uint8_t &channels) {
    if (!data || len < 2) {
        return false;
    }
    uint8_t sf_idx = static_cast<uint8_t>(((data[0] & 0x07) << 1) | ((data[1] >> 7) & 0x01));
    uint8_t ch_cfg = static_cast<uint8_t>((data[1] >> 3) & 0x0F);
    if (sf_idx == 0x0F) {
        if (len < 5) {
            return false;
        }
        sample_rate = (static_cast<uint32_t>(data[1] & 0x7F) << 17) |
                      (static_cast<uint32_t>(data[2]) << 9) |
                      (static_cast<uint32_t>(data[3]) << 1) |
                      ((data[4] >> 7) & 0x01);
    } else if (sf_idx < (sizeof(kAacSampleRateTable) / sizeof(kAacSampleRateTable[0]))) {
        sample_rate = kAacSampleRateTable[sf_idx];
    }
    if (ch_cfg > 0 && ch_cfg <= 7) {
        channels = ch_cfg;
    }
    return sample_rate > 0 && channels > 0;
}

const char *formatName(esp_extractor_format_t format) {
    switch ((uint32_t)format) {
    case ESP_EXTRACTOR_AUDIO_FORMAT_AAC:
        return "AAC";
    case ESP_EXTRACTOR_AUDIO_FORMAT_MP3:
        return "MP3";
    case ESP_EXTRACTOR_AUDIO_FORMAT_FLAC:
        return "FLAC";
    case ESP_EXTRACTOR_VIDEO_FORMAT_H264:
        return "H264";
    case ESP_EXTRACTOR_VIDEO_FORMAT_MJPEG:
        return "MJPEG";
    default:
        return "unknown";
    }
}

class Player {
public:
    esp_err_t play(const char *path, bool loop);
    void stop();
    bool isPlaying() const { return state_.load() == VIMATE_MP4_STATE_PLAYING; }
    vimate_mp4_state_t state() const { return state_.load(); }
    vimate_mp4_stats_t stats() const;

private:
    struct FileContext {
        FILE *fp = nullptr;
    };

    struct RendererInitCfg {
        Player *owner = nullptr;
    };

    struct AudioRenderCtx {
        Player *owner = nullptr;
        av_render_audio_frame_info_t info{};
    };

    struct VideoRenderCtx {
        Player *owner = nullptr;
        av_render_video_frame_info_t info{};
    };

    esp_err_t ensureInitialized();
    bool initializeRender();
    void destroyRender();
    bool prepare(const std::string &path);
    bool configureStreams();
    void cleanupPlayback();
    void closeFile();
    void resetEos();
    bool pushEos();
    bool waitDrain(uint32_t timeout_ms);
    bool processAudioFrame(const esp_extractor_frame_info_t &frame);
    bool processVideoFrame(const esp_extractor_frame_info_t &frame);
    bool ensureDrawBuffer(size_t bytes);
    bool drawFrame(const uint8_t *frame, uint16_t width, uint16_t height,
                   av_render_video_frame_type_t type);
    int writePcm(const int16_t *pcm, size_t samples, int channels);
    void setState(vimate_mp4_state_t next);

    static void taskEntry(void *arg);
    void taskLoop();
    static int fileRead(void *buffer, uint32_t size, void *ctx);
    static int fileSeek(uint32_t position, void *ctx);
    static uint32_t fileSize(void *ctx);
    static int renderEvent(av_render_event_t event, void *ctx);

    static audio_render_handle_t audioInit(void *cfg, int cfg_size);
    static int audioOpen(audio_render_handle_t render, av_render_audio_frame_info_t *info);
    static int audioWrite(audio_render_handle_t render, av_render_audio_frame_t *audio_data);
    static int audioLatency(audio_render_handle_t render, uint32_t *latency);
    static int audioFrameInfo(audio_render_handle_t render, av_render_audio_frame_info_t *info);
    static int audioSpeed(audio_render_handle_t render, float speed);
    static int audioClose(audio_render_handle_t render);
    static void audioDeinit(audio_render_handle_t render);

    static video_render_handle_t videoOpen(void *cfg, int size);
    static bool videoFormatSupported(video_render_handle_t render, av_render_video_frame_type_t type);
    static int videoSetFrameInfo(video_render_handle_t render, av_render_video_frame_info_t *info);
    static int videoGetFrameBuffer(video_render_handle_t render, av_render_frame_buffer_t *frame_buffer);
    static int videoWrite(video_render_handle_t render, av_render_video_frame_t *video_data);
    static int videoLatency(video_render_handle_t render, uint32_t *latency);
    static int videoFrameInfo(video_render_handle_t render, av_render_video_frame_info_t *info);
    static int videoClear(video_render_handle_t render);
    static int videoClose(video_render_handle_t render);

    av_render_audio_codec_t mapAudio(esp_extractor_format_t format) const;
    av_render_video_codec_t mapVideo(esp_extractor_format_t format) const;

    std::atomic<vimate_mp4_state_t> state_{VIMATE_MP4_STATE_IDLE};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> task_running_{false};
    std::atomic<bool> audio_eos_{false};
    std::atomic<bool> video_eos_{false};

    TaskHandle_t task_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t lcd_width_ = 0;
    uint16_t lcd_height_ = 0;

    RendererInitCfg renderer_cfg_{};
    audio_render_handle_t audio_render_ = nullptr;
    video_render_handle_t video_render_ = nullptr;
    av_render_handle_t av_render_ = nullptr;

    esp_extractor_handle_t extractor_ = nullptr;
    esp_extractor_config_t extractor_cfg_{};
    esp_extractor_stream_info_t audio_info_{};
    esp_extractor_stream_info_t video_info_{};
    av_render_audio_codec_t audio_codec_ = AV_RENDER_AUDIO_CODEC_NONE;
    av_render_video_codec_t video_codec_ = AV_RENDER_VIDEO_CODEC_NONE;
    uint16_t expected_width_ = 0;
    uint16_t expected_height_ = 0;
    FileContext file_ctx_{};

    uint8_t *draw_buf_ = nullptr;
    size_t draw_buf_size_ = 0;
    std::vector<int16_t> pcm_mix_;
    std::string current_path_;
    bool loop_ = false;

    mutable std::mutex stats_mu_;
    vimate_mp4_stats_t stats_{};
};

Player g_player;

esp_err_t Player::play(const char *path, bool loop) {
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state_.load() != VIMATE_MP4_STATE_IDLE) {
        stop();
    }
    esp_err_t init_err = ensureInitialized();
    if (init_err != ESP_OK) {
        setState(VIMATE_MP4_STATE_ERROR);
        return init_err;
    }

    current_path_ = path;
    loop_ = loop;
    stop_requested_.store(false);
    resetEos();
    {
        std::lock_guard<std::mutex> lock(stats_mu_);
        stats_ = {};
    }

    setState(VIMATE_MP4_STATE_LOADING);
    if (!prepare(current_path_)) {
        cleanupPlayback();
        setState(VIMATE_MP4_STATE_ERROR);
        return ESP_FAIL;
    }

    task_running_.store(true);
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "mp4_play", kTaskStackSize, this,
                                            kTaskPriority, &task_, VIMATE_TASK_CORE_IO);
    if (ok != pdPASS) {
        task_ = nullptr;
        task_running_.store(false);
        cleanupPlayback();
        setState(VIMATE_MP4_STATE_ERROR);
        return ESP_ERR_NO_MEM;
    }

    setState(VIMATE_MP4_STATE_PLAYING);
    ESP_LOGI(TAG, "play %s loop=%d", current_path_.c_str(), loop_ ? 1 : 0);
    return ESP_OK;
}

void Player::stop() {
    if (state_.load() == VIMATE_MP4_STATE_IDLE) {
        return;
    }
    setState(VIMATE_MP4_STATE_STOPPING);
    stop_requested_.store(true);
    int guard = 100;
    while (task_running_.load() && --guard >= 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (av_render_) {
        av_render_pause(av_render_, false);
        av_render_reset(av_render_);
    }
    cleanupPlayback();
    i2s_output_stop();
    stop_requested_.store(false);
    task_running_.store(false);
    task_ = nullptr;
    current_path_.clear();
    setState(VIMATE_MP4_STATE_IDLE);
}

vimate_mp4_stats_t Player::stats() const {
    std::lock_guard<std::mutex> lock(stats_mu_);
    return stats_;
}

esp_err_t Player::ensureInitialized() {
    if (initialized_.load()) {
        return ESP_OK;
    }
    panel_ = display_lcd_panel_handle();
    lcd_width_ = static_cast<uint16_t>(display_lcd_width());
    lcd_height_ = static_cast<uint16_t>(display_lcd_height());
    if (!panel_ || lcd_width_ == 0 || lcd_height_ == 0) {
        ESP_LOGE(TAG, "display panel not ready");
        return ESP_ERR_INVALID_STATE;
    }

    esp_extractor_register_default();
    esp_audio_dec_register_default();
    esp_video_dec_register_default();
    renderer_cfg_.owner = this;
    if (!initializeRender()) {
        destroyRender();
        return ESP_FAIL;
    }
    initialized_.store(true);
    ESP_LOGI(TAG, "initialized panel=%ux%u", lcd_width_, lcd_height_);
    return ESP_OK;
}

bool Player::initializeRender() {
    audio_render_cfg_t audio_cfg = {};
    audio_cfg.ops.init = audioInit;
    audio_cfg.ops.open = audioOpen;
    audio_cfg.ops.write = audioWrite;
    audio_cfg.ops.get_latency = audioLatency;
    audio_cfg.ops.get_frame_info = audioFrameInfo;
    audio_cfg.ops.set_speed = audioSpeed;
    audio_cfg.ops.close = audioClose;
    audio_cfg.ops.deinit = audioDeinit;
    audio_cfg.cfg = &renderer_cfg_;
    audio_cfg.cfg_size = sizeof(renderer_cfg_);

    audio_render_ = audio_render_alloc_handle(&audio_cfg);
    if (!audio_render_) {
        ESP_LOGE(TAG, "audio_render_alloc_handle failed");
        return false;
    }

    video_render_cfg_t video_cfg = {};
    video_cfg.ops.open = videoOpen;
    video_cfg.ops.format_support = videoFormatSupported;
    video_cfg.ops.set_frame_info = videoSetFrameInfo;
    video_cfg.ops.get_frame_buffer = videoGetFrameBuffer;
    video_cfg.ops.write = videoWrite;
    video_cfg.ops.get_latency = videoLatency;
    video_cfg.ops.get_frame_info = videoFrameInfo;
    video_cfg.ops.clear = videoClear;
    video_cfg.ops.close = videoClose;
    video_cfg.cfg = &renderer_cfg_;
    video_cfg.cfg_size = sizeof(renderer_cfg_);

    video_render_ = video_render_alloc_handle(&video_cfg);
    if (!video_render_) {
        ESP_LOGE(TAG, "video_render_alloc_handle failed");
        return false;
    }

    av_render_cfg_t cfg = {};
    cfg.audio_render = audio_render_;
    cfg.video_render = video_render_;
    cfg.sync_mode = AV_RENDER_SYNC_FOLLOW_AUDIO;
    cfg.audio_raw_fifo_size = kAudioRawFifoSize;
    cfg.video_raw_fifo_size = kVideoRawFifoSize;
    cfg.audio_render_fifo_size = kAudioRenderFifoSize;
    cfg.video_render_fifo_size = 0;
    cfg.quit_when_eos = false;
    cfg.allow_drop_data = false;
    cfg.pause_render_only = true;

    av_render_ = av_render_open(&cfg);
    if (!av_render_) {
        ESP_LOGE(TAG, "av_render_open failed");
        return false;
    }
    av_render_set_event_cb(av_render_, renderEvent, this);
    return true;
}

void Player::destroyRender() {
    if (av_render_) {
        av_render_close(av_render_);
        av_render_ = nullptr;
    }
    if (video_render_) {
        video_render_free_handle(video_render_);
        video_render_ = nullptr;
    }
    if (audio_render_) {
        audio_render_free_handle(audio_render_);
        audio_render_ = nullptr;
    }
    if (draw_buf_) {
        heap_caps_free(draw_buf_);
        draw_buf_ = nullptr;
        draw_buf_size_ = 0;
    }
}

bool Player::prepare(const std::string &path) {
    cleanupPlayback();
    resetEos();
    file_ctx_.fp = fopen(path.c_str(), "rb");
    if (!file_ctx_.fp) {
        ESP_LOGE(TAG, "open failed: %s", path.c_str());
        return false;
    }

    memset(&extractor_cfg_, 0, sizeof(extractor_cfg_));
    extractor_cfg_.type = ESP_EXTRACTOR_TYPE_MP4;
    extractor_cfg_.extract_mask = ESP_EXTRACT_MASK_AV;
    extractor_cfg_.in_read_cb = fileRead;
    extractor_cfg_.in_seek_cb = fileSeek;
    extractor_cfg_.in_size_cb = fileSize;
    extractor_cfg_.in_ctx = &file_ctx_;
    extractor_cfg_.out_pool_size = kExtractorPoolSize;
    extractor_cfg_.out_align = 16;

    esp_extractor_err_t err = esp_extractor_open(&extractor_cfg_, &extractor_);
    if (err != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "esp_extractor_open failed: %d", err);
        return false;
    }
    err = esp_extractor_parse_stream(extractor_);
    if (err != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "esp_extractor_parse_stream failed: %d", err);
        return false;
    }

    uint16_t audio_num = 0;
    uint16_t video_num = 0;
    err = esp_extractor_get_stream_num(extractor_, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &audio_num);
    if (err != ESP_EXTRACTOR_ERR_OK || audio_num == 0) {
        ESP_LOGE(TAG, "no audio stream");
        return false;
    }
    err = esp_extractor_get_stream_num(extractor_, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &video_num);
    if (err != ESP_EXTRACTOR_ERR_OK || video_num == 0) {
        ESP_LOGE(TAG, "no video stream");
        return false;
    }

    memset(&audio_info_, 0, sizeof(audio_info_));
    memset(&video_info_, 0, sizeof(video_info_));
    err = esp_extractor_get_stream_info(extractor_, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, &audio_info_);
    if (err != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "audio stream info failed: %d", err);
        return false;
    }
    err = esp_extractor_get_stream_info(extractor_, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, 0, &video_info_);
    if (err != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "video stream info failed: %d", err);
        return false;
    }

    audio_codec_ = mapAudio(audio_info_.audio_info.format);
    video_codec_ = mapVideo(video_info_.video_info.format);
    if (audio_codec_ == AV_RENDER_AUDIO_CODEC_NONE || video_codec_ == AV_RENDER_VIDEO_CODEC_NONE) {
        ESP_LOGE(TAG, "unsupported codecs audio=%s video=%s",
                 formatName(audio_info_.audio_info.format),
                 formatName(video_info_.video_info.format));
        return false;
    }
    ESP_LOGI(TAG, "audio=%s sr=%lu ch=%u bits=%u video=%s %ux%u fps=%u",
             formatName(audio_info_.audio_info.format),
             (unsigned long)audio_info_.audio_info.sample_rate,
             (unsigned)audio_info_.audio_info.channel,
             (unsigned)audio_info_.audio_info.bits_per_sample,
             formatName(video_info_.video_info.format),
             (unsigned)video_info_.video_info.width,
             (unsigned)video_info_.video_info.height,
             (unsigned)video_info_.video_info.fps);
    return configureStreams();
}

bool Player::configureStreams() {
    if (!av_render_) {
        return false;
    }
    (void)av_render_reset(av_render_);

    av_render_audio_info_t a = {};
    a.codec = audio_codec_;
    a.channel = audio_info_.audio_info.channel;
    a.bits_per_sample = audio_info_.audio_info.bits_per_sample;
    a.sample_rate = audio_info_.audio_info.sample_rate;
    a.codec_spec_info = audio_info_.spec_info;
    a.spec_info_len = static_cast<int>(audio_info_.spec_info_len);
    if (a.bits_per_sample == 0) {
        a.bits_per_sample = 16;
    }
    if (audio_codec_ == AV_RENDER_AUDIO_CODEC_AAC) {
        uint32_t asc_rate = a.sample_rate;
        uint8_t asc_channels = a.channel;
        if (parseAacAsc(audio_info_.spec_info, audio_info_.spec_info_len, asc_rate, asc_channels)) {
            a.sample_rate = asc_rate;
            a.channel = asc_channels;
        }
        if (a.sample_rate == 0) a.sample_rate = BOARD_SPK_SAMPLE_RATE;
        if (a.channel == 0) a.channel = 2;
        a.aac_no_adts = (a.spec_info_len > 0) ? 1 : 0;
    }
    int ret = av_render_add_audio_stream(av_render_, &a);
    if (ret != 0) {
        ESP_LOGE(TAG, "add audio stream failed: %d", ret);
        return false;
    }

    av_render_video_info_t v = {};
    v.codec = video_codec_;
    v.width = video_info_.video_info.width;
    v.height = video_info_.video_info.height;
    v.fps = static_cast<uint8_t>(video_info_.video_info.fps);
    v.codec_spec_info = video_info_.spec_info;
    v.spec_info_len = static_cast<int>(video_info_.spec_info_len);
    ret = av_render_add_video_stream(av_render_, &v);
    if (ret != 0) {
        ESP_LOGE(TAG, "add video stream failed: %d", ret);
        return false;
    }

    expected_width_ = video_info_.video_info.width;
    expected_height_ = video_info_.video_info.height;
    {
        std::lock_guard<std::mutex> lock(stats_mu_);
        stats_.audio_rate = a.sample_rate;
        stats_.audio_channels = a.channel;
        stats_.audio_bits = a.bits_per_sample;
        stats_.video_width = v.width;
        stats_.video_height = v.height;
        stats_.video_fps = v.fps;
    }
    return true;
}

void Player::cleanupPlayback() {
    if (extractor_) {
        esp_extractor_close(extractor_);
        extractor_ = nullptr;
    }
    closeFile();
}

void Player::closeFile() {
    if (file_ctx_.fp) {
        fclose(file_ctx_.fp);
        file_ctx_.fp = nullptr;
    }
}

void Player::resetEos() {
    audio_eos_.store(false);
    video_eos_.store(false);
}

bool Player::pushEos() {
    if (!av_render_) {
        return false;
    }
    av_render_audio_data_t audio_eos = {};
    audio_eos.eos = true;
    av_render_video_data_t video_eos = {};
    video_eos.eos = true;
    int a_ret = -1;
    int v_ret = -1;
    for (int i = 0; i < 200 && !stop_requested_.load(); ++i) {
        if (a_ret != 0) a_ret = av_render_add_audio_data(av_render_, &audio_eos);
        if (v_ret != 0) v_ret = av_render_add_video_data(av_render_, &video_eos);
        if (a_ret == 0 && v_ret == 0) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return a_ret == 0 && v_ret == 0;
}

bool Player::waitDrain(uint32_t timeout_ms) {
    const uint32_t loops = std::max<uint32_t>(1, timeout_ms / 10);
    for (uint32_t i = 0; i < loops && !stop_requested_.load(); ++i) {
        if (audio_eos_.load() && video_eos_.load()) {
            return true;
        }
        av_render_fifo_stat_t a = {};
        av_render_fifo_stat_t v = {};
        bool got_a = av_render_get_audio_fifo_level(av_render_, &a) == 0;
        bool got_v = av_render_get_video_fifo_level(av_render_, &v) == 0;
        bool empty_a = !got_a || (a.q_num <= 0 && a.render_q_num <= 0 && a.data_size <= 0 &&
                                  a.render_data_size <= 0 && a.duration == 0);
        bool empty_v = !got_v || (v.q_num <= 0 && v.render_q_num <= 0 && v.data_size <= 0 &&
                                  v.render_data_size <= 0 && v.duration == 0);
        if (empty_a && empty_v) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

bool Player::processAudioFrame(const esp_extractor_frame_info_t &frame) {
    if (!av_render_ || !frame.frame_buffer || frame.frame_size == 0) {
        return false;
    }
    av_render_audio_data_t data = {};
    data.data = frame.frame_buffer;
    data.size = frame.frame_size;
    data.pts = frame.pts;
    data.eos = EXTRACTOR_IS_EOS(frame.frame_flag);
    for (int retry = 0; retry < 40 && !stop_requested_.load(); ++retry) {
        if (av_render_add_audio_data(av_render_, &data) == 0) {
            std::lock_guard<std::mutex> lock(stats_mu_);
            stats_.audio_frames++;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

bool Player::processVideoFrame(const esp_extractor_frame_info_t &frame) {
    if (!av_render_ || !frame.frame_buffer || frame.frame_size == 0) {
        return false;
    }
    av_render_video_data_t data = {};
    data.data = frame.frame_buffer;
    data.size = frame.frame_size;
    data.pts = frame.pts;
    data.eos = EXTRACTOR_IS_EOS(frame.frame_flag);
    data.key_frame = true;
    for (int retry = 0; retry < 40 && !stop_requested_.load(); ++retry) {
        if (av_render_add_video_data(av_render_, &data) == 0) {
            std::lock_guard<std::mutex> lock(stats_mu_);
            stats_.video_frames++;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

bool Player::ensureDrawBuffer(size_t bytes) {
    if (bytes == 0) {
        return false;
    }
    if (draw_buf_ && draw_buf_size_ >= bytes) {
        return true;
    }
    if (draw_buf_) {
        heap_caps_free(draw_buf_);
        draw_buf_ = nullptr;
        draw_buf_size_ = 0;
    }
    draw_buf_ = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!draw_buf_) {
        ESP_LOGE(TAG, "draw buffer alloc failed: %u", (unsigned)bytes);
        return false;
    }
    draw_buf_size_ = bytes;
    return true;
}

bool Player::drawFrame(const uint8_t *frame, uint16_t width, uint16_t height,
                       av_render_video_frame_type_t type) {
    if (!panel_ || !frame || width == 0 || height == 0) {
        return false;
    }
    uint16_t frame_w = width;
    uint16_t frame_h = height;
    if (expected_width_ > 0 && expected_width_ <= width) frame_w = expected_width_;
    if (expected_height_ > 0 && expected_height_ <= height) frame_h = expected_height_;
    const uint16_t draw_w = std::min<uint16_t>(frame_w, lcd_width_);
    const uint16_t draw_h = std::min<uint16_t>(frame_h, lcd_height_);
    const int x = (lcd_width_ - draw_w) / 2;
    const int y = (lcd_height_ - draw_h) / 2;
    const size_t src_stride = static_cast<size_t>(width) * sizeof(uint16_t);

    const size_t row_bytes = static_cast<size_t>(draw_w) * sizeof(uint16_t);
    const size_t chunk_bytes = static_cast<size_t>(draw_w) * kBufferLines * sizeof(uint16_t);
    /* Chỉ hoán vị byte khi panel muốn big-endian (SPI/i80 S3: BOARD_LCD_SWAP_BYTES=1).
     * Panel DPI của P4 nhận RGB565 little-endian như LVGL -> swap là sai màu. */
    const bool swap_bytes = (type == AV_RENDER_VIDEO_RAW_TYPE_RGB565) && BOARD_LCD_SWAP_BYTES;
    if (!ensureDrawBuffer(chunk_bytes)) {
        return false;
    }
    for (uint16_t row = 0; row < draw_h; row += kBufferLines) {
        const uint16_t row_end = std::min<uint16_t>(row + kBufferLines, draw_h);
        const uint16_t rows = row_end - row;
        const uint8_t *src = frame + static_cast<size_t>(row) * src_stride;
        uint8_t *dst = draw_buf_;
        for (uint16_t r = 0; r < rows; ++r) {
            memcpy(dst, src, row_bytes);
            if (swap_bytes) {
                for (size_t i = 0; i < row_bytes; i += 2) {
                    std::swap(dst[i], dst[i + 1]);
                }
            }
            src += src_stride;
            dst += row_bytes;
        }
        display_lock();
        /* Qua display_panel_blit(): trên DPI 2 fb (P4) ghi vào cả hai fb, board
         * SPI/i80 vẫn là esp_lcd_panel_draw_bitmap() như trước. */
        esp_err_t err = display_panel_blit(x, y + row, draw_w, rows, draw_buf_);
        display_unlock();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "draw RGB565 failed: %s", esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

int Player::writePcm(const int16_t *pcm, size_t samples, int channels) {
    if (!pcm || samples == 0 || channels <= 0) {
        return -1;
    }
    if (channels == 1) {
        return i2s_output_write_pcm16(pcm, samples, 500);
    }
    size_t frames = samples / static_cast<size_t>(channels);
    pcm_mix_.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        int32_t sum = 0;
        for (int ch = 0; ch < channels; ++ch) {
            sum += pcm[i * channels + ch];
        }
        pcm_mix_[i] = static_cast<int16_t>(sum / channels);
    }
    return i2s_output_write_pcm16(pcm_mix_.data(), pcm_mix_.size(), 500);
}

void Player::setState(vimate_mp4_state_t next) {
    vimate_mp4_state_t old = state_.exchange(next);
    if (old != next) {
        ESP_LOGI(TAG, "state %d -> %d", (int)old, (int)next);
    }
}

void Player::taskEntry(void *arg) {
    static_cast<Player *>(arg)->taskLoop();
}

void Player::taskLoop() {
    do {
        int waiting = 0;
        while (!stop_requested_.load()) {
            esp_extractor_frame_info_t frame = {};
            esp_extractor_err_t err = esp_extractor_read_frame(extractor_, &frame);
            if (err == ESP_EXTRACTOR_ERR_EOS) {
                ESP_LOGI(TAG, "extractor EOS");
                break;
            }
            if (err == ESP_EXTRACTOR_ERR_SKIPPED) {
                std::lock_guard<std::mutex> lock(stats_mu_);
                stats_.dropped_frames++;
                waiting = 0;
                continue;
            }
            if (err == ESP_EXTRACTOR_ERR_WAITING_OUTPUT) {
                if (++waiting >= 5000) {
                    ESP_LOGW(TAG, "extractor waiting output timeout");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            if (err != ESP_EXTRACTOR_ERR_OK) {
                ESP_LOGW(TAG, "read frame err=%d", err);
                break;
            }
            waiting = 0;
            bool eos = (frame.frame_flag & EXTRACTOR_FRAME_FLAG_EOS) != 0;
            bool ok = true;
            if (frame.frame_buffer) {
                if (frame.stream_type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
                    ok = processAudioFrame(frame);
                } else if (frame.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
                    ok = processVideoFrame(frame);
                }
            }
            esp_extractor_release_frame(extractor_, &frame);
            if (!ok) {
                std::lock_guard<std::mutex> lock(stats_mu_);
                stats_.dropped_frames++;
            }
            if (eos && ok) {
                break;
            }
        }

        if (!stop_requested_.load() && pushEos()) {
            (void)waitDrain(8000);
        }
        if (av_render_) {
            av_render_reset(av_render_);
        }
        cleanupPlayback();
        resetEos();
        if (!loop_ || stop_requested_.load()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    } while (prepare(current_path_));

    i2s_output_stop();
    task_running_.store(false);
    task_ = nullptr;
    stop_requested_.store(false);
    current_path_.clear();
    setState(VIMATE_MP4_STATE_IDLE);
    vTaskDelete(NULL);
}

int Player::fileRead(void *buffer, uint32_t size, void *ctx) {
    FileContext *fc = static_cast<FileContext *>(ctx);
    if (!fc || !fc->fp) return -1;
    return static_cast<int>(fread(buffer, 1, size, fc->fp));
}

int Player::fileSeek(uint32_t position, void *ctx) {
    FileContext *fc = static_cast<FileContext *>(ctx);
    if (!fc || !fc->fp) return -1;
    return fseek(fc->fp, static_cast<long>(position), SEEK_SET);
}

uint32_t Player::fileSize(void *ctx) {
    FileContext *fc = static_cast<FileContext *>(ctx);
    if (!fc || !fc->fp) return 0;
    long cur = ftell(fc->fp);
    if (cur < 0) return 0;
    fseek(fc->fp, 0, SEEK_END);
    long end = ftell(fc->fp);
    fseek(fc->fp, cur, SEEK_SET);
    return end < 0 ? 0 : static_cast<uint32_t>(end);
}

int Player::renderEvent(av_render_event_t event, void *ctx) {
    Player *self = static_cast<Player *>(ctx);
    if (!self) return -1;
    if (event == AV_RENDER_EVENT_AUDIO_EOS) {
        self->audio_eos_.store(true);
    } else if (event == AV_RENDER_EVENT_VIDEO_EOS) {
        self->video_eos_.store(true);
    } else if (event == AV_RENDER_EVENT_AUDIO_DECODE_ERR || event == AV_RENDER_EVENT_VIDEO_DECODE_ERR) {
        ESP_LOGE(TAG, "decode error event=%d", (int)event);
        self->setState(VIMATE_MP4_STATE_ERROR);
    }
    return 0;
}

audio_render_handle_t Player::audioInit(void *cfg, int cfg_size) {
    if (!cfg || cfg_size != static_cast<int>(sizeof(RendererInitCfg))) return nullptr;
    RendererInitCfg *init = static_cast<RendererInitCfg *>(cfg);
    if (!init->owner) return nullptr;
    AudioRenderCtx *ctx = new AudioRenderCtx();
    ctx->owner = init->owner;
    return ctx;
}

int Player::audioOpen(audio_render_handle_t render, av_render_audio_frame_info_t *info) {
    AudioRenderCtx *ctx = static_cast<AudioRenderCtx *>(render);
    if (!ctx || !ctx->owner || !info) return -1;
    ctx->info = *info;
    if (info->bits_per_sample != 16 || info->sample_rate != BOARD_SPK_SAMPLE_RATE) {
        ESP_LOGW(TAG, "unsupported PCM output bits=%u rate=%lu expected=%d",
                 (unsigned)info->bits_per_sample,
                 (unsigned long)info->sample_rate,
                 BOARD_SPK_SAMPLE_RATE);
        return -1;
    }
    i2s_output_start();
    return 0;
}

int Player::audioWrite(audio_render_handle_t render, av_render_audio_frame_t *audio_data) {
    AudioRenderCtx *ctx = static_cast<AudioRenderCtx *>(render);
    if (!ctx || !ctx->owner || !audio_data || !audio_data->data || audio_data->size <= 0) {
        return -1;
    }
    const int16_t *pcm = reinterpret_cast<int16_t *>(audio_data->data);
    size_t samples = static_cast<size_t>(audio_data->size) / sizeof(int16_t);
    return ctx->owner->writePcm(pcm, samples, ctx->info.channel) > 0 ? 0 : -1;
}

int Player::audioLatency(audio_render_handle_t render, uint32_t *latency) {
    (void)render;
    if (!latency) return -1;
    *latency = 0;
    return 0;
}

int Player::audioFrameInfo(audio_render_handle_t render, av_render_audio_frame_info_t *info) {
    AudioRenderCtx *ctx = static_cast<AudioRenderCtx *>(render);
    if (!ctx || !info) return -1;
    *info = ctx->info;
    return 0;
}

int Player::audioSpeed(audio_render_handle_t render, float speed) {
    (void)render;
    (void)speed;
    return 0;
}

int Player::audioClose(audio_render_handle_t render) {
    (void)render;
    i2s_output_stop();
    return 0;
}

void Player::audioDeinit(audio_render_handle_t render) {
    delete static_cast<AudioRenderCtx *>(render);
}

video_render_handle_t Player::videoOpen(void *cfg, int size) {
    if (!cfg || size != static_cast<int>(sizeof(RendererInitCfg))) return nullptr;
    RendererInitCfg *init = static_cast<RendererInitCfg *>(cfg);
    if (!init->owner) return nullptr;
    VideoRenderCtx *ctx = new VideoRenderCtx();
    ctx->owner = init->owner;
    return ctx;
}

bool Player::videoFormatSupported(video_render_handle_t render, av_render_video_frame_type_t type) {
    (void)render;
    return type == AV_RENDER_VIDEO_RAW_TYPE_RGB565 || type == AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE;
}

int Player::videoSetFrameInfo(video_render_handle_t render, av_render_video_frame_info_t *info) {
    VideoRenderCtx *ctx = static_cast<VideoRenderCtx *>(render);
    if (!ctx || !ctx->owner || !info || !videoFormatSupported(render, info->type)) return -1;
    ctx->info = *info;
    uint16_t w = info->width;
    uint16_t h = info->height;
    if (ctx->owner->expected_width_ > 0 && ctx->owner->expected_width_ <= info->width) {
        w = ctx->owner->expected_width_;
    }
    if (ctx->owner->expected_height_ > 0 && ctx->owner->expected_height_ <= info->height) {
        h = ctx->owner->expected_height_;
    }
    {
        std::lock_guard<std::mutex> lock(ctx->owner->stats_mu_);
        ctx->owner->stats_.video_width = w;
        ctx->owner->stats_.video_height = h;
        ctx->owner->stats_.video_fps = info->fps;
    }
    ESP_LOGI(TAG, "video decoded=%ux%u display=%ux%u fps=%u type=%d",
             (unsigned)info->width, (unsigned)info->height,
             (unsigned)w, (unsigned)h, (unsigned)info->fps, (int)info->type);
    return 0;
}

int Player::videoGetFrameBuffer(video_render_handle_t render, av_render_frame_buffer_t *frame_buffer) {
    (void)render;
    (void)frame_buffer;
    return -1;
}

int Player::videoWrite(video_render_handle_t render, av_render_video_frame_t *video_data) {
    VideoRenderCtx *ctx = static_cast<VideoRenderCtx *>(render);
    if (!ctx || !ctx->owner || !video_data || !video_data->data || video_data->size <= 0) {
        return -1;
    }
    return ctx->owner->drawFrame(video_data->data, ctx->info.width, ctx->info.height,
                                 ctx->info.type) ? 0 : -1;
}

int Player::videoLatency(video_render_handle_t render, uint32_t *latency) {
    (void)render;
    if (!latency) return -1;
    *latency = 0;
    return 0;
}

int Player::videoFrameInfo(video_render_handle_t render, av_render_video_frame_info_t *info) {
    VideoRenderCtx *ctx = static_cast<VideoRenderCtx *>(render);
    if (!ctx || !info) return -1;
    *info = ctx->info;
    return 0;
}

int Player::videoClear(video_render_handle_t render) {
    (void)render;
    return 0;
}

int Player::videoClose(video_render_handle_t render) {
    delete static_cast<VideoRenderCtx *>(render);
    return 0;
}

av_render_audio_codec_t Player::mapAudio(esp_extractor_format_t format) const {
    switch (format) {
    case ESP_EXTRACTOR_AUDIO_FORMAT_AAC:
        return AV_RENDER_AUDIO_CODEC_AAC;
    case ESP_EXTRACTOR_AUDIO_FORMAT_MP3:
        return AV_RENDER_AUDIO_CODEC_MP3;
    case ESP_EXTRACTOR_AUDIO_FORMAT_FLAC:
        return AV_RENDER_AUDIO_CODEC_FLAC;
    default:
        return AV_RENDER_AUDIO_CODEC_NONE;
    }
}

av_render_video_codec_t Player::mapVideo(esp_extractor_format_t format) const {
    switch (format) {
    case ESP_EXTRACTOR_VIDEO_FORMAT_H264:
        return AV_RENDER_VIDEO_CODEC_H264;
    case ESP_EXTRACTOR_VIDEO_FORMAT_MJPEG:
        return AV_RENDER_VIDEO_CODEC_MJPEG;
    default:
        return AV_RENDER_VIDEO_CODEC_NONE;
    }
}

} // namespace

extern "C" esp_err_t vimate_mp4_player_play(const char *file_path, bool loop) {
    return g_player.play(file_path, loop);
}

extern "C" void vimate_mp4_player_stop(void) {
    g_player.stop();
}

extern "C" bool vimate_mp4_player_is_playing(void) {
    return g_player.isPlaying();
}

extern "C" vimate_mp4_state_t vimate_mp4_player_state(void) {
    return g_player.state();
}

extern "C" vimate_mp4_stats_t vimate_mp4_player_stats(void) {
    return g_player.stats();
}
