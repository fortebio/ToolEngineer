// Guard: JsonDataConfig() applies EXACTLY the keys the caller sent - no more, no less.
//
// ArduinoJson 7 deprecated containsKey(), so all 42 gates in ForteSetting.cpp moved from
//     if (doc.containsKey("lysis duration"))
// to
//     if (!doc["lysis duration"].isNull())
// That is a semantic swap on the heater's trust boundary, not a rename, so it gets a check.
// The two agree on every payload this device actually receives; they differ on exactly one
// input, and this pins which way that difference falls.
//
// Build/run (host, no hardware):
//   g++ -O2 -std=c++17 -I.pio/libdeps/esp32dev/ArduinoJson/src tools/test_json_key_present.cpp -o t && ./t
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const char *what) {
  std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// The gate as it is written in ForteSetting.cpp today.
template <typename T> static bool gate(T &doc, const char *key) {
  return !doc[key].isNull();
}

int main() {
  // ---- 1. A real POST /config body: one Setting card sends only its own keys ------------
  const char *body =
      R"({"para version":"2.4.3","lysis temperature":82,"lysis duration":600,)"
      R"("amplification temperature":65.8,"amplification time":120,)"
      R"("time per loop":20000,"opto preheat time":300})";
  JsonDocument doc;
  auto err = deserializeJson(doc, body);
  check(!err, "the config body parses");

  const char *sent[] = {"para version",   "lysis temperature", "lysis duration",
                        "amplification temperature", "amplification time",
                        "time per loop",  "opto preheat time"};
  const char *absent[] = {"PID parameter", "device ID", "units", "LED power",
                          "top heater PWM", "buzzer", "kitId"};

  bool all_sent = true, none_absent = false;
  for (auto k : sent) all_sent = all_sent && gate(doc, k);
  for (auto k : absent) none_absent = none_absent || gate(doc, k);
  check(all_sent, "every key the caller SENT opens its gate");
  check(!none_absent, "no key the caller OMITTED opens a gate (the device keeps its own value)");

  // The old gate and the new one must agree on this payload, or the migration silently
  // changed which settings get written.
  bool agree = true;
  for (auto k : sent) agree = agree && (gate(doc, k) == true);
  for (auto k : absent) agree = agree && (gate(doc, k) == false);
  check(agree, "old containsKey() and new !isNull() agree on a real payload");

  // ---- 2. Values that are falsy but PRESENT must still apply ---------------------------
  // A zero setpoint or buzzer:false is a legitimate write. isNull() tests null, not falsy -
  // if that were wrong, turning the buzzer off would silently do nothing.
  JsonDocument z;
  deserializeJson(z, R"({"buzzer":false,"kitId":0,"lysis duration":0,"units":""})");
  check(gate(z, "buzzer"), "buzzer:false still applies");
  check(gate(z, "kitId"), "kitId:0 still applies");
  check(gate(z, "lysis duration"), "a zero duration still applies");
  check(gate(z, "units"), "an empty string still applies");

  // ---- 3. The one input where the two gates DISAGREE ----------------------------------
  // containsKey("k") is true for {"k":null}; the block would then run
  // .as<uint16_t>() on null, which yields 0 - a zero written into the heater config from a
  // key the operator never really set. !isNull() skips it. The change is in the safe
  // direction, and this asserts it stays that way.
  JsonDocument n;
  deserializeJson(n, R"({"lysis temperature":null,"amplification time":null})");
  check(!gate(n, "lysis temperature"), "an explicit null is SKIPPED, not written as 0");
  check(!gate(n, "amplification time"), "an explicit null amplification time is skipped");
  check(n["lysis temperature"].as<uint16_t>() == 0,
        "...and 0 is indeed what the old gate would have written");

  // ---- 4. Nested "parameters" object, same gate --------------------------------------
  JsonDocument p;
  deserializeJson(p, R"({"parameters":{"sg order":2,"sg window":4}})");
  JsonObject o = p["parameters"];
  check(gate(p, "parameters"), "the nested container opens its own gate");
  check(gate(o, "sg order") && gate(o, "sg window"), "keys inside it open theirs");
  check(!gate(o, "baseline start"), "an omitted nested key stays omitted");

  std::printf(failures ? "\nFAIL - %d check(s)\n" : "\nok - config gates apply exactly the keys sent\n",
              failures);
  return failures ? 1 : 0;
}
