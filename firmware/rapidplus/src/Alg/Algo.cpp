#include <vector>
#include "AlgoData.h"
#include "Algo.h"
#include "sgsmooth.h"
#include "../ForteSetting.h"

/***********************************************************************
 * Function: find_crossing_higher_than()
 * Description: Scans _array forward from start_index and returns the index
 *  of the first element that is greater than or equal to 'crossing' (first
 *  upward threshold crossing).
 * pramameter: _array = data to search; crossing = threshold value;
 *  start_index = index to begin scanning from
 *  return: index of the first element >= crossing, or -1 if none found
 */
size_t find_crossing_higher_than(const std::vector<double> &_array, double crossing, int start_index)
{
    for (int index = start_index; index < _array.size(); ++index)
    {
        if (_array[index] >= crossing)
        {
            return index;
        }
    }
    return -1;
}

/***********************************************************************
 * Function: find_crossing_lower_than()
 * Description: Scans _array forward from start_index and returns the index
 *  of the first element that is less than or equal to 'crossing' (first
 *  downward threshold crossing).
 * pramameter: _array = data to search; crossing = threshold value;
 *  start_index = index to begin scanning from
 *  return: index of the first element <= crossing, or -1 if none found
 */
size_t find_crossing_lower_than(const std::vector<double> &_array, double crossing, int start_index)
{
    for (int index = start_index; index < _array.size(); ++index)
    {
        if (_array[index] <= crossing)
        {
            return index;
        }
    }
    return -1;
}

/***********************************************************************
 * Function: find_crossing_lower_than_reversed()
 * Description: Scans _array backward from start_index down to end_index and
 *  returns the index of the first element that is less than or equal to
 *  'crossing' (first downward crossing searching in reverse).
 * pramameter: _array = data to search; crossing = threshold value;
 *  start_index = index to begin the reverse scan; end_index = lowest index
 *  to scan to (default 0)
 *  return: index of the first element <= crossing, or -1 if none found
 */
size_t find_crossing_lower_than_reversed(const std::vector<double> &_array, double crossing, int start_index, int end_index = 0)
{
    for (int index = start_index; index >= end_index; --index)
    {
        if (_array[index] <= crossing)
        {
            return index;
        }
    }
    return -1;
}

/***********************************************************************
 * Function: mean()
 * Description: Computes the arithmetic mean of the elements of vec over the
 *  half-open index range [startIndex, endIndex), summing the values and
 *  dividing by the number of elements in the range.
 * pramameter: vec = data vector; startIndex = first index (inclusive);
 *  endIndex = end index (exclusive)
 *  return: average of the elements in the range, or 0.0 if vec is empty
 */
float mean(const std::vector<double> &vec, int startIndex, int endIndex)
{
    if (vec.empty())
    {
        return 0.0; // Return 0 if the vector is empty to avoid division by zero
    }

    float sum = 0.0;
    for (int i = startIndex; i < endIndex; i++)
    {
        sum += vec[i];
    }
    return (sum) / (double)(endIndex - startIndex); // Calculate average
}

/***********************************************************************
 * Function: smooth()
 * Description: Wrapper that Savitzky-Golay smooths the raw signal by
 *  calling sg_smooth with the given window and order, then assigns the
 *  resulting smoothed samples into the _smoothed output vector.
 * pramameter: _raw = raw input signal; _smoothed = output vector filled
 *  with the smoothed signal; window = SG half-window size; order = SG
 *  polynomial order
 *  return: none (result written to _smoothed)
 */
void smooth(const std::vector<double> &_raw, std::vector<double> &_smoothed, uint8_t window, uint8_t order)
{
    std::vector<double> _temp = sg_smooth(_raw, window, order);
    _smoothed.assign(_temp.begin(), _temp.end());
}

/***********************************************************************
 * Function: baseline()
 * Description: Baseline-corrects the fluorescence signal by finding the
 *  index range corresponding to the baselining time window (from
 *  baseline_start to baseline_start+baseline_range in time_data),
 *  computing the mean raw value over that window, and subtracting this
 *  baseline value from every sample of raw_data.
 * pramameter: time_data = time values; raw_data = fluorescence values;
 *  baselinedData = output baselined values; baseline_start = start time
 *  (min) for the baseline window; baseline_range = length (min) of the
 *  baseline window
 *  return: none (baseline-subtracted data written to baselinedData)
 */
void baseline(const std::vector<double> &time_data, const std::vector<double> &raw_data, std::vector<double> &baselinedData, uint8_t baseline_start, uint8_t baseline_range)
{
    /*
    :param time_data        time values
    :param raw_data         fluorescence values
    :param baselinedData    output with baselined data values
    :param baseline_start   starting point for baselining (min)
    :param baseline_range   range to use for baselining (min)
    */
    // find index of first crossing over discard time
    int discardIndex = find_crossing_higher_than(time_data, baseline_start, 0);
    // find index of baseline range
    int baselineStart = discardIndex;
    int baselineStop = find_crossing_higher_than(time_data, baseline_start + baseline_range, discardIndex) + 1;
    // calculate the average value over the baselining range
    double baselineValue = mean(raw_data, baselineStart, baselineStop);
    // subtract basline
    for (size_t i = 0; i < baselinedData.size(); i++)
    {
        baselinedData[i] = raw_data[i] - baselineValue;
    }
}

/***********************************************************************
 * Function: DiagnosticParameters::fromEEPROM()
 * Description: Loads all diagnostic thresholding parameters (min increase,
 *  sharpness, slight-positive time, shape detection, detection margin,
 *  percentiles, SG order/window, baseline start/range) into this struct
 *  from the persisted _ForteSetting.parameter values stored in EEPROM.
 * pramameter: none
 *  return: none (member fields populated from settings)
 */
