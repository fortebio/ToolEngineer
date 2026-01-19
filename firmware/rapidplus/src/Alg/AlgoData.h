#pragma once
#ifndef ALGODATA_H
#define ALGODATA_H

#include <cstring>
#include <vector>
#include <ArduinoJson.h>
// #include "..\ForteSetting.h"

// Define Point class
class Point
{
public:
       double x;
        double y;
        int i;
        Point() { clear();}

        JsonDocument toJSON() {
            JsonDocument _json;
            _json["x"] = x;
            _json["y"] = y;
            _json["i"] = i;
            return _json;
        }
        void fromJSON(JsonObject& _json) {
            x = _json["x"];
            y = _json["y"];
            i = _json["i"];
        }
        void clear() {x=-1.0; y=-1.0; i=-1;}


};


const char OutcomePositive[] = "Positive";
const char OutcomeNegative[] = "Negative";
const char OutcomeSlightPositive[] = "Slight Positive";
const char OutcomeError[] = "Error";
const char OutcomeBreak[] = "Break";


// Define FeatureDetection class
class FeatureDetection {
    public:
        Point main_peak = Point();
        Point right_arm = Point();
        Point left_arm = Point();
        bool detected_peak() {
            /*
            :param peak_features: a peak feature structure object
            :return: is the sharp increase in fluorescence detected?
            */
            if (main_peak.i != -1) {return true;} else {return false;}
        }
        bool detected_shape() {
            /*
            :param peak_features: a peak feature structure object
            :return: does the sharp increase in fluorescence has sigmoidal shape?
            */
            if (main_peak.i != -1 && left_arm.i != -1 ) {  // and right_arm.i != -1:
                return true;} else {return false;}
        }
        bool detected_ea() {
            /*
            :param peak_features: a peak feature structure object
            */
            if ((main_peak.i != -1) && (left_arm.i == -1)) {return true;} else {return false;}
        }

        JsonDocument toJSON() {
            JsonDocument _json;
            _json["main_peak"] = main_peak.toJSON();
            _json["left_arm"] = left_arm.toJSON();
            _json["right_arm"] = right_arm.toJSON();
            return _json;
        }
        void fromJSON(JsonObject& _json) {
            JsonObject _main_json = _json["main_peak"];
            main_peak.fromJSON(_main_json);
            JsonObject _left_json = _json["left_arm"];
            left_arm.fromJSON(_left_json);
            JsonObject _right_json = _json["right_arm"];
            right_arm.fromJSON(_right_json);
        }

        void clear() {main_peak.clear(); left_arm.clear(); right_arm.clear();}
};


class DiagnosticOutcome {
    public:
        char outcome[50];
        Point transition_time = Point();
        Point plateau_point = Point();
        double increase = -1.0;
        DiagnosticOutcome() {
            clear();
        }

        JsonDocument toJSON() {
            JsonDocument _json;
            _json["outcome"] = outcome;
            _json["transition_time"] = transition_time.toJSON();
            _json["plateau_point"] = plateau_point.toJSON();
            _json["increase"] = increase;
            return _json;
        }
        void fromJSON(JsonObject& _json) {
            strcpy(outcome, _json["outcome"]);
            JsonObject _transition_json = _json["transition_time"];
            transition_time.fromJSON(_transition_json);
            JsonObject _plateau_json = _json["plateau_point"];
            plateau_point.fromJSON(_plateau_json);
            increase = _json["increase"];
        }

        void clear() {memset(outcome, '\0', sizeof(outcome)); transition_time.clear(); increase = -1.0;}
};


class DiagnosticParameters {
    public:
        double min_increase;                // fluorescence level threshold
        double min_sharpness;               // amplification steepnes level
        double min_slight_positive_time;    // threshold for calling Slight Positive from Positive
        bool detect_shape;                  // lag phase detection On/Off
        double detection_margin_time;       // minimum main peak position to consider Ct value as positive
        double arm_percentile;              // percentile used for calculating lag phase
        double transition_percentile;       // percentile used for calcuating transition time (Ct) & fluorescence increase 
        uint8_t sg_order;                       // interpolation smoothing order
        uint8_t sg_window;                      // smoothing window size for algorithm
        // int sg_window_display;              // window size for display to users
        uint8_t baseline_start;                 // start of baselining (minutes)
        uint8_t baseline_range;                 // range of baselining (minutes)

        DiagnosticParameters()
        {
            clear();
        }
        JsonDocument toJSON() {
            JsonDocument _json;
            _json["min_increase"] = min_increase;
            _json["min_sharpness"] = min_sharpness;
            _json["min_slight_positive_time"] = min_slight_positive_time;
            _json["detect_shape"] = detect_shape;
            _json["detection_margin_time"] = detection_margin_time;
            _json["arm_percentile"] = arm_percentile;
            _json["transition_percentile"] = transition_percentile;
            _json["sg_order"] = sg_order;
            _json["sg_window"] = sg_window;
            // _json["sg_window_display"] = sg_window_display;
            _json["baseline_start"] = baseline_start;
            _json["baseline_range"] = baseline_range;
            return _json;
        }
        void fromJSON(JsonObject& _json) {
            min_increase = _json["min_increase"];
            min_sharpness = _json["min_sharpness"];
            min_slight_positive_time = _json["min_slight_positive_time"];
            detect_shape = _json["detect_shape"];
            detection_margin_time = _json["detection_margin_time"];
            arm_percentile = _json["arm_percentile"];
            transition_percentile = _json["transition_percentile"];
            sg_order = _json["sg_order"];
            sg_window = _json["sg_window"];
            // sg_window_display = _json["sg_window_display"];
            baseline_start = _json["baseline_start"];
            baseline_range = _json["baseline_range"];
             
        }

