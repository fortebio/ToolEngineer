/**
 * vimate.h — Global types, constants, common defines cho VIMATE firmware.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIMATE_FW_VERSION  "1.0.154"

#define VIMATE_BRAND_NAME   "GENU"
#define VIMATE_SETUP_PREFIX "GENU-Setup"

/* ===== Emotion enum — khớp internal/gateway/emotion.go trên server ===== */
typedef enum {
    EMOTION_NEUTRAL = 0,
    EMOTION_HAPPY,
    EMOTION_LAUGHING,
    EMOTION_FUNNY,
    EMOTION_SAD,
    EMOTION_ANGRY,
    EMOTION_CRYING,
    EMOTION_LOVING,
    EMOTION_EMBARRASSED,
    EMOTION_SURPRISED,
    EMOTION_SHOCKED,
    EMOTION_THINKING,
    EMOTION_WINKING,
    EMOTION_COOL,
    EMOTION_RELAXED,
    EMOTION_DELICIOUS,
    EMOTION_KISSY,
    EMOTION_CONFIDENT,
    EMOTION_SLEEPY,
    EMOTION_SILLY,
    EMOTION_CONFUSED,
    EMOTION_COUNT_
} vimate_emotion_t;

/* Convert string → enum, fallback NEUTRAL */
vimate_emotion_t vimate_emotion_from_str(const char *s);
const char *vimate_emotion_to_str(vimate_emotion_t e);

/* ===== Device state machine ===== */
typedef enum {
    DEV_STATE_BOOT = 0,
    DEV_STATE_WIFI_PROVISIONING,
    DEV_STATE_WIFI_CONNECTING,
    DEV_STATE_OTA_CHECKING,
    DEV_STATE_NOT_ACTIVATED,   /* show activation code, đợi parent */
    DEV_STATE_NO_PLAN,         /* show "mua gói tại vimate.vn" */
    DEV_STATE_READY,           /* WS connected, đợi listen */
    DEV_STATE_LISTENING,       /* mic streaming */
    DEV_STATE_THINKING,        /* server đang process */
    DEV_STATE_SPEAKING,        /* TTS playing */
    DEV_STATE_ERROR,           /* recoverable error */
} vimate_dev_state_t;

/* ===== Event bits cho FreeRTOS event group ===== */
#define VIMATE_EVT_WIFI_UP        BIT0
#define VIMATE_EVT_WIFI_DOWN      BIT1
#define VIMATE_EVT_WS_CONNECTED   BIT2
#define VIMATE_EVT_WS_DISCONNECT  BIT3
#define VIMATE_EVT_OTA_AVAILABLE  BIT4
#define VIMATE_EVT_BTN_PRESS      BIT5
#define VIMATE_EVT_BTN_LONG       BIT6
#define VIMATE_EVT_VAD_EOT        BIT7   /* VAD detected end-of-turn (silence after speech) */
#define VIMATE_EVT_AUTO_LISTEN    BIT8   /* Server asks firmware to listen after prompt */
#define VIMATE_EVT_WAKE_WORD      BIT9   /* Local WakeNet detected "Hi Lily" */
#define VIMATE_EVT_LISTEN_TIMEOUT BIT10  /* Hard cap active mic turn if VAD never closes */
#define VIMATE_EVT_TTS_START      BIT11  /* Server started speaking; pause logical listen turn */
#define VIMATE_EVT_WS_READY       BIT12  /* Auth + protocol hello accepted by server */
#define VIMATE_EVT_AUTH_REVOKED   BIT13  /* Server rejected persisted device token */
#define VIMATE_EVT_BARGE_IN       BIT14  /* Voice barge-in: trẻ nói khi robot đang phát TTS */
#define VIMATE_EVT_NAV_STOP       BIT15  /* Home/đổi app: hủy logical listen turn */
#define VIMATE_EVT_TTS_TIMEOUT    BIT16  /* Server omitted tts_stop; recover speaker + WakeNet */

extern EventGroupHandle_t g_vimate_events;

/* ===== Server config ===== */
typedef struct {
    char base_url[128];   /* "https://vimate.vn" */
    char ws_url[160];     /* "wss://vimate.vn/ws/" */
    char ota_url[160];    /* "https://vimate.vn/ota/v1/" */
    char device_token[80];/* Bearer token sau khi activate */
    char mac_id[18];      /* "AA:BB:CC:DD:EE:FF" */
    char activation_code[8]; /* 6 ký tự, server gen */
    bool activated;
} vimate_server_config_t;

extern vimate_server_config_t g_vimate_server;

/* ===== Logging tags ===== */
#define TAG_MAIN     "vimate.main"
#define TAG_WIFI     "vimate.wifi"
#define TAG_NVS      "vimate.nvs"
#define TAG_OTA      "vimate.ota"
#define TAG_WS       "vimate.ws"
#define TAG_AUDIO    "vimate.audio"
#define TAG_UI       "vimate.ui"
#define TAG_MCP      "vimate.mcp"
#define TAG_ASSET    "vimate.asset"
#define TAG_CACHE    "vimate.cache"

#ifdef __cplusplus
}
#endif
