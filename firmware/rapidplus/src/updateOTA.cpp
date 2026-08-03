#include "updateOTA.h"
#include "webDashboard.h" // dashboardDeviceBusy() / dashboardRequestRestart(): never
                          // hijack the display or reboot into a run in progress

int currentVersion = 19;
int fwVersion = 0;
volatile OtaState otaState = OTA_IDLE;
volatile uint32_t otaLastCheck = 0;   // millis() of last completed check (0 = never)
volatile bool otaCheckFailed = false; // last completed check errored (non-200)
String fwUrl = "", fwName = "", fwVer = "", fwCont = "";
String baseUrl = "https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/" + FirmwareVer + "/";
String checkFile = "updateOTA.json";

/***********************************************************************
 * Function: checkFirmware()
 * Description: Queries the GitHub-hosted updateOTA.json over HTTP (returns
 *  immediately if WiFi is not connected), parses the version code, file
 *  name, version string and content. If the remote versionCode is newer
 *  than currentVersion it sets otaState to OTA_AVAILABLE and triggers the
 *  display to show the eUpdateOTA prompt; otherwise it sets otaState to
 *  OTA_IDLE.
 * pramameter: none
 * return: none
 */
void checkFirmware(bool promptOnDevice)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(baseUrl + checkFile);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        info_displayln(payload);
        DynamicJsonDocument json(1024);
        deserializeJson(json, payload);
        fwVersion = json["versionCode"].as<int>();
        fwName = json["fileName"].as<String>();
        fwUrl = baseUrl + fwName;
        fwVer = json["version"].as<String>();
        fwCont = json["content"].as<String>();
        if (fwVersion > currentVersion)
        {
            info_displayln("Firmware update available");
            // One-shot transition + display trigger. We do NOT keep forcing
            // type_infor every main-loop tick — once set, the display task
            // draws the OTA prompt on its next iteration and user navigation
            // is free to move on/off the screen normally.
            otaState = OTA_AVAILABLE;
            if (promptOnDevice)
            {
                _displayCLD.type_infor = eUpdateOTA;
                _displayCLD.changeScreen = true;
            }
        }
        else
        {
            info_displayln("You have the lasted version");
            otaState = OTA_IDLE;
        }
        otaCheckFailed = false;
    }
    else
    {
        // The GET completed but the server said no (rate limit, DNS/TLS trouble, 404).
        // Mark it FAILED - without this, otaLastCheck stayed 0 and the web sat on
        // "Not checked yet" forever with no hint the check had actually run and failed.
        Serial.printf("[ota] check failed: HTTP %d\n", httpCode);
        otaCheckFailed = true;
    }
    // Set on ANY completed GET (success or not) so the web can tell "checked, up to date"
    // and "checked, failed" apart from "never checked".
    otaLastCheck = millis() ? millis() : 1;
    http.end();
}

/***********************************************************************
 * Function: updateFirmware()
 * Description: Performs the OTA download when otaState is OTA_USER_ACCEPTED;
 *  returns early for any other state (re-entry guard against OTA_UPDATING).
 *  If WiFi dropped it sets OTA_FAILED and returns to the start screen.
 *  Otherwise it claims the slot (OTA_UPDATING), shows the waiting screen and
 *  runs httpUpdate.update() over an insecure HTTPS client: on
 *  HTTP_UPDATE_OK it restarts into the new firmware, on failure/no-update it
 *  parks in OTA_FAILED (no auto-restart) and returns to the start screen.
 * pramameter: none
 *  return: none
 */
// Send the display back to the start screen ONLY if nothing is running. type_infor IS the
// state machine, not just a screen: a run can be started by hand (InputTask is never gated)
// during the ~2 minute download, and forcing escreenStart on top of it would derail that run
// as well as its results. Failing an OTA must never cost a sample.
static void showStartScreenIfIdle()
{
    if (dashboardDeviceBusy())
        return;
    _displayCLD.type_infor = escreenStart;
    _displayCLD.changeScreen = true;
}

