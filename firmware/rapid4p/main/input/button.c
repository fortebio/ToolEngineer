/**
 * button.c — quét nút tích cực-thấp (pull-up nội/ngoài), chống dội 30 ms. Kế thừa
 * firmware-vimate-p4/main/input/button.c. Bảng nút: BOOT (giữ 5 s = xoá WiFi), ĐO (P4), và
 * 3 nút vật lý XANH/ĐỎ/TRẮNG của vỏ máy Rapid 2.8" (tap + giữ BOARD_BTN_HOLD_MS → softkey trong
 * ui_reader.c). GPIO -1 = board không có nút đó.
 *
 * Thao tác nông dân (găng tay, tay to — 2026-09-20):
 *  - Nhấn khi màn ĐANG NGỦ chỉ đánh thức màn (wake-only): không phát tap/giữ khi nhả — tránh
 *    "bấm đại cho sáng" mà máy lại khởi động đo/xoá.
 *  - Giữ quá ngưỡng: phát long_bit lần đầu, rồi LẶP mỗi REPEAT_MS khi còn giữ (màn ngưỡng ±50
 *    liên tục; các màn khác xử lý idempotent + khoá phím trong ui_reader nên lặp vô hại).
 *  - Hai nút cùng lúc: mỗi nút vẫn phát bit riêng, app_main chọn MỘT (else-if) mỗi lần thức.
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
#define REPEAT_MS   400     /* chu kỳ lặp long_bit khi còn giữ (chỉ nút có repeat) */

typedef struct {
    int gpio;
    EventBits_t tap_bit, long_bit;   /* long_bit 0 = không có nhấn giữ */
    int long_ms;                     /* ngưỡng giữ của nút này */
    bool repeat;                     /* giữ tiếp → lặp long_bit mỗi REPEAT_MS */
    const char *name;
    int last;
    TickType_t down_at;
    TickType_t long_at;              /* lần phát long_bit gần nhất (cho repeat) */
    bool long_fired;
    bool wake_only;                  /* nhấn lúc màn ngủ: chỉ đánh thức, nuốt tap/giữ */
} btn_t;

static btn_t s_btns[] = {
    { .gpio = BOARD_BTN_BOOT_GPIO,    .tap_bit = R4P_EVT_BTN_PRESS,   .long_bit = R4P_EVT_BTN_LONG,       .long_ms = LONG_MS,           .repeat = false, .name = "BOOT",  .last = 1 },
    { .gpio = BOARD_BTN_MEASURE_GPIO, .tap_bit = R4P_EVT_BTN_MEASURE, .long_bit = 0,                      .long_ms = LONG_MS,           .repeat = false, .name = "DO",    .last = 1 },
    { .gpio = BOARD_BTN_GREEN_GPIO,   .tap_bit = R4P_EVT_BTN_GREEN,   .long_bit = R4P_EVT_BTN_GREEN_LONG, .long_ms = BOARD_BTN_HOLD_MS, .repeat = true,  .name = "XANH",  .last = 1 },
    { .gpio = BOARD_BTN_RED_GPIO,     .tap_bit = R4P_EVT_BTN_RED,     .long_bit = R4P_EVT_BTN_RED_LONG,   .long_ms = BOARD_BTN_HOLD_MS, .repeat = true,  .name = "DO",    .last = 1 },
    { .gpio = BOARD_BTN_WHITE_GPIO,   .tap_bit = R4P_EVT_BTN_WHITE,   .long_bit = R4P_EVT_BTN_WHITE_LONG, .long_ms = BOARD_BTN_HOLD_MS, .repeat = true,  .name = "TRANG", .last = 1 },
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
            /* Đọc trạng thái ngủ TRƯỚC khi đánh thức: nhấn lúc màn tắt = chỉ bật màn. BOOT giữ 5 s
             * (xoá WiFi) vẫn phải chạy dù màn ngủ → không áp cho nút không repeat. */
            b->wake_only = b->repeat && display_is_sleeping();
            display_note_user_activity();
            b->last = 0;
            if (b->wake_only) ESP_LOGI(TAG_MAIN, "BTN %s: man dang ngu -> chi danh thuc", b->name);
        }
    } else if (lvl == 1 && b->last == 0) {
        TickType_t held = (now - b->down_at) * portTICK_PERIOD_MS;
        if (!b->wake_only && !b->long_fired && (b->long_bit == 0 || held < (TickType_t)b->long_ms)) {
            xEventGroupSetBits(g_r4p_events, b->tap_bit);
            ESP_LOGI(TAG_MAIN, "BTN %s tap (%lu ms)", b->name, (unsigned long)held);
        }
        b->last = 1;
        b->wake_only = false;
    } else if (lvl == 0 && b->last == 0 && b->long_bit && !b->wake_only) {
        TickType_t held = (now - b->down_at) * portTICK_PERIOD_MS;
        if (!b->long_fired) {
            if (held >= (TickType_t)b->long_ms) {
                b->long_fired = true;
                b->long_at = now;
                xEventGroupSetBits(g_r4p_events, b->long_bit);
                ESP_LOGW(TAG_MAIN, "BTN %s giu %d ms", b->name, b->long_ms);
            }
        } else if (b->repeat && (now - b->long_at) * portTICK_PERIOD_MS >= REPEAT_MS) {
            b->long_at = now;
            xEventGroupSetBits(g_r4p_events, b->long_bit | R4P_EVT_BTN_REPEAT);
            display_note_user_activity();   /* giữ lâu vẫn là hoạt động */
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
