#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIMATE_MP4_STATE_IDLE = 0,
    VIMATE_MP4_STATE_LOADING,
    VIMATE_MP4_STATE_PLAYING,
    VIMATE_MP4_STATE_STOPPING,
    VIMATE_MP4_STATE_ERROR,
} vimate_mp4_state_t;

typedef struct {
    uint32_t audio_frames;
    uint32_t video_frames;
    uint32_t dropped_frames;
    uint16_t video_width;
    uint16_t video_height;
    uint16_t video_fps;
    uint32_t audio_rate;
    uint8_t audio_channels;
    uint8_t audio_bits;
} vimate_mp4_stats_t;

esp_err_t vimate_mp4_player_play(const char *file_path, bool loop);
void vimate_mp4_player_stop(void);
bool vimate_mp4_player_is_playing(void);
vimate_mp4_state_t vimate_mp4_player_state(void);
vimate_mp4_stats_t vimate_mp4_player_stats(void);

#ifdef __cplusplus
}
#endif
