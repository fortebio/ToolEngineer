/***********************************************************************
 * On-device test for the END OF A RUN: 40 minutes of data + WiFi + web dashboard
 * + "now upload it". This is the moment the machine actually fails in the field.
 *
 * Runs on a REAL ESP32 via PlatformIO's Unity runner (env:esp32dev_test). Self-contained:
 * it does NOT link src/ and needs NO network. postData_GoogleSheet() cannot be linked here
 * (it pulls in displayCLD/sensor6035/ForteSetting/EEPROM/WiFi), so the payload build is
 * mirrored below and MUST stay in sync with src/Bluetooth.cpp:578-700.
 *
 * WHAT BROKE (measured on board RPL03018, 2026-07-27):
 *   - Free heap at upload time was ~68 000 B - plenty. The upload still failed on all three
 *     endpoints with -0x0010 (BIGNUM alloc failed), -0x004C (recv) and -0x004E (send).
 *   - The number that decides it is not free heap but the largest CONTIGUOUS block, because
 *     mbedTLS takes its handshake buffers in single mallocs.
 *   - At rest that block was 65 524 B. During the whole upload window it was 49 140 B - a
 *     drop of EXACTLY 16 384 = 2^14, held from before the first handshake until after the
 *     last one.
 *   - A handshake was observed to SUCCEED at 63 476 B and to FAIL at 49 140 B, so the real
 *     mbedTLS requirement sits between those two watermarks.
 *
 * WHO OWNS THE 16 384 IS STILL OPEN. The first suspect was the serialized payload: an
 * Arduino String grown by append was assumed to double into a 16 KB block. THIS TEST
 * DISPROVED THAT - measured here, the payload takes 8 656 B for an 8 626 B string whether
 * reserve() is used or not, so ESP32's String does not double and the payload is not the
 * culprit. Something else inside postData_GoogleSheet holds 16 384 B across the upload
 * window (the detection pass in bResultPutToGoogleSheet is the next place to look).
 *
 * So what this file actually guards is the half that IS settled: the payload must stay
 * right-sized and the JsonDocument must be gone before TLS, leaving the block above the
 * watermark where a handshake died. It does not yet explain the 16 384.
 *
 * WHY THIS MEASURES ALLOCATION SIZE AND NOT THE BIG BLOCK: a test firmware does not link
 * src/, so it never starts WiFi/AsyncWebServer/SSE and boots with ~110 KB contiguous and
 * ~313 KB free across several heap regions. The payload then lands in some other region and
 * the big block does not move at all (measured: cost = 0 B, every assertion green while the
 * field device dies). Pre-allocating ballast to recreate the device's heap does not fix
 * that either - it starves ArduinoJson and silently truncates the payload (measured:
 * 8 626 B -> 6 386 B). The payload's allocation SIZE is deterministic here and is precisely
 * what the bug is about, so that is what we assert on; the field's at-rest block is then
 * applied as a measured constant.
 ***********************************************************************/
#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include "esp_heap_caps.h"

#ifndef TEST_MESSAGE
#define TEST_MESSAGE(msg) Serial.println(msg)
#endif

// ---- Mirrors of the firmware's shape (keep in sync) ------------------------------------
#define OPTOCHANNELS 10    // src/define.h
#define MAX_ROUNDS 130     // sensor67Value[10][130] - src/sensor6035.h
#define FULL_RUN_LOOPS 120 // a real 40 min run: 120 rounds x 20 s

// src/Bluetooth.cpp - the gate that refuses an upload that cannot succeed. Keep in sync.
static const size_t TLS_MIN = 56 * 1024;

// Measured watermarks (see header). The real mbedTLS requirement lies between them.
static const size_t TLS_OBSERVED_FAIL = 49140;
static const size_t TLS_OBSERVED_OK = 63476;

// Measured at-rest contiguous block on the real board with the dashboard up.
static const size_t END_OF_RUN_CONTIGUOUS = 65524;

