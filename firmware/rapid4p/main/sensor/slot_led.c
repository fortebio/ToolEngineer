#include "slot_led.h"
#include <stdio.h>
#include <string.h>
#include "rapid4p.h"
#include "boards/board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const int s_gpio[BOARD_SENSOR_SLOTS] = BOARD_SLOT_LED_GPIOS;
static uint8_t s_pwm[BOARD_SENSOR_SLOTS];
static bool s_ready;

static void pwm_apply(uint8_t v)
{
#if BOARD_SLOT_LED_PWM_GPIO >= 0
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BOARD_SLOT_LED_LEDC_CHANNEL, v);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BOARD_SLOT_LED_LEDC_CHANNEL);
#else
    (void)v;
#endif
}

esp_err_t slot_led_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_SENSOR_SLOTS; i++) {
        s_pwm[i] = BOARD_SLOT_LED_PWM_DEFAULT;
        if (s_gpio[i] >= 0) mask |= 1ULL << (unsigned)s_gpio[i];
    }
    if (mask) {
        gpio_config_t io = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
        ESP_ERROR_CHECK(gpio_config(&io));
        for (int i = 0; i < BOARD_SENSOR_SLOTS; i++) {
            if (s_gpio[i] >= 0) gpio_set_level(s_gpio[i], !BOARD_SLOT_LED_ON_LEVEL);
        }
    }
#if BOARD_SLOT_LED_PWM_GPIO >= 0
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = BOARD_SLOT_LED_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = BOARD_SLOT_LED_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = BOARD_SLOT_LED_PWM_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BOARD_SLOT_LED_LEDC_CHANNEL, .timer_sel = BOARD_SLOT_LED_LEDC_TIMER,
        .duty = 0, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
#endif
    s_ready = true;
    {
        char en[BOARD_SENSOR_SLOTS * 4 + 1] = "";
        for (int i = 0; i < BOARD_SENSOR_SLOTS; i++) {
            char one[6];
            snprintf(one, sizeof(one), "%s%d", i ? "/" : "", s_gpio[i]);
            strlcat(en, one, sizeof(en));
        }
        ESP_LOGI(TAG_SENSOR, "slot LED x%d en=%s pwm=GPIO%d (%d Hz, mac dinh %d/255)", BOARD_SENSOR_SLOTS, en,
                 BOARD_SLOT_LED_PWM_GPIO, BOARD_SLOT_LED_PWM_FREQ_HZ, BOARD_SLOT_LED_PWM_DEFAULT);
    }
    return ESP_OK;
}

void slot_led_on(int slot)
{
    if (!s_ready || slot < 0 || slot >= BOARD_SENSOR_SLOTS) return;
    pwm_apply(s_pwm[slot]);
    if (s_gpio[slot] >= 0) gpio_set_level(s_gpio[slot], BOARD_SLOT_LED_ON_LEVEL);
}

void slot_led_off(int slot)
{
    if (!s_ready || slot < 0 || slot >= BOARD_SENSOR_SLOTS) return;
    if (s_gpio[slot] >= 0) gpio_set_level(s_gpio[slot], !BOARD_SLOT_LED_ON_LEVEL);
    pwm_apply(0);
    vTaskDelay(pdMS_TO_TICKS(10));   /* như LED_off() gốc */
}

void slot_led_off_all(void)
{
    if (!s_ready) return;
    for (int i = 0; i < BOARD_SENSOR_SLOTS; i++) {
        if (s_gpio[i] >= 0) gpio_set_level(s_gpio[i], !BOARD_SLOT_LED_ON_LEVEL);
    }
    pwm_apply(0);
}

void slot_led_set_brightness(int slot, uint8_t v)
{
    if (slot >= 0 && slot < BOARD_SENSOR_SLOTS) s_pwm[slot] = v;
}

uint8_t slot_led_get_brightness(int slot)
{
    return (slot >= 0 && slot < BOARD_SENSOR_SLOTS) ? s_pwm[slot] : 0;
}
