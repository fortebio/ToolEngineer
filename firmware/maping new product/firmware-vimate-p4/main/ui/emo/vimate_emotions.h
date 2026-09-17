/**
 * vimate_emotions.h — 21 emotion images embed sẵn vào firmware.
 *
 * Source: twemoji 64px (Twitter open emoji) resize lên 128×128 cho LCD 2.8"
 *         → Convert RGB565+A8 LZ4 bằng LVGLImage.py.
 *
 * The 13 common emotions are embedded as GIFs by display.c. This table keeps
 * neutral as their decode-failure fallback and dedicated images only for the
 * 8 emotions that do not have an embedded GIF, avoiding duplicate flash data.
 *
 * Asset pack PNG từ SPIFFS (premium) sẽ OVERRIDE default này.
 */
#pragma once
#include "lvgl.h"
#include "vimate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Built-in fallback descriptors. Common emotions are provided by GIF assets. */
extern const lv_image_dsc_t neutral;
extern const lv_image_dsc_t laughing;
extern const lv_image_dsc_t crying;
extern const lv_image_dsc_t shocked;
extern const lv_image_dsc_t winking;
extern const lv_image_dsc_t cool;
extern const lv_image_dsc_t kissy;
extern const lv_image_dsc_t confident;
extern const lv_image_dsc_t silly;

/* Map enum → descriptor (NULL nếu chưa có) */
const lv_image_dsc_t *vimate_emotion_image(vimate_emotion_t e);

#ifdef __cplusplus
}
#endif
