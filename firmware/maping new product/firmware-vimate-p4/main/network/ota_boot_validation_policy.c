#include "ota_boot_validation_policy.h"

#include <stddef.h>

#define OTA_BOOT_MIN_STABLE_MS 30000U

bool ota_boot_health_is_valid(const ota_boot_health_t *health) {
    return health != NULL &&
           health->display_ready &&
           health->audio_ready &&
           health->wifi_ready &&
           health->ota_api_ready &&
           (!health->requires_server_session ||
            health->server_gate_reached ||
            (health->ws_protocol_ready && health->heartbeat_sent)) &&
           health->stable_ms >= OTA_BOOT_MIN_STABLE_MS;
}
