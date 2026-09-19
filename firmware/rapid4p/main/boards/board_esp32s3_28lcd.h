/**
 * Board: AI-IoT Việt Nam **ES3N28P-LCD-2.8** — ESP32-S3 N16R8 (16 MB QIO flash, 8 MB Octal PSRAM)
 * + LCD 2.8" ILI9341 SPI (240×320 dọc native, dùng ngang 320×240) + touch FT6236G.
 * Biến thể Rapid Reader 5 khe màn nhỏ; khoá sản phẩm `rapid4p-s3` (system/products.yaml).
 *
 * Nguồn pinout: firmware-vimate/main/boards/board_esp32s3_28lcd.h (Bizgeni, biến thể s3-28lcd
 * ĐÃ BUILD VÀ CHẠY THẬT 12/09/2026; bản chép trong repo:
 * `firmware/maping new product/firmware-vimate-p4/main/boards/board_esp32s3_28lcd.h`), gốc từ
 * xiaozhi-esp32_vietnam/main/boards/xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8/config.h.
 * Rapid4P bỏ khối audio ES8311/I2S (không dùng loa/mic), thêm khối "Bo cảm biến quang 5 slot"
 * (ĐỀ XUẤT) ở cuối file.
 *
 * GPIO board ĐÃ DÙNG: 0 BOOT · 1 PA loa (không dùng) · 4/5/6/7/8 I2S codec (không dùng) ·
 * 10/11/12/13 SPI LCD · 15/16 I2C0 (touch + codec) · 17/18 touch INT/RST · 42 LED · 45 BL · 46 DC.
 * GPIO KHÔNG được dùng: 3 (strap JTAG), 19/20 (USB D−/D+), 26–37 (flash + PSRAM octal),
 * 43/44 (UART0 console/nạp), 45/46 (strap — đã là BL/DC).
 * GPIO CÒN TRỐNG cho bo cảm biến: 2, 9, 14, 21, 38, 39, 40, 41, 47, 48 (10 chân).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== LCD (SPI3, ILI9341) ========================== */
#define BOARD_LCD_USE_MIPI_DSI       0
#define BOARD_LCD_USE_SPI            1
#define BOARD_LCD_USE_ILI9341        1
#define BOARD_LCD_HOST               SPI3_HOST
#define BOARD_LCD_PIN_SCLK           12
#define BOARD_LCD_PIN_MOSI           11
#define BOARD_LCD_PIN_MISO           13
#define BOARD_LCD_PIN_DC             46
#define BOARD_LCD_PIN_RST            -1   /* nối reset hệ thống — NC */
#define BOARD_LCD_PIN_CS             10
#define BOARD_LCD_PIN_BL             45
#define BOARD_LCD_HAS_BACKLIGHT      1
#define BOARD_LCD_BL_ON_LEVEL        1
#define BOARD_LCD_BL_FREQ_HZ         5000

/* Panel native DỌC 240×320; xoay 90° bằng MADCTL (swap_xy) cho UI NGANG 320×240, cổng USB
 * bên phải (mirror (1,1) = lật 180°). Ở đây H_RES/V_RES = kích thước LOGICAL (LVGL). */
#define BOARD_LCD_NATIVE_W           240
#define BOARD_LCD_NATIVE_H           320
#define BOARD_LCD_H_RES              320
#define BOARD_LCD_V_RES              240
#define BOARD_LCD_ROTATION           90
#define BOARD_LCD_BITS_PER_PIXEL     16
#define BOARD_LCD_SPI_FREQ_HZ        (40 * 1000 * 1000)
#define BOARD_LCD_SPI_MODE           0
#define BOARD_LCD_INVERT_COLOR       1
#define BOARD_LCD_USE_BGR            1     /* ELEMENT_ORDER_BGR */
#define BOARD_LCD_SWAP_XY            1
#define BOARD_LCD_MIRROR_X           0
#define BOARD_LCD_MIRROR_Y           0
/* RGB565 little-endian (LVGL) → big-endian (ILI9341 qua SPI): port swap byte khi flush. */
#define BOARD_LCD_SWAP_BYTES         1
/* Draw buffer LVGL: 2 × (320 × 40 × 2 B) = 51 KB RAM NỘI (DMA, không PSRAM — S3 DMA từ PSRAM
 * qua SPI chậm/lỗi cache). Thiếu heap nội khi WiFi + TLS → hạ 20. */
#define BOARD_LCD_DRAW_BUF_LINES     40
#define BOARD_LCD_EXTRA_BL_ENABLED   0

/* ========================== Touch FT6236G (I2C0) ==========================
 * Bus I2C0 dùng chung với codec ES8311 (0x18) trên board — Rapid4P không init codec nên touch
 * tự tạo bus (touch.c: tái dùng nếu đã có, không thì tạo). RST GPIO18 PHẢI có xung LOW ≥ 5 ms
 * rồi HIGH ≥ 300 ms, không thì chip kẹt, NUM_TOUCHES luôn 0 (vimate touch.c). */
