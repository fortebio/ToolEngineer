/*
Version 1.3 note: add function send wifi id and password via bluetooth
Version 1.4 note: add function sellect language
*/

#include "define.h"
#include "displayCLD.h"
#include "button.h"
#include "Bluetooth.h"
#include "thermometer.h"
#include "Wire.h"
#include "sensor6035.h"
#include "PIDControl.h"
#include "ForteSetting.h"
#include "Fan.h"
#include "errorCheck.h"

buttonManager _buttonManager;

WebServer server(80);

/****** RTOS Handles ******/
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t controlTaskHandle = NULL;
TaskHandle_t inputTaskHandle = NULL;
TaskHandle_t settingTaskHandle = NULL;

/***** Global Mutex ******/
SemaphoreHandle_t gI2CMutex = NULL;
SemaphoreHandle_t gSPIMutex = NULL;

/***** DisplayTask ******/
void DisplayTask(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1)
  {
    _displayCLD.loop();

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
  }
  
}

/***** NetworkTask ******/
void NetworkTask(void *pvParameters)
{
    while (1)
    {
        //server.handleClient();

        updateFirmware();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/***** SensorTask *****/
void SensorTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        if (xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            _sensor6035.loop();
            xSemaphoreGive(gI2CMutex);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

/***** ControlTask *****/
void ControlTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        _PIDControl.loop();
        _Fan.loop(); // keep the Fan on (trivial GPIO write, folded in here)

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

/***** InputTask *****/
// Button polling + buzzer timing. Both need fast, regular servicing.
// Button handlers drive the TFT heavily, but every display call takes the
// recursive gSPIMutex internally (SPILock), so running concurrently with
// DisplayTask is safe.
void InputTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        _buttonManager.loop();
        _buzzer.loop();

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5));
    }
}

/***** SettingTask *****/
// Serial JSON config receiver. Isolated because its loop() busy-waits up to
// ~30ms while draining the serial buffer; keeping it off InputTask avoids
// delaying button response.
void SettingTask(void *pvParameters)
{
    while (1)
    {
        _ForteSetting.loop();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup()
{
  Serial.setRxBufferSize(3 * 1024);
  Serial.begin(115200);

  // Log reset reason so we can correlate white-screen reports with brownout/WDT.
  // If the previous boot ended abnormally, schedule an extra display reinit
  // after the normal begin() so the panel is forced out of any stuck state.
  //esp_reset_reason_t resetReason = esp_reset_reason();
  //static const char *const resetNames[] = {
  //    "UNKNOWN", "POWERON", "EXT", "SW", "PANIC",
  //    "INT_WDT", "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO"};
  //Serial.printf("Boot reason: %s (%d)\n",
  //              (resetReason >= 0 && resetReason <= 10) ? resetNames[resetReason] : "?",
  //              (int)resetReason);
  //bool abnormalBoot = (resetReason == ESP_RST_PANIC ||
  //                     resetReason == ESP_RST_INT_WDT ||
  //                     resetReason == ESP_RST_TASK_WDT ||
  //                     resetReason == ESP_RST_WDT ||
  //                     resetReason == ESP_RST_BROWNOUT);

  // configure the I2C IO
  Wire.begin(SDA_Forte, SCL_Forte);

/***** Create Mutex *****/
  gI2CMutex = xSemaphoreCreateMutex();

  if (gI2CMutex == NULL)
    {
      Serial.println("Create I2C Mutex Failed");

      while (1)
      {
        delay(1000);
      }
    }

  gSPIMutex = xSemaphoreCreateRecursiveMutex();

  if (gSPIMutex == NULL)
  {
    Serial.println("Create SPI Mutex Failed");
    while (1) { delay(1000); }
  }

  // configure the button
  _buttonManager.buttonStart();

  // load ssid, password, id_device id from EEPROM
  loadSettingDevice();
  error.readErrorFromEEPROM(); // read error record from EEPROM, used for error process
  // error.printAllError();       // print error record to Serial Monitor, used for error process

  WiFi.begin(ssid.c_str(), password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20)
  {
    delay(50); // đợi 50ms mỗi lần
    retries++;
    Serial.print(".");
  }
  delay(100);

  _displayCLD.begin();
  // Second init pass after abnormal boot — protects against ILI9341 ending
  // up in a half-initialised state after brownout/WDT reset.
  //if (abnormalBoot)
  //{
  //  delay(50);
  //  _displayCLD.reinit();
  //}
  _ForteSetting.begin();
  _PIDControl.begin();
  _displayCLD.logoFortebiotech();

  /***** Sensor Init *****/
    if (xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
        _sensor6035.begin();

        xSemaphoreGive(gI2CMutex);
    }

  _Fan.begin();
  _PIDControl.timeoutSetting();

  //postData_Chart();
  checkFirmware();
  Serial.printf("Error slot 7: %d\n", error.searchError(errorLightSensor, errorNoData, eSensor1stReading, 6)); // test error search function
  //error.printAllError();                                                                                       // test print all error function

  /****** Create RTOS Tasks *****/

    xTaskCreatePinnedToCore(
        ControlTask,
        "ControlTask",
        8192,
        NULL,
        5,
        &controlTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        SensorTask,
        "SensorTask",
        16384,
        NULL,
        2,
        &sensorTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        DisplayTask,
        "DisplayTask",
        16384,
        NULL,
        2,
        &displayTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        NetworkTask,
        "NetworkTask",
        8192,
        NULL,
        1,
        &networkTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        InputTask,
        "InputTask",
        8192,
        NULL,
        3,
        &inputTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        SettingTask,
        "SettingTask",
        8192,
        NULL,
        1,
        &settingTaskHandle,
        0);
}

void loop()
{
  // All work has been moved into dedicated RTOS tasks created in setup():
  //   - ControlTask : _PIDControl.loop() + _Fan.loop()
  //   - SensorTask  : _sensor6035.loop()
  //   - DisplayTask : _displayCLD.loop()
  //   - InputTask   : _buttonManager.loop() + _buzzer.loop()
  //   - SettingTask : _ForteSetting.loop()
  //   - NetworkTask : updateFirmware()
  // Nothing left to do on the Arduino loop task; just idle.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
