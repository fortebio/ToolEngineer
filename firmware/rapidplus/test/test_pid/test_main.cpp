/***********************************************************************
 * On-device PID tests for the FBT-DXD firmware temperature control.
 *
 * These run on a REAL ESP32 via PlatformIO's Unity runner (env:esp32dev_test)
 * and bind tightly to the PROJECT'S real PID configuration:
 *   - the SAME library the firmware uses: br3ttb/PID_v1 (#include <PID_v1.h>),
 *     constructed exactly like PIDControl::begin() (src/PIDControl.cpp:67-78):
 *     PID(&in,&out,&set, Kp,Ki,Kd, DIRECT) + SetMode(AUTOMATIC);
 *   - the SAME gains / setpoints / thresholds / PWM limits, copied below from
 *     src/define.h and src/PIDControl.cpp WITH line citations.
 *
 * src/define.h cannot be #included here (it pulls in WiFi/HTTPClient/Bluetooth),
 * and the PIDControl class cannot be linked standalone (it pulls in
 * thermometer/displayCLD/sensor6035/buzzer/ForteSetting/EEPROM). So we mirror
 * the real constants (keep them in sync with the firmware) and drive the real
 * PID library against a simulated first-order thermal plant.
 *
 * Group 1 — closed loop with the real gains (does the controller regulate?).
 * Group 2 — deterministic control-logic equivalence (the exact overheat/ready
 *           branches from Maintain2_67 / heatNewLid23 / maintainNewLid23).
 *
 * NOTE: the thermal plant is a SIMULATION model (no heater attached during a
 * unit test). The tests assert control behaviour (convergence, bounded
 * overshoot, output saturation/limits, direction) using the real gains — not a
 * physical-tuning validation. Tolerances are sanity bands.
 ***********************************************************************/
#include <Arduino.h>
#include <unity.h>
#include <PID_v1.h>

// ===========================================================================
// REAL firmware constants — MUST stay in sync with the firmware.
// (define.h is not includable in a test, so they are mirrored here.)
// ===========================================================================

// PID gains — src/define.h:261-273 (parastructure defaults)
static const double KPID_LYSIS[3] = {30, 0.05, 30};  // kpid   (bottom heater1, lysis)
static const double KPID_AMP[3] = {60, 0.1, 40};      // kpid2  (bottom heater2&3, amplification)
static const double KPID_HOTLID[3] = {60, 0.1, 40};   // kpid3  (top hotlid2&3)

// Setpoints — src/define.h:256-257 and src/define.h:426
static const double LYSIS_TEMP = 82.0;   // lysisTemp
static const double AMPLIF_TEMP = 65.8;  // amplifTemp
static const double HOTLID_TEMP = 75.0;  // HOTLID23_TEMP

// PWM limits — src/define.h:382-383
static const double PWM_OFF = 0;
static const double PWM_FULL = 255;

// Hotlid configured PWM — src/define.h:269 hotlidPWM = {{40,100},{40,100}}.
// The (currently commented-out) safety limit at src/PIDControl.cpp:82-85 caps
// the hotlid PID output at max(low,high) of these.
static const double HOTLID_PWM_LOW = 40;
static const double HOTLID_PWM_HIGH = 100;
static const double HOTLID_MAX_PWM = 100; // max(HOTLID_PWM_LOW, HOTLID_PWM_HIGH)

// Control thresholds
static const double BOTTOM_OVERHEAT = 5;     // bottomOverheat[1], define.h:263 (OVERHEAT_THRESHOLD2)
static const double HOTLID_CUTOFF_DELTA = 10; // src/PIDControl.cpp:1431/1450/1540/1560 ("> HOTLID23_TEMP + 10")
static const double HOTLID_READY_DELTA = 3;   // src/PIDControl.cpp:1441/1460 ("temp >= HOTLID23_TEMP - 3")

// Simulation parameters (the control period equals the firmware SampleTime).
static const int SIM_DT_MS = 100; // == PID_v1 default SampleTime used by the firmware
static const double SIM_DT_S = SIM_DT_MS / 1000.0;
static const double AMBIENT = 25.0;
// First-order plant: temp += (HEAT*(pwm/255) - COOL*(temp-AMBIENT)) * dt.
// Equilibrium at full power = AMBIENT + HEAT/COOL must exceed every setpoint.
static const double PLANT_HEAT = 9.0;  // -> full-power equilibrium = 25 + 9/0.12 = 100 C
static const double PLANT_COOL = 0.12; // time constant ~ 8.3 s
static const int CONV_STEPS = 300;     // 30 s simulated per convergence test

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void msgf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    TEST_MESSAGE(buf);
}

