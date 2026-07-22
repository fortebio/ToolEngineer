#include "updateOTA.h"

int currentVersion = 18;
int fwVersion = 0;
volatile OtaState otaState = OTA_IDLE;
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
void checkFirmware()
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
            _displayCLD.type_infor = eUpdateOTA;
            _displayCLD.changeScreen = true;
        }
        else
        {
            info_displayln("You have the lasted version");
            otaState = OTA_IDLE;
        }
    }
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
        _displayCLD.type_infor = escreenStart;
        _displayCLD.changeScreen = true;
        Serial.println("OTA: WiFi not connected, aborting update");
        return;
    }

    otaState = OTA_UPDATING; // claim the slot so next tick won't re-enter
    _displayCLD.waittingUpdate();
    WiFiClientSecure client;
    client.setInsecure();
    t_httpUpdate_return ret = httpUpdate.update(client, fwUrl);

    switch (ret)
    {
    case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        ESP.restart(); // boot into new firmware — only path that restarts
        return;        // unreachable, but keep flow explicit

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
    _displayCLD.type_infor = escreenStart;
    _displayCLD.changeScreen = true;
}
