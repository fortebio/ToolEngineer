/**
 * Board: VIMATE GENU / Waveshare ESP32-S3-AUDIO-Board + Waveshare 3.5"
 * Capacitive Touch LCD.
 *
 * Source: Waveshare ESP32-S3-AUDIO-Board demo, 3.5" ST7796 profile.
 * Keep isolated behind Kconfig: this board routes LCD/touch through the
 * Waveshare audio carrier, not through the older GENU GPIO testboard pins.
 */

#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== LCD ST7796S (SPI2) ========================== */
#define BOARD_LCD_HOST              SPI2_HOST
#define BOARD_LCD_PIN_SCLK          GPIO_NUM_4
#define BOARD_LCD_PIN_MOSI          GPIO_NUM_9
#define BOARD_LCD_PIN_MISO          GPIO_NUM_8
#define BOARD_LCD_PIN_DC            GPIO_NUM_7
#define BOARD_LCD_PIN_RST           GPIO_NUM_NC  /* reset is TCA9555 EXIO0 */
#define BOARD_LCD_PIN_CS            GPIO_NUM_3
#define BOARD_LCD_PIN_BL            GPIO_NUM_5
#define BOARD_LCD_EXTRA_BL_GPIO     GPIO_NUM_NC
#define BOARD_LCD_EXTRA_BL_ENABLED  0
#define BOARD_LCD_POWER_CTRL_GPIO   GPIO_NUM_NC
#define BOARD_LCD_POWER_CTRL_ENABLED 0
#define BOARD_LCD_POWER_ON_LEVEL    1
#define BOARD_LCD_DIAG_POWER_BL     0
#define BOARD_LCD_HAS_BACKLIGHT     1
#define BOARD_LCD_BL_ON_LEVEL       1
#define BOARD_LCD_BL_FREQ_HZ        5000

/* ST7796S native portrait is normally 320x480. VIMATE UI runs landscape. */
#define BOARD_LCD_H_RES             480
#define BOARD_LCD_V_RES             320
#define BOARD_LCD_NATIVE_W          320
#define BOARD_LCD_NATIVE_H          480
#define BOARD_LCD_BITS_PER_PIXEL    16
#define BOARD_LCD_DRAW_BUF_LINES    5
/* LVGL software shadows rasterize large blurred rectangles on CPU0. On this
 * 480x320 SPI target they can hold taskLVGL past the watchdog while WakeNet,
 * Wi-Fi and the UI are active. Borders/backgrounds retain visual grouping. */
#define BOARD_UI_SOFTWARE_SHADOWS   0
/* Keep emotion semantics and artwork, but render the built-in PNG frame. GIF
 * decode + RGB565A8 scaling can starve Wi-Fi on this SPI panel. */
#define BOARD_EMOJI_GIF_RUNTIME_ENABLE 0
/* Panel SPI KHÔNG có chân TE (LCD 3.5" này không đưa TE ra header) → cách giảm xé
 * của Waveshare = chạy pclk CAO cho flush nhanh. Factory demo Waveshare chạy tới
 * 80MHz trên chính interface 18-pin này (jd9853 default 40MHz). 60MHz ≈ 41ms/khung.
 * ⚠️ Board GENU dây khác stock Waveshare → nếu nhiễu/sọc/sai màu thì HẠ 40MHz;
 *    nếu sạch có thể nâng 80MHz cho dải xé hẹp nhất. */
#define BOARD_LCD_SPI_FREQ_HZ       (60 * 1000 * 1000)
#define BOARD_LCD_SPI_MODE          0
#define BOARD_LCD_USE_ILI9341       0
#define BOARD_LCD_USE_ST7796S       1
#define BOARD_LCD_INVERT_COLOR      1
#define BOARD_LCD_USE_BGR           1
#define BOARD_LCD_SWAP_XY           1
/* (1,1) = hướng ĐÚNG cho panel + cách lắp board GENU này (xác nhận trên thiết bị:
 * emoji THẲNG, chữ đọc được). Bảng rotation Waveshare gợi ý (0,1)/(1,0) là cho
 * cách lắp khác — panel này lắp khác nên (1,1) mới thẳng. KHÔNG xoay mirror để
 * chỉnh vị trí chữ; vị trí overlay/chữ chỉnh bằng lv_obj_align (BOTTOM_MID). */
