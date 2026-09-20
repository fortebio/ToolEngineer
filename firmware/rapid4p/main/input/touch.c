/**
 * touch.c — cảm ứng → LVGL indev, hai driver chọn theo board (boards/board.h):
 *
 *   BOARD_TOUCH_USE_ST7123 (P4 + LCD 4.3"): giao thức reg16 + handshake chép từ
 *   firmware-vimate-p4/main/input/touch.c (đã chạy thật: "Touch reg16 OK (status=0x00,
 *   max points=5)"). Bẫy đã gặp (giữ nguyên cách xử):
 *     - Đọc ĐỦ 7×max_points byte ở 0x0014; đọc thiếu → chip trả rác, valid=0.
 *     - Sau reset phải chờ STATUS(0x0001) nibble thấp = 0 rồi mới dùng, nếu không engine
 *       quét không chạy, TOUCH_INFO luôn 0x00.
 *     - KHÔNG toggle reset ở đây: BOARD_TOUCH_RST_GPIO = -1 vì chân đó là GPIO22 chung
 *       với LCD_RST — display_hw_dsi.c đã reset trước DSI, touch cũng sạch theo.
 *     - INT GPIO23 là XUNG, không giữ mức suốt lúc chạm → không dùng làm cổng.
 *
 *   BOARD_TOUCH_USE_FT6236 (S3 + LCD 2.8" ES3N28P): FT6236G reg 8-bit — 0x02 số điểm (nibble
 *   thấp), 0x03..0x06 = P1 XH/XL/YH/YL (12 bit), 0xA3 chip id (0x64 = FT6x36) — theo
 *   firmware-vimate-p4/main/input/touch.c. Bẫy: RST GPIO18 PHẢI có xung LOW ≥ 5 ms rồi HIGH
 *   ≥ 300 ms trước khi nói I2C, không thì chip kẹt, số điểm luôn 0. Bus I2C0 dùng chung với
 *   codec trên board (Rapid4P không init codec → tự tạo bus).
 *
 * Phép xoay toạ độ native → logical chung cho cả hai: swap → mirror_x (H_RES) → mirror_y
 * (V_RES) theo BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y — chỉnh ở board header, không sửa đây.
 * Đọc trong LVGL task (lv_indev), không task riêng.
 */
#include "touch.h"
#include "rapid4p.h"
#include "boards/board.h"
#include "ui/display.h"
#include "ui/ui_reader.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

#ifndef BOARD_TOUCH_I2C_FREQ_HZ
#define BOARD_TOUCH_I2C_FREQ_HZ 400000
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static lv_indev_t *s_indev;
static bool s_ready;
static int s_raw_x, s_raw_y;   /* mẫu thô gần nhất (trước xoay) — cho log chẩn đoán */

/* native → logical theo knob board (suy từ BOARD_LCD_ROTATION / đo trên board):
 * swap → mirror_x (theo H_RES) → mirror_y (theo V_RES) → clamp. */
static void map_to_logical(int *px, int *py)
{
    int x = *px, y = *py;
    s_raw_x = x; s_raw_y = y;
#if BOARD_TOUCH_SWAP_XY
    { int t = x; x = y; y = t; }
#endif
#if BOARD_TOUCH_MIRROR_X
    x = (BOARD_LCD_H_RES - 1) - x;
#endif
#if BOARD_TOUCH_MIRROR_Y
    y = (BOARD_LCD_V_RES - 1) - y;
#endif
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= BOARD_LCD_H_RES) x = BOARD_LCD_H_RES - 1;
    if (y >= BOARD_LCD_V_RES) y = BOARD_LCD_V_RES - 1;
    *px = x; *py = y;
}

#if BOARD_TOUCH_USE_ST7123
/* ===================== ST7123: register 16-bit ===================== */
static int s_maxpts = 1;
#define TOUCH_DRIVER_NAME "ST7123"

