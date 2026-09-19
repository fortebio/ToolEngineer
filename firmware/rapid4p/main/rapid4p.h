/**
 * rapid4p.h — kiểu/hằng/khai báo dùng chung của firmware Rapid4P (RAPID READER 4 SLOT).
 *
 * Thay cho vimate.h của cây firmware-vimate-p4: các module kế thừa (wifi_mgr,
 * nvs_store, captive_dns, ui_wifi_setup, system_info) chỉ cần TAG log, event group,
 * bit sự kiện WiFi và vài hằng thương hiệu — tất cả ở đây.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "boards/board.h"     /* BOARD_SENSOR_SLOTS = nguồn sự thật số khe */

#ifdef __cplusplus
extern "C" {
#endif

/* Version phát hành. registry: system/products.yaml › rapid4p.firmware.version_source
 * bắt chuỗi "vX.Y.Z" từ dòng này. Đổi version = đổi Ở ĐÂY và tag fw/rapid4p/vX.Y.Z. */
#define R4P_FW_VERSION      "v0.1.0"
/* Khoá kho OTA + PCB đi theo BOARD (board header): P4 4.3" = "rapid4p"/"P4C5-43",
 * S3 2.8" = "rapid4p-s3"/"S3-28". Hai khoá registry riêng (variant_of) để không đẩy nhầm ảnh. */
#define R4P_PRODUCT_KEY     BOARD_PRODUCT_KEY
#define R4P_HW_VERSION      BOARD_HW_VERSION

#define R4P_BRAND_NAME      "FBT"
#define R4P_SETUP_PREFIX    "FBT-Rapid4P"  /* SSID SoftAP: FBT-Rapid4P-XX:XX */

/* ===== Số slot / mẫu đo (giữ ngữ nghĩa FBT-ReaderPlus-1.0) ===== */
#define R4P_SLOTS           BOARD_SENSOR_SLOTS   /* KHÔNG hard-code: đổi số khe ở board header */
#define R4P_STR_(x)         #x
#define R4P_STR(x)          R4P_STR_(x)
#define R4P_PRODUCT_NAME    "RAPID READER " R4P_STR(BOARD_SENSOR_SLOTS) " SLOT"
#define R4P_ROUNDS          3      /* numSampling: 3 vòng đo */
#define R4P_SAMPLES         3      /* numSample: 3 mẫu lux mỗi slot mỗi vòng */
#define R4P_RESULT_MIN      0      /* valueMinsensor */
#define R4P_RESULT_MAX      3000   /* valueMAXsensor */
#define R4P_THRESHOLD_DEFAULT 600  /* sensor::begin() ReaderPlus; MAPPING §5 mục 9 chưa chốt 500/600 */

/* Bệnh (sick_type) và mẫu (sample_type) — thứ tự = index lưu NVS, KHÔNG đổi. */
typedef enum { R4P_SICK_PC = 0, R4P_SICK_EHP, R4P_SICK_EMS, R4P_SICK_WSSV, R4P_SICK_TPD, R4P_SICK_COUNT } r4p_sick_t;
typedef enum { R4P_SAMPLE_VANNAMEI = 0, R4P_SAMPLE_MONODON, R4P_SAMPLE_TILAPIA, R4P_SAMPLE_PIG, R4P_SAMPLE_WATER, R4P_SAMPLE_COUNT } r4p_sample_t;
typedef enum { R4P_LANG_VI = 0, R4P_LANG_EN, R4P_LANG_ZH, R4P_LANG_TW, R4P_LANG_COUNT } r4p_lang_t;

const char *r4p_sick_name(r4p_sick_t s);       /* "PC", "EHP", ... (khoá payload/NVS) */
const char *r4p_sample_name(r4p_sample_t s);   /* "PRAWN Vannamei", ... */

/* ===== Máy trạng thái thiết bị (hiển thị góc màn + quyết định luồng) ===== */
typedef enum {
    R4P_STATE_BOOT = 0,
    R4P_STATE_WIFI_PROVISIONING,
    R4P_STATE_WIFI_CONNECTING,
    R4P_STATE_READY,
    R4P_STATE_MEASURING,
    R4P_STATE_CALIBRATING,
    R4P_STATE_UPLOADING,
    R4P_STATE_OTA,
    R4P_STATE_ERROR,
} r4p_dev_state_t;

/* ===== Bit sự kiện (FreeRTOS event group g_r4p_events) ===== */
#define R4P_EVT_WIFI_UP        BIT0
#define R4P_EVT_WIFI_DOWN      BIT1
#define R4P_EVT_BTN_PRESS      BIT2   /* BOOT tap */
#define R4P_EVT_BTN_LONG       BIT3   /* BOOT giữ 5 s → xoá WiFi, vào SoftAP */
#define R4P_EVT_BTN_MEASURE    BIT4   /* BTN3 GPIO0 tap */
#define R4P_EVT_MEASURE_DONE   BIT5   /* app/measure.c xong một chu trình */
#define R4P_EVT_UPLOAD_QUEUED  BIT6   /* có kết quả chờ gửi */
#define R4P_EVT_OTA_AVAILABLE  BIT7

extern EventGroupHandle_t g_r4p_events;

/* ===== Cấu hình server (Engineer Server, server/README.md) ===== */
typedef struct {
    char base_url[128];    /* CONFIG_RAPID4P_SERVER_BASE, vd https://hub.fortebio.tech */
    char mac_id[18];       /* "AA:BB:CC:DD:EE:FF" (system_info) */
    char device_id[24];    /* mã máy người dùng nhập qua portal; rỗng = chưa cấu hình */
} r4p_config_t;

extern r4p_config_t g_r4p_cfg;

/* ===== TAG log ===== */
#define TAG_MAIN     "r4p.main"
#define TAG_WIFI     "r4p.wifi"
#define TAG_NVS      "r4p.nvs"
#define TAG_OTA      "r4p.ota"
#define TAG_UI       "r4p.ui"
#define TAG_SENSOR   "r4p.sensor"
#define TAG_MEASURE  "r4p.measure"
#define TAG_NET      "r4p.net"

#ifdef __cplusplus
}
#endif