void DiagnosticParameters::fromEEPROM()
{
    min_increase = _ForteSetting.parameter.min_increase;
    min_sharpness = _ForteSetting.parameter.min_sharpness;
    min_slight_positive_time = _ForteSetting.parameter.min_slight_positive_time;
    detect_shape = _ForteSetting.parameter.detect_shape;
    detection_margin_time = _ForteSetting.parameter.detection_margin_time;
    arm_percentile = _ForteSetting.parameter.arm_percentile;
    transition_percentile = _ForteSetting.parameter.transition_percentile;
    sg_order = _ForteSetting.parameter.sg_order;
    sg_window = _ForteSetting.parameter.sg_window;
    baseline_start = _ForteSetting.parameter.baseline_start;
    baseline_range = _ForteSetting.parameter.baseline_range;
}

/***********************************************************************
 * Function: post_process_curve()
 * Description: Pre-processes a measurement curve by clearing the record's
 *  processed_data, baselining the raw_data over the configured baseline
 *  window, and then Savitzky-Golay smoothing the baselined data into
 *  record.processed_data.
 * pramameter: record = record holding time/raw data and receiving the
 *  processed output; baseline_start = baseline start time (min);
 *  baseline_range = baseline window length (min); sg_window = SG smoothing
 *  half-window size; sg_order = SG interpolation order (0-3)
 *  return: none (record.processed_data populated)
 */
void post_process_curve(
    Record &record,
    uint8_t baseline_start,
    uint8_t baseline_range,
    uint8_t sg_window,
    uint8_t sg_order)
{
    /*
    Baselines and smoothes data using a Savitzky-Golay algorithm (3rd party library)
    record:         record object
    baseline_range:     length in time for which to perform baselining
    baseline_start:     length in time to avoid for baselining to prevent optical wamr-up to seep into baseline.
    sg_window:          Window size for smoothing. The bigger the more values will be considered for smoothing. window is 2m*1 where m is input value. Default is 4 for 1 acq. cycle/min.
    sg_order            Interpolation order for Savitzky-Golay filtering: 0-3 orders available.
    */
    // clear all contents of processed data and replace with zeros
    record.processed_data.clear();
    // make a new vector array for baselined data
    std::vector<double> baselinedData(record.time_data.size(), 0.0);
    // baseline
    baseline(record.time_data, record.raw_data, baselinedData, baseline_start, baseline_range);
    // Savitzky-golay filtering
    smooth(baselinedData, record.processed_data, sg_window, sg_order);
}

/***********************************************************************
 * Function: differentiate()
 * Description: Computes the numerical derivative dy/dx of the signal;
 *  uses a forward difference at the first point, a backward difference at
 *  the last point, and a centered difference for all interior points,
 *  appending each result to differential_array.
 * pramameter: x_array = time/x values; y_array = (processed) fluorescence/y
 *  values; differential_array = output derivative values (appended)
 *  return: none (derivatives pushed into differential_array)
 */
void differentiate(
    const std::vector<double> &x_array,
    const std::vector<double> &y_array,
    std::vector<double> &differential_array)
{
    /*
    diffferentiates data dy/dx
    x_array:            time data
    y_array:            fluorescence data (processed)
    differential_array: output
    */
    for (size_t i = 0; i < y_array.size(); ++i)
    {
        if (i == 0)
        {
            differential_array.push_back((y_array[i + 1] - y_array[i]) / (x_array[i + 1] - x_array[i]));
        }
        else if (i == y_array.size() - 1)
        {
            differential_array.push_back((y_array[i] - y_array[i - 1]) / (x_array[i] - x_array[i - 1]));
        }
        else
        {
            differential_array.push_back((y_array[i + 1] - y_array[i - 1]) / (x_array[i + 1] - x_array[i - 1]));
        }
    }
}

/***********************************************************************
 * Function: argmax()
 * Description: Finds the index of the maximum value in _vector scanning
 *  from startIndex to the end; tracks the running maximum (starting from
 *  0.0) and returns the index where it occurs.
 * pramameter: _vector = data to search; startIndex = index to begin
 *  searching from
 *  return: index of the maximum element, or -1 if none exceeds the initial
 *  maximum of 0.0
 */
size_t argmax(std::vector<double> &_vector, size_t startIndex)
{
    /*
    Finds the index of the maximum point
    */
    double max_y = 0.0;
    int max_i = -1;
    for (size_t i = startIndex; i < _vector.size(); i++)
    {
        if (_vector[i] > max_y)
        {
            max_y = _vector[i];
            max_i = i;
        }
    }
    return max_i;
}

/***********************************************************************
 * Function: mean_slope()
 * Description: Computes the average first difference (mean slope) of
 *  _array over a span of 'window' points starting just after 'start';
 *  sums consecutive differences _array[i+1]-_array[i] and divides by
 *  window-1.
 * pramameter: _array = data; start = starting index of the span; window =
 *  number of points spanning the slope estimate
 *  return: mean per-step slope over the window
 */
double mean_slope(std::vector<double> &_array, size_t start, size_t window)
{
    double sum = 0;
    size_t i = start + 1;
    for (i; i < (start + window); i++)
        sum += (_array[i + 1] - _array[i]);
    return sum / (window - 1);
}
// Biên độ dao động (max - min) của 'window' điểm bắt đầu tại 'start'.
// Dùng để phân biệt noise pre-window (range lớn) khỏi baseline ổn định (range nhỏ).
/***********************************************************************
 * Function: range_of()
 * Description: Computes the amplitude (max - min) of 'window' consecutive
 *  points of _array starting at 'start'; scans the window tracking the
 *  minimum and maximum and returns their difference. Used to distinguish
 *  noisy pre-window regions (large range) from a stable baseline (small
 *  range).
 * pramameter: _array = data; start = first index of the window; window =
 *  number of points in the window
 *  return: max minus min over the window, or 0.0 if the window exceeds the
 *  array bounds
 */
double range_of(std::vector<double> &_array, size_t start, size_t window)
{
    if (start + window > _array.size())
        return 0.0;
    double mn = _array[start];
    double mx = _array[start];
    for (size_t i = start; i < start + window; i++)
    {
        if (_array[i] < mn)
            mn = _array[i];
        if (_array[i] > mx)
            mx = _array[i];
    }
    return mx - mn;
}

