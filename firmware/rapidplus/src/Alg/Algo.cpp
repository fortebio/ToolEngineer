#include <iostream>
#include <vector>
#include "AlgoData.h"
#include "Algo.h"
#include "sgsmooth.h"
#include "../ForteSetting.h"

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

size_t find_crossing_higher_than_reversed(const std::vector<double> &_array, double crossing, int start_index)
{
    for (int index = start_index; index >= 0; --index)
    {
        if (_array[index] >= crossing)
        {
            return index;
        }
    }
    return -1;
}

float mean(const std::vector<double> &vec, int startIndex, int endIndex)
{
    if (vec.empty())
    {
        return 0.0; // Return 0 if the vector is empty to avoid division by zero
    }

    float sum = 0.0;
    for (int i = startIndex; i < endIndex; i++)
    {
        // std::cout << vec[i] << std::endl;
        sum += vec[i];
    }
    return (sum) / (double)(endIndex - startIndex); // Calculate average
}

void smooth(const std::vector<double> &_raw, std::vector<double> &_smoothed, uint8_t window, uint8_t order)
{
    std::vector<double> _temp = sg_smooth(_raw, window, order);
    _smoothed.assign(_temp.begin(), _temp.end());
}

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
    // std::cout << "baseline: " << baselineStart << " / " << baselineStop << std::endl;
    // calculate the average value over the baselining range
    double baselineValue = mean(raw_data, baselineStart, baselineStop);
    // std::cout << "baseline value: " << baselineValue << std::endl;
    // subtract basline
    for (size_t i = 0; i < baselinedData.size(); i++)
    {
        baselinedData[i] = raw_data[i] - baselineValue;
    }
}

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
    // sg_window_display = _json["sg_window_display"];
    baseline_start = _ForteSetting.parameter.baseline_start;
    baseline_range = _ForteSetting.parameter.baseline_range;
}

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

// bool check_breakData(std::vector<double> &_array, double crossing, int start_index)
// {
//     size_t countIncreases = 0;
//     // if (crossing <= 4)
//     // {
//     //     return true;
//     // }
//     for (size_t i = start_index; i < _array.size(); i++)
//     {
//         if ((_array[i] - _array[i - 1]) >= 0)
//         {
//             countIncreases++;
//             if ((_array[i + 1] - _array[i]) < 0)
//             {
//                 countIncreases = 0;
//             }
//             if (countIncreases >= 9)
//             {
//                 break;
//             }
//         }
//     }
//     Serial.printf("Break data increases count: %d\n", countIncreases);
//     if (countIncreases == 0)
//     {
//         return false;
//     }
//     return true;
// }
double mean_slope(std::vector<double> &_array, size_t start, size_t window)
{
    double sum = 0;
    for (int i = start + 1; i < (start + window); i++)
        sum += (_array[i] - _array[i - 1]);
    return sum / (window - 1);
}

bool checkJump(std::vector<double> &_array, double crossing, size_t index)
{
    if ((index < 2) || ((index + 9) >= _array.size()))
    {
        return false;
    }
    int jump = _array[index] - _array[index - 1];

    if (jump < crossing)
        return false;
    double slope = mean_slope(_array, index, 9);
    if (fabs(slope) > 1.0)
        return false;
    int total_rise = _array[index + 9] - _array[index];
    if (total_rise > crossing)
        return false;

    return true;
}

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

    // điều kiện tăng hợp lệ
    // if (positive_count < window * 0.7)   // ≥70% điểm tăng
    if (positive_count < window) // ≥70% điểm tăng
        return false;

    if (total_rise <= 0)
        return false;

    return true;
}

size_t check_breakData(std::vector<double> &_array, double crossing, int start_index)
{
    // size_t countIncreases = 0;
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

// size_t find_breakData(std::vector<double> &_array, int start_index)
// {
//     size_t break_i = start_index;
//     size_t countIncreases = 0;

//     for (size_t i = start_index; i < _array.size(); i++)
//     {
//         if ((_array[i] - _array[i - 1]) >= 0)
//         {
//             countIncreases++;
//             if ((_array[i + 1] - _array[i]) < 0)
//             {
//                 countIncreases = 0;
//                 continue;
//             }
//             if (countIncreases >= 9)
//             {
//                 break_i = i;
//                 break;
//             }
//         }
//     }
//     return break_i;
// }

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
    // Serial.printf("Discard index: %d\n", discard_index);
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

    // // calculate transition time ("Ct value")
    // record.outcome.transition_time.i = find_crossing_lower_than_reversed(
    //     record.differential_data,
    //     record.peak_features.main_peak.y * parameters.transition_percentile,
    //     record.peak_features.main_peak.i);
    // // if no point found, then assign to first point (really rare occurrence)
    // if (record.outcome.transition_time.i == -1)
    // {
    //     record.outcome.transition_time.i = 0;
    // }
    for (size_t i = 0; i < 12; i += 1)
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

    // Serial.println("About to detect");
    // Serial.println(record.outcome.increase);

    // Test 2: Check for fluorescence increase above threshold
    if (record.outcome.increase > parameters.min_increase)
    { // check for fluorescence increase
        // Serial.println("Increase yes");
        // test 3: Test for min sharpness
        if (record.peak_features.main_peak.y > parameters.min_sharpness)
        { // check for main peak having min steepness
            // Serial.println("Sharpness yes");
            // Test 4: Test for lag pahse (if applicable)
            if (parameters.detect_shape == false)
            { // if not using shape detection, give positive
                // Serial.println("Shape Off - Yes");
                strcpy(record.outcome.outcome, OutcomePositive);
            }
            // else if ((record.outcome.plateau_point.x - record.outcome.transition_time.x) < 3)
            // {
            //     strcpy(record.outcome.outcome, OutcomeBreak);
            // }
            else if (record.peak_features.detected_shape())
            { // if using shape detected, check if the shape is right
                // Serial.println("Shape On -  yes");
                strcpy(record.outcome.outcome, OutcomePositive);
            }
            else if (record.peak_features.detected_ea())
            {
                strcpy(record.outcome.outcome, OutcomeError);
            }
        }
    }

    //  if positive, check if slight positive (transition time beyond a certain time i.e. t = 22 min)
    if (strcmp(record.outcome.outcome, OutcomePositive) == 0 && record.outcome.transition_time.x >= parameters.min_slight_positive_time)
    {
        strcpy(record.outcome.outcome, OutcomeSlightPositive);
    }
    //  if positive, turn negative if main peak is at the last point in array
    // 22/04/2024: originally created to disable potential spike at long Ct, but removed to detect low conc cts
    // if (strcmp(outcome.outcome, OutcomeSlightPositive) == 0 && peak_features.left_arm.i >= y_data.size()-2) {
    //     strcpy(outcome.outcome, OutcomeNegative);
    // }
    return;
}