static esp_err_t read16(uint16_t reg, uint8_t *out, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t w[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, w, 2, out, len, pdMS_TO_TICKS(50));
}

/* Một mẫu: true nếu đang chạm, toạ độ LOGICAL. */
static bool touch_sample(int *out_x, int *out_y)
{
    uint8_t info = 0;
    if (read16(0x0010, &info, 1) != ESP_OK) return false;
    if (!(info & 0x08)) return false;
    uint8_t p[7 * 10] = {0};
    int rn = 7 * s_maxpts;
    if (rn < 7) rn = 7;
    if (rn > (int)sizeof(p)) rn = sizeof(p);
    if (read16(0x0014, p, rn) != ESP_OK) return false;
    if (!(p[0] & 0x80)) return false;
    int x = ((p[0] & 0x3F) << 8) | p[1];
    int y = ((p[2] & 0x3F) << 8) | p[3];
    map_to_logical(&x, &y);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}

/* Trước khi nói I2C: không có việc gì (reset chung với LCD do display_hw_dsi.c làm). */
static void chip_pre_bus(void) {}

/* Handshake: chờ STATUS nibble thấp = 0, đọc max points. */
static void chip_probe(void)
{
    uint8_t st = 0xFF; int tries = 0;
    do {
        if (read16(0x0001, &st, 1) != ESP_OK) { st = 0xFF; break; }
        if (!(st & 0x0F)) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (++tries < 60);
    uint8_t maxpts = 0;
    if (read16(0x0009, &maxpts, 1) == ESP_OK) {
        s_maxpts = (maxpts >= 1 && maxpts <= 10) ? maxpts : 1;
        ESP_LOGI(TAG_MAIN, "Touch reg16 OK (addr 0x%02X, status=0x%02X, max points=%d)",
                 BOARD_TOUCH_I2C_ADDR, st, maxpts);
    } else {
        ESP_LOGW(TAG_MAIN, "Touch reg16 khong response I2C (addr 0x%02X) - kiem day/RST",
                 BOARD_TOUCH_I2C_ADDR);
    }
}

#elif BOARD_TOUCH_USE_FT6236
/* ===================== FT6236G: register 8-bit ===================== */
#define TOUCH_DRIVER_NAME "FT6236"
#define FT_REG_TD_STATUS   0x02   /* nibble thấp = số điểm chạm 0..2 */
#define FT_REG_CHIP_ID     0xA3   /* 0x64 = FT6x36 */
#define FT_REG_VENDOR_ID   0xA8

static esp_err_t read8(uint8_t reg, uint8_t *out, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, pdMS_TO_TICKS(50));
}

/* Một lần đọc 5 byte từ 0x02: [TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL]. Toạ độ native dọc
 * 240×320 (12 bit), map_to_logical() đưa về 320×240 ngang. */
static bool touch_sample(int *out_x, int *out_y)
{
    uint8_t b[5] = {0};
    if (read8(FT_REG_TD_STATUS, b, sizeof(b)) != ESP_OK) return false;
    const int n = b[0] & 0x0F;
    if (n == 0 || n > 2) return false;
    int x = ((b[1] & 0x0F) << 8) | b[2];
    int y = ((b[3] & 0x0F) << 8) | b[4];
    map_to_logical(&x, &y);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}

/* Xung reset trước khi nói I2C (LOW 10 ms → HIGH 300 ms). Nhiều board ES3N28P treo RST trên
 * GPIO18: không pulse → chip kẹt, TD_STATUS luôn 0. */
static void chip_pre_bus(void)
{
#if BOARD_TOUCH_RST_GPIO >= 0
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_TOUCH_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(BOARD_TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG_MAIN, "Touch RST pulse xong (GPIO%d)", BOARD_TOUCH_RST_GPIO);
#endif
}

