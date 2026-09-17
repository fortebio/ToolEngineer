/**
 * diagnostics.c — reset reason + heap telemetry.
 *
 * Keep this intentionally small: no LVGL calls, no network, no allocations in
 * the periodic hot path. It is safe to leave enabled during beta/RC soak tests.
 */
#include "diagnostics.h"
#include "vimate.h"
#include "core/task_profile.h"
#include "ui/display.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *reset_reason_name(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

void diagnostics_log_boot(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGW(TAG_MAIN, "boot reset_reason=%s(%d)",
             reset_reason_name(reason), (int)reason);
}

#if CONFIG_VIMATE_DIAG_ENABLE
#if configUSE_TRACE_FACILITY
#define DIAG_MAX_TASKS 48
#define DIAG_STACK_LOW_BYTES 1536

static bool diagnostics_is_important_task(const char *name) {
    return name &&
           (strcmp(name, "mic_enc") == 0 ||
            strcmp(name, "spk_dec") == 0 ||
            strcmp(name, "ws_client") == 0 ||
            strcmp(name, "vimate_display") == 0 ||
            strcmp(name, "lvgl_port") == 0 ||
            strcmp(name, "vimate_diag") == 0 ||
            strcmp(name, "wifi") == 0 ||
            strcmp(name, "nimble_host") == 0);
}

static void diagnostics_log_task_stacks(TaskStatus_t *tasks) {
    if (!tasks) {
        return;
    }
    UBaseType_t task_count = uxTaskGetSystemState(tasks, DIAG_MAX_TASKS, NULL);
    for (UBaseType_t i = 0; i < task_count; ++i) {
        const char *name = tasks[i].pcTaskName;
        uint32_t stack_free = (uint32_t)tasks[i].usStackHighWaterMark;
        /* IDLEx / ipcx có stack 1–1,5 KB theo thiết kế → còn ~1 KB là bình thường,
         * không cảnh báo (nhiễu 8 dòng mỗi kỳ diag trên P4). */
        bool sys_small = name && (strncmp(name, "IDLE", 4) == 0 || strncmp(name, "ipc", 3) == 0);
        if (stack_free < DIAG_STACK_LOW_BYTES && !sys_small) {
            ESP_LOGW(TAG_MAIN, "diag stack low task=%s free=%u prio=%u",
                     name ? name : "?",
                     (unsigned)stack_free,
                     (unsigned)tasks[i].uxCurrentPriority);
        } else if (diagnostics_is_important_task(name)) {
            ESP_LOGI(TAG_MAIN, "diag stack task=%s free=%u prio=%u",
                     name,
                     (unsigned)stack_free,
                     (unsigned)tasks[i].uxCurrentPriority);
        }
    }
    if (task_count >= DIAG_MAX_TASKS) {
        ESP_LOGW(TAG_MAIN, "diag stack task list truncated max=%u",
                 (unsigned)DIAG_MAX_TASKS);
    }
}
#endif

static void diagnostics_task(void *arg) {
    (void)arg;
    uint32_t loop_count = 0;
#if configUSE_TRACE_FACILITY
    TaskStatus_t *tasks = heap_caps_calloc(DIAG_MAX_TASKS, sizeof(TaskStatus_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tasks) {
        tasks = heap_caps_calloc(DIAG_MAX_TASKS, sizeof(TaskStatus_t),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
#endif
    while (1) {
        uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        uint32_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG_MAIN,
                 "diag heap internal=%u min=%u psram=%u min=%u task_stack=%u flush=%u",
                 (unsigned)internal_free, (unsigned)internal_min,
                 (unsigned)psram_free, (unsigned)psram_min,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL),
                 (unsigned)display_flush_count());
        {
            uint32_t rf = 0, ra = 0, rus = 0;
            if (display_rotate_stats(&rf, &ra, &rus)) {
                ESP_LOGI(TAG_MAIN, "diag rotate full=%u areas=%u full_max=%uus",
                         (unsigned)rf, (unsigned)ra, (unsigned)rus);
            }
        }
        if (internal_free < 48 * 1024 || internal_min < 32 * 1024) {
            ESP_LOGW(TAG_MAIN, "diag low internal heap free=%u min=%u",
                     (unsigned)internal_free, (unsigned)internal_min);
        }
#if configUSE_TRACE_FACILITY
        if ((++loop_count % 2) == 0) {
            diagnostics_log_task_stacks(tasks);
        }
#endif
        /* Thay vì ngủ 1 lèo: poll 5 s/lần để dump wake capture (audio_pipeline.c,
         * bản diag) sớm khi đã ghi đủ. */
        {
            extern bool audio_pipeline_wake_capture_dump(void);
            int left_ms = CONFIG_VIMATE_DIAG_INTERVAL_SEC * 1000;
            while (left_ms > 0) {
                int step = left_ms > 5000 ? 5000 : left_ms;
                vTaskDelay(pdMS_TO_TICKS(step));
                left_ms -= step;
                (void)audio_pipeline_wake_capture_dump();
            }
        }
    }
}
#endif

esp_err_t diagnostics_start(void) {
#if CONFIG_VIMATE_DIAG_ENABLE
    BaseType_t ok = xTaskCreatePinnedToCore(diagnostics_task, "vimate_diag",
                                            3072, NULL,
                                            VIMATE_TASK_PRIO_DIAGNOSTICS, NULL,
                                            VIMATE_TASK_CORE_UI);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
#else
    return ESP_OK;
#endif
}
