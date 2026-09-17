/**
 * vimate_emotions.c — map vimate_emotion_t enum → embedded lv_image_dsc_t.
 */
#include "vimate_emotions.h"

static const lv_image_dsc_t *EMO_TABLE[EMOTION_COUNT_] = {
    [EMOTION_NEUTRAL]     = &neutral,
    /* These 13 emotions use embedded GIFs in display.c. Neutral is the
     * low-cost fallback if the GIF cannot be decoded. */
    [EMOTION_HAPPY]       = &neutral,
    [EMOTION_LAUGHING]    = &laughing,
    [EMOTION_FUNNY]       = &neutral,
    [EMOTION_SAD]         = &neutral,
    [EMOTION_ANGRY]       = &neutral,
    [EMOTION_CRYING]      = &crying,
    [EMOTION_LOVING]      = &neutral,
    [EMOTION_EMBARRASSED] = &neutral,
    [EMOTION_SURPRISED]   = &neutral,
    [EMOTION_SHOCKED]     = &shocked,
    [EMOTION_THINKING]    = &neutral,
    [EMOTION_WINKING]     = &winking,
    [EMOTION_COOL]        = &cool,
    [EMOTION_RELAXED]     = &neutral,
    [EMOTION_DELICIOUS]   = &neutral,
    [EMOTION_KISSY]       = &kissy,
    [EMOTION_CONFIDENT]   = &confident,
    [EMOTION_SLEEPY]      = &neutral,
    [EMOTION_SILLY]       = &silly,
    [EMOTION_CONFUSED]    = &neutral,
};

const lv_image_dsc_t *vimate_emotion_image(vimate_emotion_t e) {
    if (e < 0 || e >= EMOTION_COUNT_) e = EMOTION_NEUTRAL;
    return EMO_TABLE[e] ? EMO_TABLE[e] : &neutral;
}