/***********************************************************************
 * Function: checkJump()
 * Description: Detects a single sudden step (jump) at position 'index' in
 *  _array; requires the one-step rise _array[index+1]-_array[index] to
 *  exceed 'crossing', to be at least 2.5x the amplitude of the preceding
 *  8-point window (range_of guard against noise), to sit on a near-flat
 *  region (|mean_slope| over 6 points <= 1.0), and to not keep rising past
 *  the threshold over the next 6 points, marking a genuine isolated jump.
 * pramameter: _array = signal; crossing = minimum jump magnitude / rise
 *  threshold; index = position to test for a jump
 *  return: true if a qualifying jump is detected at index, false otherwise
 *  (including out-of-range positions)
 */
// After a genuine step the signal often overshoots/settles for a few samples
// before flattening, so the flatness check skips JUMP_SETTLE_SKIP samples right
// after the jump and measures the slope over the SETTLED window. This lets a real
// step that decays into a flat level still be recognized as a break (e.g. a
// 294->360 step that settles back to ~345), while a sustained amplification ramp
// still fails the slope/total_rise guards.
#define JUMP_SETTLE_SKIP 3 // post-jump settling samples to skip before the flatness check
#define JUMP_FLAT_WINDOW 6 // window length for the flatness (mean-slope) check

bool checkJump(std::vector<double> &_array, double crossing, size_t index)
{
    // Bounds: range_of(index-7, 8) needs index >= 7 (otherwise size_t underflows
    // to an out-of-bounds read); mean_slope(index+skip, window) reads up to
    // _array[index + skip + window], so guard the upper end too.
    if ((index < 7) || ((index + JUMP_SETTLE_SKIP + JUMP_FLAT_WINDOW) >= _array.size()))
    {
        return false;
    }
    double jump = _array[index + 1] - _array[index];
    if (jump < crossing)
        return false;

    // GUARD: jump phải vượt trội so với biên độ pre-window (8 điểm trước, x2.5)
    //
    // MERGE 2.4.4 + 2.4.3AT: this factor is 2.5, the v2.4.3AT value, NOT the 2.2 that v2.4.4
    // carries. The two numbers were tuned against two different break pipelines and are not
    // interchangeable. v2.4.4's 2.2 was picked to rescue RPL03008#9 on the OLD path, where the
    // threshold was still divided by FORTE_SLOPES[i] and no climb repair ran; v2.4.3AT then
    // removed that division, pinned the size gate at BREAK_JUMP_THRESHOLD and put
    // neutralise_climbs() upstream of check_breakData - so the curve this factor now judges is
    // not the curve 2.2 was measured on, and the case it was lowered for no longer arrives here
    // the same way. Keeping 2.2 would ship a combination neither branch ever measured.
    // Re-measure before changing it: tools/test_algo_accuracy.py plus the 726-channel label set
    // are the only thing that can settle it, and both are currently missing from the repo.
    double pre_range = range_of(_array, index - 7, 8);
    if (jump < pre_range * 2.5)
        return false;

    // Flatness measured AFTER the settling transient (skip JUMP_SETTLE_SKIP samples).
    double slope = mean_slope(_array, index + JUMP_SETTLE_SKIP, JUMP_FLAT_WINDOW);
    if (fabs(slope) > 1.0)
        return false;
    // v2.4.3a: was int. The operands are double and the comparison is against a double
    // threshold, so truncating toward zero let a rise of e.g. 20.9 test as 20 and pass a
    // crossing of 20 - the guard rejected genuine breaks it was meant to catch.
    double total_rise = _array[index + 6] - _array[index + 1];
    if (total_rise > crossing)
        return false;

    return true;
}

/***********************************************************************
 * Function: is_rising_trend()
 * Description: Determines whether _data shows a consistent upward trend
 *  over 'window' steps starting at 'start'; counts positive consecutive
 *  differences and accumulates the total rise, requiring nearly all steps
 *  to be positive (>= window-1) and the net rise to be greater than zero.
 * pramameter: data = signal; start = starting index of the trend window;
 *  window = number of steps to evaluate
 *  return: true if the window forms a rising trend, false otherwise
 *  (including out-of-range windows)
 */
bool is_rising_trend(const std::vector<double> &data,
                     int start,
                     int window)
{
    if (start < 0 || start + window >= (int)data.size())
        return false;

    int positive_count = 0;
    double total_rise = 0.0;

    for (int i = start + 1; i <= start + window; i++)
    {
        double diff = data[i] - data[i - 1];
        total_rise += diff;

        if (diff > 0)
            positive_count++;
    }

    if (positive_count < window - 1)
        return false;
    if (total_rise <= 0)
        return false;

    return true;
}

/***********************************************************************
 * Function: check_breakData()
 * Description: Scans _array forward from start_index and returns the index
 *  of the first sample where checkJump reports a sudden step/break of at
 *  least 'crossing'; used to locate an abrupt discontinuity in the signal.
 * pramameter: _array = signal to scan; crossing = jump threshold passed to
 *  checkJump; start_index = index to begin scanning from
 *  return: index of the first detected jump, or 0 if none is found
 */
size_t check_breakData(std::vector<double> &_array, double crossing, int start_index)
{
    size_t indexBreak = 0;
    for (size_t i = start_index; i < _array.size(); i++)
    {
        if (checkJump(_array, crossing, i))
        {
            /* code */
            indexBreak = i;
            return indexBreak;
        }
    }
    return 0;
}

/***********************************************************************
 * Function: window_median()
 * Description: Median of _array over the half-open index range [lo, hi).
 *  Median, not mean, because the thing being measured is the level on either
 *  side of a discontinuity and a mean would drag that level toward the very
 *  step it is trying to measure across. Copies at most 8 values and insertion
 *  sorts them - at this call rate that is cheaper on an ESP32 than anything
 *  cleverer, and it allocates nothing.
 * pramameter: _array = data; lo = first index; hi = one past the last
 *  return: median, or 0.0 for an empty or out-of-range span
 */
