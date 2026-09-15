#include "errorCheck.h"
#include "Bluetooth.h"
#include <ArduinoJson.h>
#include "ForteSetting.h"
#include "webDashboard.h" // dashboardSuspend/Resume around the TLS these posts block in

ErrorCheck error; // global variable to record the error type and times, used for opto sensor reading error process

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
        String jsonPost = "";
        {
            JsonDocument dataPostGoogleSheet;
            dataPostGoogleSheet["method"] = "error";
            dataPostGoogleSheet["id_device"] = _ForteSetting.parameter.device_id;
            dataPostGoogleSheet["version"] = FirmwareVer;

            JsonArray error_array = dataPostGoogleSheet["error"].to<JsonArray>();
            char tmp[10];
            sprintf(tmp, "%04d", e.EncodeError(e.error[0]));
            JsonObject errorObj = error_array.add<JsonObject>();
            errorObj["Slot"] = errorSlotStr[e.error[0].errorModule][e.error[0].errorSlot];
            errorObj["error_code"] = String(tmp);
            errorObj["error_msg"] = e.decodeError(e.error[0]);
            serializeJson(dataPostGoogleSheet, jsonPost);
        } // <- JsonDocument released before the TLS handshake asks for its contiguous block

        // All three destinations, not just GAS - see postJsonToAllTargets.
        dashboardSuspend();
        postJsonToAllTargets(jsonPost, "error");
        dashboardResume();
        e.clear(); // clear the error record after posting, used for error process
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
        String jsonPost = "";
        {
            JsonDocument dataPostGoogleSheet;
            dataPostGoogleSheet["method"] = "error";
            dataPostGoogleSheet["id_device"] = _ForteSetting.parameter.device_id;
            dataPostGoogleSheet["version"] = FirmwareVer;

            JsonArray error_array = dataPostGoogleSheet["error"].to<JsonArray>();
            for (size_t i = 0; i < error.numUnit; i++)
            {
                char tmp[10];
                sprintf(tmp, "%04d", error.EncodeError(error.error[i]));
                JsonObject errorObj = error_array.add<JsonObject>();
                errorObj["Slot"] = errorSlotStr[error.error[i].errorModule][error.error[i].errorSlot];
                errorObj["error_code"] = String(tmp);
                errorObj["error_msg"] = error.decodeError(error.error[i]);
            }
            serializeJson(dataPostGoogleSheet, jsonPost);
        } // <- JsonDocument released before the TLS handshake asks for its contiguous block

        // GAS + ingest + ERP, the same three the results go to. This used to post to GAS only,
        // through a bare HTTPClient with no WiFiClientSecure - so the two systems anyone
        // watches never heard about a failed channel.
        //
        // Suspended for the same reason postData_GoogleSheet suspends: this runs at the end of
        // a run, on DisplayTask, and it blocks in mbedTLS for seconds per destination. It is
        // called AFTER postData_GoogleSheet has already resumed the dashboard, so without this
        // the TLS here would run with SSE pushing (GOTCHA 2 heap, GOTCHA 11 async_tcp).
        // suspend/resume is a plain flag, so nesting or repeating it is harmless.
        dashboardSuspend();
        postJsonToAllTargets(jsonPost, "error");
        dashboardResume();
    }
    error.saveErrorToEEPROM(); // save error record to EEPROM, used for error process when WiFi is disconnected, and post it to Google Sheet when WiFi is connected in the future
}