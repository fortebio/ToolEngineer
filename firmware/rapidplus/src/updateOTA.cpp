#include "updateOTA.h"
#include "webDashboard.h" // dashboardDeviceBusy() / dashboardRequestRestart(): never
                          // hijack the display or reboot into a run in progress
#include "secrets.h"      // SECRET_OTA_CHECK_URL + SECRET_INGEST_TOKEN
#include "ForteSetting.h" // protoID: the device ID sent as ?device=
#include <Preferences.h>  // the "I just installed an update" latch - see otaMarkInstalled()

volatile OtaState otaState = OTA_IDLE;
volatile uint32_t otaLastCheck = 0;   // millis() of last completed check (0 = never)
volatile bool otaCheckFailed = false; // last completed check errored (non-200)
String fwUrl = "", fwVer = "";

// Every OTA request carries the SAME Bearer the run results already POST with
// (Bluetooth.cpp postJsonToAllTargets). Deliberately not a second credential: a machine
// that can upload results is a machine the server already trusts, and a per-device OTA
// token has no way to reach the 109 units that would need it.
// Bounded so boot cannot stall: checkFirmware() runs inside setup() and now costs up to TWO
// attempts, and the library defaults let a black-holed host sit in the handshake far longer
// than anyone waiting in front of the machine will tolerate.
static const uint16_t OTA_HTTP_TIMEOUT_MS = 8000;

static void addBearer(HTTPClient *http)
{
    http->addHeader("Authorization", "Bearer " SECRET_INGEST_TOKEN);
}

