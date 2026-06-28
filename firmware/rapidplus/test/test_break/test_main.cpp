/***********************************************************************
 * On-device tests for the amplification-curve BREAK detector.
 *
 * These run on a REAL ESP32 via PlatformIO's Unity runner (env:esp32dev_test)
 * and cover every scenario of check_breakData()/checkJump() from
 * src/Alg/Algo.cpp, including the two real datasets the change was tuned on.
 *
 * src/Alg/Algo.cpp cannot be linked standalone (it pulls in ForteSetting/
 * _ForteSetting and the display/sensor globals via fromEEPROM/post_process/...),
 * so the PURE break-detection functions are mirrored below. They MUST stay in
 * sync with src/Alg/Algo.cpp: range_of(), mean_slope(), checkJump() (the #2 fix:
 * flatness measured on the SETTLED window after JUMP_SETTLE_SKIP samples; lower
 * bound index>=7 for a valid pre-window), and check_breakData().
 *
 * checkJump_old() reproduces the OLD flatness window (mean_slope at the jump,
 * no skip) with the SAME safe bounds, purely to demonstrate that the fix is what
 * lets a "step that settles/decays" be detected.
 ***********************************************************************/
#include <Arduino.h>
#include <unity.h>
#include <vector>
#include <cmath>

// Some Unity variants (e.g. the ESP-IDF SDK copy on the include path) don't
// expose TEST_MESSAGE; fall back to Serial so diagnostics compile/print anyway.
#ifndef TEST_MESSAGE
#define TEST_MESSAGE(msg) Serial.println(msg)
#endif

// ---- mirror of src/Alg/Algo.cpp (KEEP IN SYNC) -----------------------------
#define JUMP_SETTLE_SKIP 3 // post-jump settling samples skipped before the flatness check
#define JUMP_FLAT_WINDOW 6 // window length for the flatness (mean-slope) check
static const int BREAKING_START_INDEX = 6;
static const double CROSSING = 20.0; // = min_increase (counts), define.h default 20

static double range_of(std::vector<double> &a, size_t start, size_t window)
{
    if (start + window > a.size())
        return 0.0;
    double mn = a[start], mx = a[start];
    for (size_t i = start; i < start + window; i++)
    {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    return mx - mn;
}

static double mean_slope(std::vector<double> &a, size_t start, size_t window)
{
    double sum = 0;
    for (size_t i = start + 1; i < (start + window); i++)
        sum += (a[i + 1] - a[i]);
    return sum / (window - 1);
}

// FIXED checkJump (#2: flatness on the settled window; pre-window needs index>=7).
static bool checkJump(std::vector<double> &a, double crossing, size_t index)
{
    if ((index < 7) || ((index + JUMP_SETTLE_SKIP + JUMP_FLAT_WINDOW) >= a.size()))
        return false;
    double jump = a[index + 1] - a[index];
    if (jump < crossing)
        return false;
    double pre_range = range_of(a, index - 7, 8);
    if (jump < pre_range * 2.5)
        return false;
    double slope = mean_slope(a, index + JUMP_SETTLE_SKIP, JUMP_FLAT_WINDOW);
    if (fabs(slope) > 1.0)
        return false;
    int total_rise = a[index + 6] - a[index + 1];
    if (total_rise > crossing)
        return false;
    return true;
}

// OLD flatness window (slope measured AT the jump, no settle skip) — same bounds,
// only to prove the #2 change is what fixes the "step that settles" case.
static bool checkJump_old(std::vector<double> &a, double crossing, size_t index)
{
    if ((index < 7) || ((index + JUMP_SETTLE_SKIP + JUMP_FLAT_WINDOW) >= a.size()))
        return false;
    double jump = a[index + 1] - a[index];
    if (jump < crossing)
        return false;
    double pre_range = range_of(a, index - 7, 8);
    if (jump < pre_range * 2.5)
        return false;
    double slope = mean_slope(a, index, 6); // <-- OLD: no skip
    if (fabs(slope) > 1.0)
        return false;
    int total_rise = a[index + 6] - a[index + 1];
    if (total_rise > crossing)
        return false;
    return true;
}

static size_t check_breakData(std::vector<double> &a, double crossing, int start_index)
{
    for (size_t i = start_index; i < a.size(); i++)
        if (checkJump(a, crossing, i))
            return i;
    return 0;
}
static size_t check_breakData_old(std::vector<double> &a, double crossing, int start_index)
{
    for (size_t i = start_index; i < a.size(); i++)
        if (checkJump_old(a, crossing, i))
            return i;
    return 0;
}

// ---- helpers ----------------------------------------------------------------
static void msgf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    TEST_MESSAGE(buf);
}

