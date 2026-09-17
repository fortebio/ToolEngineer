/**
 * Board: VIMATE ESP32-S3 ES3N28P + 2.8" LCD (ILI9341, 240×320 portrait native).
 *
 * Pinout chuẩn theo board AI-IoT Việt Nam ES3N28P-LCD-2.8 (tham chiếu
 *   xiaozhi-esp32_vietnam/main/boards/xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8/config.h).
 *
 * - LCD: ILI9341 SPI3, BGR order, INVERT_COLOR=true
 *   Rotation 270° → landscape 320×240, swap_xy=true
 * - Audio: ES8311 I2C codec với I2S DMA (chứ không phải INMP441/MAX98357A rời)
 *   PA pin GPIO1 (active low → bật loa)
 * - Touch: FT6236G qua I2C dùng chung bus codec
 * - Boot button: GPIO0
 * - LED: GPIO42
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== LCD (SPI3) ========================== */
#define BOARD_LCD_HOST          SPI3_HOST
#define BOARD_LCD_PIN_SCLK      12
#define BOARD_LCD_PIN_MOSI      11
#define BOARD_LCD_PIN_MISO      13
#define BOARD_LCD_PIN_DC        46
#define BOARD_LCD_PIN_RST       -1   /* tied to system reset — NC */
#define BOARD_LCD_PIN_CS        10
#define BOARD_LCD_PIN_BL        45
#define BOARD_LCD_HAS_BACKLIGHT 1
#define BOARD_LCD_BL_ON_LEVEL   1
#define BOARD_LCD_BL_FREQ_HZ    5000

/* Rotation 90° landscape 320×240 — mirror cả X+Y để USB cổng phía bên phải */
#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         240
#define BOARD_LCD_NATIVE_W      240   /* panel native portrait */
#define BOARD_LCD_NATIVE_H      320
#define BOARD_LCD_BITS_PER_PIXEL 16
#define BOARD_LCD_SPI_FREQ_HZ   (40 * 1000 * 1000)
#define BOARD_LCD_USE_ILI9341   1
#define BOARD_LCD_INVERT_COLOR  1
#define BOARD_LCD_USE_BGR       1     /* ELEMENT_ORDER_BGR */
#define BOARD_LCD_SWAP_XY       1
#define BOARD_LCD_MIRROR_X      0     /* (1,1) lật 180° — đổi (0,0) chiều chuẩn USB phải */
#define BOARD_LCD_MIRROR_Y      0
#define BOARD_LCD_SWAP_BYTES    1     /* RGB565 little-endian (LVGL) → big-endian (ILI9341) */

/* ========================== Audio (ES8311 I2C codec + I2S) ========================== */
#define BOARD_AUDIO_USE_ES8311   1
#define BOARD_AUDIO_I2S_NUM      I2S_NUM_0
#define BOARD_AUDIO_I2S_MCLK     4
#define BOARD_AUDIO_I2S_BCLK     5
#define BOARD_AUDIO_I2S_WS       7
#define BOARD_AUDIO_I2S_DIN      6     /* mic in */
#define BOARD_AUDIO_I2S_DOUT     8     /* speaker out */
#define BOARD_AUDIO_PA_PIN       1     /* power amplifier enable (active low) */
#define BOARD_AUDIO_PA_ON_LEVEL  0

#define BOARD_AUDIO_CODEC_I2C_NUM    I2C_NUM_0
#define BOARD_AUDIO_CODEC_I2C_SCL    15
#define BOARD_AUDIO_CODEC_I2C_SDA    16
#define BOARD_AUDIO_CODEC_I2C_ADDR   0x18   /* ES8311_CODEC_DEFAULT_ADDR */

#define BOARD_MIC_SAMPLE_RATE        16000
#define BOARD_SPK_SAMPLE_RATE        24000
#define BOARD_SPK_BITS_PER_SAMPLE    16
#define BOARD_SPK_CHANNELS           1
#define BOARD_SPK_VOLUME_DEFAULT     80

/* Compatibility aliases for older audio wrapper code paths. The active path
 * uses vimate_es8311.c with one full-duplex I2S bus. */
#define BOARD_MIC_I2S_NUM        BOARD_AUDIO_I2S_NUM
#define BOARD_MIC_I2S_BCLK       BOARD_AUDIO_I2S_BCLK
#define BOARD_MIC_I2S_WS         BOARD_AUDIO_I2S_WS
#define BOARD_MIC_I2S_DIN        BOARD_AUDIO_I2S_DIN
#define BOARD_MIC_BITS_PER_SAMPLE 32
#define BOARD_MIC_CHANNELS        1
#define BOARD_SPK_I2S_NUM        BOARD_AUDIO_I2S_NUM
#define BOARD_SPK_I2S_BCLK       BOARD_AUDIO_I2S_BCLK
#define BOARD_SPK_I2S_WS         BOARD_AUDIO_I2S_WS
#define BOARD_SPK_I2S_DOUT       BOARD_AUDIO_I2S_DOUT

/* ========================== Touch FT6236G ========================== */
#define BOARD_TOUCH_I2C_NUM      I2C_NUM_0
#define BOARD_TOUCH_I2C_SCL      15
#define BOARD_TOUCH_I2C_SDA      16
#define BOARD_TOUCH_INT_GPIO     17
#define BOARD_TOUCH_RST_GPIO     18
#define BOARD_TOUCH_I2C_ADDR     0x38

/* ========================== Buttons ========================== */
#define BOARD_BTN_BOOT_GPIO      0
#define BOARD_BTN_STOP_GPIO      -1

/* ========================== LED ========================== */
#define BOARD_LED_GPIO           42
#define BOARD_LED_COUNT          1

/* ========================== Power ========================== */
#define BOARD_POWER_HOLD_GPIO    -1
#define BOARD_CHARGING_DET_GPIO  -1

/* ========================== Board metadata ========================== */
#define BOARD_NAME               "vimate_es3n28p_lcd28"
#define BOARD_REV                "v1"

#ifdef __cplusplus
}
#endif
