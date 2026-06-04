#ifndef _UPDATEOTA_H_
#define _UPDATEOTA_H_

#include "define.h"
#include "displayCLD.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Update.h>

// OTA state machine. Single volatile enum replaces the previous pair of
// volatile bools (flag_check_Update + flagUpdate) so transitions are atomic
// from any task's point of view (one byte write on ESP32).
//
// Lifecycle:
//   OTA_IDLE
//     └─ checkFirmware() finds newer version ─► OTA_AVAILABLE
//                                                 │
//                                                 ├─ user RED on eUpdateOTA ─► OTA_USER_ACCEPTED
//                                                 │      └─ updateFirmware() picks up ─► OTA_UPDATING
//                                                 │            ├─ HTTP_UPDATE_OK ─► ESP.restart()
//                                                 │            └─ failure ─► OTA_FAILED ─► (back to OTA_IDLE on next user nav)
//                                                 │
//                                                 └─ user BLUE on eUpdateOTA ─► OTA_DISMISSED (no re-prompt this boot)
enum OtaState : uint8_t
{
    OTA_IDLE = 0,        // no update available / nothing pending
    OTA_AVAILABLE,       // newer firmware found, prompt awaiting user
    OTA_USER_ACCEPTED,   // user pressed RED, NetworkTask should start download
    OTA_UPDATING,        // download in progress (re-entry guard)
    OTA_FAILED,          // download/install failed; user can retry from menu
    OTA_DISMISSED        // user dismissed the prompt; do not re-prompt this boot
};

void checkFirmware(void);
void updateFirmware(void);

extern volatile OtaState otaState;
extern String fwCont, fwVer;

#endif
