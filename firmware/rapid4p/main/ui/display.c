/**
 * display.c — phần CHUNG của lớp màn hình Rapid4P (mọi board). Xem display.h.
 *
 * 2026-09-19: tách phần phụ thuộc panel ra display_hw_<dsi|spi>.c (display_hw.h). Ở đây chỉ còn:
 * đèn nền LEDC (kênh 0 / timer 0 — LED slot dùng kênh 1 / timer 1), tự tắt màn khi rảnh,
 * lvgl_port_init (task LVGL core UI prio 6), hàng đợi + display task chạy callback dưới lock.
 * Trình tự display_init(): LEDC → lvgl_port_init → display_hw_init → đèn nền 100 % → queue/task.
 */
#include "display.h"
#include "display_hw.h"
#include "rapid4p.h"
#include "boards/board.h"
#include "core/task_profile.h"

#include "driver/ledc.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* ===== Trạng thái ===== */
#define DISPLAY_SCHED_Q_LEN 16
typedef struct { void (*cb)(void *); void *arg; } display_sched_item_t;

static struct {
    bool hw_ready;
    lv_display_t *disp;
    QueueHandle_t sched_q;
    TaskHandle_t task;
    int backlight_percent;
    int sleep_timeout_sec;
    int64_t last_activity_us;
    bool sleeping;
    esp_timer_handle_t sleep_timer;
} s_d;

bool display_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max)
{
    return display_hw_rotate_stats(full_frames, area_blits, full_us_max);
}

uint32_t display_flush_count(void) { return display_hw_flush_count(); }

/* ===== Đèn nền ===== */
static void backlight_apply_hw(int percent)
{
#if BOARD_LCD_HAS_BACKLIGHT
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (1023 * percent) / 100;
#if !BOARD_LCD_BL_ON_LEVEL
    duty = 1023 - duty;
#endif
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#else
    (void)percent;
#endif
}

static void sleep_timer_cb(void *arg)
{
    (void)arg;
    if (s_d.sleep_timeout_sec <= 0 || s_d.sleeping) return;
    const int64_t idle_us = esp_timer_get_time() - s_d.last_activity_us;
    if (idle_us >= (int64_t)s_d.sleep_timeout_sec * 1000000LL) {
        s_d.sleeping = true;
        backlight_apply_hw(0);
        ESP_LOGI(TAG_UI, "backlight off (idle %d s)", s_d.sleep_timeout_sec);
    }
}

void display_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_d.backlight_percent = percent;
    s_d.sleeping = false;
    s_d.last_activity_us = esp_timer_get_time();
    backlight_apply_hw(percent);
}

void display_set_sleep_timeout(int seconds)
{
    s_d.sleep_timeout_sec = seconds < 0 ? 0 : seconds;
    s_d.last_activity_us = esp_timer_get_time();
}

void display_note_user_activity(void)
{
    s_d.last_activity_us = esp_timer_get_time();
    if (s_d.sleeping) {
        s_d.sleeping = false;
        backlight_apply_hw(s_d.backlight_percent);
    }
}

bool display_is_sleeping(void) { return s_d.sleeping; }

/* ===== Hàng đợi + display task ===== */
bool display_schedule(void (*cb)(void *), void *arg)
{
    if (!s_d.sched_q) {
        ESP_LOGW(TAG_UI, "display_schedule truoc khi init");
        return false;
    }
    display_sched_item_t item = { .cb = cb, .arg = arg };
    if (xQueueSend(s_d.sched_q, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG_UI, "display sched_q day - bo callback");
        return false;
    }
    return true;
}

static void display_task(void *arg)
{
    (void)arg;
    display_sched_item_t item;
    ESP_LOGI(TAG_UI, "display task core %d prio %d", xPortGetCoreID(), uxTaskPriorityGet(NULL));
    while (1) {
        if (xQueueReceive(s_d.sched_q, &item, portMAX_DELAY) == pdTRUE && item.cb) {
            lvgl_port_lock(0);
            item.cb(item.arg);
            lvgl_port_unlock();
        }
    }
}

void display_lock(void) { lvgl_port_lock(0); }
void display_unlock(void) { lvgl_port_unlock(); }
bool display_hw_ready(void) { return s_d.hw_ready; }
int display_lcd_width(void) { return BOARD_LCD_H_RES; }
int display_lcd_height(void) { return BOARD_LCD_V_RES; }

/* ===== Init ===== */
esp_err_t display_init(void)
{
    s_d.hw_ready = false;

#if BOARD_LCD_HAS_BACKLIGHT
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = BOARD_LCD_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .gpio_num = BOARD_LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
#endif

    /* LVGL task = task duy nhất rasterize + flush. Core 0 (UI), prio 6. Khởi tạo TRƯỚC
     * display_hw_init vì lvgl_port_add_disp*() cần port đã sống. */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 10240;
    lvgl_cfg.task_priority = R4P_TASK_PRIO_DISPLAY;
    lvgl_cfg.task_affinity = R4P_TASK_CORE_UI;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG_UI, "lvgl_port_init loi");

    /* Panel theo board (display_hw_dsi.c / display_hw_spi.c). Lỗi → trả lỗi, KHÔNG abort. */
    ESP_RETURN_ON_ERROR(display_hw_init(&s_d.disp), TAG_UI, "display_hw_init loi - bo display, van boot");
    backlight_apply_hw(100);

    s_d.backlight_percent = 100;
    s_d.sleep_timeout_sec = 0;
    s_d.last_activity_us = esp_timer_get_time();
    s_d.sleeping = false;
    s_d.hw_ready = true;

    s_d.sched_q = xQueueCreate(DISPLAY_SCHED_Q_LEN, sizeof(display_sched_item_t));
    configASSERT(s_d.sched_q);
    BaseType_t r = xTaskCreatePinnedToCore(display_task, "r4p_disp", R4P_TASK_STACK_DISPLAY, NULL,
                                           R4P_TASK_PRIO_DISPLAY, &s_d.task, R4P_TASK_CORE_UI);
    configASSERT(r == pdPASS);

    const esp_timer_create_args_t targs = { .callback = sleep_timer_cb, .name = "bl_sleep" };
    if (esp_timer_create(&targs, &s_d.sleep_timer) == ESP_OK) {
        esp_timer_start_periodic(s_d.sleep_timer, 1000000);
    }

    ESP_LOGI(TAG_UI, "Display HW ready %dx%d (BL=100%%)", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}