struct LoopResult
{
    double finalTemp;
    double maxTemp; // for overshoot
    double minOut;
    double maxOut;
};

// Run the REAL PID library closed-loop against the simulated plant, exactly the
// way the firmware drives it: set input/setpoint, Compute(), apply output as PWM.
static LoopResult runClosedLoop(const double k[3], double setpoint, double startTemp,
                                int steps, double outMax)
{
    double input = startTemp;
    double output = 0;
    double sp = setpoint;

    // Construct like PIDControl::begin(): PID(&in,&out,&set,Kp,Ki,Kd,DIRECT).
    PID pid(&input, &output, &sp, k[0], k[1], k[2], DIRECT);
    pid.SetOutputLimits(0, outMax);
    pid.SetSampleTime(SIM_DT_MS);
    pid.SetMode(AUTOMATIC);

    LoopResult r = {startTemp, startTemp, outMax, 0};
    double temp = startTemp;
    for (int i = 0; i < steps; i++)
    {
        delay(SIM_DT_MS + 5); // ensure >= SampleTime real time so Compute() fires
        input = temp;
        pid.Compute();
        if (output < r.minOut)
            r.minOut = output;
        if (output > r.maxOut)
            r.maxOut = output;
        // advance the plant by one control period
        temp += (PLANT_HEAT * (output / 255.0) - PLANT_COOL * (temp - AMBIENT)) * SIM_DT_S;
        if (temp > r.maxTemp)
            r.maxTemp = temp;
    }
    r.finalTemp = temp;
    return r;
}

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
// Group 1 — closed loop with the REAL gains and the REAL PID_v1 library
// ===========================================================================

// Note: with the real gains the integral term (Ki small) needs minutes to drive
// the last fraction of a degree to zero (the firmware holds continuously). Over a
// bounded sim we therefore assert robust directional behaviour: saturate when
// cold, significantly reduce the error toward setpoint, and stay bounded (no
// overshoot/runaway). The exact thresholds live in the deterministic logic tests.
static void test_amplification_pid_converges(void)
{
    const double start = 45.0;
    LoopResult r = runClosedLoop(KPID_AMP, AMPLIF_TEMP, start, CONV_STEPS, PWM_FULL);
    double err0 = fabs(AMPLIF_TEMP - start), errF = fabs(AMPLIF_TEMP - r.finalTemp);
    msgf("amp(kpid2): start=%.1f final=%.2f max=%.2f out[%.0f..%.0f] err %.2f->%.2f",
         start, r.finalTemp, r.maxTemp, r.minOut, r.maxOut, err0, errF);
    TEST_ASSERT_TRUE_MESSAGE(r.maxOut >= 200.0, "should saturate high while cold");
    TEST_ASSERT_TRUE_MESSAGE(errF <= 0.4 * err0, "should drive significantly toward setpoint");
    TEST_ASSERT_TRUE_MESSAGE(r.maxTemp <= AMPLIF_TEMP + 5.0, "no large overshoot");
    TEST_ASSERT_TRUE(r.minOut >= 0.0 && r.maxOut <= PWM_FULL); // output stays in [0,255]
}

static void test_hotlid_pid_heats_to_ready(void)
{
    // Hotlid PID currently uses default 0..255 limits (SetOutputLimits commented out).
    const double start = AMBIENT; // 25 C, far below the 75 C target
    LoopResult r = runClosedLoop(KPID_HOTLID, HOTLID_TEMP, start, CONV_STEPS, PWM_FULL);
    double err0 = fabs(HOTLID_TEMP - start), errF = fabs(HOTLID_TEMP - r.finalTemp);
    msgf("hotlid(kpid3): start=%.1f final=%.2f max=%.2f out[%.0f..%.0f] err %.2f->%.2f",
         start, r.finalTemp, r.maxTemp, r.minOut, r.maxOut, err0, errF);
    TEST_ASSERT_TRUE_MESSAGE(r.maxOut > 200.0, "should saturate high while far below");
    TEST_ASSERT_TRUE_MESSAGE(errF <= 0.4 * err0, "should heat substantially toward 75 C");
    TEST_ASSERT_TRUE_MESSAGE(r.finalTemp >= HOTLID_TEMP - 6.0, "should reach near the hotlid target");
    TEST_ASSERT_TRUE_MESSAGE(r.maxTemp <= HOTLID_TEMP + 5.0, "no large overshoot");
    TEST_ASSERT_TRUE(r.minOut >= 0.0 && r.maxOut <= PWM_FULL);
}

