#include "AlgoData.h"
#include <iostream>
#include <vector>

#ifndef ALGO_H
#define ALGO_H

size_t find_crossing_higher_than(const std::vector<double> &_array, double crossing, int start_index);

size_t find_crossing_lower_than(const std::vector<double> &_array, double crossing, int start_index);

size_t find_crossing_lower_than_reversed(const std::vector<double> &_array, double crossing, int start_index, int end_index);

float mean(const std::vector<double> &vec, uint8_t startIndex, uint8_t endIndex);

void smooth(const std::vector<double> &_raw, std::vector<double> &_smoothed, uint8_t window, uint8_t order);

void baseline(const std::vector<double> &time_data, const std::vector<double> &raw_data, std::vector<double> &baselinedData, uint8_t baseline_start, uint8_t baseline_range);

void post_process_curve(Record &record, uint8_t baseline_start, uint8_t baseline_range, uint8_t sg_window, uint8_t sg_order);

void differentiate(const std::vector<double> &x_array, const std::vector<double> &y_array, std::vector<double> &differential_array);

size_t argmax(std::vector<double> &_vector, size_t startIndex);

size_t check_breakData(std::vector<double> &_array, double crossing, int start_index);

size_t check_risingData(std::vector<double> &_array, int start_index, int window);

void find_sigmoidal_feature(Record &record, DiagnosticParameters &parameters);
void predict_outcome(Record &record, DiagnosticParameters &parameters);

/* v2.4.3a - ADVISORY non-specific-amplification scoring. Nothing here feeds predict_outcome();
 * the two values are reported so a flagged Positive can be reviewed, and so the flags can later
 * be compared against confirmed farm or PCR outcomes. Thresholds come from 29,015 calibrated
 * curves, 1 Jan - 12 Aug 2026, prototype units excluded. Tuning any of them changes only which
 * results are FLAGGED, never which results are CALLED. */
#define NSA_MIN_SHARPNESS 15.0 // dF/dt at the main peak, below this the rise is shallow
#define NSA_LATE_CT       17.0 // transition time (min) at or after which onset counts as late
#define NSA_LOW_INCREASE  60.0 // calibrated fluorescence gain below which the gain is small
#define NSA_LONG_LAG       2.5 // peak-minus-Ct (min) above which the lag phase is drawn out
#define NSA_SLOW_PLATEAU   6.0 // plateau-minus-Ct (min) above which levelling off is slow
#define NSA_REVIEW_SCORE     4 // score at or above which a Positive is marked for review

uint8_t nsa_score(Record &record, DiagnosticParameters &parameters);
double arm_width_minutes(const Record &record);

/* ---------------------------------------------------------------------------
 * BREAK_JUMP_THRESHOLD - the single-reading step that counts as an electrical break.
 *
 * This used to BE parameters.min_increase: both call sites of check_breakData() passed the
 * amplification-size threshold in as the jump `crossing`. One number, two unrelated jobs, so
 * raising the size gate silently retuned the break detector - and not in one direction, because
 * checkJump() uses `crossing` TWICE with opposite senses:
 *     if (jump < crossing)       return false;   // higher threshold -> FEWER jumps found
 *     if (total_rise > crossing) return false;   // higher threshold -> MORE  jumps found
 * The net effect of a change cannot be reasoned out, only measured. So it is pinned here as a
 * compile-time constant instead: 20.0 is the value min_increase carried when the break detector
 * was last characterised, and it is deliberately NOT a runtime parameter - parastructure is at
 * its 402-byte ceiling (ForteSetting.cpp:229) and, more to the point, a detector this load-bearing
 * should not be reachable from a config file at all.
 *
 * Verified: re-scoring all 25,219 curves from 2026 with min_increase at 30 and this pinned at 20
 * reproduces the v2.4.3a Break set exactly - same curves, same indices, zero differences. */
#define BREAK_JUMP_THRESHOLD 20.0

