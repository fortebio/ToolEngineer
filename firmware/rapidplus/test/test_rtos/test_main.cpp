/***********************************************************************
 * On-device FreeRTOS mechanism tests for the FBT-DXD firmware.
 *
 * These run on a REAL ESP32 via PlatformIO's Unity test runner
 * (env:esp32dev_test). They are self-contained: they do NOT link src/ or
 * touch any hardware. Instead they recreate the exact RTOS patterns the
 * firmware relies on (see src/main.cpp, src/displayCLD.h) and verify them:
 *
 *   A. Mutex                  -> mirrors gI2CMutex (xSemaphoreCreateMutex,
 *                                50 ms take-timeout used by SensorTask)
 *   B. Recursive mutex/SPILock -> mirrors gSPIMutex + the SPILock RAII guard
 *   C. Tasks                  -> xTaskCreatePinnedToCore: run, core affinity,
 *                                priority preemption, vTaskDelayUntil cadence
 *   D. Handshake              -> the type_infor / changeScreen producer-consumer
 *   E. Regression guards      -> (1) write-record-BEFORE-signal ordering that was
 *                                fixed in sensor6035.cpp; (2) a shared resource
 *                                (model of unguarded EEPROM) loses updates without
 *                                a mutex and is correct with one.
 *
 * IMPORTANT: Unity's TEST_ASSERT_* use longjmp back into the runner, so they
 * MUST only be called from the test task (the one running setup()). Child
 * tasks only record results into shared variables; the test task asserts
 * after joining them via a counting "done" semaphore.
 ***********************************************************************/
#include <Arduino.h>
#include <unity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Some Unity variants (e.g. the ESP-IDF SDK copy on the include path) don't
// expose TEST_MESSAGE; fall back to Serial so diagnostics compile/print anyway.
#ifndef TEST_MESSAGE
#define TEST_MESSAGE(msg) Serial.println(msg)
#endif

// Flip to 1 to deliberately break the ordering in test_ordering_write_before_signal
// (signal BEFORE writing the record) and prove the regression test actually fails.
#define INJECT_ORDERING_BUG 0

#define HELPER_STACK 2048
#define JOIN_TIMEOUT_MS 3000

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Print a formatted informational line into the Unity output.
static void msgf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    TEST_MESSAGE(buf);
}

