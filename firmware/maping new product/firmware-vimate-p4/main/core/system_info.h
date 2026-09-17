/**
 * system_info.h — chip info, MAC address, free heap, etc.
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_info_init(void);
const char *system_info_get_mac_str(void);   /* "AA:BB:CC:DD:EE:FF" */
const char *system_info_get_chip_model(void); /* "ESP32-S3" */
size_t system_info_free_heap(void);

#ifdef __cplusplus
}
#endif