/* ---------------------------------------------------------------------------
 * THE TWO-ARM SHAPE RULE IS GONE. Do not reintroduce it without new evidence.
 *
 * It was "share < 0.45 AND steepness < 15" (arm A) OR "rise width > 10 min AND increase < 45"
 * (arm B). Scored against 68 curves labelled by eye, arm A condemned ZERO real wells but caught
 * only 9 of 31 non-specific ones - and in every one of the 22 misses the steepness term was
 * already in the non-specific range while the SHARE term vetoed it. Five of those misses had a
 * share above 1.0, which happens when a curve ends below its own plateau: the denominator
 * shrinks and the ratio stops being a measurement at all.
 *
 * A sigmoid-fraction variant (how much of the climb the transition accounts for) was built to
 * replace share and is worse: it orders 45 of 88 real-vs-non-specific pairs backwards, where
 * chance is 44. Steepness orders 2 of 88. min_sharpness alone catches 15 of arm A's 22 misses.
 *
 * SHARE ITSELF IS GONE TOO (2026-08-16). Scored across all 666 real-vs-non-specific comparisons
 * in the 68-curve label set it ranks at chance, so emitting it was collecting noise. What is
 * emitted in its place is WINDOW_RATE - the largest mean climb over any 4-minute window - which
 * orders 14 of those 666 backwards against 23 for the instantaneous peak derivative. It decides
 * nothing either; it is the measurement that should set the next min_sharpness.
 *
 * WINDOW RATE and RISE WIDTH are MEASURED and REPORTED on every well - see DiagnosticOutcome.
 * They cost nothing to emit and they are the labelled data needed to set thresholds from
 * measurement rather than from eyeballing. They just do not decide anything. */
#define SHAPE_DERIV_FRACTION 0.35 // fraction of the peak derivative that still counts as "fast"
#define WINDOW_RATE_MIN      4.0  // minutes; the window whose mean climb window_rate() reports

/* ---------------------------------------------------------------------------
 * THE REVIEW GATE - what happens to a well the new thresholds remove.
 *
 * A well that would have been Positive under the thresholds v2.4.3 shipped with, but fails
 * min_increase or min_sharpness as they now stand, is not silently dropped. It is marked, and
 * what the mark BECOMES is the only difference between the two builds:
 *
 *   default             v2.4.3AT   reported "Flagged" (F) - a third state, repeat the sample
 *   SHAPE_RULE_NEGATIVE v2.4.3a    reported "Negative" (N)
 *
 * Both builds compute and report identical numbers - Ct, increase, steepness, share, rise width -
 * so a run from one compares line-for-line against the same curve scored by the other.
 *
 * The legacy values below are the v2.4.3 thresholds and exist ONLY to answer "would this have
 * been Positive before?". They are not a second classifier and must not be tuned. */
#define LEGACY_MIN_INCREASE  20.0
#define LEGACY_MIN_SHARPNESS  5.0

double window_rate(const Record &record, DiagnosticParameters &parameters);
double rise_width_minutes(const Record &record, DiagnosticParameters &parameters);
bool   removed_by_new_gate(const Record &record, DiagnosticParameters &parameters);

/* ---------------------------------------------------------------------------
 * CLIMB NEUTRALISATION - repair the curve, then let the ordinary scorer read it.
 *
 * The failure this exists for: RPL01004 slot 2, 24 July. The curve sits flat at 207.6, steps
 * +19.34 in ONE reading at 5.0 min, and sits flat at 229.3 for the remaining 25 minutes. There is
 * no amplification anywhere in it. checkJump() does not fire - 19.34 is 0.66 short of
 * BREAK_JUMP_THRESHOLD, and that floor is the only one of its five tests the step fails. Having
 * escaped the break path the step reaches the scorer, where Savitzky-Golay spreads one reading
 * across nine and turns the edge into a shoulder: increase 24.2, peak steepness 14.7. POSITIVE.
 *
 * Why REPAIR rather than truncate. check_breakData() returns an index and the caller cuts the
 * curve there, discarding the whole run. Neutralisation mends the discontinuity and lets the
 * scorer decide what is left: a well whose "amplification" WAS the artefact falls to Negative,
 * and a well with real amplification underneath keeps a readable result instead of a Break.
 *
 * Two kinds, told apart by how much of the move is still there afterwards rather than by the raw
 * step. Classifying on the raw step leaves a dead zone - a climb retaining 0.69 of itself is
 * neither a clean offset nor a clean spike and gets silently skipped.
 *   OFFSET  a level shift. Correct by the MEASURED shift, not the raw step.
 *   SPIKE   a transient. Interpolate across the whole excursion.
 *
 * Upward AND downward. checkJump's test 2 is `jump >= crossing` with crossing positive, so a
 * dropout is structurally invisible to it however large. 58 of 248 climbs in the fleet are
 * downward - a whole category the present detector cannot see.
 *
 * Measured over 25,219 curves: 237 (0.94%) contain a climb, 253 climbs applied, 112 calls change.
 * Of 39 step-shaped Positives, 28 become Negative. Of 47 wells reported Break, 19 keep a
 * readable result. 99.06% of the fleet is untouched.
 *
 * This runs UPSTREAM of check_breakData and does not replace it. BREAK_JUMP_THRESHOLD stays 20.0. */
