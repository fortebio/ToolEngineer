/**
 * touch.c — ST7123 → LVGL indev. Giao thức reg16 + handshake chép từ
 * firmware-vimate-p4/main/input/touch.c (đã chạy thật: "Touch reg16 OK (status=0x00,
 * max points=5)").
 *
 * Bẫy đã gặp (giữ nguyên cách xử):
 *   - Đọc ĐỦ 7×max_points byte ở 0x0014; đọc thiếu → chip trả rác, valid=0.
 *   - Sau reset phải chờ STATUS(0x0001) nibble thấp = 0 rồi mới dùng, nếu không engine
 *     quét không chạy, TOUCH_INFO luôn 0x00.
 *   - KHÔNG toggle reset ở đây: BOARD_TOUCH_RST_GPIO = -1 vì chân đó là GPIO22 chung
 *     với LCD_RST — display.c đã reset trước DSI, touch cũng sạch theo.
 *   - INT GPIO23 là XUNG, không giữ mức suốt lúc chạm → không dùng làm cổng.
 */
#include "touch.h"
#include "rapid4p.h"
#include "boards/board.h"
#include "ui/display.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static lv_indev_t *s_indev;
static int s_maxpts = 1;
static bool s_ready;

static esp_err_t read16(uint16_t reg, uint8_t *out, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t w[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, w, 2, out, len, pdMS_TO_TICKS(50));
}

/* Một mẫu: true nếu đang chạm, toạ độ LOGICAL (800×480). */
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
    /* native (dọc 480×800) → logical theo knob board (suy từ BOARD_LCD_ROTATION):
     * swap → mirror_x (theo H_RES) → mirror_y (theo V_RES). */
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
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int last_x, last_y;
    int x, y;
    if (touch_sample(&x, &y)) {
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
}

bool touch_is_ready(void) { return s_ready; }

esp_err_t touch_init(void)
{
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
        .scl_speed_hz = 400000,
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
    /* Handshake: chờ STATUS nibble thấp = 0. */
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

    display_lock();
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, indev_read_cb);
    display_unlock();
    s_ready = true;
    ESP_LOGI(TAG_MAIN, "Touch ST7123 init OK (SCL=%d SDA=%d addr=0x%02X) -> lv_indev",
             BOARD_TOUCH_I2C_SCL, BOARD_TOUCH_I2C_SDA, BOARD_TOUCH_I2C_ADDR);
    return ESP_OK;
}
