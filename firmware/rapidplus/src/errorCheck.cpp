#include "errorCheck.h"
#include "Bluetooth.h"
#include <ArduinoJson.h>

ErrorCheck error; // global variable to record the error type and times, used for opto sensor reading error process
void postError_Googlesheet(uint8_t errorModule, uint8_t errorType, uint8_t errorProcessStep, uint8_t errorSlot);
void postError_fullGoogleSheet(void);

/***********************************************************************
 * Function: postError_Googlesheet()
 * Description: Reports a single error event to the Google Sheet web app. If
 *  WiFi is connected it builds a JSON payload (method "error", device id,
 *  firmware version and one error entry with slot string, 4-digit encoded
 *  error code and decoded message) and HTTP POSTs it, then clears the local
 *  error record. If WiFi is disconnected it appends the error to the global
 *  error object instead. In all cases it saves the error log to EEPROM for
 *  later posting.
 * pramameter: errorModule - module/board the error occurred on
 * pramameter: errorType - type/category of the error
 * pramameter: errorProcessStep - process step at which the error occurred
 * pramameter: errorSlot - slot index associated with the error
 *  return: none
 */
void postError_Googlesheet(uint8_t errorModule, uint8_t errorType, uint8_t errorProcessStep, uint8_t errorSlot)
{
    ErrorCheck e;
    if (WiFi.status() == WL_CONNECTED)
    {
        e.addError(errorModule, errorType, errorProcessStep, errorSlot); // add error record to the error variable, used for error process
        JsonDocument dataPostGoogleSheet;
        String jsonPost = "";

        HTTPClient http;
        http.begin(serverName);
        http.addHeader("Content-Type", "application/json");

        dataPostGoogleSheet["method"] = "error";
        dataPostGoogleSheet["id_device"] = id_device;
        dataPostGoogleSheet["version"] = FirmwareVer;

        JsonArray error_array = dataPostGoogleSheet.createNestedArray("error");
        char tmp[10];
        sprintf(tmp, "%04d", e.EncodeError(e.error[0]));
        JsonObject errorObj = error_array.createNestedObject();
        errorObj["Slot"] = errorSlotStr[e.error[0].errorModule][e.error[0].errorSlot];
        errorObj["error_code"] = String(tmp);
        errorObj["error_msg"] = e.decodeError(e.error[0]);
        serializeJson(dataPostGoogleSheet, jsonPost);
        int httpResponseCode = http.POST(jsonPost);
        e.clear(); // clear the error record after posting to Google Sheet, used for error process
    }
    else if (WiFi.status() == WL_DISCONNECTED)
    {
        error.addError(errorModule, errorType, errorProcessStep, errorSlot); // add error record to the error variable, used for error process
    }
    error.saveErrorToEEPROM(); // save error record to EEPROM, used for error process when WiFi is disconnected, and post it to Google Sheet when WiFi is connected in the future
}

/***********************************************************************
 * Function: postError_fullGoogleSheet()
 * Description: Flushes the entire accumulated error log to the Google Sheet
 *  web app when WiFi is connected, iterating over all error.numUnit records
 *  and building a JSON payload (method "error", device id, firmware version
 *  and one entry per error with slot string, 4-digit encoded code and
 *  decoded message), then HTTP POSTs it. Always saves the error log back to
 *  EEPROM afterwards.
 * pramameter: none
 *  return: none
 */
void postError_fullGoogleSheet(void)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        JsonDocument dataPostGoogleSheet;
        String jsonPost = "";

        HTTPClient http;
        http.begin(serverName);
        http.addHeader("Content-Type", "application/json");

        dataPostGoogleSheet["method"] = "error";
        dataPostGoogleSheet["id_device"] = id_device;
        dataPostGoogleSheet["version"] = FirmwareVer;

        JsonArray error_array = dataPostGoogleSheet.createNestedArray("error");
        for (size_t i = 0; i < error.numUnit; i++)
        {
            char tmp[10];
            sprintf(tmp, "%04d", error.EncodeError(error.error[i]));
            JsonObject errorObj = error_array.createNestedObject();
            errorObj["Slot"] = errorSlotStr[error.error[i].errorModule][error.error[i].errorSlot];
            errorObj["error_code"] = String(tmp);
            errorObj["error_msg"] = error.decodeError(error.error[i]);
        }
        serializeJson(dataPostGoogleSheet, jsonPost);
        int httpResponseCode = http.POST(jsonPost);
    }
    error.saveErrorToEEPROM(); // save error record to EEPROM, used for error process when WiFi is disconnected, and post it to Google Sheet when WiFi is connected in the future
}