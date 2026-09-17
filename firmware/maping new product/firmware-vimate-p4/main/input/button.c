/**
 * button.c — BOOT button (GPIO0) polling.
 * Tích cực-thấp với pull-up internal. Debounce 30ms.
 *
 * - Tap < 1s     → BTN_PRESS bit
 * - Hold ≥ 5s    → BTN_LONG bit (clear WiFi creds and re-enter BLE setup)
 */
#include "button.h"
#include "vimate.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "ui/display.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DEBOUNCE_MS 30
#define LONG_MS     5000

static void btn_task(void *arg) {
    int last = 1;
    TickType_t down_at = 0;
    bool long_fired = false;
    while (1) {
        int lvl = gpio_get_level(BOARD_BTN_BOOT_GPIO);
        TickType_t now = xTaskGetTickCount();
        if (lvl == 0 && last == 1) {
            /* falling edge */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(BOARD_BTN_BOOT_GPIO) == 0) {
                down_at = now;
                long_fired = false;
                display_note_user_activity();
                last = 0;
            }
        } else if (lvl == 1 && last == 0) {
            /* rising edge */
            TickType_t held = (now - down_at) * portTICK_PERIOD_MS;
            if (!long_fired && held < LONG_MS) {
                xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BTN_PRESS);
                ESP_LOGI(TAG_MAIN, "BTN tap (%lu ms)", (unsigned long)held);
            } else if (held >= LONG_MS) {
                ESP_LOGW(TAG_MAIN, "BTN hold released (%lu ms)", (unsigned long)held);
            }
            last = 1;
        } else if (lvl == 0 && last == 0 && !long_fired) {
            TickType_t held = (now - down_at) * portTICK_PERIOD_MS;
            if (held >= LONG_MS) {
                long_fired = true;
                xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BTN_LONG);
                ESP_LOGW(TAG_MAIN, "BTN long-press detected — enter WiFi setup");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t button_init(void) {
    if (BOARD_BTN_BOOT_GPIO < 0) {
        ESP_LOGI(TAG_MAIN, "Button disabled on this board");
        return ESP_OK;
    }
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_BTN_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    /* Core 0 — input poll nhẹ, cùng Core với main loop nhận event nhanh. */
    xTaskCreatePinnedToCore(btn_task, "btn", 3072, NULL,
                            VIMATE_TASK_PRIO_NETWORK, NULL, VIMATE_TASK_CORE_UI);
    ESP_LOGI(TAG_MAIN, "Button init GPIO%d", BOARD_BTN_BOOT_GPIO);
    return ESP_OK;
}