static std::vector<double> vec(const double *arr, int n)
{
    return std::vector<double>(arr, arr + n);
}
// Build: `flat` copies of base, one `step` (level = base+step), then `tail` copies of (base+step).
static std::vector<double> stepCurve(double base, double step, int nFlat, int nTail)
{
    std::vector<double> v;
    for (int i = 0; i < nFlat; i++) v.push_back(base);
    for (int i = 0; i < nTail; i++) v.push_back(base + step);
    return v;
}

void setUp(void) {}
void tearDown(void) {}

// ---- real datasets the change was analysed on -------------------------------
static const double DATA1_SMOOTH[120] = {
    378,407,431,440,442,442,443,444,447,448,446,447,446,447,451,456,469,487,500,518,
    527,538,547,559,568,574,583,591,597,598,605,607,607,608,609,611,610,611,613,609,
    610,611,609,610,608,607,608,607,609,607,606,609,607,606,606,606,607,607,607,609,
    609,608,610,608,607,609,609,612,611,613,610,610,613,615,613,613,615,614,618,614,
    618,618,619,620,619,623,623,622,623,624,626,627,623,625,626,628,631,630,630,631,
    629,632,631,633,630,632,633,635,634,637,635,637,635,637,640,639,638,637,639,638};

static const double DATA2_STEP[120] = {
    168,241,267,276,278,279,278,280,286,285,284,285,286,282,284,288,288,288,288,288,
    288,288,284,283,281,283,280,280,280,281,280,279,280,281,282,285,281,285,284,286,
    283,287,290,288,289,292,291,292,295,294,294,360,356,352,349,350,350,346,345,347,
    347,345,343,344,348,343,346,344,344,344,343,346,343,344,345,345,344,344,342,343,
    344,342,345,346,347,347,345,345,345,344,345,344,344,344,343,344,344,347,344,343,
    344,343,343,344,345,344,347,348,344,345,341,346,344,347,345,344,345,345,348,349};

// === REAL DATA ==============================================================

// Smooth amplification rise: largest single-step jump after index 6 is only +18
// (< 20), so there is no isolated break — correctly returns 0.
static void test_real_data1_smooth_rise_no_break(void)
{
    std::vector<double> v = vec(DATA1_SMOOTH, 120);
    size_t b = check_breakData(v, CROSSING, BREAKING_START_INDEX);
    msgf("data1 (smooth) break index = %u (expect 0)", (unsigned)b);
    TEST_ASSERT_EQUAL_UINT32(0, b);
}

// Isolated +66 step at index 50->51 that settles to ~345: now detected as a break.
static void test_real_data2_step_detected_at_50(void)
{
    std::vector<double> v = vec(DATA2_STEP, 120);
    size_t b = check_breakData(v, CROSSING, BREAKING_START_INDEX);
    msgf("data2 (step+settle) break index = %u (expect 50)", (unsigned)b);
    TEST_ASSERT_EQUAL_UINT32(50, b);
}

// The same step was MISSED by the old flatness window (post-jump decline ~ -2/step
// gave |slope| > 1). This is exactly what the #2 fix addresses.
static void test_real_data2_old_window_missed_it(void)
{
    std::vector<double> v = vec(DATA2_STEP, 120);
    size_t bOld = check_breakData_old(v, CROSSING, BREAKING_START_INDEX);
    size_t bNew = check_breakData(v, CROSSING, BREAKING_START_INDEX);
    msgf("data2 old=%u new=%u", (unsigned)bOld, (unsigned)bNew);
    TEST_ASSERT_EQUAL_UINT32(0, bOld);  // old missed it
    TEST_ASSERT_EQUAL_UINT32(50, bNew); // fix catches it
}

// === SYNTHETIC: BREAK DETECTED ==============================================

