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