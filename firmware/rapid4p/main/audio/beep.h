/**
 * beep.h — bíp phản hồi cho nông dân (đeo găng, nhìn màn từ xa): nhấn nút → bíp ngắn, đo xong →
 * 2 tiếng, lỗi → tiếng trầm dài. Loa nhỏ gắn trong vỏ máy Rapid 2.8" qua codec ES8311 + PA của
 * board ES3N28P (audio/beep.c, chỉ board S3 — CONFIG_RAPID4P_BEEP).
 *
 * Mọi hàm gọi được từ bất kỳ task nào (đẩy vào queue, không chặn); trước beep_init hoặc khi board
 * không có codec/loa → no-op. Không có CONFIG_RAPID4P_BEEP (P4) → inline rỗng, không kéo esp_codec_dev.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BEEP_KEY = 0,   /* nhấn nút cơ: 2 kHz 40 ms */
    BEEP_DONE,      /* đo / cân chỉnh xong: 2 tiếng 1,2 kHz + 1,6 kHz */
    BEEP_ERROR,     /* lỗi cảm biến / đo lỗi: 400 Hz 400 ms */
    BEEP_BOOT,      /* khởi động xong (xác nhận loa sống): 1 kHz + 1,5 kHz ngắn */
    BEEP_COUNT
} beep_id_t;

#if CONFIG_RAPID4P_BEEP
/* Dò ES8311 trên I2C (tái dùng bus touch/cảm biến), dựng I2S TX + task bíp. Không thấy codec →
 * ESP_ERR_NOT_FOUND, log WARN một lần, mọi beep_* thành no-op — KHÔNG chặn boot. */
esp_err_t beep_init(void);
void beep_play(beep_id_t id);
bool beep_available(void);
#else
static inline esp_err_t beep_init(void) { return ESP_OK; }
static inline void beep_play(beep_id_t id) { (void)id; }
static inline bool beep_available(void) { return false; }
#endif

static inline void beep_key(void)   { beep_play(BEEP_KEY); }
static inline void beep_done(void)  { beep_play(BEEP_DONE); }
static inline void beep_error(void) { beep_play(BEEP_ERROR); }

#ifdef __cplusplus
}
#endif