        void fromEEPROM();

        void clear()
        {
            min_increase = 30.0;
            min_sharpness = 10.0;
            min_slight_positive_time = 22.0;
            detect_shape = true;
            detection_margin_time = 6.0;
            arm_percentile = 0.5;
            transition_percentile = 0.25;
            sg_order = 0;
            sg_window = 2;
            // sg_window_display = 2;
            baseline_start = 3;
            baseline_range = 4;
        }
};


class DataIn {
    public:
        struct DiagnosticParameters parameters;
        std::vector<double> time_data;
        std::vector<double> raw_data;
        JsonDocument toJSON() {
            JsonDocument _doc;
            _doc["parameters"] = parameters.toJSON();
            JsonArray _array_times = _doc["time_data"].to<JsonArray>();
            addVectorToJSON(time_data, _array_times);
            JsonArray _array_raw = _doc["raw_data"].to<JsonArray>();
            addVectorToJSON(raw_data, _array_raw);
            return _doc;
        }

        void fromJSON(JsonDocument& _doc) {
            JsonObject _json_parameters = _doc["parameters"];
            parameters.fromJSON(_json_parameters);
            JsonArray _json_times = _doc["time_data"].as<JsonArray>();
            loadVectorFromJSON(time_data, _json_times);
            JsonArray _json_raw = _doc["raw_data"].as<JsonArray>();
            loadVectorFromJSON(raw_data, _json_raw);
        }

        void fromEEPROM(JsonDocument& _doc) {
            // JsonObject _json_parameters = _doc["parameters"];
            // parameters.fromJSON(_json_parameters);
            parameters.fromEEPROM();
            JsonArray _json_times = _doc["time_data"].as<JsonArray>();
            loadVectorFromJSON(time_data, _json_times);
            JsonArray _json_raw = _doc["raw_data"].as<JsonArray>();
            loadVectorFromJSON(raw_data, _json_raw);
        }

        void clear() {
            parameters.clear();
            raw_data.clear();
            time_data.clear();
        }       

    private:
        void addVectorToJSON(std::vector<double>& _array, JsonArray& _json) {
            for (double value : _array) {
                _json.add(value);
            }
        }

        void loadVectorFromJSON(std::vector<double>& _array, JsonArray& _json) {
            for (JsonVariant value : _json) {
                _array.push_back(value.as<double>());
            }
        }
};


class Record {
    public:
        struct DiagnosticOutcome outcome;
        struct FeatureDetection peak_features;
        std::vector<double> time_data;
        std::vector<double> raw_data;
        std::vector<double> processed_data;
        std::vector<double> differential_data;

        JsonDocument toJSON() {
            JsonDocument _doc;
            _doc["outcome"] = outcome.toJSON();
            _doc["peak_features"] = peak_features.toJSON();
            JsonArray _array_times = _doc["time_data"].to<JsonArray>();
            addVectorToJSON(time_data, _array_times);
            JsonArray _array_raw = _doc["raw_data"].to<JsonArray>();
            addVectorToJSON(raw_data, _array_raw);
            JsonArray _array_processed = _doc["processed_data"].to<JsonArray>();
            addVectorToJSON(processed_data, _array_processed);
            JsonArray _array_differential = _doc["differential_data"].to<JsonArray>();
            addVectorToJSON(differential_data, _array_differential);

            return _doc;
        }

        void fromJSON(JsonDocument& _doc) {
            JsonObject _json_outcome = _doc["outcome"];
            outcome.fromJSON(_json_outcome);
            JsonObject _json_features = _doc["peak_features"];
            peak_features.fromJSON(_json_features);
            JsonArray _json_times = _doc["time_data"].as<JsonArray>();
            loadVectorFromJSON(time_data, _json_times);
            JsonArray _json_raw = _doc["raw_data"].as<JsonArray>();
            loadVectorFromJSON(raw_data, _json_raw);
            JsonArray _json_processed = _doc["processed_data"].as<JsonArray>();
            loadVectorFromJSON(processed_data, _json_processed);
            JsonArray _json_differential = _doc["differential_data"].as<JsonArray>();
            loadVectorFromJSON(differential_data, _json_processed);
            
        }

        void clear() {
            outcome.clear();
            peak_features.clear();
            raw_data.clear();
            time_data.clear();
            processed_data.clear();
            differential_data.clear();
        }       

    private:
        void addVectorToJSON(std::vector<double>& _array, JsonArray& _json) {
            for (double value : _array) {
                _json.add(value);
            }
        }

        void loadVectorFromJSON(std::vector<double>& _array, JsonArray& _json) {
            for (JsonVariant value : _json) {
                _array.push_back(value.as<double>());
            }
        }

};



// // Define Calibration class
// struct Calibration {
//     double slopes[10];
//     double origins[10];
// };


// // Define Configuration class
// class Configuration {
// public:
//     Calibration calibration;
//     double led_power[20];
//     char units[20];
//     char device_id[20];
//     int slots;
//     int amplification_time;
//     int measurement_interval;
//     char software_version[20];

//     // Constructor
//     Configuration(Calibration calib, const double* led, const char* unit, const char* dev_id,
//         int slot, int ampl_time, int meas_interval, const char* soft_ver)
//         : calibration(calib), slots(slot),
//         amplification_time(ampl_time), measurement_interval(meas_interval) {
//         // Copy LED values directly into led_power
//         for (int i = 0; i < 10; ++i) {
//             led_power[i] = led[i];
//         }
//         // Copy unit, dev_id, and soft_ver strings
//         strncpy(units, unit, sizeof(units));
//         strncpy(device_id, dev_id, sizeof(device_id));
//         strncpy(software_version, soft_ver, sizeof(software_version));
//     }
// };

#endif // ALGOCLASSES_H