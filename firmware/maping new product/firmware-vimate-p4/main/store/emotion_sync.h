/**
 * emotion_sync.h — Sync custom emotion PNG (64×64, alpha) parent/admin upload
 *                  từ server xuống SPIFFS partition "emo_spiffs" (512KB).
 *
 * File naming: /spiffs_emo/<emotion_key>.png (vd "happy.png", "neutral.png").
 *
 * Sync triggers:
 *   - Boot (sau lesson image cache sync, trước WS connect)
 *   - Optional: periodic check version mỗi giờ
 *
 * UI render: ui_emotion_show() check FS theo thứ tự ưu tiên:
 *   1. /spiffs_emo/<key>.png (custom — parent uploaded)
 *   2. /spiffs_asset/emo/<key>.png (asset pack premium)
 *   3. Built-in lv_image_dsc_t static
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t emotion_sync_init(void);

/* Blocking sync — gọi 1 lần sau boot. */
void emotion_sync_blocking(void);

/* Check FS có custom emotion cho key này không (cheap stat). */
bool emotion_sync_has_custom(const char *emotion_key);

/* Trả về FS path "/spiffs_emo/<key>.png" để lv_image_set_src dùng.
 * Path tĩnh trong buffer nội bộ — copy ra ngay nếu cần preserve. */
const char *emotion_sync_path(const char *emotion_key);

#ifdef __cplusplus
}
#endif