// Wait for `n` helper tasks to signal completion on a counting semaphore.
// Returns true if all completed before the timeout (so a logic hang becomes a
// clean FAIL instead of locking up the whole suite).
static bool joinHelpers(SemaphoreHandle_t doneSem, int n, uint32_t timeoutMs)
{
    for (int i = 0; i < n; i++)
    {
        if (xSemaphoreTake(doneSem, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
            return false;
    }
    return true;
}

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
// Group A — Mutex (mirrors gI2CMutex)
// ===========================================================================

static void test_mutex_create(void)
{
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(m);
    vSemaphoreDelete(m);
}

static void test_mutex_take_give(void)
{
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(m, pdMS_TO_TICKS(100)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGive(m));
    vSemaphoreDelete(m);
}

// A helper that grabs a mutex and holds it for holdMs, then releases.
struct HoldArgs
{
    SemaphoreHandle_t mtx;
    uint32_t holdMs;
    SemaphoreHandle_t done;
};
static void holderTask(void *p)
{
    HoldArgs *a = (HoldArgs *)p;
    xSemaphoreTake(a->mtx, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(a->holdMs)); // keep it locked
    xSemaphoreGive(a->mtx);
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

// Reproduces SensorTask's xSemaphoreTake(gI2CMutex, pdMS_TO_TICKS(50)): when the
// bus is held, the take must time out (~50 ms) and return pdFALSE so the caller
// skips its turn rather than blocking forever.
static void test_mutex_timeout_when_held(void)
{
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    SemaphoreHandle_t done = xSemaphoreCreateCounting(1, 0);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_NOT_NULL(done);

    static HoldArgs a;
    a.mtx = m;
    a.holdMs = 300;
    a.done = done;

    // Higher priority + same core as this task: holder runs first and grabs it.
    xTaskCreatePinnedToCore(holderTask, "holder", HELPER_STACK, &a, 4, NULL, xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(20)); // let the holder acquire the mutex

    uint32_t t0 = millis();
    BaseType_t got = xSemaphoreTake(m, pdMS_TO_TICKS(50));
    uint32_t dt = millis() - t0;
    msgf("timed take returned %d after %u ms", (int)got, dt);

    TEST_ASSERT_EQUAL(pdFALSE, got);          // could not get it while held
    TEST_ASSERT_UINT32_WITHIN(40, 50, dt);    // waited ~50 ms (10..90)

    TEST_ASSERT_TRUE(joinHelpers(done, 1, JOIN_TIMEOUT_MS));
    vSemaphoreDelete(m);
    vSemaphoreDelete(done);
}

// Shared read-modify-write worker used by the mutual-exclusion / lost-update
// tests. The two workers run on DIFFERENT cores (true parallelism) with a
// widened read->write window, so an unguarded RMW loses updates while a
// mutex-guarded one stays exact.
struct RmwArgs
{
    volatile uint32_t *counter;
    uint32_t iters;
    SemaphoreHandle_t mtx; // NULL => unguarded
    SemaphoreHandle_t done;
};
static void rmwWorker(void *p)
{
    RmwArgs *a = (RmwArgs *)p;
    for (uint32_t i = 0; i < a->iters; i++)
    {
        if (a->mtx)
            xSemaphoreTake(a->mtx, portMAX_DELAY);
        uint32_t t = *a->counter;
        for (volatile int k = 0; k < 200; k++)
        {
        } // widen the read->write window so a truly-parallel sibling collides
        *a->counter = t + 1;
        if (a->mtx)
            xSemaphoreGive(a->mtx);
    }
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

// Run two RMW workers (one per core, equal priority) and return the final counter.
static uint32_t runRmw(uint32_t iters, SemaphoreHandle_t mtx)
{
    volatile uint32_t counter = 0;
    SemaphoreHandle_t done = xSemaphoreCreateCounting(2, 0);
    static RmwArgs a0, a1;
    a0 = (RmwArgs){&counter, iters, mtx, done};
    a1 = (RmwArgs){&counter, iters, mtx, done};

    // Pin the workers to DIFFERENT cores so they genuinely run in parallel —
    // that is what makes an unguarded read-modify-write actually lose updates.
    xTaskCreatePinnedToCore(rmwWorker, "rmw0", HELPER_STACK, &a0, 2, NULL, 0);
    xTaskCreatePinnedToCore(rmwWorker, "rmw1", HELPER_STACK, &a1, 2, NULL, 1);

    bool ok = joinHelpers(done, 2, JOIN_TIMEOUT_MS);
    vSemaphoreDelete(done);
    TEST_ASSERT_TRUE_MESSAGE(ok, "RMW workers did not finish in time");
    return counter;
}

// With a mutex, no updates are lost: counter == 2 * iters.
static void test_mutex_mutual_exclusion(void)
{
    const uint32_t N = 2000;
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(m);
    uint32_t result = runRmw(N, m);
    msgf("with-mutex counter = %u (expected %u)", result, 2 * N);
    TEST_ASSERT_EQUAL_UINT32(2 * N, result);
    vSemaphoreDelete(m);
}

// ===========================================================================
// Group B — Recursive mutex / SPILock (mirrors gSPIMutex + SPILock)
// ===========================================================================

static void test_recursive_mutex_nested_take(void)
{
    SemaphoreHandle_t r = xSemaphoreCreateRecursiveMutex();
    TEST_ASSERT_NOT_NULL(r);
    // Same task may take it repeatedly (nested SPILock scopes rely on this).
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(r, pdMS_TO_TICKS(100)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(r, pdMS_TO_TICKS(100)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(r, pdMS_TO_TICKS(100)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGiveRecursive(r));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGiveRecursive(r));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGiveRecursive(r));
    vSemaphoreDelete(r);
}

struct RecAttemptArgs
{
    SemaphoreHandle_t rmtx;
    volatile BaseType_t result;
    SemaphoreHandle_t done;
};
static void recAttemptTask(void *p)
{
    RecAttemptArgs *a = (RecAttemptArgs *)p;
    a->result = xSemaphoreTakeRecursive(a->rmtx, pdMS_TO_TICKS(50));
    if (a->result == pdTRUE)
        xSemaphoreGiveRecursive(a->rmtx);
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

// A recursive mutex held by one task must still block a DIFFERENT task.
static void test_recursive_mutex_blocks_other_task(void)
{
    SemaphoreHandle_t r = xSemaphoreCreateRecursiveMutex();
    SemaphoreHandle_t done = xSemaphoreCreateCounting(1, 0);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(r, pdMS_TO_TICKS(100)));

    static RecAttemptArgs a;
    a.rmtx = r;
    a.result = pdTRUE;
    a.done = done;
    xTaskCreatePinnedToCore(recAttemptTask, "recAtt", HELPER_STACK, &a, 3, NULL, xPortGetCoreID());

    TEST_ASSERT_TRUE(joinHelpers(done, 1, JOIN_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_MESSAGE(pdFALSE, a.result, "other task must not acquire a held recursive mutex");

    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGiveRecursive(r));
    vSemaphoreDelete(r);
    vSemaphoreDelete(done);
}

// Local copy of the firmware's SPILock RAII guard (src/displayCLD.h:17-34),
// parameterised on the handle so we can test it without pulling in the display
// driver. Verifies construct=acquire, scope-exit=release, and that nested
// guards (the reason gSPIMutex is recursive) do not deadlock.
class TestSPILock
{
public:
    TestSPILock(SemaphoreHandle_t h, TickType_t timeout = pdMS_TO_TICKS(200)) : handle(h)
    {
        taken = (handle != NULL) && (xSemaphoreTakeRecursive(handle, timeout) == pdTRUE);
    }
    ~TestSPILock()
    {
        if (taken)
            xSemaphoreGiveRecursive(handle);
    }
    bool acquired() const { return taken; }

private:
    SemaphoreHandle_t handle;
    bool taken;
};

static void test_spilock_raii(void)
{
    SemaphoreHandle_t spi = xSemaphoreCreateRecursiveMutex();
    SemaphoreHandle_t done = xSemaphoreCreateCounting(1, 0);
    TEST_ASSERT_NOT_NULL(spi);

    {
        TestSPILock outer(spi);
        TEST_ASSERT_TRUE(outer.acquired());
        {
            TestSPILock inner(spi); // nested -> must still succeed (recursive)
            TEST_ASSERT_TRUE(inner.acquired());
        } // inner releases here
    } // outer releases here

    // After both guards are gone, a separate task must be able to acquire it,
    // proving the RAII destructors released everything (balanced give count).
    static RecAttemptArgs a;
    a.rmtx = spi;
    a.result = pdFALSE;
    a.done = done;
    xTaskCreatePinnedToCore(recAttemptTask, "spiAtt", HELPER_STACK, &a, 3, NULL, xPortGetCoreID());
    TEST_ASSERT_TRUE(joinHelpers(done, 1, JOIN_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, a.result, "SPILock must fully release on scope exit");

    vSemaphoreDelete(spi);
    vSemaphoreDelete(done);
}

// ===========================================================================
// Group C — Tasks: creation, core affinity, priority, periodic timing
// ===========================================================================

struct RunFlagArgs
{
    volatile uint32_t *counter;
    SemaphoreHandle_t done;
};
static void incOnceTask(void *p)
{
    RunFlagArgs *a = (RunFlagArgs *)p;
    (*a->counter)++;
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

static void test_task_creation_and_run(void)
{
    volatile uint32_t counter = 0;
    SemaphoreHandle_t done = xSemaphoreCreateCounting(1, 0);
    static RunFlagArgs a;
    a = (RunFlagArgs){&counter, done};

    TaskHandle_t h = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(incOnceTask, "incOnce", HELPER_STACK, &a, 3, &h, xPortGetCoreID());
    TEST_ASSERT_EQUAL(pdPASS, ok);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_TRUE(joinHelpers(done, 1, JOIN_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_UINT32(1, counter);
    vSemaphoreDelete(done);
}

struct CoreArgs
{
    volatile int core;
    SemaphoreHandle_t done;
};
static void reportCoreTask(void *p)
{
    CoreArgs *a = (CoreArgs *)p;
    a->core = xPortGetCoreID();
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

// The firmware pins tasks to specific cores (ControlTask/SensorTask/InputTask ->
// core 1; Display/Network/Setting -> core 0). Verify pinning is honoured.
static void test_task_core_affinity(void)
{
    SemaphoreHandle_t done = xSemaphoreCreateCounting(2, 0);
    static CoreArgs c0, c1;
    c0 = (CoreArgs){-1, done};
    c1 = (CoreArgs){-1, done};

    xTaskCreatePinnedToCore(reportCoreTask, "core0", HELPER_STACK, &c0, 3, NULL, 0);
    xTaskCreatePinnedToCore(reportCoreTask, "core1", HELPER_STACK, &c1, 3, NULL, 1);

    TEST_ASSERT_TRUE(joinHelpers(done, 2, JOIN_TIMEOUT_MS));
    msgf("task pinned to 0 ran on %d, pinned to 1 ran on %d", c0.core, c1.core);
    TEST_ASSERT_EQUAL_INT(0, c0.core);
    TEST_ASSERT_EQUAL_INT(1, c1.core);
    vSemaphoreDelete(done);
}

// Priority-preemption fixture: low-prio task signals a high-prio one and the
// high-prio task must run to completion BEFORE the low-prio task continues.
static volatile int gSeq[4];
static volatile int gSeqIdx;
static SemaphoreHandle_t gGoHigh;
static SemaphoreHandle_t gPreemptDone;
enum
{
    STEP_LOW_BEFORE = 1,
    STEP_HIGH = 2,
    STEP_LOW_AFTER = 3
};
static void lowPrioTask(void *p)
{
    gSeq[gSeqIdx++] = STEP_LOW_BEFORE;
    xSemaphoreGive(gGoHigh);          // unblock the higher-priority task
    gSeq[gSeqIdx++] = STEP_LOW_AFTER; // must run AFTER the high task preempts
    xSemaphoreGive(gPreemptDone);
    vTaskDelete(NULL);
}
static void highPrioTask(void *p)
{
    xSemaphoreTake(gGoHigh, portMAX_DELAY);
    gSeq[gSeqIdx++] = STEP_HIGH;
    xSemaphoreGive(gPreemptDone);
    vTaskDelete(NULL);
}

static void test_task_priority_preemption(void)
{
    gSeqIdx = 0;
    gGoHigh = xSemaphoreCreateBinary();
    gPreemptDone = xSemaphoreCreateCounting(2, 0);
    TEST_ASSERT_NOT_NULL(gGoHigh);

    // Both on core 0 so priority alone decides ordering (this test task is on
    // core 1 and blocks on the join, so it won't interfere).
    xTaskCreatePinnedToCore(highPrioTask, "high", HELPER_STACK, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(lowPrioTask, "low", HELPER_STACK, NULL, 2, NULL, 0);

    TEST_ASSERT_TRUE(joinHelpers(gPreemptDone, 2, JOIN_TIMEOUT_MS));
    msgf("sequence = %d,%d,%d", gSeq[0], gSeq[1], gSeq[2]);
    TEST_ASSERT_EQUAL_INT(STEP_LOW_BEFORE, gSeq[0]);
    TEST_ASSERT_EQUAL_INT(STEP_HIGH, gSeq[1]);      // preempted low immediately
    TEST_ASSERT_EQUAL_INT(STEP_LOW_AFTER, gSeq[2]);

    vSemaphoreDelete(gGoHigh);
    vSemaphoreDelete(gPreemptDone);
}

struct PeriodicArgs
{
    volatile uint32_t ticks;
    volatile bool stop;
    SemaphoreHandle_t done;
};
static void periodicTask(void *p)
{
    PeriodicArgs *a = (PeriodicArgs *)p;
    TickType_t last = xTaskGetTickCount();
    while (!a->stop)
    {
        a->ticks++;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20)); // same primitive every task uses
    }
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

// vTaskDelayUntil at a 20 ms period should fire ~10 times in ~200 ms (the
// fixed-cadence pattern of every firmware task).
static void test_periodic_task_timing(void)
{
    SemaphoreHandle_t done = xSemaphoreCreateCounting(1, 0);
    static PeriodicArgs a;
    a.ticks = 0;
    a.stop = false;
    a.done = done;

    xTaskCreatePinnedToCore(periodicTask, "periodic", HELPER_STACK, &a, 3, NULL, xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(200));
    a.stop = true;
    TEST_ASSERT_TRUE(joinHelpers(done, 1, JOIN_TIMEOUT_MS));

    msgf("periodic task fired %u times in ~200 ms (expected ~10)", a.ticks);
    TEST_ASSERT_UINT32_WITHIN(2, 10, a.ticks); // 8..12
    vSemaphoreDelete(done);
}

// ===========================================================================
// Group D — Inter-task handshake (the type_infor / changeScreen pattern)
// ===========================================================================

static volatile int gSharedState;
static volatile bool gChangeScreen;
static volatile int gObservedState;
static volatile bool gConsumerSaw;
static SemaphoreHandle_t gHandshakeDone;

static void producerTask(void *p)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    gSharedState = 42;     // set the new screen state
    gChangeScreen = true;  // then raise the redraw flag
    xSemaphoreGive(gHandshakeDone);
    vTaskDelete(NULL);
}
static void consumerTask(void *p)
{
    uint32_t t0 = millis();
    while (!gChangeScreen && (millis() - t0) < 1000)
        vTaskDelay(pdMS_TO_TICKS(2));
    if (gChangeScreen)
    {
        gObservedState = gSharedState; // read state announced by producer
        gChangeScreen = false;         // consume the flag (DisplayTask clears it)
        gConsumerSaw = true;
    }
    xSemaphoreGive(gHandshakeDone);
    vTaskDelete(NULL);
}

static void test_changescreen_handshake(void)
{
    gSharedState = 0;
    gChangeScreen = false;
    gObservedState = -1;
    gConsumerSaw = false;
    gHandshakeDone = xSemaphoreCreateCounting(2, 0);

    xTaskCreatePinnedToCore(consumerTask, "consumer", HELPER_STACK, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(producerTask, "producer", HELPER_STACK, NULL, 3, NULL, 1);

    TEST_ASSERT_TRUE(joinHelpers(gHandshakeDone, 2, JOIN_TIMEOUT_MS));
    TEST_ASSERT_TRUE_MESSAGE(gConsumerSaw, "consumer never observed changeScreen");
    TEST_ASSERT_EQUAL_INT(42, gObservedState);
    TEST_ASSERT_FALSE_MESSAGE(gChangeScreen, "flag should be cleared after consumption");
    vSemaphoreDelete(gHandshakeDone);
}

// ===========================================================================
// Group E — Regression guards for the two concurrency bugs found earlier
// ===========================================================================

// E1: model the end-of-amplification finish in sensor6035.cpp. The producer
// (SensorTask role) must WRITE the record before raising changeScreen; the
// consumer (DisplayTask role, other core) reads the record when it sees the
// flag. Correct ordering => the consumer never reads a stale record.
static volatile int gRecord;
static volatile int gExpected;
static volatile bool gFinishFlag;
static volatile int gStaleReads;
static const int E1_ROUNDS = 200;
static SemaphoreHandle_t gRoundDone; // consumer -> producer, one per round
static SemaphoreHandle_t gE1Done;

static void e1ProducerTask(void *p)
{
    for (int r = 1; r <= E1_ROUNDS; r++)
    {
#if INJECT_ORDERING_BUG
        gFinishFlag = true; // BUG: signal before the record is written
        gExpected = r;
        gRecord = r;
#else
        gExpected = r;      // FIX: fully write the record first ...
        gRecord = r;
        gFinishFlag = true; // ... then raise the flag
#endif
        // Wait for the consumer to process this round (ping-pong handshake).
        xSemaphoreTake(gRoundDone, pdMS_TO_TICKS(1000));
    }
    xSemaphoreGive(gE1Done);
    vTaskDelete(NULL);
}
static void e1ConsumerTask(void *p)
{
    for (int r = 1; r <= E1_ROUNDS; r++)
    {
        uint32_t t0 = millis();
        while (!gFinishFlag && (millis() - t0) < 1000)
            taskYIELD();
        int v = gRecord; // read AFTER observing the flag
        if (v != gExpected)
            gStaleReads++;
        gFinishFlag = false;
        xSemaphoreGive(gRoundDone);
    }
    xSemaphoreGive(gE1Done);
    vTaskDelete(NULL);
}

static void test_ordering_write_before_signal(void)
{
    gRecord = 0;
    gExpected = 0;
    gFinishFlag = false;
    gStaleReads = 0;
    gRoundDone = xSemaphoreCreateBinary();
    gE1Done = xSemaphoreCreateCounting(2, 0);

    // Producer and consumer on opposite cores (as SensorTask/DisplayTask are).
    xTaskCreatePinnedToCore(e1ConsumerTask, "e1cons", HELPER_STACK, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(e1ProducerTask, "e1prod", HELPER_STACK, NULL, 3, NULL, 1);

    TEST_ASSERT_TRUE(joinHelpers(gE1Done, 2, JOIN_TIMEOUT_MS));
    msgf("stale reads over %d rounds = %d", E1_ROUNDS, gStaleReads);
#if INJECT_ORDERING_BUG
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, gStaleReads, "bug build should expose stale reads");
#else
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, gStaleReads, "record must be written before signaling the display");
#endif
    vSemaphoreDelete(gRoundDone);
    vSemaphoreDelete(gE1Done);
}

// E2: a shared resource standing in for the unguarded EEPROM object. Without a
// mutex, concurrent read-modify-write loses updates; with one, it is exact.
// This is the rationale for adding a global gEEPROMMutex (out of scope here).
static void test_mutex_prevents_lost_updates(void)
{
    const uint32_t N = 2000;

    // Unguarded parallel RMW should lose updates, but the exact amount is
    // timing-dependent. Retry a few times and keep the worst (lowest) result;
    // stop as soon as a loss is observed so the test is robust, not flaky.
    uint32_t noMutex = 2 * N;
    for (int attempt = 0; attempt < 5 && noMutex >= 2 * N; attempt++)
    {
        uint32_t r = runRmw(N, NULL);
        if (r < noMutex)
            noMutex = r;
    }

    uint32_t withMutex;
    {
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        TEST_ASSERT_NOT_NULL(m);
        withMutex = runRmw(N, m);
        vSemaphoreDelete(m);
    }

    msgf("min no-mutex = %u, with-mutex = %u, expected = %u", noMutex, withMutex, 2 * N);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * N, withMutex, "mutex must prevent lost updates");
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(2 * N, noMutex, "unguarded parallel RMW is expected to lose updates");
}

// ===========================================================================
// Unity runner
// ===========================================================================

void setup()
{
    Serial.begin(115200);
    delay(2000); // let USB-serial settle so the host test monitor catches output
    UNITY_BEGIN();

    // A — mutex
    RUN_TEST(test_mutex_create);
    RUN_TEST(test_mutex_take_give);
    RUN_TEST(test_mutex_timeout_when_held);
    RUN_TEST(test_mutex_mutual_exclusion);

    // B — recursive mutex / SPILock
    RUN_TEST(test_recursive_mutex_nested_take);
    RUN_TEST(test_recursive_mutex_blocks_other_task);
    RUN_TEST(test_spilock_raii);

    // C — tasks
    RUN_TEST(test_task_creation_and_run);
    RUN_TEST(test_task_core_affinity);
    RUN_TEST(test_task_priority_preemption);
    RUN_TEST(test_periodic_task_timing);

    // D — handshake
    RUN_TEST(test_changescreen_handshake);

    // E — regression guards
    RUN_TEST(test_ordering_write_before_signal);
    RUN_TEST(test_mutex_prevents_lost_updates);

    UNITY_END();
}

void loop() {}
