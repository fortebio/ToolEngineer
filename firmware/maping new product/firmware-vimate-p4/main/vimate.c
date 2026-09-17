/**
 * vimate.c — implementation các utility functions defined in vimate.h.
 */
#include "vimate.h"
#include <string.h>

static const char *EMO_NAMES[EMOTION_COUNT_] = {
    "neutral", "happy", "laughing", "funny", "sad", "angry", "crying",
    "loving", "embarrassed", "surprised", "shocked", "thinking",
    "winking", "cool", "relaxed", "delicious", "kissy", "confident",
    "sleepy", "silly", "confused",
};

vimate_emotion_t vimate_emotion_from_str(const char *s) {
    if (!s) return EMOTION_NEUTRAL;
    for (int i = 0; i < EMOTION_COUNT_; i++) {
        if (strcmp(EMO_NAMES[i], s) == 0) return (vimate_emotion_t)i;
    }
    return EMOTION_NEUTRAL;
}

const char *vimate_emotion_to_str(vimate_emotion_t e) {
    if (e < 0 || e >= EMOTION_COUNT_) return "neutral";
    return EMO_NAMES[e];
}
