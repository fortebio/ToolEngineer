/**
 * button.c — quét nút tích cực-thấp (pull-up nội/ngoài), chống dội 30 ms. Kế thừa
 * firmware-vimate-p4/main/input/button.c. Bảng nút: BOOT (giữ 5 s = xoá WiFi), ĐO (P4), và
 * 3 nút vật lý XANH/ĐỎ/TRẮNG của vỏ máy Rapid 2.8" (tap + giữ BOARD_BTN_HOLD_MS → softkey trong
 * ui_reader.c). GPIO -1 = board không có nút đó.
 */
#include "button.h"
#include "rapid4p.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "ui/display.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DEBOUNCE_MS 30
#define LONG_MS     5000

typedef struct {
    int gpio;
    EventBits_t tap_bit, long_bit;   /* long_bit 0 = không có nhấn giữ */
    int long_ms;                     /* ngưỡng giữ của nút này */
    const char *name;
    int last;
    TickType_t down_at;
    bool long_fired;
} btn_t;

static btn_t s_btns[] = {
    { .gpio = BOARD_BTN_BOOT_GPIO,    .tap_bit = R4P_EVT_BTN_PRESS,   .long_bit = R4P_EVT_BTN_LONG,       .long_ms = LONG_MS,           .name = "BOOT",  .last = 1 },
    { .gpio = BOARD_BTN_MEASURE_GPIO, .tap_bit = R4P_EVT_BTN_MEASURE, .long_bit = 0,                      .long_ms = LONG_MS,           .name = "DO",    .last = 1 },
    { .gpio = BOARD_BTN_GREEN_GPIO,   .tap_bit = R4P_EVT_BTN_GREEN,   .long_bit = R4P_EVT_BTN_GREEN_LONG, .long_ms = BOARD_BTN_HOLD_MS, .name = "XANH",  .last = 1 },
    { .gpio = BOARD_BTN_RED_GPIO,     .tap_bit = R4P_EVT_BTN_RED,     .long_bit = R4P_EVT_BTN_RED_LONG,   .long_ms = BOARD_BTN_HOLD_MS, .name = "DO",    .last = 1 },
    { .gpio = BOARD_BTN_WHITE_GPIO,   .tap_bit = R4P_EVT_BTN_WHITE,   .long_bit = R4P_EVT_BTN_WHITE_LONG, .long_ms = BOARD_BTN_HOLD_MS, .name = "TRANG", .last = 1 },
};
#define N_BTN (sizeof(s_btns) / sizeof(s_btns[0]))

static void btn_poll(btn_t *b)
{
    if (b->gpio < 0) return;
    int lvl = gpio_get_level(b->gpio);
    TickType_t now = xTaskGetTickCount();
    if (lvl == 0 && b->last == 1) {
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(b->gpio) == 0) {
            b->down_at = now;
            b->long_fired = false;
            display_note_user_activity();
            b->last = 0;
        }
    } else if (lvl == 1 && b->last == 0) {
        TickType_t held = (now - b->down_at) * portTICK_PERIOD_MS;
        if (!b->long_fired && (b->long_bit == 0 || held < (TickType_t)b->long_ms)) {
            xEventGroupSetBits(g_r4p_events, b->tap_bit);
            ESP_LOGI(TAG_MAIN, "BTN %s tap (%lu ms)", b->name, (unsigned long)held);
        }
        b->last = 1;
    } else if (lvl == 0 && b->last == 0 && b->long_bit && !b->long_fired) {
        TickType_t held = (now - b->down_at) * portTICK_PERIOD_MS;
        if (held >= (TickType_t)b->long_ms) {
            b->long_fired = true;
            xEventGroupSetBits(g_r4p_events, b->long_bit);
            ESP_LOGW(TAG_MAIN, "BTN %s giu %d ms", b->name, b->long_ms);
        }
    }
}

static void btn_task(void *arg)
{
    (void)arg;
    while (1) {
        for (size_t i = 0; i < N_BTN; i++) btn_poll(&s_btns[i]);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t button_init(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < N_BTN; i++) {
        s_btns[i].last = 1;
        if (s_btns[i].gpio >= 0) mask |= 1ULL << (unsigned)s_btns[i].gpio;
    }
    if (!mask) {
        ESP_LOGI(TAG_MAIN, "Button: board khong co nut");
        return ESP_OK;
    }
    gpio_config_t io = {
        .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    xTaskCreatePinnedToCore(btn_task, "btn", 3072, NULL, R4P_TASK_PRIO_NETWORK, NULL, R4P_TASK_CORE_UI);
    ESP_LOGI(TAG_MAIN, "Button init BOOT=GPIO%d DO=GPIO%d XANH=GPIO%d DO(RED)=GPIO%d TRANG=GPIO%d",
             BOARD_BTN_BOOT_GPIO, BOARD_BTN_MEASURE_GPIO, BOARD_BTN_GREEN_GPIO, BOARD_BTN_RED_GPIO, BOARD_BTN_WHITE_GPIO);
    return ESP_OK;
}
