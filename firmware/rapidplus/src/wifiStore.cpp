#include "wifiStore.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h> // WiFi.SSID(): mark which saved network is the one currently connected

/* Saved WiFi networks live in NVS (the Preferences library), namespace "wifinets".
 *
 * WHY NVS, NOT LittleFS (the first version's mistake):
 * `uploadfs` / `uploadall` reflash the ENTIRE LittleFS (spiffs) partition from the data/
 * folder, which has no wifi.json - so a file written at runtime there is WIPED on every UI
 * update. NVS lives in the separate `nvs` partition (0x9000), which uploadfs does not
 * touch, so the saved list now SURVIVES firmware/UI uploads. (The preferred pair in EEPROM
 * already survived for the same reason - different partition from spiffs.)
 *
 * Storage layout in the namespace: "n" = count (uint8), then "s0".."s4" / "p0".."p4" =
 * each network's ssid/password. Keys stay <=15 chars (an NVS limit). The whole list is
 * rewritten on every change - at most WIFI_STORE_MAX short records, so partial updates buy
 * nothing, and a full rewrite keeps the set self-consistent.
 *
 * Concurrency: reads run in setup()/web, writes on SettingTask (wifiStoreAdd via PEND_WIFI)
 * and AsyncTCP (wifiStoreRemove). NVS is thread-safe (its own internal lock), and unlike
 * the EEPROM library it has NO shared 4096-byte RAM buffer to double-free (CLAUDE.md
 * Setting #2) - so this is safe from any task with no extra mutex.
 */

static const char *NS = "wifinets";

// Read the whole list into `out` (ordered, index 0 = preferred). Returns the count.
static uint8_t loadRaw(WifiNet *out, uint8_t max)
{
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true))
        return 0; // namespace never created yet -> empty
    uint8_t n = p.getUChar("n", 0);
    if (n > WIFI_STORE_MAX)
        n = WIFI_STORE_MAX;
    uint8_t out_n = 0;
    char key[12]; // "s" + up to 3 digits fits in 5, but -Wformat-truncation cannot narrow %u
    for (uint8_t i = 0; i < n && out_n < max; i++)
    {
        snprintf(key, sizeof(key), "s%u", i);
        String s = p.getString(key, "");
        if (!s.length())
            continue; // a blank ssid would make WiFi.begin() hang on nothing
        snprintf(key, sizeof(key), "p%u", i);
        out[out_n].ssid = s;
        out[out_n].pass = p.getString(key, "");
        out_n++;
    }
    p.end();
    return out_n;
}

// Overwrite the whole list. Clears stale keys first so a shrink leaves nothing behind.
static bool saveRaw(const WifiNet *nets, uint8_t n)
{
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false))
    {
        Serial.println("[wifi] NVS open failed for write");
        return false;
    }
    p.clear(); // drop every key in the namespace, then rewrite
    p.putUChar("n", n);
    char key[12]; // "s" + up to 3 digits fits in 5, but -Wformat-truncation cannot narrow %u
    for (uint8_t i = 0; i < n; i++)
    {
        snprintf(key, sizeof(key), "s%u", i);
        p.putString(key, nets[i].ssid);
        snprintf(key, sizeof(key), "p%u", i);
        p.putString(key, nets[i].pass);
    }
    p.end();
    return true;
}

uint8_t wifiStoreLoad(WifiNet *out, uint8_t max)
{
    return loadRaw(out, max);
}

uint8_t wifiStoreCount()
{
    WifiNet tmp[WIFI_STORE_MAX];
    return loadRaw(tmp, WIFI_STORE_MAX);
}

bool wifiStoreGetPass(const String &ssid, String &passOut)
{
    WifiNet cur[WIFI_STORE_MAX];
    uint8_t n = loadRaw(cur, WIFI_STORE_MAX);
    for (uint8_t i = 0; i < n; i++)
    {
        if (cur[i].ssid == ssid)
        {
            passOut = cur[i].pass;
            return true;
        }
    }
    return false;
}

bool wifiStoreAdd(const String &ssid, const String &pass)
{
    if (!ssid.length() || ssid.length() > 32)
        return false;

    WifiNet cur[WIFI_STORE_MAX];
    uint8_t n = loadRaw(cur, WIFI_STORE_MAX);

    // New/updated entry first (the one the operator just chose -> tried first on boot),
    // then the rest minus any duplicate of it, capped at WIFI_STORE_MAX (oldest drops off).
    WifiNet next[WIFI_STORE_MAX];
    next[0].ssid = ssid;
    next[0].pass = pass;
    uint8_t kept = 1;
    for (uint8_t i = 0; i < n && kept < WIFI_STORE_MAX; i++)
    {
        if (cur[i].ssid == ssid)
            continue; // replaced by the head entry - no duplicates
        next[kept++] = cur[i];
    }
    return saveRaw(next, kept);
}