static void test_lysis_pid_converges(void)
{
    const double start = 55.0;
    LoopResult r = runClosedLoop(KPID_LYSIS, LYSIS_TEMP, start, CONV_STEPS, PWM_FULL);
    double err0 = fabs(LYSIS_TEMP - start), errF = fabs(LYSIS_TEMP - r.finalTemp);
    msgf("lysis(kpid): start=%.1f final=%.2f max=%.2f out[%.0f..%.0f] err %.2f->%.2f",
         start, r.finalTemp, r.maxTemp, r.minOut, r.maxOut, err0, errF);
    TEST_ASSERT_TRUE_MESSAGE(r.maxOut >= 200.0, "should saturate high while cold");
    TEST_ASSERT_TRUE_MESSAGE(errF <= 0.4 * err0, "should drive significantly toward setpoint");
    TEST_ASSERT_TRUE_MESSAGE(r.maxTemp <= LYSIS_TEMP + 5.0, "no large overshoot");
    TEST_ASSERT_TRUE(r.minOut >= 0.0 && r.maxOut <= PWM_FULL);
}

// DIRECT action: below setpoint => output drives up; above setpoint => output to 0.
static void test_pid_direction_direct(void)
{
    double input, output = 0, sp = AMPLIF_TEMP;
    PID pid(&input, &output, &sp, KPID_AMP[0], KPID_AMP[1], KPID_AMP[2], DIRECT);
    pid.SetOutputLimits(0, PWM_FULL);
    pid.SetSampleTime(SIM_DT_MS);

    // far below setpoint -> output should be driven high
    input = AMPLIF_TEMP - 20;
    pid.SetMode(AUTOMATIC);
    for (int i = 0; i < 3; i++)
    {
        delay(SIM_DT_MS + 5);
        input = AMPLIF_TEMP - 20;
        pid.Compute();
    }
    msgf("direction: below-setpoint output=%.0f", output);
    TEST_ASSERT_TRUE_MESSAGE(output > 100.0, "DIRECT: below setpoint must drive output up");

    // far above setpoint -> output should fall to the lower limit (0)
    for (int i = 0; i < 5; i++)
    {
        delay(SIM_DT_MS + 5);
        input = AMPLIF_TEMP + 20;
        pid.Compute();
    }
    msgf("direction: above-setpoint output=%.0f", output);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, (float)output);
}

// Output must stay within [0,255] even under sustained large error (PID_v1
// clamps both the output and its integral term -> anti-windup).
static void test_pid_output_clamped_and_antiwindup(void)
{
    double input, output = 0, sp = AMPLIF_TEMP;
    PID pid(&input, &output, &sp, KPID_AMP[0], KPID_AMP[1], KPID_AMP[2], DIRECT);
    pid.SetOutputLimits(0, PWM_FULL);
    pid.SetSampleTime(SIM_DT_MS);
    input = AMPLIF_TEMP - 50; // huge sustained error
    pid.SetMode(AUTOMATIC);

    double maxOut = 0, minOut = PWM_FULL;
    for (int i = 0; i < 40; i++) // wind the integrator hard
    {
        delay(SIM_DT_MS + 5);
        input = AMPLIF_TEMP - 50;
        pid.Compute();
        if (output > maxOut) maxOut = output;
        if (output < minOut) minOut = output;
        TEST_ASSERT_TRUE(output >= 0.0 && output <= PWM_FULL);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)PWM_FULL, (float)maxOut); // saturated to the ceiling, never above

    // Now flip far above setpoint; despite the wound-up integral it must reach 0
    // quickly (bounded windup), not stay stuck high.
    for (int i = 0; i < 60; i++)
    {
        delay(SIM_DT_MS + 5);
        input = AMPLIF_TEMP + 50;
        pid.Compute();
        TEST_ASSERT_TRUE(output >= 0.0 && output <= PWM_FULL);
    }
    msgf("antiwindup: after flip output=%.0f", output);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, (float)output);
}

// The hotlid safety limit (src/PIDControl.cpp:82-85, currently commented out):
// with SetOutputLimits(0, max(hotlidPWM)) the PID can never command more than
// the configured safe PWM, even far below setpoint.
static void test_hotlid_output_limit_caps_pwm(void)
{
    LoopResult r = runClosedLoop(KPID_HOTLID, HOTLID_TEMP, AMBIENT, 80, HOTLID_MAX_PWM);
    msgf("hotlid limited: out[%.0f..%.0f] cap=%.0f", r.minOut, r.maxOut, HOTLID_MAX_PWM);
    TEST_ASSERT_TRUE_MESSAGE(r.maxOut <= HOTLID_MAX_PWM, "limited hotlid PID must not exceed the cap");
    TEST_ASSERT_TRUE_MESSAGE(r.maxOut >= HOTLID_MAX_PWM - 1, "should saturate at the cap while far below");
}