#define BOARD_TOUCH_USE_ST7123       0
#define BOARD_TOUCH_USE_FT6236       1
#define BOARD_TOUCH_I2C_NUM          I2C_NUM_0
#define BOARD_TOUCH_I2C_SCL          15
#define BOARD_TOUCH_I2C_SDA          16
#define BOARD_TOUCH_I2C_FREQ_HZ      400000
#define BOARD_TOUCH_INT_GPIO         17
#define BOARD_TOUCH_RST_GPIO         18
#define BOARD_TOUCH_I2C_ADDR         0x38
#define BOARD_TOUCH_INT_ACTIVE_LOW   0
#define BOARD_TOUCH_TASK_PRIO        5
/* Toạ độ FT6236 trả trong hệ NATIVE dọc 240×320 → logical ngang: swap theo màn, mirror_x theo
 * màn; KHÔNG mirror_y dù màn có MIRROR_Y (vimate đo trên board: trục Y chạm đã thuận chiều,
 * mirror thêm là lật ngược — home lưới 2 hàng chạm hàng dưới ra hàng trên). Chạm lệch trục →
 * chỉnh 3 knob này, không sửa touch.c. */
#define BOARD_TOUCH_SWAP_XY          BOARD_LCD_SWAP_XY
#define BOARD_TOUCH_MIRROR_X         BOARD_LCD_MIRROR_X
#define BOARD_TOUCH_MIRROR_Y         0

/* ========================== WiFi (native, chỉ 2,4 GHz) ========================== */
#define BOARD_WIFI_BAND_2G_ONLY      0     /* S3 không có 5 GHz — không cần khoá băng */

/* ========================== Buttons ========================== */
/* GPIO0 = BOOT (pull-up, nhấn = LOW): tap = quay lại, giữ 5 s = xoá WiFi vào SoftAP. */
#define BOARD_BTN_BOOT_GPIO          0
/* Nút ĐO vật lý: board không có nút thứ hai → ĐỀ XUẤT nút ngoài trên GPIO47 (pull-up nội,
 * nhấn = LOW). Không nối thì để -1: đo bằng chạm màn / nút web dashboard. */
#define BOARD_BTN_MEASURE_GPIO       47

/* ========================== LED ========================== */
#define BOARD_STATUS_LED_GPIO        42    /* LED đơn trên board — tư liệu, chưa có driver */

/* ========================== Power ========================== */
#define BOARD_POWER_HOLD_GPIO        -1
#define BOARD_CHARGING_DET_GPIO      -1

/* ========================== Bo cảm biến quang 5 slot — ĐỀ XUẤT 2026-09-19 ==========================
 *
 * Chưa có schematic bo giao tiếp cho board này; số chân dưới đây chọn trong 10 GPIO còn trống
 * của ES3N28P (xem đầu file), tránh strap/USB/flash/PSRAM/UART0. Khi biết header thật của
 * board: sửa Ở ĐÂY, không sửa driver (thư mục sensor, app).
 *
 * Bo con kế thừa ReaderPlus/ReaderMax: mux TCA9548A 0x70 (A0–A2 = GND), kênh 0..4 mỗi kênh một
 * TCS34725 (0x29, ID 0x44/0x4D); LED chiếu: enable riêng từng khe + PWM chung (LEDC 5 kHz 8-bit,
 * mặc định 127/255). Nguồn LED / driver theo docs/HARDWARE-ARCHITECTURE.md (D1–D6).
 *
 * Bus riêng I2C_NUM_1 (S3 có 2 controller) để không chen lịch quét chạm 10 ms trên I2C0.
 * Muốn dùng chung I2C0 (15/16): đổi 3 dòng BOARD_SENSOR_I2C_* — sensor_bus.c tự tái dùng bus
 * qua i2c_master_get_bus_handle(). */
#define BOARD_SENSOR_I2C_NUM         I2C_NUM_1
#define BOARD_SENSOR_I2C_SDA         41
#define BOARD_SENSOR_I2C_SCL         40
/* 100 kHz: dây tới bo con + 5 nhánh sau mux; pull-up nội S3 (~45 K) quá yếu — bo con PHẢI có
 * pull-up 4,7 K. */
#define BOARD_SENSOR_I2C_FREQ_HZ     100000
#define BOARD_SENSOR_MUX_ADDR        0x70
#define BOARD_SENSOR_TCS_ADDR        0x29
/* NGUỒN SỰ THẬT về số khe (registry optical_slots = 5, registry_check đối chiếu). */
#define BOARD_SENSOR_SLOTS           5
#define BOARD_SENSOR_MUX_CHANNELS    { 0, 1, 2, 3, 4 }
#define BOARD_SLOT_LED_GPIOS         { 2, 9, 14, 21, 38 }
#define BOARD_SLOT_LED_ON_LEVEL      1
#define BOARD_SLOT_LED_PWM_GPIO      39
#define BOARD_SLOT_LED_PWM_FREQ_HZ   5000
#define BOARD_SLOT_LED_PWM_DEFAULT   127     /* /255, như ReaderPlus PWM_LED[] */
/* LEDC: kênh 0 + timer 0 đã dành cho đèn nền LCD (display.c). */
#define BOARD_SLOT_LED_LEDC_CHANNEL  LEDC_CHANNEL_1
#define BOARD_SLOT_LED_LEDC_TIMER    LEDC_TIMER_1
/* Còn trống sau khi gán: GPIO48. */

/* ========================== Board metadata ========================== */
#define BOARD_NAME                   "rapid4p_s3_ili9341_lcd28"
#define BOARD_REV                    "es3n28p-v1"
/* Khoá kho OTA trên Engineer Server + PCB (system/products.yaml › rapid4p-s3, hw S3-28). */
#define BOARD_PRODUCT_KEY            "rapid4p-s3"
#define BOARD_HW_VERSION             "S3-28"

#ifdef __cplusplus
}
#endif
