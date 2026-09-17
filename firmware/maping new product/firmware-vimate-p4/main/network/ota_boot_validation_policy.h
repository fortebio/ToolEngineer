#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool display_ready;
    bool audio_ready;
    bool wifi_ready;
    bool ota_api_ready;
    bool requires_server_session;
    bool server_gate_reached;
    bool ws_protocol_ready;
    bool heartbeat_sent;
    uint32_t stable_ms;
} ota_boot_health_t;

bool ota_boot_health_is_valid(const ota_boot_health_t *health);