// Filter down to query-safe characters instead of escaping. Both strings that go through
// here - the machine ID ("RPL03003") and FirmwareVer ("v2.4.5") - are short and known in
// shape; one stray space or '&' malforms the request with nothing reporting it. The server
// whitelists the same set (main.py _ID_OK), so both ends agree on what survives.
static String urlSafe(const char *s)
{
    String out;
    for (const char *p = s; *p; p++)
        if (isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-')
            out += *p;
    return out;
}

// ---------------------------------------------------------------------------
// "I just installed an update" latch
//
// The server builds the update history by watching ?ver= change between polls. That infers
// well but cannot see two things: a re-flash of the SAME version, and the difference between
// "this machine rebooted" and "this machine finished an update". Both matter to whoever is
// running a fleet upgrade, so the machine states it outright instead of leaving it to be
// guessed - the same reason ?ver= exists at all.
//
// NVS, not RTC memory: the reboot after an install is DEFERRED (dashboardRequestRestart
// waits for idle), so the mains can be cut in between and RTC RAM would lose the fact that
// an install ever happened, precisely on the machine whose update someone is watching for.
// One flash write per update is nothing next to the ~2 MB image that was just written.
static const char *OTA_NVS_NS = "otaflag";
static const char *OTA_NVS_KEY = "installed";

static void otaMarkInstalled()
{
    Preferences p;
    if (!p.begin(OTA_NVS_NS, false))
        return; // never let a bookkeeping failure touch the update path itself
    p.putBool(OTA_NVS_KEY, true);
    p.end();
}

static bool otaInstalledPending()
{
    Preferences p;
    if (!p.begin(OTA_NVS_NS, true))
        return false;
    bool v = p.getBool(OTA_NVS_KEY, false);
    p.end();
    return v;
}

// Cleared ONLY after a host has actually answered the check, so a boot with no network (or
// a dead front door) reports the install on the next poll instead of dropping it. Reporting
// twice is impossible: the first success clears the latch.
static void otaClearInstalled()
{
    Preferences p;
    if (!p.begin(OTA_NVS_NS, false))
        return;
    p.remove(OTA_NVS_KEY);
    p.end();
}

// One /ota/check attempt against one host. Returns true only when this host gave a REAL,
// PARSEABLE answer - anything else returns false so the caller falls through to the next
// front door. That distinction is the whole point of having two hosts: a Cloudflare Access
// login page, a WAF interstitial or a captive portal all answer 200 with HTML, and treating
// that as "no update" would report success and never contact the safety net.
static bool otaCheckHost(const char *base, const String &q, bool promptOnDevice)
{
    // Fresh WiFiClientSecure per attempt. Reusing one across hosts leaves a half-closed
    // socket behind - the same trap postJsonRetry() documents at Bluetooth.cpp:474, and it
    // is worse here because the second host is the LAST resort.
    WiFiClientSecure client;
    client.setInsecure(); // no CA pinned anywhere in this firmware (Bluetooth.cpp:414 too),
                          // which is exactly why moving from ISRG Root X1 (Funnel) to Google
                          // Trust Services (Cloudflare) cannot break TLS on the fleet.
    // Bounded, because this runs inside setup() at boot and now costs up to TWO attempts.
    // On the library defaults a dead host can sit in the TLS handshake for a very long time,
    // and every second of it is a second the machine shows nothing and does nothing.
    client.setHandshakeTimeout(OTA_HTTP_TIMEOUT_MS / 1000);
    HTTPClient http;
    http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // ?ver= REPORTS state, it does not ask anything. It rides the check the machine already
    // makes (at boot - i.e. right after an update installs - then every 6 h), so the fleet
    // costs no extra request and a machine that never updates still declares what it runs.
    // Before this the server had to infer the version from sessions.version, i.e. only after
    // somebody happened to run the next sample.
    // &updated=1 says "the version I am reporting arrived via an install I just completed",
    // which is the one fact ?ver= alone cannot carry (see otaMarkInstalled). The server logs
    // that as an update event unconditionally - including a re-flash of the same version,
    // which a version-change watcher is blind to by construction.
    bool fresh = otaInstalledPending();
    http.begin(client, String(base) + "?device=" + q + "&ver=" + urlSafe(FirmwareVer.c_str()) +
                           (fresh ? "&updated=1" : ""));
    addBearer(&http);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        // 401 = token rotated without a re-flash. 530/1033 = Cloudflare tunnel down.
        Serial.printf("[ota] check %s -> HTTP %d\n", base, httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    info_displayln(payload);

    // A 200 is NOT an answer until it parses. Discarding this return value was a real hole:
    // an HTML login page deserialises to nothing, json["update"] reads false, and the code
    // below would have said "up to date" AND returned true - silencing the fallback host at
    // exactly the moment (an edge/auth misconfiguration) it exists for. is<bool>() is checked
    // too, because a truncated body can parse into a partial object where the key is absent.
    JsonDocument json;
    if (deserializeJson(json, payload) != DeserializationError::Ok || !json["update"].is<bool>())
    {
        Serial.printf("[ota] check %s -> 200 but body is not the /ota/check contract\n", base);
        return false;
    }

    // The host answered the contract, so the &updated=1 that rode along has been recorded.
    // Cleared HERE and not earlier: a 401, a Cloudflare interstitial or a dead tunnel all
    // return above, and dropping the latch on those would lose the install report for good.
    // Placed before the in-flight branch below so every path that returns true clears it.
    if (fresh)
    {
        otaClearInstalled();
        Serial.println("[ota] install reported to server");
    }

    // Parse into LOCALS. fwUrl is handed to httpUpdate.update() as a const String& and is read
    // for the whole ~2 minute download, so reassigning the global from this task while
    // NetworkTask is streaming would free the buffer under a live reader. Nothing is published
    // until the state check below says no install is in flight.
    String name = json["version"].is<const char *>() ? json["version"].as<String>() : String();
    String url = json["url"].is<const char *>() ? json["url"].as<String>() : String();

    // A check that completes DURING a download must not demote the state: otaState ==
    // OTA_UPDATING is the only thing making dashboardDeviceBusy() true for those ~2 minutes,
    // and clearing it re-opens POST /wifi + the deferred-restart gate onto a live flash write.
    // OTA_USER_ACCEPTED is latched by the web click and consumed by NetworkTask within 10 ms.
    OtaState st = otaState;
    if (st == OTA_UPDATING || st == OTA_USER_ACCEPTED)
    {
        Serial.println("[ota] check answered while an install is in flight - result discarded");
        return true; // the host DID answer; just nothing to do with it
    }

    fwVer = name;
    fwUrl = url;
    // EXACT file name, not a substring. The server stores files, not versions, and the app
    // builds every upload name as "fbt_v<version>.bin" (manager_machine_screen.dart
    // otaFileNameFor), so the name this build must see is exactly one string.
    //
    // The earlier fwVer.indexOf(FirmwareVer) < 0 test looked equivalent and was not: any name
    // that EXTENDS the running version - "fbt_v2.4.4_rc1.bin", "fbt_v2.4.4AT.bin", and the
    // app's own upload dialog suggests exactly those - contains it, so the fleet would decline
    // the fix SILENTLY and forever. Exact comparison fails the other way instead: a misnamed
    // image re-prompts visibly, and a visible wrong beats an invisible one on 109 machines.
    if (name.length() && url.length() && name != "fbt_" + FirmwareVer + ".bin")
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
    return true;
}

/***********************************************************************
 * Function: checkFirmware()
 * Description: Asks the server (GET /ota/check, Bearer) whether an admin has selected a
 *  firmware image for this fleet. Returns immediately if WiFi is not connected.
 *
 *  TWO front doors, tried in order (Cloudflare, then the Tailscale Funnel). OTA is the
 *  only way to reach a fielded machine, so knowing a single host would turn one outage
 *  into 109 units nobody can fix. The first host to answer 200 wins; the choice is not
 *  remembered anywhere.
 *
 *  The server answers {update,version,size,sha256,url} where `version` is the UPLOADED
 *  FILE NAME - the server has no idea what a version means, so the comparison is ours, and
 *  it is EXACT: this build updates for any name that is not literally
 *  "fbt_" + FirmwareVer + ".bin". The naming convention is therefore load-bearing, not
 *  cosmetic, and it is what stops the prompt from coming back after the install (the new
 *  build's FirmwareVer rebuilds the same string). The app already produces exactly this
 *  shape - manager_machine_screen.dart otaFileNameFor().
 *
 *  On a newer build it sets otaState to OTA_AVAILABLE and (when promptOnDevice) triggers
 *  the eUpdateOTA prompt; otherwise OTA_IDLE.
 * pramameter: promptOnDevice - also take over the TFT with the update prompt
 * return: none
 */
void checkFirmware(bool promptOnDevice)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }
    // An install already in flight owns fwUrl (httpUpdate holds it by reference for the whole
    // download) and owns otaState (the only flag keeping dashboardDeviceBusy() true). Asking
    // again here would also open a second mbedTLS session against the ~42 KB contiguous-heap
    // budget of GOTCHA 2, mid-flash-write. otaCheckHost() re-checks for the TOCTOU.
    if (otaState == OTA_UPDATING || otaState == OTA_USER_ACCEPTED)
    {
        return;
    }

    // ?device= drives per-machine pinning on the server (a pin beats the fleet-wide target),
    // and it is what makes ?ver= mean anything: the reported version is filed under this ID.
    String q = urlSafe(protoID);

    // Cloudflare first, Tailscale Funnel as the net. Stateless on purpose: the winning host
    // is not remembered, so there is no NVS write and no stale preference to go wrong. The
    // whole cost of that is one failed request per 6 h poll while the primary is down.
    static const char *const HOSTS[] = {SECRET_OTA_CHECK_URL, SECRET_OTA_CHECK_URL_FALLBACK};
    bool answered = false;
    for (size_t i = 0; i < sizeof(HOSTS) / sizeof(HOSTS[0]) && !answered; i++)
        answered = otaCheckHost(HOSTS[i], q, promptOnDevice);

    // Only FAILED when EVERY front door failed - a Cloudflare outage that the Funnel covered
    // is not something to report as a failed check. Without this the web sat on "Not checked
    // yet" forever with no hint the check had actually run.
    otaCheckFailed = !answered;
    if (!answered)
        Serial.println("[ota] check failed: no host answered");
    // Set on ANY completed round (success or not) so the web can tell "checked, up to date"
    // and "checked, failed" apart from "never checked".
    otaLastCheck = millis() ? millis() : 1;
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
    // The Bearer goes on via the request callback (HTTPUpdate.cpp:219, fired just before
    // GET) rather than a hand-written download loop. That keeps the ONE call this project
    // has actually measured NetworkTask's 6144-byte stack against - see the headroom print
    // below, which is the go/no-go gate for the whole fleet upgrade.
    //
    // Integrity: HTTPUpdate collects the `x-MD5` response header and feeds it to
    // Update.setMD5() (HTTPUpdate.cpp:223,344), and the server now sends it. So the image
    // is verified end-to-end without a byte of hashing code here.
    // ponytail: md5-from-the-same-server catches transport/storage corruption, NOT a bad
    // upload (server would hash the truncated file and agree with itself). Close that at
    // the upload end - have the app verify /ota/check's sha256 after PUT - not here.
    t_httpUpdate_return ret = httpUpdate.update(client, fwUrl, "", addBearer);

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
        // Latch the fact BEFORE the reboot is even requested. The new image is already staged
        // in the other partition at this point, so from here on the machine WILL come up on
        // it - by the deferred restart below, by a watchdog, or by someone pulling the plug.
        // Writing the flag later (or from the new build) would miss those.
        otaMarkInstalled();
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
