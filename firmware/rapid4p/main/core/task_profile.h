#pragma once

#include "sdkconfig.h"

/*
 * Bố trí task FreeRTOS của Rapid4P (kế thừa cách chia của vimate-p4, bỏ audio).
 *
 * Core 0 (UI): display task, LVGL, touch, nút, main task.
 * Core 1 (IO): WiFi/lwIP (sdkconfig ghim), task ĐO (I2C bus riêng, ~34 s/chu trình),
 *              tải kết quả, OTA.
 *
 * Ưu tiên: input (touch 7) > LVGL/display (6) > network/main (5) > đo (4) > nền.
 * Task đo thấp hơn LVGL để chu trình 34 s không bao giờ làm khựng màn; nó chỉ chờ
 * I2C + vTaskDelay nên không cần cao.
 */
#define R4P_TASK_CORE_UI             0
#define R4P_TASK_CORE_IO             1

#define R4P_TASK_PRIO_DIAGNOSTICS    1
#define R4P_TASK_PRIO_BACKGROUND     3
#define R4P_TASK_PRIO_MEASURE        4
#define R4P_TASK_PRIO_NETWORK        5
#define R4P_TASK_PRIO_DISPLAY        6

#define R4P_TASK_STACK_DISPLAY       8192
#define R4P_TASK_STACK_MEASURE       4096
#define R4P_TASK_STACK_UPLOAD        6144
#define R4P_TASK_STACK_OTA           8192
