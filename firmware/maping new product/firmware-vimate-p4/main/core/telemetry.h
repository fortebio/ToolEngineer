#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void telemetry_install_json_hooks(void);
void telemetry_set_last_error(const char *msg);
esp_err_t telemetry_start(void);
bool telemetry_has_successful_heartbeat(void);
esp_err_t telemetry_try_upload_coredump(void);

#ifdef __cplusplus
}
#endif