// Classic isolated step on a flat baseline, flat after.
static void test_classic_step_flat_after(void)
{
    std::vector<double> v = stepCurve(280, 60, 25, 25); // jump at index 24->25
    TEST_ASSERT_EQUAL_UINT32(24, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Jump exactly at the threshold (jump == crossing) still counts (>=).
static void test_jump_exactly_threshold(void)
{
    std::vector<double> v = stepCurve(280, 20, 20, 20); // +20 step at index 19->20
    TEST_ASSERT_EQUAL_UINT32(19, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Step that overshoots then SETTLES/decays into a flat level -> break (the #2 case).
static void test_step_then_settle_decline(void)
{
    std::vector<double> v;
    for (int i = 0; i < 20; i++) v.push_back(290);      // baseline
    double tail[] = {360, 356, 352, 350, 350, 349, 350, 349, 350}; // overshoot then flat
    for (double x : tail) v.push_back(x);
    for (int i = 0; i < 15; i++) v.push_back(350);      // settled plateau
    size_t bNew = check_breakData(v, CROSSING, BREAKING_START_INDEX);
    size_t bOld = check_breakData_old(v, CROSSING, BREAKING_START_INDEX);
    msgf("settle case: old=%u new=%u (jump at 19)", (unsigned)bOld, (unsigned)bNew);
    TEST_ASSERT_EQUAL_UINT32(19, bNew); // fixed: detected
    TEST_ASSERT_EQUAL_UINT32(0, bOld);  // old: missed
}

// Two valid steps -> check_breakData returns the FIRST one.
static void test_returns_first_break(void)
{
    std::vector<double> v;
    for (int i = 0; i < 15; i++) v.push_back(280); // baseline
    for (int i = 0; i < 15; i++) v.push_back(340); // first step (jump at 14)
    for (int i = 0; i < 15; i++) v.push_back(400); // second step (jump at 29)
    TEST_ASSERT_EQUAL_UINT32(14, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// === SYNTHETIC: NO BREAK ====================================================

// Jump below the threshold -> not a break.
static void test_jump_below_threshold(void)
{
    std::vector<double> v = stepCurve(280, 15, 20, 20); // +15 < 20
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Jump that keeps rising afterwards (amplification onset) -> not an isolated break.
static void test_jump_then_sustained_rise(void)
{
    std::vector<double> v;
    for (int i = 0; i < 15; i++) v.push_back(280);  // baseline
    double t = 305;
    for (int i = 0; i < 20; i++) { v.push_back(t); t += 10; } // +25 step then +10/step ramp
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Step on a NOISY baseline (jump does not dominate the pre-window range) -> no break.
static void test_noisy_baseline_rejected(void)
{
    std::vector<double> v;
    for (int i = 0; i < 20; i++) v.push_back(i % 2 ? 310 : 250); // range ~60
    for (int i = 0; i < 20; i++) v.push_back(290 + 40);          // +40 step (< 2.5*60)
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// A real step but the level keeps drifting up (>1/step) even after settling -> no break.
static void test_post_jump_keeps_drifting(void)
{
    std::vector<double> v;
    for (int i = 0; i < 15; i++) v.push_back(280); // baseline
    double t = 340;
    for (int i = 0; i < 20; i++) { v.push_back(t); t += 2; } // +60 step then +2/step drift
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Jump inside the warm-up region (index < BREAKING_START_INDEX) is ignored.
static void test_jump_in_warmup_ignored(void)
{
    std::vector<double> v = stepCurve(280, 80, 4, 36); // jump at index 3 (< 6/7)
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Jump too close to the end (index + skip + window >= size) -> out of range, no break.
static void test_jump_near_end_out_of_range(void)
{
    std::vector<double> v = stepCurve(280, 80, 27, 3); // jump at index 26, size 30
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// Perfectly flat line -> no break.
static void test_flat_line_no_break(void)
{
    std::vector<double> v(40, 300.0);
    TEST_ASSERT_EQUAL_UINT32(0, check_breakData(v, CROSSING, BREAKING_START_INDEX));
}

// === runner =================================================================
void setup()
{
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();

    // real datasets
    RUN_TEST(test_real_data1_smooth_rise_no_break);
    RUN_TEST(test_real_data2_step_detected_at_50);
    RUN_TEST(test_real_data2_old_window_missed_it);

    // break detected
    RUN_TEST(test_classic_step_flat_after);
    RUN_TEST(test_jump_exactly_threshold);
    RUN_TEST(test_step_then_settle_decline);
    RUN_TEST(test_returns_first_break);

    // no break
    RUN_TEST(test_jump_below_threshold);
    RUN_TEST(test_jump_then_sustained_rise);
    RUN_TEST(test_noisy_baseline_rejected);
    RUN_TEST(test_post_jump_keeps_drifting);
    RUN_TEST(test_jump_in_warmup_ignored);
    RUN_TEST(test_jump_near_end_out_of_range);
    RUN_TEST(test_flat_line_no_break);

    UNITY_END();
}

void loop() {}
