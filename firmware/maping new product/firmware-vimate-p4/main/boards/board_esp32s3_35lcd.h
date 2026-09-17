/**
 * Board: VIMATE ESP32-S3 + 3.5" LCD 320×480 — "3.5inch ESP32-S3 Display" (LCDwiki).
 *
 * Panel ST77922 QSPI (4 data line), KHÔNG phải ST7796 SPI. Pinout chính chủ:
 *   https://www.lcdwiki.com/3.5inch_ESP32-S3_Display
 *   - LCD QSPI: SCLK12, D0=11, D1=13, D2=14, D3=9, CS=10, RST=EN(-1), BL=41
 *   - Touch ST77922 tích hợp IIC @0x55 (KHÔNG phải FT6336): SDA=38, SCL=39, INT=47, RST=48
 *   - Audio ES8311: MCLK17, BCLK18, LRCK/WS21, DOUT15(loa), DIN16(mic), PA=1, I2C 38/39
 *   - SD (SDMMC): CLK5, CMD4, D0-3 = 6,7,2,3 · Battery ADC=8 · không AXP/TCA
 *
 * ⚠️ HARDWARE — verify lần flash đầu (ponytail: núm chỉnh, không thấy trên giấy):
 *  - Màu âm bản → đổi INVERT_COLOR; đỏ-xanh đảo → đổi USE_BGR.
 *  - Ảnh lộn/xoay sai → SWAP_XY / MIRROR_X/Y.
 *  - Mic & loa đảo → hoán DIN↔DOUT. PA câm → đổi PA_ON_LEVEL.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== LCD ST77922 QSPI (SPI2) ========================== */
#define BOARD_LCD_HOST          SPI2_HOST
#define BOARD_LCD_PIN_PCLK      12     /* SCLK */
#define BOARD_LCD_PIN_D0        11
#define BOARD_LCD_PIN_D1        13
#define BOARD_LCD_PIN_D2        14
#define BOARD_LCD_PIN_D3        9
#define BOARD_LCD_PIN_CS        10
#define BOARD_LCD_PIN_RST       -1     /* nối EN/reset hệ thống → software reset */
#define BOARD_LCD_PIN_BL        41
#define BOARD_LCD_EXTRA_BL_GPIO     -1
#define BOARD_LCD_EXTRA_BL_ENABLED  0
#define BOARD_LCD_POWER_CTRL_GPIO   -1
#define BOARD_LCD_POWER_CTRL_ENABLED 0
#define BOARD_LCD_POWER_ON_LEVEL    1
#define BOARD_LCD_DIAG_POWER_BL     0
#define BOARD_LCD_HAS_BACKLIGHT 1
#define BOARD_LCD_BL_ON_LEVEL   1
#define BOARD_LCD_BL_FREQ_HZ    5000

/* LANDSCAPE 480×320 theo đúng công thức demo chính chủ (main.cpp LV_DISP_ROT_90):
 * panel native DỌC 320×480, xoay 90° bằng LVGL SW-ROTATE + FULL_REFRESH + buffer
 * full màn (thiếu full_refresh là sọc — panel hỏng partial-flush, đã học từ glitch
 * xanh). H_RES/V_RES = LOGICAL landscape; display.c tự đưa native vào esp_lvgl_port. */
#define BOARD_LCD_H_RES         480
#define BOARD_LCD_V_RES         320
#define BOARD_LCD_NATIVE_W      320
#define BOARD_LCD_NATIVE_H      480
#define BOARD_LCD_GAP_X         0
#define BOARD_LCD_GAP_Y         0
#define BOARD_LCD_BITS_PER_PIXEL 16
#define BOARD_LCD_SPI_FREQ_HZ   (80 * 1000 * 1000)   /* QSPI — demo chính chủ chạy 80MHz */
#define BOARD_LCD_SPI_MODE      0
#define BOARD_LCD_USE_ILI9341   0
#define BOARD_LCD_USE_ST7796S   0
#define BOARD_LCD_USE_ST77922   1
#define BOARD_LCD_INVERT_COLOR  0     /* knob: đổi 1 nếu màu âm bản */
#define BOARD_LCD_USE_BGR       0     /* knob: đổi 1 nếu đỏ-xanh đảo */
#define BOARD_LCD_SWAP_XY       0     /* portrait — như demo chính chủ */
#define BOARD_LCD_MIRROR_X      0     /* knob: nếu lộn ngược đổi mx/my=1 */
#define BOARD_LCD_MIRROR_Y      0
#define BOARD_LCD_SWAP_BYTES    1
#define BOARD_LCD_TCA9555_KICK  0
#define BOARD_LCD_BOOT_COLOR_HOLD_MS 250