static void chip_probe(void)
{
    uint8_t id = 0, vendor = 0;
    if (read8(FT_REG_CHIP_ID, &id, 1) == ESP_OK) {
        read8(FT_REG_VENDOR_ID, &vendor, 1);
        ESP_LOGI(TAG_MAIN, "Touch FT6236 chip id=0x%02X vendor=0x%02X (0x64 = FT6x36)", id, vendor);
    } else {
        ESP_LOGW(TAG_MAIN, "Touch FT6236 khong response I2C (addr 0x%02X) - kiem day/RST GPIO%d",
                 BOARD_TOUCH_I2C_ADDR, BOARD_TOUCH_RST_GPIO);
    }
}
#endif

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int last_x, last_y;
    static bool was_pressed;
    int x, y;
    const bool pressed = touch_sample(&x, &y);
    if (pressed) {
        last_x = x; last_y = y;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        display_note_user_activity();
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
#if CONFIG_RAPID4P_TOUCH_LOG
    /* Chẩn đoán trục chạm khi bring-up: chỉ log lúc NHẤN xuống và THẢ (không log mỗi mẫu).
     * Đối chiếu: góc trên-trái màn → logical (~0,~0); góc dưới-phải → (~W−1,~H−1). Lệch trục →
     * chỉnh BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y trong board header. */
    if (pressed && !was_pressed) {
        ESP_LOGI(TAG_MAIN, "touch DOWN logical=(%d,%d) raw=(%d,%d) man %dx%d", x, y, s_raw_x, s_raw_y,
                 BOARD_LCD_H_RES, BOARD_LCD_V_RES);
        ui_reader_touch_debug(x, y, s_raw_x, s_raw_y);   /* hiện trên header LCD (đang trong LVGL task) */
    } else if (!pressed && was_pressed) {
        ESP_LOGI(TAG_MAIN, "touch UP   logical=(%d,%d)", last_x, last_y);
    }
#endif
    was_pressed = pressed;
}

bool touch_is_ready(void) { return s_ready; }

esp_err_t touch_init(void)
{
    chip_pre_bus();

    esp_err_t r = i2c_master_get_bus_handle(BOARD_TOUCH_I2C_NUM, &s_bus);
    if (r == ESP_OK && s_bus) {
        ESP_LOGI(TAG_MAIN, "Touch: dung lai bus I2C%d", (int)BOARD_TOUCH_I2C_NUM);
    } else {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = BOARD_TOUCH_I2C_NUM,
            .sda_io_num = BOARD_TOUCH_I2C_SDA,
            .scl_io_num = BOARD_TOUCH_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        r = i2c_new_master_bus(&bus_cfg, &s_bus);
        if (r != ESP_OK) {
            ESP_LOGE(TAG_MAIN, "Touch I2C bus init fail: %s", esp_err_to_name(r));
            return r;
        }
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TOUCH_I2C_ADDR,
        .scl_speed_hz = BOARD_TOUCH_I2C_FREQ_HZ,
    };
    r = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "Touch add device fail: %s", esp_err_to_name(r));
        return r;
    }
    if (BOARD_TOUCH_INT_GPIO >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = 1ULL << (unsigned)BOARD_TOUCH_INT_GPIO,
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&int_cfg);
    }
    chip_probe();

    display_lock();
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, indev_read_cb);
    /* Chạm GIỮ ô softkey = giữ nút cơ → cùng ngưỡng BOARD_BTN_HOLD_MS (mặc định LVGL 400 ms quá ngắn:
     * chạm lâu một chút ở "Đang đo" đã thành Dừng). Nút ± màn ngưỡng lặp sau ngưỡng này (như nút cơ). */
    lv_indev_set_long_press_time(s_indev, BOARD_BTN_HOLD_MS);
    display_unlock();
    s_ready = true;
    ESP_LOGI(TAG_MAIN, "Touch " TOUCH_DRIVER_NAME " init OK (SCL=%d SDA=%d addr=0x%02X) -> lv_indev",
             BOARD_TOUCH_I2C_SCL, BOARD_TOUCH_I2C_SDA, BOARD_TOUCH_I2C_ADDR);
    return ESP_OK;
}