static double window_median(const std::vector<double> &_array, size_t lo, size_t hi)
{
    if (hi > _array.size())
        hi = _array.size();
    if (lo >= hi)
        return 0.0;
    double buf[8];
    size_t n = 0;
    for (size_t i = lo; i < hi && n < 8; i++)
        buf[n++] = _array[i];
    for (size_t i = 1; i < n; i++)
    {
        double v = buf[i];
        size_t j = i;
        while (j > 0 && buf[j - 1] > v)
        {
            buf[j] = buf[j - 1];
            j--;
        }
        buf[j] = v;
    }
    return (n & 1) ? buf[n / 2] : 0.5 * (buf[n / 2 - 1] + buf[n / 2]);
}

/***********************************************************************
 * Function: max_abs_step()
 * Description: The largest single-reading change anywhere in the curve. Used
 *  as the safety invariant: a repair must never leave a bigger discontinuity
 *  than it found.
 *
 *  Deliberately NOT the curve's range. Removing a downward artefact legitimately
 *  INCREASES range - RPL02019 slot 2 rises 212 to 251, drops 43, then keeps
 *  rising, and taking the drop out reveals one continuous 82-unit rise. A range
 *  check flags that correct repair as a failure; the step check does not.
 * pramameter: _array = data
 *  return: max |a[i+1] - a[i]|, or 0.0 for fewer than two points
 */
static double max_abs_step(const std::vector<double> &_array)
{
    double m = 0.0;
    for (size_t i = 0; i + 1 < _array.size(); i++)
    {
        double d = _array[i + 1] - _array[i];
        if (d < 0)
            d = -d;
        if (d > m)
            m = d;
    }
    return m;
}

/***********************************************************************
 * Function: find_first_climb()
 * Description: Scans for the first vertical climb - a single-reading change
 *  that is both large in absolute terms and large against the local noise -
 *  and classifies it as a level shift or a transient.
 * pramameter: _array = data to scan; index = receives the position;
 *  amount = receives the MEASURED level shift for an offset (the raw step for
 *  a spike or a tail step); kind = receives the classification
 *  return: true if a climb was found
 */
enum ClimbKind
{
    CLIMB_KIND_OFFSET = 0, /* a level shift - correct by the measured amount */
    CLIMB_KIND_SPIKE,      /* a transient - interpolate across the excursion */
    CLIMB_KIND_TAIL        /* too close to the end to classify - hold the level */
};

static bool find_first_climb(const std::vector<double> &_array, size_t *index,
                             double *amount, uint8_t *kind)
{
    if (_array.size() < (size_t)CLIMB_START_INDEX + 10)
        return false;

    /* Scan to the LAST transition, not to size-5. See CLIMB_TAIL_MIN in Algo.h: stopping short
     * left the final four transitions unreachable, and at a 30-minute run that is the end of the
     * assay rather than dead time. */
    for (size_t i = CLIMB_START_INDEX; i + 1 < _array.size(); i++)
    {
        double step = _array[i + 1] - _array[i];
        double mag = (step < 0) ? -step : step;
        if (mag < CLIMB_MIN_STEP)
            continue;
        /* range_of takes a non-const reference, so measure the local spread here rather than
         * casting the constness away. Same window checkJump uses: the 8 readings ending at i. */
        double mn = _array[i - CLIMB_START_INDEX], mx = mn;
        for (size_t k = i - CLIMB_START_INDEX; k <= i; k++)
        {
            if (_array[k] < mn) mn = _array[k];
            if (_array[k] > mx) mx = _array[k];
        }
        if (mag < CLIMB_NOISE_MULT * (mx - mn))
            continue;

        *index = i;

        /* Not enough readings after the step to measure a settled level or to watch an excursion
         * come back. Classifying anyway would be guessing on two or three noisy points. */
        if (i + CLIMB_TAIL_MIN >= _array.size())
        {
            *kind = CLIMB_KIND_TAIL;
            *amount = step;
            return true;
        }

        /* How much of the move is still there once things settle? b0 is the level before,
         * b1 the level after the settling transient. */
        double b0 = window_median(_array, i - 5, i + 1);
        double b1 = window_median(_array, i + 3, i + 9);
        double lvl = b1 - b0;
        double lvlMag = (lvl < 0) ? -lvl : lvl;

        if (lvlMag >= CLIMB_PERSIST_LO * mag)
        {
            *kind = CLIMB_KIND_OFFSET;
            *amount = lvl; /* correct by the MEASURED shift, never by the raw step */
        }
        else
        {
            *kind = CLIMB_KIND_SPIKE;
            *amount = step;
        }
        return true;
    }
    return false;
}

/***********************************************************************
 * Function: neutralise_climbs()
 * Description: Repairs vertical climbs in place, one at a time, re-detecting
 *  on the repaired curve after each. See the note on CLIMB_* in Algo.h for what
 *  this is for and why it is a repair rather than a truncation.
 *
 *  Re-detection matters: level shifts for later climbs must be measured on the
 *  curve as it now stands. Reusing values computed on the original made
 *  multi-climb curves worse, not better - RPL02019 slot 2 went from range 46
 *  to 84.
 *
 *  The whole pass is reverted if it would leave a bigger single-reading
 *  discontinuity than it found. That cannot happen on any curve in the 2026
 *  dataset (0 of 237), but the check costs one array copy and the alternative
 *  is a silent corruption of a sample.
 * pramameter: _array = calibrated curve, repaired in place; applied = receives
 *  how many climbs were repaired (may be NULL)
 *  return: none
 */