/* ========================== Audio (ES8311 I2C codec + I2S full-duplex) ========================== */
#define BOARD_AUDIO_USE_ES8311   1
#define BOARD_AUDIO_I2S_NUM      I2S_NUM_0
#define BOARD_AUDIO_I2S_MCLK     17
#define BOARD_AUDIO_I2S_BCLK     18
#define BOARD_AUDIO_I2S_WS       21
#define BOARD_AUDIO_I2S_DIN      16     /* mic in */
#define BOARD_AUDIO_I2S_DOUT     15     /* speaker out */
#define BOARD_AUDIO_PA_PIN       1      /* power amplifier enable */
#define BOARD_AUDIO_PA_ON_LEVEL  0      /* knob: đổi 1 nếu loa câm (active high) */

#define BOARD_AUDIO_CODEC_I2C_NUM    I2C_NUM_0
#define BOARD_AUDIO_CODEC_I2C_SCL    39
#define BOARD_AUDIO_CODEC_I2C_SDA    38
#define BOARD_AUDIO_CODEC_I2C_ADDR   0x18   /* ES8311_CODEC_DEFAULT_ADDR */

#define BOARD_MIC_SAMPLE_RATE        16000
#define BOARD_SPK_SAMPLE_RATE        24000
#define BOARD_SPK_BITS_PER_SAMPLE    16
#define BOARD_SPK_CHANNELS           1
#define BOARD_SPK_VOLUME_DEFAULT     80

/* Compatibility aliases (active path = vimate_es8311.c, 1 full-duplex I2S bus). */
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

/* ========================== Touch ST77922 tích hợp (IIC 0x55, chung bus codec 38/39) ==========================
 * KHÔNG phải FT6336. Panel ST77922 có touch controller riêng @0x55, register 16-bit
 * big-endian (xem demo chính chủ components/esp_bsp/bsp_touch.c). */
#define BOARD_TOUCH_USE_ST77922  1
#define BOARD_TOUCH_I2C_NUM      I2C_NUM_0
#define BOARD_TOUCH_I2C_SCL      39
#define BOARD_TOUCH_I2C_SDA      38
#define BOARD_TOUCH_INT_GPIO     47
#define BOARD_TOUCH_RST_GPIO     48
#define BOARD_TOUCH_I2C_ADDR     0x55   /* I2C_ST77922_ADDRESS (demo chính chủ) */
/* Chạm trả toạ độ NATIVE dọc (x 0..319, y 0..479) → map sang landscape 90°:
 * x'=raw_y, y'=319-raw_x (khớp lib chính chủ ST77922_Touch.cpp rotation=1).
 * Nếu đổi màn sang ROTATION_270 thì đổi thành MIRROR_X=1, MIRROR_Y=0. */
#define BOARD_TOUCH_SWAP_XY      1
#define BOARD_TOUCH_MIRROR_X     0
#define BOARD_TOUCH_MIRROR_Y     1

/* ========================== Buttons ========================== */
#define BOARD_BTN_BOOT_GPIO      0
#define BOARD_BTN_STOP_GPIO      -1

/* ========================== LED ========================== */
#define BOARD_LED_GPIO           -1     /* board không có LED riêng */
#define BOARD_LED_COUNT          0

/* ========================== Power ========================== */
#define BOARD_POWER_HOLD_GPIO    -1
#define BOARD_CHARGING_DET_GPIO  -1
#define BOARD_BAT_ADC_GPIO       8

/* ========================== Board metadata ========================== */
#define BOARD_NAME               "vimate_s3_35lcd_st77922"
#define BOARD_REV                "v1"

#ifdef __cplusplus
}
#endif