// ===========================================================================
// Group 2 — deterministic control-logic equivalence (exact firmware branches)
// ===========================================================================

// Mirror of Maintain2_67() decision (src/PIDControl.cpp:1668-1688).
enum BottomDecision { DEC_OVERHEAT, DEC_UNDERHEAT, DEC_PID };
static BottomDecision bottomDecision(double temp)
{
    if (temp > AMPLIF_TEMP + BOTTOM_OVERHEAT)       // > 70.8 -> overheat (stop+rerun)
        return DEC_OVERHEAT;
    else if (temp < AMPLIF_TEMP - BOTTOM_OVERHEAT)  // < 60.8 -> underheat (stop+rerun)
        return DEC_UNDERHEAT;
    return DEC_PID;                                 // otherwise PID drives the heater
}

static void test_bottom_heater_overheat_underheat_boundaries(void)
{
    // overheat boundary at 65.8 + 5 = 70.8
    TEST_ASSERT_EQUAL(DEC_OVERHEAT, bottomDecision(70.9));
    TEST_ASSERT_EQUAL(DEC_PID, bottomDecision(70.0));
    // underheat boundary at 65.8 - 5 = 60.8
    TEST_ASSERT_EQUAL(DEC_UNDERHEAT, bottomDecision(60.7));
    TEST_ASSERT_EQUAL(DEC_PID, bottomDecision(61.0));
    // on target
    TEST_ASSERT_EQUAL(DEC_PID, bottomDecision(AMPLIF_TEMP));
}

// Mirror of the hotlid overheat cut-off (src/PIDControl.cpp:1431/1450/1540/1560):
// "if (temp > HOTLID23_TEMP + 10) PWM_OFF; else Compute()".
static bool hotlidCutoff(double temp) { return temp > HOTLID_TEMP + HOTLID_CUTOFF_DELTA; }

static void test_hotlid_overheat_cutoff_boundary(void)
{
    TEST_ASSERT_FALSE(hotlidCutoff(85.0));  // exactly +10 is NOT > +10 -> still PID
    TEST_ASSERT_TRUE(hotlidCutoff(85.1));   // above -> cut PWM off
    TEST_ASSERT_TRUE(hotlidCutoff(90.0));
    TEST_ASSERT_FALSE(hotlidCutoff(HOTLID_TEMP)); // on target -> PID
}

// Mirror of the hotlid ready flag (src/PIDControl.cpp:1441/1460): the flag is set
// in the non-overheat branch when temp >= HOTLID23_TEMP - 3, i.e. ready over [72, 85].
static bool hotlidReady(double temp)
{
    if (hotlidCutoff(temp)) return false;             // overheat branch never sets the flag
    return temp >= HOTLID_TEMP - HOTLID_READY_DELTA;  // >= 72
}

static void test_hotlid_ready_flag_threshold(void)
{
    TEST_ASSERT_FALSE(hotlidReady(71.9)); // just below 72
    TEST_ASSERT_TRUE(hotlidReady(72.0));  // at the ready threshold
    TEST_ASSERT_TRUE(hotlidReady(75.0));  // at target
    TEST_ASSERT_TRUE(hotlidReady(85.0));  // still ready (not overheating)
    TEST_ASSERT_FALSE(hotlidReady(85.1)); // overheat -> not ready
}

// ===========================================================================
// Unity runner
// ===========================================================================
void setup()
{
    delay(2000);
    UNITY_BEGIN();

    // Group 1 — closed loop with real gains
    RUN_TEST(test_amplification_pid_converges);
    RUN_TEST(test_hotlid_pid_heats_to_ready);
    RUN_TEST(test_lysis_pid_converges);
    RUN_TEST(test_pid_direction_direct);
    RUN_TEST(test_pid_output_clamped_and_antiwindup);
    RUN_TEST(test_hotlid_output_limit_caps_pwm);

    // Group 2 — deterministic control-logic equivalence
    RUN_TEST(test_bottom_heater_overheat_underheat_boundaries);
    RUN_TEST(test_hotlid_overheat_cutoff_boundary);
    RUN_TEST(test_hotlid_ready_flag_threshold);

    UNITY_END();
}

void loop() {}
