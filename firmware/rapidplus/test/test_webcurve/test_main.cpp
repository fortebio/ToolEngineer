/***********************************************************************
 * On-device heap tests for the web dashboard's GET /curve at FULL RUN scale.
 *
 * WHY THIS EXISTS
 * A full amplification run is amplification_time = 120 rounds x timePerLoop =
 * 20 s = 40 minutes (src/define.h). The device reboots at the END of such a run.
 * At that exact moment screen_Result() (displayLCD.cpp) runs
 *   releaseBluetoothStack() -> getDataAmplificationEEPROM() -> serial dump ->
 *   postData_GoogleSheet()  <-- mbedTLS needs ~32-40 KB CONTIGUOUS (CLAUDE.md GOTCHA 2)
 * while the browser may GET /curve, which now returns the whole finished run.
 *
 * At 120 rounds /curve is 10 x 120 = 1200 floats. Building that as a JsonDocument,
 * serializing it to a String, and letting AsyncWebServer copy it for the response
 * keeps all three alive at once: measured here at ~30 KB. handleCurve() therefore
 * STREAMS the payload through a small rolling buffer instead (~1.5 KB), and these
 * tests guard that: the streamed shape must stay cheap, and must stay dramatically
 * cheaper than materialising, which is why it exists. CurveWriter below mirrors the
 * one in src/webDashboard.cpp.
 *
 * Self-contained like the other tests here (test_build_src = no): src/ is NOT
 * linked, so this recreates the exact data shapes and the exact serialisation
 * code path instead of calling handleCurve().
 *
 * Run:  pio test -e esp32dev_test -f test_webcurve -v
 ***********************************************************************/
#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include "esp_heap_caps.h"

// ---- mirror of the firmware's run data / settings -------------------------
typedef uint16_t Word;

static const uint8_t LOOPS = 120;       // _ForteSetting.parameter.amplification_time
static const uint32_t INTERVAL = 20000; // timePerLoop (ms) -> 120 * 20 s = 40 min
static const int CH = 10;
static const int MAXLOOPS = 130; // sensor67Value[10][130]

static Word sensor67Value[CH][MAXLOOPS];
static float origins[CH];
static float slopes[CH];

// mbedTLS handshake needs this much CONTIGUOUS heap, and it runs at the same
// moment as /curve at the end of a run (CLAUDE.md GOTCHA 2 / -32512).
static const size_t TLS_CONTIGUOUS_NEED = 40 * 1024;

// What a single /curve response is allowed to cost at peak. It must be small
// enough that it cannot eat the block the TLS upload needs. Streaming the JSON
// keeps this at ~1-2 KB; the JsonDocument+String path blows way past it.
static const size_t CURVE_HEAP_BUDGET = 8 * 1024;

static double r1(double v) { return round(v * 10.0) / 10.0; }

static size_t freeHeap() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
static size_t largestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

// A realistic finished run: sigmoid amplification, raw sensor counts.
static void fillFullRun()
{
  for (int c = 0; c < CH; c++)
  {
    origins[c] = 100.0f + c;
    slopes[c] = 1.5f;
    float amp = (c % 4 == 3) ? 0.0f : 500.0f; // some negatives, like a real plate
    for (int j = 0; j < MAXLOOPS; j++)
    {
      float y = 150.0f + amp / (1.0f + expf(-0.35f * (j - 60)));
      sensor67Value[c][j] = (Word)y;
    }
  }
}