void neutralise_climbs(std::vector<double> &_array, uint8_t *applied)
{
    if (applied)
        *applied = 0;
    if (_array.size() < (size_t)CLIMB_START_INDEX + 10)
        return;

    const std::vector<double> original = _array;
    const double stepBefore = max_abs_step(original);
    uint8_t n = 0;

    for (uint8_t pass = 0; pass < CLIMB_MAX_PASSES; pass++)
    {
        size_t i = 0;
        double amount = 0.0;
        uint8_t kind = CLIMB_KIND_OFFSET;
        if (!find_first_climb(_array, &i, &amount, &kind))
            break;

        if (kind == CLIMB_KIND_TAIL)
        {
            /* TAIL. Hold the last level the curve was known to be at. Whatever this step is -
             * offset, transient, or a reaction starting far too late to be callable - there is
             * no measurement left in the readings after it, and leaving it in place hands the
             * derivative search a peak that outranks the real one. */
            for (size_t k = i + 1; k < _array.size(); k++)
                _array[k] = _array[i];
        }
        else if (kind == CLIMB_KIND_OFFSET)
        {
            /* OFFSET. Do NOT shift from i+1: the readings between the old level and the settled
             * new one are a transient, possibly a dropout, and applying the correction to them
             * invents an excursion that was never recorded. RPL02020 slot 1 goes 133 -> 35 -> 265;
             * applying the +132 correction from the dropout drove it to -97. Same idea as the
             * existing JUMP_SETTLE_SKIP. */
            double b1 = window_median(_array, i + 3, i + 9);
            double lvlMag = (amount < 0) ? -amount : amount;
            size_t j = i + 1;
            while (j < _array.size())
            {
                double d = _array[j] - b1;
                if (d < 0) d = -d;
                if (d <= CLIMB_SETTLE_TOL * lvlMag)
                    break;
                j++;
            }
            if (j >= _array.size())
                break; /* never settles - not a level shift after all; leave the curve alone */

            for (size_t k = j; k < _array.size(); k++)
                _array[k] -= amount;

            /* bridge the settling transient linearly between the two now-aligned endpoints */
            if (j > i + 1)
            {
                double y0 = _array[i], y1 = _array[j];
                size_t span = j - i;
                for (size_t k = i + 1; k < j; k++)
                    _array[k] = y0 + (y1 - y0) * (double)(k - i) / (double)span;
            }
        }
        else
        {
            /* SPIKE. Walk forward while the curve is still displaced by more than half the step,
             * so the whole excursion is spanned rather than just its first reading. */
            double mag = (amount < 0) ? -amount : amount;
            size_t k = i + 1;
            while (k < _array.size())
            {
                double d = _array[k] - _array[i];
                if (d < 0) d = -d;
                if (d <= 0.5 * mag)
                    break;
                k++;
            }
            if (k >= _array.size())
                break; /* runs to the end - a level shift misread as a spike; leave it */

            double y0 = _array[i], y1 = _array[k];
            size_t span = k - i;
            for (size_t m = i + 1; m < k; m++)
                _array[m] = y0 + (y1 - y0) * (double)(m - i) / (double)span;
        }
        n++;
    }

    if (n && max_abs_step(_array) > stepBefore)
    {
        _array = original; /* a repair must never make the discontinuity worse */
        n = 0;
    }
    if (applied)
        *applied = n;
}

/***********************************************************************
 * Function: check_risingData()
 * Description: Scans _array forward from start_index and returns the index
 *  of the first position where is_rising_trend reports a consistent upward
 *  trend over the given window; used to locate the onset of a sustained
 *  rise in the signal.
 * pramameter: _array = signal to scan; start_index = index to begin
 *  scanning from; window = trend window length passed to is_rising_trend
 *  return: index where a rising trend begins, or 0 if none is found
 */
size_t check_risingData(std::vector<double> &_array, int start_index, int window)
{
    size_t indexRising = 0;
    for (size_t i = start_index; i < _array.size(); i++)
    {
        if (is_rising_trend(_array, i, window))
        {
            indexRising = i;
            return indexRising;
        }
    }
    return 0;
}

/***********************************************************************
 * Function: find_sigmoidal_feature()
 * Description: Locates the sigmoidal/exponential amplification feature on
 *  the differential curve; finds the global maximum (main peak) of
 *  differential_data after the detection-margin time, then locates the
 *  left and right arm crossings where the differential falls below
 *  arm_percentile of the peak (left arm searched in reverse, right arm
 *  forward), storing each feature's index/time/value into
 *  record.peak_features. Returns early if no peak is found.
 * pramameter: record = record holding time/differential data and receiving
 *  the detected peak features; parameters = diagnostic thresholds
 *  (detection_margin_time, arm_percentile, ...)
 *  return: none (record.peak_features populated)
 */
void find_sigmoidal_feature(Record &record, DiagnosticParameters &parameters)
{
    /*
    Finds a sharp increase in fluorescence, which would likely be the exponential phase of amplification.
    :param record               RECORD OBJECT
    :param parameters:      structure defining the diagnostic thresholding parameters
    :return: void
    */

    // find the highest peak after discard time in minutes
    int discard_index = find_crossing_higher_than(record.time_data, parameters.detection_margin_time, 0);
    // find the global maximum after the detection margin time in minutes
    record.peak_features.main_peak.i = argmax(record.differential_data, discard_index);
    // if no peak found, return result in a default state
    if (record.peak_features.main_peak.i == -1)
    {
        return;
    }
    record.peak_features.main_peak.x = record.time_data[record.peak_features.main_peak.i];
    record.peak_features.main_peak.y = record.differential_data[record.peak_features.main_peak.i];
    // find the percentile crossing in the left arm of the gaussian peak (if none, it returns -1)
    record.peak_features.left_arm.i = find_crossing_lower_than_reversed(record.differential_data,
                                                                        record.peak_features.main_peak.y * parameters.arm_percentile,
                                                                        record.peak_features.main_peak.i,
                                                                        discard_index - 1);
    if (record.peak_features.left_arm.i != -1)
    {
        record.peak_features.left_arm.x = record.time_data[record.peak_features.left_arm.i];
        record.peak_features.left_arm.y = record.differential_data[record.peak_features.left_arm.i];
    }

    // find the percentile crossing in the right arm of the gaussian peak (if none, it returns -1)
    record.peak_features.right_arm.i = find_crossing_lower_than(record.differential_data,
                                                                record.peak_features.main_peak.y * parameters.arm_percentile,
                                                                record.peak_features.main_peak.i);
    if (record.peak_features.right_arm.i != -1)
    {
        record.peak_features.right_arm.x = record.time_data[record.peak_features.right_arm.i];
        record.peak_features.right_arm.y = record.differential_data[record.peak_features.right_arm.i];
    }
    return;
}

