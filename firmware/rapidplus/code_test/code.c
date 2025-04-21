void postData_GoogleSheet(void)
{
  info_displayln("Read data Amplifications from EEPROM");
  JsonDocument dataPostGoogleSheet;
  String jsonPost = "";

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    dataPostGoogleSheet["id_device"] = id_device;
    dataPostGoogleSheet["version"] = FirmwareVer;

    serializeJson(dataPostGoogleSheet, jsonPost);
    Serial.println("Post data: " + jsonPost);
    int httpResponseCode = http.POST(jsonPost);

    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println("Response: " + response);
      Serial.println("Data posted successfully!");
    }
    else
    {
      Serial.println("Error on sending POST: " + String(httpResponseCode));
    }
    http.end();
  }
  else
  {
    Serial.println("WiFi disconnected!");
  }
  // esp_restart();
}