#define CLIMB_MIN_STEP    8.0  // calibrated units; below this is ordinary wobble
#define CLIMB_NOISE_MULT  4.0  // the step must exceed this multiple of the local 8-point spread
#define CLIMB_PERSIST_LO  0.35 // fraction of the step still present after -> offset, else spike
#define CLIMB_SETTLE_TOL  0.25 // fraction of the shift within which the new level counts as settled
#define CLIMB_START_INDEX 7    // same lower bound as checkJump - optical warm-up is left alone
#define CLIMB_MAX_PASSES  6    // re-detection passes; 15 curves in the fleet carry more than one
/* Readings that must FOLLOW a step for it to be classified. Below this many, offset and spike
 * cannot be told apart and settling cannot be confirmed - so the tail is held flat instead.
 *
 * This closes a hole the 30-minute run opened. The scan used to stop at size-5 outright, which
 * left the last four transitions unreachable; at 40 minutes that region was dead time, at 30 it
 * is the end of the assay. The damage is not the step sitting in the data, it is that the step
 * BECOMES THE PEAK: find_sigmoidal_feature() searches the derivative to the end of the curve, the
 * spike wins, its height sets the fraction used to locate the transition, and the transition slides
 * to the end. Two textbook sigmoids in the 2026 fleet report Ct 28.0 and Slight Positive when the
 * reaction plateaued at 22 minutes; a third, flat apart from the spike, is called Slight Positive
 * off the artefact alone.
 *
 * Measured over 25,219 curves at 30 min: 641 carry a step over CLIMB_MIN_STEP in the last five
 * readings, 9 also clear the local-noise gate, 4 change call - and all four change for the better
 * (two S->P on clean sigmoids, one S->N on the spike-only curve). No systematic end-of-run
 * artefact exists: the median step is flat at ~1.05 RFU across the final ten readings.
 *
 * HOLD, do not truncate. raw_data and time_data are resized together in sensor6035.cpp and
 * shortening one desynchronises them; holding the level preserves length and scored identically
 * on all four curves. Nothing callable is lost - a rise starting this late has Ct past
 * min_slight_positive_time with no plateau behind it. */
#define CLIMB_TAIL_MIN    6

void neutralise_climbs(std::vector<double> &_array, uint8_t *applied);

const String strJson =
    {
        "{"
        "  \"parameters\": {"
        "    \"min_increase\": 5,"
        "    \"min_sharpness\": 2,"
        "    \"min_slight_positive_time\": 22.0,"
        "    \"detect_shape\": true,"
        "    \"detection_margin_time\": 6.0,"
        "    \"arm_percentile\": 0.5,"
        "    \"transition_percentile\": 0.2,"
        "    \"sg_order\": 0,"
        "    \"sg_window\": 2,"
        "    \"baseline_start\": 3,"
        "    \"baseline_range\": 4"
        "  },"
        "  \"time_data\": ["
        "    0.0,"
        "    1.0,"
        "    2.0,"
        "    3.0,"
        "    4.0,"
        "    5.0,"
        "    6.0,"
        "    7.0,"
        "    8.0,"
        "    9.0,"
        "    10.0,"
        "    11.0,"
        "    12.0,"
        "    13.0,"
        "    14.0,"
        "    15.0,"
        "    16.0,"
        "    17.0,"
        "    18.0,"
        "    19.0,"
        "    20.0,"
        "    21.0,"
        "    22.0,"
        "    23.0,"
        "    24.0,"
        "    25.0,"
        "    26.0,"
        "    27.0,"
        "    28.0,"
        "    29.0"
        "  ],"
        "  \"raw_data\": ["
        "    731.37,"
        "    749.02,"
        "    758.82,"
        "    750.98,"
        "    772.55,"
        "    762.75,"
        "    762.75,"
        "    762.75,"
        "    774.51,"
        "    768.63,"
        "    768.63,"
        "    768.63,"
        "    768.63,"
        "    770.59,"
        "    774.51,"
        "    772.55,"
        "    774.51,"
        "    782.35,"
        "    792.16,"
        "    815.69,"
        "    850.98,"
        "    923.53,"
        "    988.24,"
        "    1068.63,"
        "    1127.45,"
        "    1178.43,"
        "    1215.69,"
        "    1233.33,"
        "    1252.94,"
        "    1266.67"
        "  ]"
        "}"};
#endif