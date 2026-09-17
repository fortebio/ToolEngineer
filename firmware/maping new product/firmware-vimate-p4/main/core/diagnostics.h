/**
 * diagnostics.h — lightweight runtime telemetry for soak/field debugging.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void diagnostics_log_boot(void);
esp_err_t diagnostics_start(void);

#ifdef __cplusplus
}
#endif

