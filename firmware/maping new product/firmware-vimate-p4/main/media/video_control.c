#include "media/video_control.h"

#include "audio/audio_pipeline.h"
#include "media/mp4_player.h"
#include "store/course_media_cache.h"
#include "ui/display.h"
#include "ui/ui_emotion.h"
#include "ui/ui_image.h"
#include "vimate.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool starts_with(const char *s, const char *prefix) {
    return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool file_exists_min(const char *path, size_t min_bytes) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           (size_t)st.st_size >= min_bytes;
}

static bool resolve_video_source(const char *source, char *out_path, size_t out_size) {
    if (!source || !source[0] || !out_path || out_size == 0) {
        return false;
    }
    if (course_media_cache_path(source, out_path, out_size)) {
        return true;
    }
    if (starts_with(source, "/sdcard/") || starts_with(source, "/spiffs/")) {
        if (file_exists_min(source, 1024)) {
            strlcpy(out_path, source, out_size);
            return true;
        }
        return false;
    }
    if (starts_with(source, "sdcard/")) {
        char abs_path[256];
        snprintf(abs_path, sizeof(abs_path), "/%s", source);
        if (file_exists_min(abs_path, 1024)) {
            strlcpy(out_path, abs_path, out_size);
            return true;
        }
    }
    return false;
}

esp_err_t video_control_play(const char *source, const char *title, bool loop) {
    char local_path[256] = {0};
    const char *label = (title && title[0]) ? title : "Video";
    if (!resolve_video_source(source, local_path, sizeof(local_path))) {
        ESP_LOGW(TAG_MCP, "video source not cached or missing: %s", source ? source : "");
        if (source && (starts_with(source, "http://") || starts_with(source, "https://"))) {
            course_media_cache_sync_async();
            display_set_message_timed(label, "Video chưa có trong thẻ SD, đang đồng bộ lại.", 6000);
            return ESP_ERR_NOT_FOUND;
        }
        display_set_message_timed(label, "Không tìm thấy file MP4 trên thiết bị.", 6000);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG_MCP, "video play: %s loop=%d", local_path, loop ? 1 : 0);
    audio_pipeline_mic_stop();
    audio_pipeline_wake_stop();
    audio_pipeline_speaker_stop();
    display_set_backlight(100);
    display_hide_home();
    display_hide_alarm();
    display_hide_countdown();
    display_hide_clock();
    display_stop_slideshow();
    display_set_chat_message("system", "");
    display_set_message(NULL, NULL);
    ui_image_hide();
    ui_emotion_hide();

    esp_err_t err = vimate_mp4_player_play(local_path, loop);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_MCP, "video play failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_NOT_SUPPORTED) {
            display_set_message_timed(label, "Firmware này chưa bật MP4 player.", 6000);
        } else {
            display_set_message_timed(label, "Không phát được MP4 này.", 6000);
        }
    }
    return err;
}

void video_control_stop(void) {
    vimate_mp4_player_stop();
    audio_pipeline_speaker_stop();
    display_set_message(NULL, NULL);
    display_set_state(DEV_STATE_READY);
}
