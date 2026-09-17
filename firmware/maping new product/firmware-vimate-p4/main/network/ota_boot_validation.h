#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Start a rollback guard for a freshly installed PENDING_VERIFY image. */
esp_err_t ota_boot_validation_start(bool display_ready, bool audio_ready);

/* Record that the authenticated HTTPS bootstrap/OTA endpoint completed. */
void ota_boot_validation_note_ota_api_result(esp_err_t result);