#define BOARD_LCD_MIRROR_X          1
#define BOARD_LCD_MIRROR_Y          1
#define BOARD_LCD_SWAP_BYTES        1
#define BOARD_LCD_TCA9555_KICK      1
#define BOARD_LCD_BOOT_COLOR_HOLD_MS 250

/* ========================== Audio (ES8311 DAC + ES7210 ADC) ========================== */
#define BOARD_AUDIO_USE_ES8311       1
#define BOARD_AUDIO_I2S_NUM          I2S_NUM_0
#define BOARD_AUDIO_I2S_MCLK         GPIO_NUM_12
#define BOARD_AUDIO_I2S_BCLK         GPIO_NUM_13
#define BOARD_AUDIO_I2S_WS           GPIO_NUM_14
#define BOARD_AUDIO_I2S_DIN          GPIO_NUM_15  /* ES7210 TDM mic -> ESP32 */
#define BOARD_AUDIO_I2S_DOUT         GPIO_NUM_16  /* ESP32 -> ES8311 speaker */
#define BOARD_AUDIO_PA_PIN           GPIO_NUM_NC
#define BOARD_AUDIO_PA_ON_LEVEL      1
#define BOARD_AUDIO_PA_TCA9555_EXIO  8

#define BOARD_AUDIO_CODEC_I2C_NUM    I2C_NUM_0
#define BOARD_AUDIO_CODEC_I2C_SCL    GPIO_NUM_10
#define BOARD_AUDIO_CODEC_I2C_SDA    GPIO_NUM_11
#define BOARD_AUDIO_CODEC_I2C_ADDR   0x18
#define BOARD_AUDIO_ADC_I2C_ADDR     0x40   /* ES7210 */
#define BOARD_AUDIO_USE_ES7210_ADC   1
#define BOARD_AUDIO_TCA9555_I2C_ADDR 0x20   /* exact strap may vary; probe in bring-up */

/* Hi Lily uses aggressive WakeNet detection plus a local-only gain. This does
 * not change the PCM sent to ASR, so recognition audio keeps its current gain. */
#define BOARD_WAKE_DETECTION_AGGRESSIVE 1
#define BOARD_WAKE_GAIN_PCT             150
/* Lower than the model preset so Vietnamese-accented "Hi Lily" triggers
 * reliably; still keyword-only, unlike generic voice activation. */
#define BOARD_WAKE_DET_THRESHOLD         0.60f
/* Measured ambient after the 200% HPF gain exceeds the generic VAD floor. */
#define BOARD_MIC_VAD_RMS_MIN            420

#define BOARD_MIC_SAMPLE_RATE        24000
#define BOARD_SPK_SAMPLE_RATE        24000
#define BOARD_SPK_BITS_PER_SAMPLE    16
#define BOARD_SPK_CHANNELS           1
#define BOARD_SPK_VOLUME_DEFAULT     80

#define BOARD_MIC_I2S_NUM            BOARD_AUDIO_I2S_NUM
#define BOARD_MIC_I2S_BCLK           BOARD_AUDIO_I2S_BCLK
#define BOARD_MIC_I2S_WS             BOARD_AUDIO_I2S_WS
#define BOARD_MIC_I2S_DIN            BOARD_AUDIO_I2S_DIN
#define BOARD_MIC_BITS_PER_SAMPLE    32
#define BOARD_MIC_CHANNELS           1
#define BOARD_SPK_I2S_NUM            BOARD_AUDIO_I2S_NUM
#define BOARD_SPK_I2S_BCLK           BOARD_AUDIO_I2S_BCLK
#define BOARD_SPK_I2S_WS             BOARD_AUDIO_I2S_WS
#define BOARD_SPK_I2S_DOUT           BOARD_AUDIO_I2S_DOUT