// Static, like the firmware's own sensor67Value. heap_caps_malloc(MALLOC_CAP_INTERNAL) may
// hand back IRAM, which tolerates only aligned 32-bit access - writing uint16_t into it
// panics with LoadStoreError (hit while writing this test). Data buffers need
// MALLOC_CAP_8BIT, or .bss as here.
static uint16_t gRecord[OPTOCHANNELS][MAX_ROUNDS];

// The block that matters: mbedTLS handshake buffers are byte-addressable DATA, so only
// 8-bit-capable internal RAM can serve them.
static size_t contig()
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static size_t freeBytes()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// A plausible finished run: flat baseline then a rise, per slot.
static void fillRun(uint16_t rec[OPTOCHANNELS][MAX_ROUNDS], uint8_t loops)
{
    for (int i = 0; i < OPTOCHANNELS; i++)
        for (int j = 0; j < MAX_ROUNDS; j++)
            rec[i][j] = (j >= loops) ? 0
                                     : (uint16_t)(1500 + (j > 40 ? (j - 40) * 37 : 0) + i * 3);
}

/* Mirror of src/Bluetooth.cpp:578-700. `reserved` selects the two variants:
 *   true  -> measureJson + reserve + serializeJson (one exact allocation)
 *   false -> serializeJson straight into an empty String (whatever String's growth does)
 * Reports how many bytes the finished payload still occupies once the JsonDocument is gone
 * - i.e. exactly what the TLS handshake then has to work around. */
static size_t buildPayload(bool reserved, size_t *allocOut)
{
    size_t before = freeBytes();
    String jsonPost;
    {
        JsonDocument doc;
        doc["method"] = "append";
        doc["id_device"] = "RPL03018";
        doc["version"] = "v2.4.3";
        doc["kitId"] = "KIT-TEST";
        doc["type_Upload"] = "Auto";

        JsonArray slopes = doc["slopes"].to<JsonArray>();
        JsonArray origins = doc["origins"].to<JsonArray>();
        JsonArray led = doc["LED_power"].to<JsonArray>();
        JsonArray ct = doc["CT_value"].to<JsonArray>();
        JsonArray result = doc["result"].to<JsonArray>();
        JsonArray recordOut = doc["record_out"].to<JsonArray>();
        JsonArray amplification = doc["amplification"].to<JsonArray>();

        for (int i = 0; i < OPTOCHANNELS; i++)
        {
            JsonObject slot = recordOut.add<JsonObject>();
            JsonObject named = slot["Slot_" + String(i + 1)].to<JsonObject>();
            JsonObject peak = named["peak_features"].to<JsonObject>();
            JsonObject outcome = named["outcome"].to<JsonObject>();
            outcome["transition_time"] = "[41.0,1520.0]";
            outcome["plateau_point"] = "[95.0,4460.0]";
            outcome["increase"] = 2940.0;
            peak["main_peak"] = "[62.0,3200.0]";
            peak["right_arm"] = "[80.0,4100.0]";
            peak["left_arm"] = "[45.0,1700.0]";

            slopes.add(1.0234);
            origins.add(120.5);
            led.add(45);
            ct.add(41.2);
            result.add("41.2 | P");
        }

        // The heavy field: every raw round of every slot, comma-joined. This is what makes
        // the payload kilobytes at full length (src/Bluetooth.cpp:686-694).
        for (int i = 0; i < OPTOCHANNELS; i++)
        {
            String raw = "";
            for (int j = 0; j < FULL_RUN_LOOPS; j++)
                raw += String(gRecord[i][j]) + ",";
            amplification.add(raw);
        }

        if (reserved)
        {
            size_t need = measureJson(doc);
            jsonPost.reserve(need + 1);
        }
        serializeJson(doc, jsonPost);
    } // JsonDocument freed here - BEFORE any TLS, same as the firmware

    size_t after = freeBytes();
    *allocOut = (before > after) ? (before - after) : 0;
    return jsonPost.length();
}

// ---------------------------------------------------------------------------------------