void updateFirmware(void)
{
    // Re-entry guard: only act on the explicit "user accepted" state.
    // OTA_UPDATING means a previous call is already mid-download; bail out.
    if (otaState != OTA_USER_ACCEPTED)
    {
        return;
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        // No WiFi at the moment user pressed RED — mark failed so the user
        // sees a clear state instead of the call silently being dropped.
        otaState = OTA_FAILED;
        showStartScreenIfIdle();
        Serial.println("OTA: WiFi not connected, aborting update");
        return;
    }

    // Busy is re-checked HERE, not only where the user pressed Update. The web handler
    // checks at click time and latches OTA_USER_ACCEPTED; NetworkTask can act on it
    // minutes later, by which time the user may have walked to the machine and started a
    // run. Downloading then hijacks the display and ends in a restart mid-run = lost
    // sample. Park in OTA_FAILED (visible state, no silent retry) instead.
    if (dashboardDeviceBusy())
    {
        otaState = OTA_FAILED;
        Serial.println("OTA: device became busy after the update was accepted, aborting");
        return;
    }

    otaState = OTA_UPDATING; // claim the slot so next tick won't re-enter
    _displayCLD.waittingUpdate();
    WiFiClientSecure client;
    client.setInsecure();
    // The library would ESP.restart() inside update() on success (HTTPUpdate.cpp:353),
    // which is exactly the call this function has to gate on "is a run in progress".
    httpUpdate.rebootOnUpdate(false);
    t_httpUpdate_return ret = httpUpdate.update(client, fwUrl);

    // GO/NO-GO number for the whole fleet-upgrade plan, printed on every real update (the
    // 10 s [stack] census in main.cpp cannot catch it - the reboot lands 800 ms after this).
    // NetworkTask's stack was cut 8192 -> 6144 while this call has to hold mbedTLS +
    // HTTPClient + Update at once, and an overflow is a PANIC, not HTTP_UPDATE_FAILED, so
    // the margin must be measured rather than assumed. NULL = the calling task = this one.
    Serial.printf("[ota] NetworkTask stack headroom after update(): %u B free of 6144\n",
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));

    switch (ret)
    {
    case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        // The image is already staged in the other OTA partition, so the reboot can wait
        // for idle - dashboardLoop() (same task) does it. A run that started during the
        // ~2 minute download keeps its sample; the new firmware activates at the next
        // idle moment. Restarting straight from here would throw that away.
        dashboardRequestRestart();
        // Close the accept latch. Nothing else clears it on this path, and while the reboot
        // is deferred NetworkTask keeps calling updateFirmware() every 10 ms: a RED press
        // that landed DURING the ~2 minute download (physical button, or the dashboard chip,
        // which fillActions() labels "Update" the whole time) would otherwise start the
        // entire download again and push the reboot out by another two minutes - repeatable
        // forever, re-erasing the partition esp_ota_set_boot_partition already points at.
        otaState = OTA_IDLE;
        // waittingUpdate() painted "Waiting..." over whatever the machine was showing and
        // never touched type_infor. On the on-device path that is still eUpdateOTA, so a bare
        // repaint would redraw "You have a new update! Press red button" right after doing
        // exactly that - and a press there re-enters the same branch.
        //
        // changeScreen is set ONLY inside this branch. It is not a repaint flag: for several
        // states the switch in displayLCD.cpp re-runs real work. On escreenFinished it calls
        // screen_Result('f'), which is the whole end-of-run pipeline - EEPROM read, CSV dump
        // and postData_GoogleSheet() - so re-arming it there uploads the same assay to GAS +
        // ingest + ERP a SECOND time. escreenReview would re-read EEPROM for ~8 s. The web
        // path therefore keeps showing "Waiting..." until the operator presses a button; that
        // is the honest cost of not re-running a pipeline behind their back.
        if (_displayCLD.type_infor == eUpdateOTA)
        {
            _displayCLD.type_infor = escreenStart;
            _displayCLD.changeScreen = true;
        }
        return;

    case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                      httpUpdate.getLastError(),
                      httpUpdate.getLastErrorString().c_str());
        break;
    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

    default:
        Serial.printf("HTTP_UPDATE_? unexpected ret=%d\n", (int)ret);
        break;
    }

    // Failure path: don't auto-restart (the previous behaviour caused an
    // endless reboot loop when checkFirmware succeeded but the .bin download
    // kept failing). Park in OTA_FAILED and return user to the start screen
    // so the device stays usable.
    otaState = OTA_FAILED;
    showStartScreenIfIdle();
}
