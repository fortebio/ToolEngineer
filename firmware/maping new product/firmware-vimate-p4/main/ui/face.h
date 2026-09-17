/**
 * face.h — "mặt robot" toàn màn cho board P4 4.3" (15/09/2026).
 *
 * Bộ 32 clip GIF (docs/01_gif → tools/build_face_gifs.py → spiffs_face_image/, flash
 * vào emo_spiffs, đọc /spiffs_emo/<key>.gif): mắt cyan trên nền đen 400×240, máy nhân
 * đôi thành 800×480 = cả màn. Thay bộ emoji Noto 13 mặt (README-P4 §6.5).
 *
 * File này CHỈ là bảng + luật chọn mặt (không LVGL, không task): display.c gọi dưới
 * display_lock() từ apply_state / apply_emotion / callback hết clip / tick idle.
 *
 * Ba loại clip (theo chính nội dung GIF, xem contact sheet khi dựng):
 *   LOOP      — chạy lặp khi trạng thái còn: idle_normal (chớp mắt), listening,
 *               thinking, speaking, sym_music/alarm/celebrate…
 *   ONCE      — "phản ứng": bắt đầu từ mắt thường → biểu cảm → về mắt thường; chạy
 *               một lượt rồi display.c quay về mặt nền (emo_*, idle_bored/look_around/
 *               sleepy, boot_up).
 *   ONCE_HOLD — chạy một lượt rồi ĐỨNG ở khung cuối tới khi có trạng thái khác
 *               (connect_success ✓, connect_fail ✗, sym_* dạng biểu tượng tĩnh).
 *
 * Map 21 cảm xúc server → 10 emo_* (face_for_emotion) và 11 trạng thái thiết bị →
 * mặt nền (face_for_state) ghi ngay trong face.c, README-P4 §6.5b có bảng.
 */
#pragma once

#include "vimate.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_NONE = 0,
    /* trạng thái máy */
    FACE_BOOT_UP,
    FACE_CONNECT_SUCCESS,
    FACE_CONNECT_FAIL,
    FACE_IDLE_NORMAL,
    FACE_IDLE_BORED,
    FACE_IDLE_LOOK_AROUND,
    FACE_IDLE_SLEEPY,
    FACE_LISTENING,
    FACE_THINKING,
    FACE_SPEAKING,
    /* cảm xúc (phản ứng một lượt) */
    FACE_EMO_NORMAL,
    FACE_EMO_HAPPY,
    FACE_EMO_SAD,
    FACE_EMO_ANGRY,
    FACE_EMO_CRY,
    FACE_EMO_CONFUSED,
    FACE_EMO_SURPRISED,
    FACE_EMO_SCARED,
    FACE_EMO_SHY,
    FACE_EMO_WINK,
    /* biểu tượng tính năng */
    FACE_SYM_ALARM,
    FACE_SYM_REMINDER,
    FACE_SYM_TIMER,
    FACE_SYM_TIMER_DIGITS,
    FACE_SYM_COUNTDOWN,
    FACE_SYM_CELEBRATE,
    FACE_SYM_EVENT,
    FACE_SYM_MUSIC,
    FACE_SYM_WEATHER_SUN,
    FACE_SYM_WEATHER_CLOUD,
    FACE_SYM_WEATHER_RAIN,
    FACE_SYM_WEATHER_SNOW,
    FACE_COUNT_
} face_id_t;

typedef enum {
    FACE_PLAY_LOOP = 0,
    FACE_PLAY_ONCE,
    FACE_PLAY_ONCE_HOLD,
} face_play_t;

/* Tên file (không đuôi) trong /spiffs_emo/ — cũng là key cache PSRAM. NULL nếu NONE. */
const char *face_key(face_id_t id);
face_play_t face_play_mode(face_id_t id);
/* Tra ngược từ key (server / lệnh gỡ lỗi). FACE_NONE nếu không có. */
face_id_t face_from_key(const char *key);

/* Cảm xúc server → clip phản ứng. NEUTRAL → FACE_NONE (giữ mặt nền). */
face_id_t face_for_emotion(vimate_emotion_t e);
/* Mặt nền (ổn định) theo trạng thái thiết bị: READY/BOOT/kết nối → idle_normal,
 * LISTENING → listening, THINKING → thinking, SPEAKING → speaking, ERROR → connect_fail
 * (đứng ở ✗ tới khi đổi trạng thái). */
face_id_t face_for_state(vimate_dev_state_t s);
/* Clip "vào trạng thái" chạy một lượt TRƯỚC mặt nền: lần đầu vào BOOT → boot_up; vào
 * READY từ BOOT / kết nối / OTA / lỗi (không phải từ nghe-nghĩ-nói) → connect_success.
 * FACE_NONE nếu không có. prev = -1 lúc chưa có trạng thái. */
face_id_t face_entry_clip(int prev, vimate_dev_state_t next);
/* Trạng thái đang "trong lượt" (nghe/nghĩ/nói) — biểu tượng tính năng phải nhường. */
bool face_state_is_conversation(vimate_dev_state_t s);

/* Biến thể idle khi READY: idle_s = giây kể từ hoạt động cuối, rnd = số ngẫu nhiên.
 * Trả FACE_NONE nếu chưa tới lúc. next_delay_s ra khoảng chờ tới lần kế. */
face_id_t face_idle_variation(uint32_t idle_s, uint32_t rnd, uint32_t *next_delay_s);

#ifdef __cplusplus
}
#endif