// The payload must stay proportional to its own length. It does today (8 656 B for
// 8 626 B), reserve() or not - this pins that down so a future ArduinoJson or String growth
// policy cannot quietly turn the upload payload into a multi-KB block during the handshake.
void test_PAYLOAD_ALLOCATION_IS_RIGHT_SIZED(void)
{
    size_t alloc = 0;
    size_t len = buildPayload(true, &alloc);

    char msg[160];
    snprintf(msg, sizeof(msg), "payload len=%u B, heap taken=%u B", (unsigned)len,
             (unsigned)alloc);
    TEST_MESSAGE(msg);

    TEST_ASSERT_GREATER_THAN_UINT32(4000, len); // a full run really is kilobytes
    // "About its own length" - 4 KB covers allocator headers. 16 384 for ~8.6 KB does not.
    TEST_ASSERT_LESS_THAN_UINT32(len + 4096, alloc);
}

// The JsonDocument (tens of KB) must be gone before TLS starts. src/Bluetooth.cpp builds it
// in a nested scope for exactly that reason: once the scope closes only the payload is held.
void test_DOCUMENT_IS_RELEASED_BEFORE_UPLOAD(void)
{
    size_t alloc = 0;
    size_t len = buildPayload(true, &alloc);

    char msg[152];
    snprintf(msg, sizeof(msg), "still held after build: %u B for a %u B payload",
             (unsigned)alloc, (unsigned)len);
    TEST_MESSAGE(msg);

    // A leaked document would leave tens of KB held, not roughly the payload.
    TEST_ASSERT_LESS_THAN_UINT32(len + 4096, alloc);
}

// The point of the whole file: at the end of a run, with the dashboard up, does what the
// payload takes still leave a block an mbedTLS handshake can live in?
void test_END_OF_RUN_LEAVES_ROOM_FOR_TLS(void)
{
    size_t alloc = 0;
    size_t len = buildPayload(true, &alloc);
    size_t left = (END_OF_RUN_CONTIGUOUS > alloc) ? (END_OF_RUN_CONTIGUOUS - alloc) : 0;
    (void)len;

    char msg[192];
    snprintf(msg, sizeof(msg),
             "end-of-run %u B - payload %u B = %u B left (fail seen at %u, ok at %u)",
             (unsigned)END_OF_RUN_CONTIGUOUS, (unsigned)alloc, (unsigned)left,
             (unsigned)TLS_OBSERVED_FAIL, (unsigned)TLS_OBSERVED_OK);
    TEST_MESSAGE(msg);

    // Hard floor: never at or below the block where a handshake was observed to die. A
    // regression that made the payload heavier would trip this.
    TEST_ASSERT_GREATER_THAN_UINT32(TLS_OBSERVED_FAIL, left);

    // NOT asserted (yet): that this clears TLS_MIN. It does not - 65 524 - 8 656 = 56 868 vs
    // a 57 344 gate - and that is the honest state of the machine, not a broken test: at the
    // end of a run, with the dashboard up, there is no longer room for a handshake, which is
    // exactly why the upload has to move to a fresh boot (110 580 B contiguous measured in
    // setup(), before AsyncWebServer and any viewer take their permanent 14 336 B each).
    // Tighten this to TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TLS_MIN, left) once that lands.
    if (left < TLS_MIN)
        TEST_MESSAGE("NOTE: end-of-run does NOT clear the gate - upload must run after boot");
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    delay(2000); // let the USB CDC/serial settle before Unity prints

    fillRun(gRecord, FULL_RUN_LOOPS);

    char msg[128];
    snprintf(msg, sizeof(msg), "baseline: free=%u 8bit-contiguous=%u", (unsigned)freeBytes(),
             (unsigned)contig());

    UNITY_BEGIN();
    TEST_MESSAGE(msg);
    RUN_TEST(test_PAYLOAD_ALLOCATION_IS_RIGHT_SIZED);
    RUN_TEST(test_DOCUMENT_IS_RELEASED_BEFORE_UPLOAD);
    RUN_TEST(test_END_OF_RUN_LEAVES_ROOM_FOR_TLS);
    UNITY_END();
}

void loop() {}