bool wifiStoreRemove(const String &ssid)
{
    WifiNet cur[WIFI_STORE_MAX];
    uint8_t n = loadRaw(cur, WIFI_STORE_MAX);

    WifiNet next[WIFI_STORE_MAX];
    uint8_t kept = 0;
    bool hit = false;
    for (uint8_t i = 0; i < n; i++)
    {
        if (cur[i].ssid == ssid)
        {
            hit = true;
            continue;
        }
        next[kept++] = cur[i];
    }
    if (!hit)
    {
        Serial.printf("[wifi] forget '%s': not in saved list (%u saved)\n",
                      ssid.c_str(), n);
        return false;
    }
    bool ok = saveRaw(next, kept);
    Serial.printf("[wifi] forget '%s': %s\n", ssid.c_str(),
                  ok ? "removed" : "WRITE FAILED");
    return ok;
}

/* ---- Trial-then-commit -------------------------------------------------------------
 * Separate NVS namespace so a trial never disturbs the committed list. Keys:
 *   pend (bool) + ts/tp (trial ssid/pass) | res (uint8) + rs (result ssid)
 */
static const char *TRIAL_NS = "wifitrial";

void wifiStoreSetTrial(const String &ssid, const String &pass)
{
    Preferences p;
    if (!p.begin(TRIAL_NS, false))
        return;
    p.putBool("pend", true);
    p.putString("ts", ssid);
    p.putString("tp", pass);
    p.putUChar("res", WIFI_TRIAL_NONE); // clear any old result on a fresh attempt
    p.remove("rs");
    p.end();
}

bool wifiStoreGetTrial(String &ssidOut, String &passOut)
{
    Preferences p;
    if (!p.begin(TRIAL_NS, true))
        return false;
    bool pend = p.getBool("pend", false);
    if (pend)
    {
        ssidOut = p.getString("ts", "");
        passOut = p.getString("tp", "");
    }
    p.end();
    if (pend && !ssidOut.length())
    {
        // pend=true with an empty ssid (e.g. an interrupted write) would otherwise stay
        // set forever and be re-read every boot. Self-heal: drop it.
        wifiStoreClearTrial();
        return false;
    }
    return pend;
}

void wifiStoreClearTrial()
{
    Preferences p;
    if (!p.begin(TRIAL_NS, false))
        return;
    p.putBool("pend", false);
    p.remove("ts");
    p.remove("tp");
    p.end();
}

void wifiStoreSetTrialResult(WifiTrialResult r, const String &ssid, const String &reason)
{
    Preferences p;
    if (!p.begin(TRIAL_NS, false))
        return;
    p.putUChar("res", (uint8_t)r);
    p.putString("rs", ssid);
    p.putString("rr", reason);
    p.end();
}

WifiTrialResult wifiStoreGetTrialResult(String &ssidOut, String &reasonOut)
{
    Preferences p;
    if (!p.begin(TRIAL_NS, true))
        return WIFI_TRIAL_NONE;
    WifiTrialResult r = (WifiTrialResult)p.getUChar("res", WIFI_TRIAL_NONE);
    ssidOut = p.getString("rs", "");
    reasonOut = p.getString("rr", "");
    p.end();
    return r;
}

String wifiStoreListJson()
{
    WifiNet cur[WIFI_STORE_MAX];
    uint8_t n = loadRaw(cur, WIFI_STORE_MAX);

    String live = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
    JsonDocument doc;
    doc["max"] = WIFI_STORE_MAX;
    doc["current"] = live; // which network the machine is on right now (may be off-list)
    JsonArray arr = doc["nets"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++)
    {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = cur[i].ssid;
        // No password here, on purpose: the dashboard is unauthenticated, and anyone who
        // can reach it could otherwise read every WiFi password the lab has typed in.
        o["saved"] = true;
        if (live.length() && cur[i].ssid == live)
            o["active"] = true; // the one currently connected -> panel marks it
    }
    // Report the last trial's outcome so the panel can say "wrong password, re-enter".
    // Clear it right after reporting: the notice should appear ONCE (on the first panel
    // open after the failed attempt), not on every open / every reboot forever.
    String rssid, rreason;
    WifiTrialResult r = wifiStoreGetTrialResult(rssid, rreason);
    if (r == WIFI_TRIAL_FAILED)
    {
        JsonObject t = doc["trial"].to<JsonObject>();
        t["result"] = "failed";
        t["ssid"] = rssid;
        t["reason"] = rreason; // "auth" | "range" -> the web picks the right message
        wifiStoreSetTrialResult(WIFI_TRIAL_NONE, "");
    }
    String out;
    serializeJson(doc, out);
    return out;
}
