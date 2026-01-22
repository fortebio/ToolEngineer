#include "updateOTA.h"

int currentVersion = 11;
int fwVersion = 0;
bool flag_check_Update = false;
bool flagUpdate = false;
String fwUrl = "", fwName = "", fwVer = "", fwCont = "";
String baseUrl = "https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/" + FirmwareVer + "/";
String checkFile = "updateOTA.json";

void checkFirmware()
{
    if (WiFi.status() == WL_CONNECTED)
    {
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
                flag_check_Update = true;
                //return true;
            }
            else
            {
                info_displayln("You have the lasted version");
                flag_check_Update = false;
                //return false;
            }
        }
        http.end();
    }
}

void updateFirmware(void)
{
    if ((WiFi.status() == WL_CONNECTED) && (flagUpdate == true))
    {
        _displayCLD.waittingUpdate();
        WiFiClientSecure client;
        client.setInsecure();
        t_httpUpdate_return ret = httpUpdate.update(client, fwUrl);

        switch (ret)
        {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            break;
        
        default:
            break;
        }
        ESP.restart();
        //rebootEspWithReason("OTA done!");
    }
}