// ---------------------------------------------------------------------------
// PATH A - what handleCurve() does today: JsonDocument -> String -> response copy
// ---------------------------------------------------------------------------
static size_t buildCurve_document(String &outJson, size_t &peakCost)
{
  const size_t before = freeHeap();
  size_t peakFree = before;

  {
    JsonDocument doc;
    doc["count"] = LOOPS;
    doc["intervalMs"] = INTERVAL;
    JsonArray series = doc["series"].to<JsonArray>();
    for (int ch = 0; ch < CH; ch++)
    {
      JsonArray a = series.add<JsonArray>();
      float slope = slopes[ch];
      for (uint8_t j = 0; j < LOOPS; j++)
      {
        float raw = (float)sensor67Value[ch][j];
        a.add(r1((slope != 0.0f) ? (raw - origins[ch]) / slope : raw));
      }
    }
    if (freeHeap() < peakFree)
      peakFree = freeHeap(); // document alive

    String out;
    serializeJson(doc, out);
    if (freeHeap() < peakFree)
      peakFree = freeHeap(); // document + string alive

    // AsyncWebServer's req->send(200, type, String) keeps its own copy of the
    // body while the document/string are still in scope on the async task.
    String responseCopy = out;
    if (freeHeap() < peakFree)
      peakFree = freeHeap(); // document + string + response copy alive

    outJson = out;
  }

  peakCost = before - peakFree;
  return before;
}

// ---------------------------------------------------------------------------
// PATH B - streamed: emit the same JSON through a small rolling buffer, which is
// what a chunked AsyncWebServer response does. Nothing big is ever resident.
// ---------------------------------------------------------------------------
struct CurveWriter
{
  uint8_t n;
  uint32_t interval;
  int ch = 0;
  int j = 0;
  int stage = 0; // 0 = header, 1 = body, 3 = done
  String pend;

  bool finished() const { return stage == 3 && pend.length() == 0; }

  void step()
  {
    if (stage == 0)
    {
      pend += "{\"count\":";
      pend += n;
      pend += ",\"intervalMs\":";
      pend += interval;
      pend += ",\"series\":[";
      pend += "[";
      stage = 1;
      ch = 0;
      j = 0;
      return;
    }
    if (stage == 1)
    {
      if (j < n)
      {
        if (j)
          pend += ",";
        float raw = (float)sensor67Value[ch][j];
        float s = slopes[ch];
        pend += String(r1((s != 0.0f) ? (raw - origins[ch]) / s : raw), 1);
        j++;
        return;
      }
      pend += "]";
      ch++;
      j = 0;
      if (ch < CH)
      {
        pend += ",[";
        return;
      }
      pend += "]}";
      stage = 3;
    }
  }

  // Mirrors AsyncWebServer's chunked callback: fill up to maxLen, return 0 at EOF.
  size_t fill(uint8_t *buf, size_t maxLen)
  {
    while (pend.length() < maxLen && stage != 3)
      step();
    if (stage == 3 && pend.length() == 0)
      return 0;
    size_t k = pend.length() < maxLen ? pend.length() : maxLen;
    memcpy(buf, pend.c_str(), k);
    pend.remove(0, k);
    return k;
  }
};

// Cost pass: stream and DISCARD. Nothing is accumulated, so what we measure is only
// what the server itself would hold (the writer's rolling buffer). Keeping a sink here
// would measure the test, not the code under test.
static size_t streamedHeapCost()
{
  const size_t before = freeHeap();
  size_t peakFree = before;

  CurveWriter w;
  w.n = LOOPS;
  w.interval = INTERVAL;

  uint8_t chunk[1460]; // one TCP segment, the size AsyncWebServer asks for
  while (w.fill(chunk, sizeof(chunk)) > 0)
  {
    size_t f = freeHeap();
    if (f < peakFree)
      peakFree = f;
  }
  return before > peakFree ? before - peakFree : 0;
}

// Content pass: same writer, collected into a String so the test can compare values.
// Its cost is NOT measured (the sink is the test's, not the server's).
static void buildCurve_streamed(String &outJson)
{
  CurveWriter w;
  w.n = LOOPS;
  w.interval = INTERVAL;
  uint8_t chunk[1460];
  outJson = "";
  outJson.reserve(9000);
  size_t got;
  while ((got = w.fill(chunk, sizeof(chunk))) > 0)
    outJson.concat((const char *)chunk, got);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

// The two paths must produce the same numbers, or the fix changes the contract.
void test_streamed_matches_document(void)
{
  String a, b;
  size_t ca;
  buildCurve_document(a, ca);
  buildCurve_streamed(b);

  JsonDocument da, db;
  TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, deserializeJson(da, a).code(),
                            "document path produced invalid JSON");
  TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, deserializeJson(db, b).code(),
                            "streamed path produced invalid JSON");

  TEST_ASSERT_EQUAL_UINT32(da["count"].as<uint32_t>(), db["count"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(da["intervalMs"].as<uint32_t>(), db["intervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(LOOPS, db["count"].as<uint32_t>());

  for (int c = 0; c < CH; c++)
  {
    JsonArray ra = da["series"][c].as<JsonArray>();
    JsonArray rb = db["series"][c].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ra.size(), rb.size(), "series length differs");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LOOPS, rb.size(), "series must hold the whole run");
    for (size_t j = 0; j < ra.size(); j++)
      TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.051f, ra[j].as<float>(), rb[j].as<float>(),
                                       "calibrated value differs between paths");
  }
}

