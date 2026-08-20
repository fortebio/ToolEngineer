#ifndef _FORTESETTING_H
#define _FORTESETTING_H

#include "HardwareSerial.h"
#include "buzzer.h"
#include "Fan.h"
#include "PIDControl.h"
#include "define.h"
#include <ArduinoJson.h>
#include "sensor6035.h"
#include "LED.h"


class ForteSetting
{
private:
    /* data */
    char recvData[2*1024];         //received data will store here
    uint32_t recvLen = 0;        //length of the received data
    unsigned long recvTime = 0; //time to receive the last daqta, used to check all data is received
    bool moreMsg = false;       //used for long msg receiving


    bool BuzzerConfig();
    bool FanConfig();
    bool HeaterSimuConfig();
    bool TemperatureOutput();
    bool HeaterStepSet();
    bool JsonDataConfig();
    bool ParaRead();
    bool EEPROMRead();
    bool resultOutput();
    bool restart();
    bool start_amplification_simulation();

    int paraIntSplit(char * source, int *para);

    // Read a framed command from a serial stream into recvData (shared by the Serial and
    // SerialBT paths - both are Arduino Stream). `window` = inter-byte idle timeout (ms).
    // Returns false only if the buffer overflowed and was flushed (caller should bail).
    bool readCommand(Stream &port, unsigned long window);

    // NUL-terminate + validate parameter.device_id right after it is loaded from EEPROM.
    // parameter.device_id is the ONLY device-ID store (the id_device global is gone), so this
    // is the single trust boundary for it - see the comment on the definition.
    void sanitiseDeviceId();

public:
    ForteSetting(/* args */);
    ~ForteSetting();

    void begin();
    void loop();
    void rerun();

    parastructure parameter;

    // ---- Web settings intake -------------------------------------------------
    // The web (AsyncTCP task) must NOT touch `parameter` or EEPROM: JsonDataConfig()
    // rewrites the struct other tasks read and does a ~100 ms blocking flash write.
    // These post*() only stash a string + set a flag; loop() (SettingTask) drains them
    // and does the work. Same shape as buttonManager::postShortPress -> InputTask.
    // AsyncTCP is a single task, so one producer -> a flag is enough, no mutex.
    // All EEPROM writes (parameter, WiFi creds, device id) therefore happen on ONE
    // task, which is what keeps EEPROM.begin()/end() from racing.
    enum e_pending
    {
        PEND_NONE = 0,
        PEND_CONFIG, // full/partial parameter JSON -> JsonDataConfig()
        PEND_WIFI,   // ssid + password -> saveSettingDevice() + restart
        PEND_ID,     // device id -> parameter.device_id (the one and only store)
        PEND_REVIEW,   // reload the last run from EEPROM -> recompute -> cache for the web
        PEND_OTACHECK, // ask the server whether a newer firmware exists (blocking HTTPS)
    };

    // Return false if a request is already queued (caller should answer 429/503).
    bool postConfigJson(const String &json);
    bool postWifiCreds(const String &ssid, const String &pass);
    bool postDeviceId(const String &id);
    // Re-load the last completed run from EEPROM and recompute its results, so the web
    // Result tab can review it even after a reboot (the RAM cache is gone by then).
    bool postReviewLast();
    // Run checkFirmware() off the web task: it does a blocking HTTPS GET, which must never
    // happen on AsyncTCP. Result lands in otaState / fwVer for GET /ota.
    //
    // promptOnDevice decides whether finding a build also takes over the TFT with the
    // eUpdateOTA prompt, and the two callers want OPPOSITE answers. The web button passes
    // false: someone is looking at a browser, and grabbing the screen from a remote click
    // strands whoever is standing at the machine. The 6 h poll in dashboardLoop() passes
    // TRUE - nobody is watching a browser, and the prompt IS the only place the RED button
    // means "install". Without it the poll can only ever be seen by someone who happens to
    // open the dashboard, which is not a fleet update mechanism.
    bool postOtaCheck(bool promptOnDevice = false);

    // ---- Outcome of the last web-queued request --------------------------------
    // The POST can only ACK that it QUEUED: the handler must not block waiting for
    // the apply (blocking the AsyncTCP task is what trips the task watchdog). But
    // drainPending() runs up to ~10 ms later and DROPS the request if a run started
    // in between - so "queued" is not "written". Without publishing the outcome the
    // web would report "Saved" for a write that never happened.
    // The client gets these on the 1 s `home` event: it remembers the seq its POST
    // returned and waits for that seq to leave CFG_PENDING.
    enum e_cfgState
    {
        CFG_NONE = 0, // nothing has been posted yet
        CFG_PENDING,  // queued, SettingTask has not drained it
        CFG_APPLIED,  // written to EEPROM
        CFG_BUSY,     // dropped: the device became busy before it could be applied
    };
    volatile e_cfgState cfgState = CFG_NONE;
    volatile uint32_t cfgSeq = 0; // bumped by each accepted post*()

private:
    volatile e_pending pendingKind = PEND_NONE; // written LAST by the poster
    String pendingA;                            // config json | ssid | id
    String pendingB;                            // password
    bool otaPromptOnDevice = false;             // PEND_OTACHECK payload; see postOtaCheck()
    uint32_t restartAt = 0;                     // millis() to reboot after a WiFi save

    void drainPending(); // called at the top of loop(), on SettingTask
};

extern ForteSetting _ForteSetting;


#define protoID       _ForteSetting.parameter.device_id
#define OpticalUnits  _ForteSetting.parameter.units

#endif
