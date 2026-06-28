#ifndef _DISPLAY_LCD_H
#define _DISPLAY_LCD_H
#include "Arduino.h"
#include "define.h"
// #include "ForteSetting.h"
//  #include "U8g2lib.h"
#include "Arduino_GFX_Library.h"
// #include "bluetooth.h"

// Declared here in addition to define.h to handle circular includes
// (define.h -> updateOTA.h -> displayCLD.h, before extern reaches the bottom of define.h).
extern SemaphoreHandle_t gSPIMutex;

// RAII guard for the TFT SPI bus.
// Uses gSPIMutex (recursive) so nested calls (e.g. loop() -> screen_Start())
// don't deadlock. Lock auto-releases when the guard goes out of scope.
class SPILock
{
public:
    SPILock(TickType_t timeout = pdMS_TO_TICKS(200))
    {
        taken = (gSPIMutex != NULL) &&
                (xSemaphoreTakeRecursive(gSPIMutex, timeout) == pdTRUE);
    }
    ~SPILock()
    {
        if (taken)
            xSemaphoreGiveRecursive(gSPIMutex);
    }
    bool acquired() const { return taken; }

private:
    bool taken;
};

// typedef enum
// {
//     VietNamese,
//     English,
//     Null
// } language_pointer;

typedef enum
{
    escreenStart, // display start screen
    ewaitingReadsensor,
    // echoosetube,        //not use in the new design
    epreheating80,
    eheathotlid1,
    ewaitLysisTube,
    eheatLysis,
    ewaitphase2,
    eheating67, // this include the 2 heating blocks and 2 hot lids
    ewaitampTube,
    eoptoreading,
    // eshowresult,
    // eincreaseto80,  //
    eprepare,
    escreenResult,
    escreenErrorResult,
    escreenFinished,
    escreenReview,
    errprocess, // error process display, for button to check err status
    // eErrResart,          //restart after the button is pressed
    escreenRestart,  // used for restart display
    ebuttonrestart,  // when press white button to restart
    ewaitingtimeout, // wait above display to be finished
    // escreenAverageResult,
    // ecalibSensor,
    // e_setting,
    // e_connect_bluetooth,
    // e_language,
    // logdata
    eSettingMenu,
    eUpLoadData,
    eSettingBluetooth,
    eSettingWifi,

    eSelectAmpli,
    eSelectMode,
    eSelectSlot,  // display select slot calib
    eCalibrating, // display calib
    eWaitingCalib,
    eCalibComplete,
    eSetPowerLed,
    eSavePowerLed,
    eSaveCalib, // display set power led
    eUpdateOTA,
    epreheat67,         // wait for the preheat to be finished
    ecalibPreheatStart, // calib flow p0: prompt to press red to start preheat 55C
    ecalibPreheating,   // calib flow p0: heating heater2,3 to 55C
    ecalibSelect,       // calib flow p1: choose Calib (blue) or Amplification (red)
} e_statuslcd;

class displayCLD
{
private:
    unsigned long timeRefresh = 0;
    unsigned long timer10minEnd = 0;
    unsigned long timer30minEnd = 0;

public:
    /* data */
    Arduino_ESP32SPI *bus;
    Arduino_GFX *display; //   1

    displayCLD(/* args */);
    ~displayCLD();
    void begin();
    void loop();
    void rerun();

    // Re-initialise the ILI9341 panel (resets via TFT_RESET pin + re-sends
    // init commands) and force a full redraw on the next loop() iteration.
    // Safe to call from any task; takes gSPIMutex internally.
    void reinit();

    // Set this from any task to request a panel re-init at the next
    // DisplayTask loop() iteration. Cheaper than calling reinit() directly
    // because it doesn't block the caller waiting for the SPI mutex.
    volatile bool requestReinit = false;

    void logoFortebiotech();
    void screen_Start();
    // void screen_Complete();
    void screen_Result(char key);
    void screen_errorResult(void);
    // void screen_Average_Result();
    // void waiting_Readsensor();

    void ErrorDisplay(String strDescript);
    void ErrorProcess(String strDescript, String strValue); // strDescript is what happened, strValue is the value that can be displayed
    void ErrorProcessatBegin(String strDescript, String strValue);
    bool ErrorStatus();  // if it's error status, then return true
    bool FinishStatus(); // if the process is finished, then return true
    void TemperatureBottomSeqDisplay();
    void TemperatureTopSeqDisplay();
    void NextTestDisplay();  // show the reboot msg for the user to know the status
    void ErrRebootDisplay(); // show the reboot msg after error happened
    void RestartProcess(String strDescript, String strValue);

    void drawHeat67Header(const char *line1, const char *line2); // shared header renderer for the two 67C screens
    void Heat67LCD_Header();                                     // display inf
    void Heat67LCD();                                            // heat to 67 degree
    void Preheat67LCD_Header();                                  // display inf
    void Preheat67LCD();                                         // heat to 67 degree

    void calibPreheatStartLCD(); // calib p0: prompt press red to preheat 55C
    void calibPreheatingLCD();   // calib p0: heating heater2,3 to 55C
    void calibSelectLCD();       // calib p1: choose Calib or Amplification

    void preHeat80CLD_Header(); // display inf
    void preHeat80CLD();        // heat to 80 degree

    void waitLysisTube();
    void waitLysis10min();

    void startHeating10mins(); // activate the 10mins timers, activated after button pressing

    void waitBtnStartPhase2();

    void waitAmpTube();

    void startAmplification();
    void waitAmplification30min();

    void prepare();

    /**
     * @brief Setting Menu
     */
    void setting_Menu(void);
    void setting_Language(void);
    void setting_Bluetooth(void);
    void setting_Wifi(void);
    /*    void screen_Calib();
        void waiting_Calib();
        void screen_Calib_Complete();
        void log_data();
        void set_language();
        void setting();
    */

    /* Calibration */
    void display_Select_menu_calib(void);
    void display_Select_mode(void);
    void display_Select_slot(void);
    void display_Calib(void);
    void display_Waiting_Calib(void);
    void display_Calib_Complete(void);
    void display_Set_powerled(void);
    void set_flag_calib(void);

    void calculate(void);
    void saving_calib(void);

    void display_UpdateOTA(void);
    void waittingUpdate(void);

    int slot = 0;
    bool flag_calib_done = false;
    uint8_t index = 0;
    uint8_t led_power[3] = {0};

    void set_connect_bluetooth();
    //  e_statuslcd type_infor = escreenStart;
    e_statuslcd type_infor = escreenStart; // e_language;    //start directly
    int couter = 0;
    int instantStatus[2];
    bool changeScreen = true;
    bool temperatureShow = false;
    bool bheadershow = false; // if there is header needed to show static, then only write once without refreshing every time
    // language_pointer language_state = Null; // 0: Vietnamese 1: English
    int language = 1; // 0:VietNamese 1: English //change default as English
    volatile int step = 1;
};
extern displayCLD _displayCLD;

#endif