/* ========================== Touch (FT6336 via shared I2C, reset on TCA9555 EXIO1) ========================== */
#define BOARD_TOUCH_RUNTIME_ENABLE  0  /* Temporary: avoid touch/audio contention on GENU v6. */
#define BOARD_TOUCH_USE_CAPSENSE     0
#define BOARD_TOUCH_I2C_NUM          I2C_NUM_0
#define BOARD_TOUCH_I2C_SCL          GPIO_NUM_10
#define BOARD_TOUCH_I2C_SDA          GPIO_NUM_11
#define BOARD_TOUCH_INT_GPIO         GPIO_NUM_NC
#define BOARD_TOUCH_RST_GPIO         GPIO_NUM_NC
#define BOARD_TOUCH_I2C_ADDR         0x38
#define BOARD_TOUCH_PAD_HEAD_GPIO    GPIO_NUM_NC
#define BOARD_TOUCH_PAD_BODY_GPIO    GPIO_NUM_NC
#define BOARD_TOUCH_PAD_BACK_GPIO    GPIO_NUM_NC

/* ========================== Servo / robot actuators ========================== */
#define BOARD_SERVO_HEAD_GPIO        GPIO_NUM_NC
#define BOARD_SERVO_LEFT_ARM_GPIO    GPIO_NUM_NC
#define BOARD_SERVO_RIGHT_ARM_GPIO   GPIO_NUM_NC
/* Servo hardware is not fully configured yet. Keep every actuator disabled so
 * emotion/speaking events cannot configure PWM or drive any servo GPIO. */
#define BOARD_SERVO_EMOTION_RUNTIME_ENABLE 0
#define BOARD_SERVO_EMOTION_GPIO           BOARD_SERVO_LEFT_ARM_GPIO
#define BOARD_SERVO_EMOTION_MIN_US         1450
#define BOARD_SERVO_EMOTION_NEUTRAL_US     1500
#define BOARD_SERVO_EMOTION_MAX_US         1550

/* ========================== SD card (1-line SDMMC) ========================== */
#define BOARD_SD_USE_SDMMC_1BIT      1
/* Waveshare ESP32-S3-Audio + 3.5" LCD — khe TF (sơ đồ chân chính hãng):
 *   CLK=GPIO40, CMD=GPIO42, DAT0=GPIO41, CD=EXIO3 (qua TCA9555).
 * TRƯỚC ĐÂY để 45/46/47 = chân CAMERA D4/D5/D6 → SDMMC nói chuyện chân TRỐNG
 * → lỗi 0x108 INVALID_RESPONSE (thẻ KHÔNG hư). */
#define BOARD_SD_CLK                 GPIO_NUM_40
#define BOARD_SD_CMD                 GPIO_NUM_42
#define BOARD_SD_D0                  GPIO_NUM_41

/* ========================== Buttons / LED / Power ========================== */
#define BOARD_BTN_BOOT_GPIO          GPIO_NUM_0
#define BOARD_BTN_STOP_GPIO          GPIO_NUM_NC
#define BOARD_LED_GPIO               GPIO_NUM_48
#define BOARD_LED_COUNT              1
#define BOARD_BATTERY_ADC_GPIO       GPIO_NUM_1
#define BOARD_DEBUG_GPIO             GPIO_NUM_NC  /* cũ GPIO41 — nay là SD DAT0; define này không dùng */
#define BOARD_POWER_HOLD_GPIO        GPIO_NUM_NC
#define BOARD_CHARGING_DET_GPIO      GPIO_NUM_NC

#define BOARD_NAME                   "waveshare_s3_audio_35"
#define BOARD_REV                    "v1.1"

#ifdef __cplusplus
}
#endif
