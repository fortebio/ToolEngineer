/**
 * face.c — bảng 32 clip mặt robot + luật chọn mặt. Xem face.h.
 */
#include "face.h"
#include "boards/board.h"
#include <string.h>

typedef struct {
    const char *key;
    face_play_t mode;
} face_entry_t;

/* Thứ tự PHẢI khớp enum face_id_t. Chế độ chạy theo nội dung clip (contact sheet lúc
 * dựng bộ, 15/09): emo_* mở đầu và kết thúc bằng mắt thường → ONCE; connect_fail kết
 * thúc bằng ✗ → ONCE_HOLD (đứng suốt lúc ERROR); sym_* là biểu tượng → LOOP nếu clip
 * có chuyển động liên tục, ONCE_HOLD nếu dựng xong rồi đứng yên. */
static const face_entry_t FACES[FACE_COUNT_] = {
    [FACE_NONE]              = { NULL,                 FACE_PLAY_LOOP },
    [FACE_BOOT_UP]           = { "boot_up",            FACE_PLAY_ONCE },
    [FACE_CONNECT_SUCCESS]   = { "connect_success",    FACE_PLAY_ONCE },       /* ✓ giữ ~2 s trong clip rồi về idle */
    [FACE_CONNECT_FAIL]      = { "connect_fail",       FACE_PLAY_ONCE_HOLD },
    [FACE_IDLE_NORMAL]       = { "idle_normal",        FACE_PLAY_LOOP },
    [FACE_IDLE_BORED]        = { "idle_bored",         FACE_PLAY_ONCE },
    [FACE_IDLE_LOOK_AROUND]  = { "idle_look_around",   FACE_PLAY_ONCE },
    [FACE_IDLE_SLEEPY]       = { "idle_sleepy",        FACE_PLAY_ONCE },
    [FACE_LISTENING]         = { "listening",          FACE_PLAY_LOOP },
    [FACE_THINKING]          = { "thinking",           FACE_PLAY_LOOP },
    [FACE_SPEAKING]          = { "speaking",           FACE_PLAY_LOOP },
    [FACE_EMO_NORMAL]        = { "emo_normal",         FACE_PLAY_ONCE },
    [FACE_EMO_HAPPY]         = { "emo_happy",          FACE_PLAY_ONCE },
    [FACE_EMO_SAD]           = { "emo_sad",            FACE_PLAY_ONCE },
    [FACE_EMO_ANGRY]         = { "emo_angry",          FACE_PLAY_ONCE },
    [FACE_EMO_CRY]           = { "emo_cry",            FACE_PLAY_ONCE },
    [FACE_EMO_CONFUSED]      = { "emo_confused",       FACE_PLAY_ONCE },
    [FACE_EMO_SURPRISED]     = { "emo_surprised",      FACE_PLAY_ONCE },
    [FACE_EMO_SCARED]        = { "emo_scared",         FACE_PLAY_ONCE },
    [FACE_EMO_SHY]           = { "emo_shy",            FACE_PLAY_ONCE },
    [FACE_EMO_WINK]          = { "emo_wink",           FACE_PLAY_ONCE },
    [FACE_SYM_ALARM]         = { "sym_alarm",          FACE_PLAY_LOOP },       /* chuông rung */
    [FACE_SYM_REMINDER]      = { "sym_reminder",       FACE_PLAY_LOOP },       /* chuông lắc */
    [FACE_SYM_TIMER]         = { "sym_timer",          FACE_PLAY_LOOP },       /* kim quay */
    [FACE_SYM_TIMER_DIGITS]  = { "sym_timer_digits",   FACE_PLAY_LOOP },       /* số chạy */
    [FACE_SYM_COUNTDOWN]     = { "sym_countdown",      FACE_PLAY_ONCE_HOLD },  /* 3-2-1-0 */
    [FACE_SYM_CELEBRATE]     = { "sym_celebrate",      FACE_PLAY_LOOP },       /* pháo hoa */
    [FACE_SYM_EVENT]         = { "sym_event",          FACE_PLAY_ONCE_HOLD },  /* bong bóng sao */
    [FACE_SYM_MUSIC]         = { "sym_music",          FACE_PLAY_LOOP },       /* equalizer */
    [FACE_SYM_WEATHER_SUN]   = { "sym_weather_sun",    FACE_PLAY_LOOP },       /* tia quay */
    [FACE_SYM_WEATHER_CLOUD] = { "sym_weather_cloud",  FACE_PLAY_ONCE_HOLD },
    [FACE_SYM_WEATHER_RAIN]  = { "sym_weather_rain",   FACE_PLAY_LOOP },       /* mưa rơi */
    [FACE_SYM_WEATHER_SNOW]  = { "sym_weather_snow",   FACE_PLAY_LOOP },       /* tuyết xoay */
};

const char *face_key(face_id_t id) {
    if (id <= FACE_NONE || id >= FACE_COUNT_) return NULL;
    return FACES[id].key;
}

face_play_t face_play_mode(face_id_t id) {
    if (id <= FACE_NONE || id >= FACE_COUNT_) return FACE_PLAY_LOOP;
    return FACES[id].mode;
}

