#pragma once

/*
 * board.h — chọn board phần cứng theo Kconfig.
 *
 * Giữ include này ở mọi module dùng pinout để build edu/S3 không lẫn chân.
 * Không include trực tiếp board_esp32s3_28lcd.h trong code chung.
 */

#include "sdkconfig.h"

#if defined(CONFIG_VIMATE_BOARD_P4_43LCD)
#include "board_esp32p4_43lcd.h"
#elif defined(CONFIG_VIMATE_BOARD_GENU_GPIO_V6)
#include "board_genu_gpio_v6.h"
#elif defined(CONFIG_VIMATE_BOARD_S3_35LCD)
#include "board_esp32s3_35lcd.h"
#else
#include "board_esp32s3_28lcd.h"
#endif

/* Boards opt out when the touch controller must stay off at runtime. */
#ifndef BOARD_TOUCH_RUNTIME_ENABLE
#define BOARD_TOUCH_RUNTIME_ENABLE 1
#endif

/* Boards opt out when the audio codec pinout is not confirmed yet. Source vẫn
 * biên dịch đầy đủ; chỉ app_main bỏ qua audio_pipeline_init(). */
#ifndef BOARD_AUDIO_RUNTIME_ENABLE
#define BOARD_AUDIO_RUNTIME_ENABLE 1
#endif

/* Panel MIPI-DSI (ESP32-P4). Mặc định 0 → mọi board SPI/i80 giữ nguyên đường cũ. */
#ifndef BOARD_LCD_USE_MIPI_DSI
#define BOARD_LCD_USE_MIPI_DSI 0
#endif

/* ui_image_hide(): thời gian nhường display task/LCD flush "unhook" ảnh cũ trước khi
 * free buffer. S3 ST7796 SPI flush toàn màn ~600 ms nên mặc định 600; board DSI/DMA
 * ghi đè nhỏ hơn. Chỉ ngủ khi thật sự có buffer (ui_image.c). */
#ifndef BOARD_UI_IMAGE_UNHOOK_MS
#define BOARD_UI_IMAGE_UNHOOK_MS 600
#endif
/* Chân INT touch giữ mức THẤP suốt lúc chạm (đo trên board) → touch.c dùng làm cổng
 * "không chạm thì khỏi đọc I2C". 0 = không tin INT, luôn đọc I2C (hành vi cũ). */
#ifndef BOARD_TOUCH_INT_ACTIVE_LOW
#define BOARD_TOUCH_INT_ACTIVE_LOW 0
#endif

/* ESP-SR AFE: core/prio của task xử lý nội bộ (lib tạo trong afe_create). Mặc định
 * của lib = core 0 prio 5 (giữ cho S3); P4 chuyển sang core 1 (README-P4 §9.3). */
#ifndef BOARD_AUDIO_AFE_CORE
#define BOARD_AUDIO_AFE_CORE 0
#endif
#ifndef BOARD_AUDIO_AFE_PRIO
#define BOARD_AUDIO_AFE_PRIO 5
#endif
/* Prio task afe_feed (đọc I2S RX → downsample → đẩy ring AFE). 6 = S3; P4 dùng 7. */
#ifndef BOARD_AUDIO_FEED_TASK_PRIO
#define BOARD_AUDIO_FEED_TASK_PRIO 6
#endif
/* Log "TDM ch rms" từng kênh 3 s/lần (chẩn đoán thứ tự kênh ES7210). 0 = im. */
#ifndef BOARD_AUDIO_DIAG_TDM_RMS
#define BOARD_AUDIO_DIAG_TDM_RMS 0
#endif

/* Optional low-travel emotion servo. Disabled unless a board explicitly opts in. */
#ifndef BOARD_SERVO_EMOTION_RUNTIME_ENABLE
#define BOARD_SERVO_EMOTION_RUNTIME_ENABLE 0
#endif
#ifndef BOARD_SERVO_EMOTION_GPIO
#define BOARD_SERVO_EMOTION_GPIO GPIO_NUM_NC
#endif
#ifndef BOARD_SERVO_EMOTION_MIN_US
#define BOARD_SERVO_EMOTION_MIN_US 1450
#endif
#ifndef BOARD_SERVO_EMOTION_NEUTRAL_US
#define BOARD_SERVO_EMOTION_NEUTRAL_US 1500
#endif
#ifndef BOARD_SERVO_EMOTION_MAX_US
#define BOARD_SERVO_EMOTION_MAX_US 1550
#endif

/* WakeNet tuning remains board-specific so ASR microphone gain is unchanged. */
#ifndef BOARD_WAKE_DETECTION_AGGRESSIVE
#define BOARD_WAKE_DETECTION_AGGRESSIVE 0
#endif
#ifndef BOARD_WAKE_GAIN_PCT
#define BOARD_WAKE_GAIN_PCT 100
#endif
#ifndef BOARD_WAKE_DET_THRESHOLD
#define BOARD_WAKE_DET_THRESHOLD 0.0f
#endif

/* Khoá STA vào băng 2,4 GHz (esp_wifi_set_band_mode) — chỉ có nghĩa với co-processor
 * 2 băng (P4 + C5). S3 một băng: 0, không gọi. */
#ifndef BOARD_WIFI_BAND_2G_ONLY
#define BOARD_WIFI_BAND_2G_ONLY 0
#endif

/* Xung SDMMC khi mount lần đầu (kHz); 400 = SDMMC_FREQ_PROBING (bring-up, an toàn). */
#ifndef BOARD_SD_MMC_FREQ_KHZ
#define BOARD_SD_MMC_FREQ_KHZ 400
#endif

/* "Mặt robot" toàn màn (main/ui/face.c, README-P4 §6.5b): GIF nền đen kín nhân
 * BOARD_FACE_GIF_SCALE lần thành cả màn, thay emoji Noto/Chuppy đặt giữa. Mặc định 0
 * = S3 giữ nguyên đường emoji cũ (GIF alpha 128/160 px, embed trong app). */
#ifndef BOARD_FACE_GIF_FULLSCREEN
#define BOARD_FACE_GIF_FULLSCREEN 0
#endif
#ifndef BOARD_FACE_GIF_SCALE
#define BOARD_FACE_GIF_SCALE 2
#endif
/* Sàn thời gian mỗi khung (ms) cho bộ mặt; lvgl_gif mặc định 150 cho GIF emoji S3. */
#ifndef BOARD_FACE_GIF_MIN_FRAME_MS
#define BOARD_FACE_GIF_MIN_FRAME_MS 40
#endif
/* Khi READY rảnh: cứ MIN..MAX giây chen một clip idle_*; buồn ngủ chỉ khi rảnh ≥ SLEEPY. */
#ifndef BOARD_FACE_IDLE_VARIATION_MIN_S
#define BOARD_FACE_IDLE_VARIATION_MIN_S 15
#endif
#ifndef BOARD_FACE_IDLE_VARIATION_MAX_S
#define BOARD_FACE_IDLE_VARIATION_MAX_S 30
#endif
#ifndef BOARD_FACE_IDLE_SLEEPY_AFTER_S
#define BOARD_FACE_IDLE_SLEEPY_AFTER_S 45
#endif
