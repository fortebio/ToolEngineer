#ifndef _WIFISTORE_H_
#define _WIFISTORE_H_

#include <Arduino.h>

/* Saved WiFi networks, so the machine can be moved between rooms/sites without being
 * reconfigured each time.
 *
 * Stored in NVS (Preferences, namespace "wifinets"), deliberately NOT in EEPROM and NOT in
 * LittleFS:
 *   - EEPROM has exactly one SSID + one password slot, packed tight against ADDR_ID_BLE;
 *     a second pair would shift addresses and a field upgrade would read garbage.
 *   - LittleFS gets fully reflashed by `uploadfs`/`uploadall` (the image comes from data/,
 *     which has no wifi.json), so anything written there is WIPED on every UI update.
 *   - NVS lives in its own partition that uploadfs does not touch, so the saved list
 *     SURVIVES firmware/UI uploads. It also has room for several entries and drops the
 *     54-char password cap of the EEPROM slot.
 *
 * The first entry is the preferred one: connection attempts walk the list in order.
 */

#define WIFI_STORE_MAX 5 // bounds the NVS keys and the boot join sequence

struct WifiNet
{
    String ssid;
    String pass;
};

// Read the saved list into `out` (up to `max`). Returns how many were read.
uint8_t wifiStoreLoad(WifiNet *out, uint8_t max);

// Add, or update the password of an SSID already stored. Newest goes to the FRONT, so
// "the network I just configured" is the one tried first. Returns false only on a
// filesystem error or an invalid ssid.
bool wifiStoreAdd(const String &ssid, const String &pass);

// Forget one network. Returns true if it was there.
bool wifiStoreRemove(const String &ssid);

uint8_t wifiStoreCount();

// Look up the saved password for an SSID (the password never goes to the web, so a
// "connect to this saved network" action reads it here on-device). Returns false if the
// SSID is not in the list.
bool wifiStoreGetPass(const String &ssid, String &passOut);

// JSON for the web: {"nets":[{"ssid":"..."},...]} - SSIDs ONLY. Passwords never leave
// the device; the panel has no reason to show them and a dashboard is not authenticated.
String wifiStoreListJson();

/* ---- Trial-then-commit --------------------------------------------------------------
 * A wrong password must NOT overwrite the working network. So a save/connect does NOT
 * commit: it parks the new credentials as a TRIAL and reboots. setup() tries the trial
 * FIRST; only if it actually connects does it commit (EEPROM preferred + list front).
 * If it fails, the trial is discarded, the previous network is kept, and a result is left
 * for the web to report ("wrong password - re-enter"). A test-connection is the only way
 * to verify a password, and that needs a reboot (runtime WiFi.begin deadlocks async_tcp).
 */
enum WifiTrialResult : uint8_t
{
    WIFI_TRIAL_NONE = 0,   // no attempt since this flag was last read/cleared
    WIFI_TRIAL_OK = 1,     // last trial connected and was committed
    WIFI_TRIAL_FAILED = 2, // last trial could not connect (wrong password / out of range)
};

// Park new credentials to be tested on the next boot. Clears any previous result.
void wifiStoreSetTrial(const String &ssid, const String &pass);
// True + fills ssid/pass if a trial is pending (call from setup()).
bool wifiStoreGetTrial(String &ssidOut, String &passOut);
// Drop the pending trial (after it has been tested).
void wifiStoreClearTrial();
// Record what happened, so the web can tell the user. `ssid` = which network was tried;
// `reason` = "auth" (association failed - likely wrong password) or "range" (SSID not
// found - out of range), so the panel doesn't cry "wrong password" for a moved machine.
void wifiStoreSetTrialResult(WifiTrialResult r, const String &ssid, const String &reason = "");
// Read the last result (WIFI_TRIAL_NONE if nothing pending to report).
WifiTrialResult wifiStoreGetTrialResult(String &ssidOut, String &reasonOut);

#endif