/***********************************************************************
 * Function: predict_outcome()
 * Description: Classifies an amplification curve into a diagnostic outcome.
 *  Defaults to Negative; if a peak was detected it finds the transition
 *  time (Ct) by reverse-searching for a transition_percentile crossing
 *  (retrying with a 1.1x-growing threshold up to 12 times), determines the
 *  plateau point, and computes the fluorescence increase. It then applies
 *  threshold tests (min_increase, min_sharpness, detection_margin_time,
 *  shape/EA detection) to assign Positive, SlightPositive, Error, or
 *  Negative, writing results into record.outcome.
 * pramameter: record = record with peak features and processed/differential
 *  data, receiving the outcome; parameters = thresholding parameters for
 *  amplification detection
 *  return: none (record.outcome populated with the predicted result)
 */
static void predict_outcome_core(Record &record, DiagnosticParameters &parameters)
{
    /*
    function calculating whether an amplification curve has amplified or not
    :param record:          record object
    :param parameters:      structure containing the thresholding parameters for amplficiation detection
    */

    double thresholdIncreaseRate = 1.0;
    size_t ctCount = 0;

    // set outcome to negative as default
    strcpy(record.outcome.outcome, OutcomeNegative);
    // Test 1: If no peak found, then return Negative
    if (!record.peak_features.detected_peak())
    {
        return;
    }

    for (size_t i = 0; i < 12; i++)
    {
        double crossing = record.peak_features.main_peak.y * parameters.transition_percentile * thresholdIncreaseRate;
        // calculate transition time ("Ct value") at increase transition_percentile values until finding a value
        record.outcome.transition_time.i = find_crossing_lower_than_reversed(record.differential_data,
                                                                             crossing,
                                                                             record.peak_features.main_peak.i);
        // exit the loop if transition found. increase transition_percentile value and try again if not found
        if (record.outcome.transition_time.i != -1)
            break;

        thresholdIncreaseRate *= 1.1;
    }
    // if still no point found, then assign to first point (really rare occurrence)
    if (record.outcome.transition_time.i == -1)
    {
        record.outcome.transition_time.i = 0;
    }

    record.outcome.transition_time.x = record.time_data[record.outcome.transition_time.i];
    record.outcome.transition_time.y = record.processed_data[record.outcome.transition_time.i];

    // find plateau value
    if (record.peak_features.right_arm.i != -1)
    {
        record.outcome.plateau_point.i = find_crossing_lower_than(
            record.differential_data,
            record.peak_features.main_peak.y * parameters.transition_percentile,
            record.peak_features.main_peak.i);
        // return last index of array if point not found
        if (record.outcome.plateau_point.i == -1)
        {
            record.outcome.plateau_point.i = record.processed_data.size() - 1;
        }
    }
    // if peak is at last point of array, then get this point as plateau
    else
    {
        record.outcome.plateau_point.i = record.processed_data.size() - 1;
    }
    record.outcome.plateau_point.y = record.processed_data[record.outcome.plateau_point.i];
    record.outcome.plateau_point.x = record.time_data[record.outcome.plateau_point.i];
    // calculate increase
    record.outcome.increase = record.outcome.plateau_point.y - record.outcome.transition_time.y;

    // Test 2: Check for fluorescence increase above threshold
    if (record.outcome.increase > parameters.min_increase)
    { // check for fluorescence increase
        // test 3: Test for min sharpness
        if (record.peak_features.main_peak.y > parameters.min_sharpness)
        { // check for main peak having min steepness
            /* v2.4.5at (plan Doi 2): shape evidence is weighed BEFORE the early-Ct gate.
             *
             * The early-Ct test used to sit at the top of this chain, so it vetoed wells the
             * shape test would have accepted. find_sigmoidal_feature() already forces the peak
             * past discard_index and clamps the left arm there, but transition_time is found by
             * find_crossing_lower_than_reversed() with NO lower bound - so a wide, early rise can
             * hold a valid left_arm (detected_shape() true) while its 40%-of-peak crossing lands
             * just before the margin. That well has a recognisable lag phase, enough increase and
             * enough steepness, and was still called '!'. Shape evidence is the stronger claim;
             * the ordering was inverted. */
            if (parameters.detect_shape == false)
            { // if not using shape detection, give positive
                strcpy(record.outcome.outcome, OutcomePositive);
            }
            else if (record.peak_features.detected_shape())
            { // if using shape detected, check if the shape is right
                strcpy(record.outcome.outcome, OutcomePositive);
            }
            else if (record.peak_features.detected_ea())
            {
                strcpy(record.outcome.outcome, OutcomeError);
            }

            /* Ct policy, applied to the Positive just assigned rather than gating it.
             *
             * Both marks live INSIDE the min_sharpness block on purpose. The Slight-Positive
             * demotion used to sit outside it, which was harmless only because nothing else
             * assigned Positive; with a second Positive-assigning path above, a demotion that
             * can see outcomes this block did not set is a trap.
             *
             * MIN_CALLABLE_CT (4.0) and min_slight_positive_time (22.0) are mutually exclusive,
             * so the order between them cannot change a result. */
            if (strcmp(record.outcome.outcome, OutcomePositive) == 0)
            {
                if (record.outcome.transition_time.x < MIN_CALLABLE_CT)
                {
                    /* Real reaction, unreportable number. F is exactly this state - "the machine
                     * will not vouch for this well, repeat the sample" - and E is returned to
                     * meaning "the analysis could not run". */
                    strcpy(record.outcome.outcome, OutcomeFlagged);
                    record.outcome.shape_flag = SHAPE_FLAG_EARLY_RISE;
                }
                else if (record.outcome.transition_time.x >= parameters.min_slight_positive_time)
                {
                    strcpy(record.outcome.outcome, OutcomeSlightPositive);
                }
            }
        }
        return;
    }
}

