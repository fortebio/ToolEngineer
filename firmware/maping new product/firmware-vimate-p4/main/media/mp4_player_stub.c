#include "media/mp4_player.h"

esp_err_t vimate_mp4_player_play(const char *file_path, bool loop) {
    (void)file_path;
    (void)loop;
    return ESP_ERR_NOT_SUPPORTED;
}

void vimate_mp4_player_stop(void) {}

bool vimate_mp4_player_is_playing(void) {
    return false;
}

vimate_mp4_state_t vimate_mp4_player_state(void) {
    return VIMATE_MP4_STATE_IDLE;
}

vimate_mp4_stats_t vimate_mp4_player_stats(void) {
    vimate_mp4_stats_t stats = {0};
    return stats;
}