face_id_t face_from_key(const char *key) {
    if (!key || !key[0]) return FACE_NONE;
    for (int i = FACE_NONE + 1; i < FACE_COUNT_; i++) {
        if (FACES[i].key && strcmp(FACES[i].key, key) == 0) return (face_id_t)i;
    }
    return FACE_NONE;
}

/* 21 cảm xúc server (vimate.h) → 10 clip emo_*. Bộ mới không có mặt riêng cho
 * laughing/funny/loving/cool/… nên gom theo nét: cười → happy, nháy → wink, ngượng/
 * yêu → shy, sốc → scared. relaxed → emo_normal (chớp mắt chậm), sleepy → idle_sleepy. */
face_id_t face_for_emotion(vimate_emotion_t e) {
    switch (e) {
        case EMOTION_HAPPY:
        case EMOTION_LAUGHING:
        case EMOTION_DELICIOUS:   return FACE_EMO_HAPPY;
        case EMOTION_FUNNY:
        case EMOTION_WINKING:
        case EMOTION_COOL:
        case EMOTION_CONFIDENT:
        case EMOTION_SILLY:       return FACE_EMO_WINK;
        case EMOTION_SAD:         return FACE_EMO_SAD;
        case EMOTION_ANGRY:       return FACE_EMO_ANGRY;
        case EMOTION_CRYING:      return FACE_EMO_CRY;
        case EMOTION_LOVING:
        case EMOTION_EMBARRASSED:
        case EMOTION_KISSY:       return FACE_EMO_SHY;
        case EMOTION_SURPRISED:   return FACE_EMO_SURPRISED;
        case EMOTION_SHOCKED:     return FACE_EMO_SCARED;
        case EMOTION_THINKING:    return FACE_THINKING;
        case EMOTION_CONFUSED:    return FACE_EMO_CONFUSED;
        case EMOTION_RELAXED:     return FACE_EMO_NORMAL;
        case EMOTION_SLEEPY:      return FACE_IDLE_SLEEPY;
        case EMOTION_NEUTRAL:
        default:                  return FACE_NONE;
    }
}

face_id_t face_for_state(vimate_dev_state_t s) {
    switch (s) {
        case DEV_STATE_LISTENING:         return FACE_LISTENING;
        case DEV_STATE_THINKING:          return FACE_THINKING;
        case DEV_STATE_SPEAKING:          return FACE_SPEAKING;
        case DEV_STATE_ERROR:             return FACE_CONNECT_FAIL;
        case DEV_STATE_BOOT:
        case DEV_STATE_READY:
        case DEV_STATE_WIFI_PROVISIONING:
        case DEV_STATE_WIFI_CONNECTING:
        case DEV_STATE_OTA_CHECKING:
        case DEV_STATE_NOT_ACTIVATED:
        case DEV_STATE_NO_PLAN:
        default:                          return FACE_IDLE_NORMAL;
    }
}

bool face_state_is_conversation(vimate_dev_state_t s) {
    return s == DEV_STATE_LISTENING || s == DEV_STATE_THINKING || s == DEV_STATE_SPEAKING;
}

face_id_t face_entry_clip(int prev, vimate_dev_state_t next) {
    if (next == DEV_STATE_BOOT) {
        return prev < 0 ? FACE_BOOT_UP : FACE_NONE;
    }
    if (next != DEV_STATE_READY) return FACE_NONE;
    switch (prev) {
        case -1:
        case DEV_STATE_BOOT:
        case DEV_STATE_WIFI_PROVISIONING:
        case DEV_STATE_WIFI_CONNECTING:
        case DEV_STATE_OTA_CHECKING:
        case DEV_STATE_NOT_ACTIVATED:
        case DEV_STATE_NO_PLAN:
        case DEV_STATE_ERROR:             return FACE_CONNECT_SUCCESS;
        default:                          return FACE_NONE;
    }
}

/* Idle: cứ MIN..MAX giây lại chen một clip nhỏ để mặt không "đơ": nhìn quanh 50 %,
 * chán 30 %, buồn ngủ 20 % (buồn ngủ chỉ khi rảnh đủ lâu). Đồng hồ màn chờ (60 s mặc
 * định) sẽ thay mặt sau đó, nên các số này để nhỏ hơn 60. */
face_id_t face_idle_variation(uint32_t idle_s, uint32_t rnd, uint32_t *next_delay_s) {
    const uint32_t lo = BOARD_FACE_IDLE_VARIATION_MIN_S;
    const uint32_t hi = BOARD_FACE_IDLE_VARIATION_MAX_S > lo ? BOARD_FACE_IDLE_VARIATION_MAX_S : lo + 1;
    if (next_delay_s) *next_delay_s = lo + (rnd >> 8) % (hi - lo);
    uint32_t pick = rnd % 100;
    if (pick < 50) return FACE_IDLE_LOOK_AROUND;
    if (pick < 80) return FACE_IDLE_BORED;
    if (idle_s >= BOARD_FACE_IDLE_SLEEPY_AFTER_S) return FACE_IDLE_SLEEPY;
    return FACE_IDLE_LOOK_AROUND;
}