// THE regression: a /curve response at full-run scale must not eat the heap the
// TLS upload needs at the very same moment (end of run).
void test_curve_heap_cost_within_budget(void)
{
  String json;
  size_t costDoc = 0;

  buildCurve_document(json, costDoc);
  const size_t bytes = json.length();
  const size_t costStream = streamedHeapCost();

  Serial.printf("\n[curve] full run: %u rounds x %d channels -> %u byte payload\n",
                (unsigned)LOOPS, CH, (unsigned)bytes);
  Serial.printf("[curve] peak heap cost: streamed (handleCurve) = %u B, "
                "materialised (JsonDocument+String+copy) = %u B  -> %ux worse\n",
                (unsigned)costStream, (unsigned)costDoc,
                (unsigned)(costStream ? costDoc / costStream : 0));
  Serial.printf("[curve] budget = %u B (must leave TLS its %u B contiguous block)\n",
                (unsigned)CURVE_HEAP_BUDGET, (unsigned)TLS_CONTIGUOUS_NEED);

  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, bytes, "payload must not be empty");

  // THE GUARD: the shape handleCurve() actually uses must stay cheap, so /curve can
  // never starve the mbedTLS handshake postData_GoogleSheet runs at the end of a run.
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(
      CURVE_HEAP_BUDGET, costStream,
      "streamed /curve got expensive - it can now starve the TLS upload that runs at "
      "the end of the same run (-> OOM reboot after a 40 minute run). Keep handleCurve "
      "emitting through a small rolling buffer; do not materialise the payload.");

  // Characterisation: materialising the payload is dramatically worse, and that gap is
  // the entire reason handleCurve streams. If this ever fails the naive shape became
  // affordable and handleCurve could be simplified back - re-measure before doing so.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      costStream * 4, costDoc,
      "materialising /curve is no longer much worse than streaming - re-evaluate");
}

// After serving /curve, a TLS-sized contiguous block must still be obtainable.
//
// WEAK TEST - READ THIS BEFORE TRUSTING IT. src/ is not linked here, so there is no
// WiFi stack, no AsyncWebServer and no Bluetooth resident: this environment has
// ~220 KB free where the real firmware has a fraction of that, fragmented. It passing
// proves almost nothing; it would only catch a catastrophic leak. The number that
// matters is the COST measured above, which is environment-independent.
void test_tls_block_still_available_after_curve(void)
{
  String json;
  size_t cost = 0;
  buildCurve_document(json, cost);

  Serial.printf("[heap] free=%u largest=%u  (NOTE: no WiFi/AsyncWebServer/BT here,\n"
                "       so this is FAR more headroom than the real firmware has)\n",
                (unsigned)freeHeap(), (unsigned)largestBlock());

  void *tls = heap_caps_malloc(TLS_CONTIGUOUS_NEED, MALLOC_CAP_8BIT);
  bool got = tls != nullptr;
  if (tls)
    heap_caps_free(tls);
  TEST_ASSERT_TRUE_MESSAGE(got, "no contiguous block left for the TLS handshake");
}

void setup()
{
  delay(2000);
  fillFullRun();
  UNITY_BEGIN();
  RUN_TEST(test_streamed_matches_document);
  RUN_TEST(test_curve_heap_cost_within_budget);
  RUN_TEST(test_tls_block_still_available_after_curve);
  UNITY_END();
}

void loop() {}