/***********************************************************************
 * Function: arm_width_minutes()
 * Description: Width of the rise in minutes, measured between the two points
 *  where dF/dt falls to arm_percentile of the main peak. ADVISORY ONLY - the
 *  value is reported, never tested against a threshold, so it cannot change a
 *  call. Interpretation from 29,015 calibrated curves (Jan-Aug 2026): under
 *  1 min is a sensor transient rather than a reaction; over 8 min is a slow
 *  non-specific rise. Meaningful only at arm_percentile 0.5 - at 0.9 the
 *  measurement took just 15 distinct values across 4,000 curves.
 * pramameter: record = record whose peak features have been detected
 *  return: width in minutes, or -1.0 if either arm was not found
 */
double arm_width_minutes(const Record &record)
{
    const FeatureDetection &pk = record.peak_features;
    if (pk.left_arm.i == -1 || pk.right_arm.i == -1)
        return -1.0;
    return pk.right_arm.x - pk.left_arm.x;
}

/***********************************************************************
 * Function: nsa_score()
 * Description: Counts how many of six non-specific-amplification traits a
 *  curve shows. ADVISORY ONLY - nothing in predict_outcome() reads it, so it
 *  cannot change a call. A score of NSA_REVIEW_SCORE or more marks a Positive
 *  as worth a second look; measured on 4,354 in-scope Positives it flags 9.5%
 *  and misfires on 0.24% of the strongest results. Returns 0 when no peak was
 *  detected, because the outcome fields it reads are unset in that case and a
 *  Negative needs no review flag.
 * pramameter: record = record with outcome and peak features populated;
 *  parameters = thresholding parameters (reserved for per-site thresholds)
 *  return: 0-6
 */
uint8_t nsa_score(Record &record, DiagnosticParameters &parameters)
{
    (void)parameters; /* reserved: per-site thresholds will be read from here */
    const FeatureDetection &pk = record.peak_features;
    const DiagnosticOutcome &oc = record.outcome;

    if (pk.main_peak.i == -1)
        return 0;

    uint8_t score = 0;
    if (pk.main_peak.y < NSA_MIN_SHARPNESS)                             score++; // shallow rise
    if (oc.transition_time.x >= NSA_LATE_CT)                            score++; // late onset
    if (oc.increase < NSA_LOW_INCREASE)                                 score++; // small gain
    if ((pk.main_peak.x - oc.transition_time.x) > NSA_LONG_LAG)         score++; // drawn-out lag
    if ((oc.plateau_point.x - oc.transition_time.x) > NSA_SLOW_PLATEAU) score++; // slow to level off
    if (pk.right_arm.i == -1)                                           score++; // never came off the peak
    return score;
}

/***********************************************************************
 * Function: window_rate()
 * Description: The fastest sustained climb the curve manages - the largest mean
 *  gain across any WINDOW_RATE_MIN-minute window after the detection margin,
 *  expressed in calibrated units per minute.
 *
 *  This is the same quantity min_sharpness measures, read over a window instead
 *  of at a single point, and it ranks better: against the 68 eye-labelled curves
 *  it orders 14 of 666 real-vs-non-specific comparisons backwards, where the
 *  instantaneous peak derivative orders 23 and chance is 333. A slow reaction
 *  with a large total gain scores here where an instantaneous peak under-reads
 *  it, which is exactly the population min_sharpness is most likely to cut.
 *
 *  ADVISORY. Nothing reads it. It replaced `share` (counted gain / total climb),
 *  which was emitted for the same reason and turned out to rank at chance - see
 *  the note in Algo.h. Reported on every well so the next revision of
 *  min_sharpness can be set from measurement rather than from one labelled well.
 * pramameter: record = record with processed and time data populated;
 *  parameters = thresholding parameters (detection_margin_time)
 *  return: RFU per minute, or -1.0 when the curve is too short to measure
 */
double window_rate(const Record &record, DiagnosticParameters &parameters)
{
    const std::vector<double> &p = record.processed_data;
    const std::vector<double> &t = record.time_data;
    if (p.size() < 4 || t.size() != p.size())
        return -1.0;

    /* Readings per window, taken from the curve's own sampling interval rather than assumed:
     * timePerLoop is a runtime parameter and a unit set to a different round length must not
     * silently report a rate over a different span. */
    double dt = t[1] - t[0];
    if (dt <= 0.0)
        return -1.0;
    size_t n = (size_t)(WINDOW_RATE_MIN / dt + 0.5);
    if (n == 0 || n >= p.size())
        return -1.0;

    size_t di = 0;
    while (di < t.size() && t[di] < parameters.detection_margin_time)
        di++;
    if (di + n >= p.size())
        return -1.0;

    double best = 0.0;
    for (size_t i = di; i + n < p.size(); i++)
    {
        double r = (p[i + n] - p[i]) / WINDOW_RATE_MIN;
        if (r > best)
            best = r;
    }
    return best;
}

/***********************************************************************
 * Function: rise_width_minutes()
 * Description: How long the curve keeps climbing FAST - the total time the
 *  smoothed derivative spends at or above SHAPE_DERIV_FRACTION of its own peak,
 *  measured after the detection margin. Scaling to the curve's own peak rather
 *  than an absolute rate is what makes this comparable between a bright well and
 *  a dim one. A real reaction runs out of reagent and holds that band for two to
 *  three minutes; a well that never makes a step holds it for ten or more.
 * pramameter: record = record with differential data populated; parameters =
 *  thresholding parameters (detection_margin_time)
 *  return: width in minutes, or -1.0 when no positive peak exists
 */
double rise_width_minutes(const Record &record, DiagnosticParameters &parameters)
{
    const std::vector<double> &d = record.differential_data;
    if (d.size() < 2 || record.time_data.size() != d.size())
        return -1.0;

    size_t di = 0;
    while (di < record.time_data.size() && record.time_data[di] < parameters.detection_margin_time)
        di++;
    if (di >= d.size())
        return -1.0;

    double peak = d[di];
    for (size_t i = di; i < d.size(); i++)
        if (d[i] > peak)
            peak = d[i];
    if (peak <= 0.0)
        return -1.0;

    /* Sum of sample intervals rather than (last - first): a shoulder can dip below the band and
     * come back, and counting the gap would report one long rise where there were two short ones. */
    const double cut = peak * SHAPE_DERIV_FRACTION;
    double width = 0.0;
    for (size_t i = di; i < d.size(); i++)
    {
        if (d[i] < cut)
            continue;
        double step = (i + 1 < record.time_data.size())
                          ? (record.time_data[i + 1] - record.time_data[i])
                          : (record.time_data[i] - record.time_data[i - 1]);
        width += step;
    }
    return width;
}

