#include "network/ota_boot_validation_policy.h"

#include <assert.h>
#include <stdbool.h>

static ota_boot_health_t healthy(void) {
    return (ota_boot_health_t){
        .display_ready = true,
        .audio_ready = true,
        .wifi_ready = true,
        .ota_api_ready = true,
        .requires_server_session = true,
        .server_gate_reached = false,
        .ws_protocol_ready = true,
        .heartbeat_sent = true,
        .stable_ms = 30000,
    };
}

int main(void) {
    ota_boot_health_t h = healthy();
    assert(ota_boot_health_is_valid(&h));

    h.ws_protocol_ready = false;
    assert(!ota_boot_health_is_valid(&h));
    h = healthy();
    h.heartbeat_sent = false;
    assert(!ota_boot_health_is_valid(&h));
    h = healthy();
    h.ota_api_ready = false;
    assert(!ota_boot_health_is_valid(&h));
    h = healthy();
    h.stable_ms = 29999;
    assert(!ota_boot_health_is_valid(&h));
    h = healthy();
    h.audio_ready = false;
    assert(!ota_boot_health_is_valid(&h));

    h = healthy();
    h.requires_server_session = false;
    h.ws_protocol_ready = false;
    h.heartbeat_sent = false;
    assert(ota_boot_health_is_valid(&h));

    h = healthy();
    h.server_gate_reached = true;
    h.ws_protocol_ready = false;
    h.heartbeat_sent = false;
    assert(ota_boot_health_is_valid(&h));

    return 0;
}
