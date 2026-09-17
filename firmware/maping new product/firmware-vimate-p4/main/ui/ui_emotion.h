/**
 * ui_emotion.h — legacy wrapper cho display_set_emotion().
 * Emotion được render full màn bằng display.c.
 */
#pragma once
#include "vimate.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_emotion_show(vimate_emotion_t e);
void ui_emotion_hide(void);

#ifdef __cplusplus
}
#endif
