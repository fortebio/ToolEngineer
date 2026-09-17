#pragma once

#include "sdkconfig.h"

/*
 * Central FreeRTOS placement for VIMATE firmware.
 *
 * Core 0: UI, WS/control and lightweight device state.
 * Core 1: audio path and large IO/background workers.
 *
 * Audio keeps the highest app priority because mic/I2S must not stutter.
 * Image/OTA/asset workers stay below audio and display, so downloading a
 * banner or firmware cannot preempt realtime playback/capture.
 */
#define VIMATE_TASK_CORE_UI             0
#define VIMATE_TASK_CORE_IO             1

#define VIMATE_TASK_PRIO_DIAGNOSTICS    1
#define VIMATE_TASK_PRIO_HEARTBEAT      2
#define VIMATE_TASK_PRIO_BACKGROUND     3
#define VIMATE_TASK_PRIO_IMAGE          4
#define VIMATE_TASK_PRIO_NETWORK        5
#define VIMATE_TASK_PRIO_DISPLAY        6
#define VIMATE_TASK_PRIO_AUDIO          7

#if CONFIG_VIMATE_BOARD_GENU_GPIO_V6
/* GENU uses micro-opus' external pseudostack, so the mic task no longer needs
 * the old silk call-stack reserve. These measured sizes retain several KB of
 * headroom while returning internal RAM to WakeNet, TLS and image workers. */
#define VIMATE_TASK_STACK_DISPLAY       6144
#define VIMATE_TASK_STACK_AUDIO_MIC     7168
#define VIMATE_TASK_STACK_WEBSOCKET     5632
#else
#define VIMATE_TASK_STACK_DISPLAY       8192
#define VIMATE_TASK_STACK_AUDIO_MIC     12288
#define VIMATE_TASK_STACK_WEBSOCKET     8192
#endif
#define VIMATE_TASK_STACK_IMAGE         8192
#define VIMATE_TASK_STACK_AUDIO_SPK     6144
#define VIMATE_TASK_STACK_BACKGROUND    6144
#define VIMATE_TASK_STACK_OTA_NOW       8192