/***********************************************************************
 * Function: removed_by_new_gate()
 * Description: Would this well have been Positive under the thresholds v2.4.3
 *  shipped with, but fails them as they now stand? That set is what gets marked
 *  for review rather than silently dropped.
 *
 *  Asks the question the other way round from the classifier: the classifier has
 *  already run with the CURRENT thresholds and left its verdict in the record, so
 *  here we only re-test the two gates that moved, against the legacy values, and
 *  compare. Every other condition - the detection margin, the shape test, the
 *  slight-positive time - is unchanged between the two, so re-testing them would
 *  only be a chance to disagree with the classifier about something neither
 *  threshold touches.
 * pramameter: record = record already classified; parameters = current thresholds
 *  return: true if the well was a Positive before these thresholds moved
 */
bool removed_by_new_gate(const Record &record, DiagnosticParameters &parameters)
{
    const DiagnosticOutcome &oc = record.outcome;

    /* Already Positive under the current thresholds - nothing was removed. */
    if (strcmp(oc.outcome, OutcomePositive) == 0 ||
        strcmp(oc.outcome, OutcomeSlightPositive) == 0)
        return false;

    /* An Error or a Break failed for a reason that has nothing to do with these two gates;
     * relabelling it would hide the real fault behind a review flag. */
    if (oc.outcome[0] != '\0' && strcmp(oc.outcome, OutcomeNegative) != 0)
        return false;

    /* No peak was found, so neither gate is what stopped it. */
    if (record.peak_features.main_peak.i == -1 || oc.increase < 0)
        return false;

    const bool passedLegacy = (oc.increase > LEGACY_MIN_INCREASE) &&
                              (record.peak_features.main_peak.y > LEGACY_MIN_SHARPNESS);
    const bool passesNow    = (oc.increase > parameters.min_increase) &&
                              (record.peak_features.main_peak.y > parameters.min_sharpness);
    /* The lag-phase test sits inside the same branch in the classifier and is unchanged, so a
     * well that would have been called Error for rising too soon must not be called Flagged. */
    const bool lagOk        = (oc.transition_time.x >= parameters.detection_margin_time);

    return passedLegacy && !passesNow && lagOk;
}

/***********************************************************************
 * Function: predict_outcome()
 * Description: Runs the classifier, then attaches the advisory fields and
 *  applies the shape rule. The split exists so the advisory values are
 *  populated on EVERY exit path of the classifier (no peak, increase below
 *  threshold, and the main branch) without threading the calls through each
 *  return.
 *
 *  Two builds differ HERE and nowhere else:
 *    v2.4.3AT (default)             - the gated well is reported Flagged (F)
 *    v2.4.3a  (SHAPE_RULE_NEGATIVE) - a flagged Positive becomes Negative
 *  Both compute and report identical numbers, so a run from either build can be
 *  compared against the other.
 * pramameter: record = record to classify; parameters = thresholding parameters
 *  return: none (record.outcome populated)
 */
void predict_outcome(Record &record, DiagnosticParameters &parameters)
{
    predict_outcome_core(record, parameters);

    /* ADVISORY ONLY - reported, never tested. Must stay AFTER the classifier. */
    record.outcome.arm_width = arm_width_minutes(record);
    record.outcome.suspect_score = nsa_score(record, parameters);

    /* Measured on EVERY curve and reported, flagged or not - they cost nothing to emit and they
     * are the labelled data needed to set thresholds from measurement later. Neither decides
     * anything: the two-arm shape rule they used to feed was withdrawn (see Algo.h). */
    record.outcome.window_rate = window_rate(record, parameters);
    record.outcome.rise_width = rise_width_minutes(record, parameters);

    /* The review gate. shape_flag is a REASON CODE (SHAPE_FLAG_* in Algo.h), not a bit: this gate
     * sets SHAPE_FLAG_THRESHOLD_BAND, and predict_outcome_core() may already have set
     * SHAPE_FLAG_EARLY_RISE. Two different questions, so they must stay distinguishable. */
    /* v2.4.5at: predict_outcome_core() may already have flagged this well SHAPE_FLAG_EARLY_RISE.
     * This assignment was unconditional and would clobber it. The two reasons answer different
     * questions, so the gate only runs when core left the flag clear. */
    if (record.outcome.shape_flag == SHAPE_FLAG_NONE)
        record.outcome.shape_flag = removed_by_new_gate(record, parameters) ? SHAPE_FLAG_THRESHOLD_BAND : SHAPE_FLAG_NONE;

    /* Only the threshold-band reason is downgradable. An early riser cleared both min_increase
     * and min_sharpness, so calling it Negative in the a-variant would assert the opposite of
     * what was measured; it stays Flagged in both builds. */
    if (record.outcome.shape_flag == SHAPE_FLAG_THRESHOLD_BAND)
    {
        /* Every measurement behind the call is left standing either way: Ct, increase, steepness,
         * window rate and rise_width still describe what the curve did. An operator asking "why is this
         * not Positive when it clearly rose?" must be able to read the answer off the same record
         * rather than be told to trust the machine. */
#ifdef SHAPE_RULE_NEGATIVE
        strcpy(record.outcome.outcome, OutcomeNegative);
#else
        /* Flagged is its own state, not a Positive wearing a mark. Leaving it as P with a flag
         * beside it puts the burden on every downstream reader to notice a second field; a well
         * the machine cannot vouch for should not be counted as a detection by anything that
         * only reads the letter. */
        strcpy(record.outcome.outcome, OutcomeFlagged);
#endif
    }
}
