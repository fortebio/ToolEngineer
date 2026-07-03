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
    double pre_range = range_of(_array, index - 7, 8);
    if (jump < pre_range * 2.5)
        return false;

    // Flatness measured AFTER the settling transient (skip JUMP_SETTLE_SKIP samples).
    double slope = mean_slope(_array, index + JUMP_SETTLE_SKIP, JUMP_FLAT_WINDOW);
    if (fabs(slope) > 1.0)
        return false;
    int total_rise = _array[index + 6] - _array[index + 1];
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
void predict_outcome(Record &record, DiagnosticParameters &parameters)
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
            // Test 4: Test for lag pahse (if applicable)
            if (record.outcome.transition_time.x < parameters.detection_margin_time)
            {
                /* Transition time is too short  => Rising to soon */
                strcpy(record.outcome.outcome, OutcomeError);
            }
            else if (parameters.detect_shape == false)
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
        }

        //  if positive, check if slight positive (transition time beyond a certain time i.e. t = 22 min)
        if (strcmp(record.outcome.outcome, OutcomePositive) == 0 && record.outcome.transition_time.x >= parameters.min_slight_positive_time)
        {
            strcpy(record.outcome.outcome, OutcomeSlightPositive);
        }
        return;
    }
}
