#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "vimate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Low-travel servo feedback synchronized with LLM emotion and TTS playback. */
esp_err_t servo_emotion_init(void);
void servo_emotion_set_emotion(vimate_emotion_t emotion);
void servo_emotion_set_speaking(bool speaking);
void servo_emotion_stop(void);

#ifdef __cplusplus
}
#endif
