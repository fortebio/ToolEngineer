#pragma once
#ifndef ALGODATA_H
#define ALGODATA_H

#include <cstring>
#include <vector>
#include <ArduinoJson.h>

// Define Point class
class Point
{
public:
    double x;
    double y;
    int i;
    Point() { clear(); }

    JsonDocument toJSON()
    {
        JsonDocument _json;
        _json["x"] = x;
        _json["y"] = y;
        _json["i"] = i;
        return _json;
    }
    void fromJSON(JsonObject &_json)
    {
        x = _json["x"];
        y = _json["y"];
        i = _json["i"];
    }
    void clear()
    {
        x = -1.0;
        y = -1.0;
        i = -1;
    }
};

const char OutcomePositive[] = "Positive";
const char OutcomeNegative[] = "Negative";
const char OutcomeSlightPositive[] = "Slight Positive";
const char OutcomeError[] = "Error";
// v2.4.3AT only. A well that amplified but whose curve shape does not match a real reaction.
// It is a THIRD state, not a decorated Positive: the operator is being told the machine cannot
// stand behind this one, which is a different message from "Positive" and from "Negative" alike.
// The v2.4.3a build does not use it - there a flagged well is reported OutcomeNegative outright.
//
// Every consumer of the outcome INITIAL must know this letter. The letter is what travels:
// result[i] = outcome[0] feeds the TFT table (displayLCD screen_Result), GET /slots, and the
// uploaded payload. displayLCD's chain has no else branch, so an unknown letter prints an empty
// cell rather than anything visible - adding a state without adding its branch loses it silently.
const char OutcomeFlagged[] = "Flagged";

// Define FeatureDetection class
class FeatureDetection
{
public:
    Point main_peak = Point();
    Point right_arm = Point();
    Point left_arm = Point();
    bool detected_peak()
    {
        /*
        :param peak_features: a peak feature structure object
        :return: is the sharp increase in fluorescence detected?
        */
        if (main_peak.i != -1)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    bool detected_shape()
    {
        /*
        :param peak_features: a peak feature structure object
        :return: does the sharp increase in fluorescence has sigmoidal shape?
        */
        if (main_peak.i != -1 && left_arm.i != -1)
        { // and right_arm.i != -1:
            return true;
        }
        else
        {
            return false;
        }
    }
    bool detected_ea()
    {
        /*
        :param peak_features: a peak feature structure object
        */
        if ((main_peak.i != -1) && (left_arm.i == -1))
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    JsonDocument toJSON()
    {
        JsonDocument _json;
        _json["main_peak"] = main_peak.toJSON();
        _json["left_arm"] = left_arm.toJSON();
        _json["right_arm"] = right_arm.toJSON();
        return _json;
    }
    void fromJSON(JsonObject &_json)
    {
        JsonObject _main_json = _json["main_peak"];
        main_peak.fromJSON(_main_json);
        JsonObject _left_json = _json["left_arm"];
        left_arm.fromJSON(_left_json);
        JsonObject _right_json = _json["right_arm"];
        right_arm.fromJSON(_right_json);
    }

    void clear()
    {
        main_peak.clear();
        left_arm.clear();
        right_arm.clear();
    }
};

class DiagnosticOutcome
{
public:
    char outcome[50];
    Point transition_time = Point();
    Point plateau_point = Point();
    double increase = -1.0;
    // v2.4.3a, ADVISORY ONLY - neither field is read by predict_outcome(), so neither can
    // change a call. suspect_score counts how many of six non-specific-amplification traits a
    // curve shows (0-6); arm_width is the width of the rise in minutes at arm_percentile, or
    // -1 when no arm was found. Not persisted to EEPROM and not part of parastructure.
    uint8_t suspect_score = 0;
    double arm_width = -1.0;
    // v2.4.3a shape measurements. window_rate = largest mean climb over any 4-minute window after
    // the detection margin, in calibrated units per minute; rise_width = minutes the smoothed
    // derivative holds at or above 35% of its own peak (a real reaction holds it 2-3 min);
    // shape_flag is a REASON CODE, not a bit (v2.4.5at). 0 = not flagged; 1 = the review gate
    // took this well (Positive under the v2.4.3 thresholds, Negative now); 2 = early rise, the
    // well amplified with a recognisable shape but Ct < MIN_CALLABLE_CT so the number is not
    // reportable. See SHAPE_FLAG_* in Algo.h. Only reason 1 is downgradable by
    // SHAPE_RULE_NEGATIVE - reason 2 cleared both size gates and is not a negative.
    // Both numbers are reported for EVERY well, flagged or not - they cost nothing to emit and
    // they are the labelled data needed to set these thresholds from measurement later.
    // What shape_flag BECOMES depends on the build: "Flagged" (F) in v2.4.3AT, "Negative" (N)
    // in v2.4.3a (SHAPE_RULE_NEGATIVE). -1 means "not measurable here".
    // NOTE: neither decides anything - the two-arm shape rule they fed was withdrawn (Algo.h).
    // window_rate REPLACED `share` on 2026-08-16: share ranked at chance against the 68-curve
    // label set, so it was collecting noise. Do not reinstate it.
    double window_rate = -1.0;
    double rise_width = -1.0;
    uint8_t shape_flag = 0;
    // v2.4.5at. neutralise_climbs() repairs electrical steps on a CALIBRATED COPY of the curve;
    // the array that gets uploaded is the untouched raw capture (sensor67Value), so a chart drawn
    // from the upload still shows a step the call never saw. These two say so: how many climbs
    // were repaired, and the reading index of the first, or -1. Reported, never read by any call.
    uint8_t climbs_fixed = 0;
    int16_t climb_first_i = -1;
    DiagnosticOutcome()
    {
        clear();
    }

    JsonDocument toJSON()
    {
        JsonDocument _json;
        _json["outcome"] = outcome;
        _json["transition_time"] = transition_time.toJSON();
        _json["plateau_point"] = plateau_point.toJSON();
        _json["increase"] = increase;
        _json["suspect_score"] = suspect_score;
        _json["arm_width"] = arm_width;
        _json["window_rate"] = window_rate;
        _json["rise_width"] = rise_width;
        _json["shape_flag"] = shape_flag;
        _json["climbs_fixed"] = climbs_fixed;
        _json["climb_first_i"] = climb_first_i;
        return _json;
    }
    void fromJSON(JsonObject &_json)
    {
        strcpy(outcome, _json["outcome"]);
        JsonObject _transition_json = _json["transition_time"];
        transition_time.fromJSON(_transition_json);
        JsonObject _plateau_json = _json["plateau_point"];
        plateau_point.fromJSON(_plateau_json);
        increase = _json["increase"];
        suspect_score = _json["suspect_score"] | 0;
        arm_width = _json["arm_width"] | -1.0;
        window_rate = _json["window_rate"] | -1.0;
        rise_width = _json["rise_width"] | -1.0;
        shape_flag = _json["shape_flag"] | 0;
        climbs_fixed = _json["climbs_fixed"] | 0;
        climb_first_i = _json["climb_first_i"] | -1;
    }

    void clear()
    {
        memset(outcome, '\0', sizeof(outcome));
        transition_time.clear();
        // v2.4.3a: plateau_point was never reset here while every other member was, so a
        // recycled DiagnosticOutcome carried the previous curve's plateau into the next slot.
        plateau_point.clear();
        increase = -1.0;
        suspect_score = 0;
        arm_width = -1.0;
        // Every new field resets here. This is the defect that left plateau_point stale across
        // slots in v2.4.3 - a recycled DiagnosticOutcome carrying the previous curve's numbers.
        window_rate = -1.0;
        rise_width = -1.0;
        shape_flag = 0;
        climbs_fixed = 0;
        climb_first_i = -1;
    }
};

class DiagnosticParameters
{
public:
    double min_increase;             // fluorescence level threshold
    double min_sharpness;            // amplification steepnes level
    double min_slight_positive_time; // threshold for calling Slight Positive from Positive
    bool detect_shape;               // lag phase detection On/Off
    double detection_margin_time;    // minimum main peak position to consider Ct value as positive
    double arm_percentile;           // percentile used for calculating lag phase
    double transition_percentile;    // percentile used for calcuating transition time (Ct) & fluorescence increase
    uint8_t sg_order;                // interpolation smoothing order
    uint8_t sg_window;               // smoothing window size for algorithm
    uint8_t baseline_start; // start of baselining (minutes)
    uint8_t baseline_range; // range of baselining (minutes)

    DiagnosticParameters()
    {
        clear();
    }
    JsonDocument toJSON()
    {
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
        _json["baseline_start"] = baseline_start;
        _json["baseline_range"] = baseline_range;
        return _json;
    }
    void fromJSON(JsonObject &_json)
    {
        min_increase = _json["min_increase"];
        min_sharpness = _json["min_sharpness"];
        min_slight_positive_time = _json["min_slight_positive_time"];
        detect_shape = _json["detect_shape"];
        detection_margin_time = _json["detection_margin_time"];
        arm_percentile = _json["arm_percentile"];
        transition_percentile = _json["transition_percentile"];
        sg_order = _json["sg_order"];
        sg_window = _json["sg_window"];
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
        baseline_start = 3;
        baseline_range = 4;
    }
};

class DataIn
{
public:
    struct DiagnosticParameters parameters;
    std::vector<double> time_data;
    std::vector<double> raw_data;
    JsonDocument toJSON()
    {
        JsonDocument _doc;
        _doc["parameters"] = parameters.toJSON();
        JsonArray _array_times = _doc["time_data"].to<JsonArray>();
        addVectorToJSON(time_data, _array_times);
        JsonArray _array_raw = _doc["raw_data"].to<JsonArray>();
        addVectorToJSON(raw_data, _array_raw);
        return _doc;
    }

    void fromJSON(JsonDocument &_doc)
    {
        JsonObject _json_parameters = _doc["parameters"];
        parameters.fromJSON(_json_parameters);
        JsonArray _json_times = _doc["time_data"].as<JsonArray>();
        loadVectorFromJSON(time_data, _json_times);
        JsonArray _json_raw = _doc["raw_data"].as<JsonArray>();
        loadVectorFromJSON(raw_data, _json_raw);
    }

    void fromEEPROM(JsonDocument &_doc)
    {
        parameters.fromEEPROM();
        JsonArray _json_times = _doc["time_data"].as<JsonArray>();
        loadVectorFromJSON(time_data, _json_times);
        JsonArray _json_raw = _doc["raw_data"].as<JsonArray>();
        loadVectorFromJSON(raw_data, _json_raw);
    }

    void clear()
    {
        parameters.clear();
        raw_data.clear();
        time_data.clear();
    }

private:
    void addVectorToJSON(std::vector<double> &_array, JsonArray &_json)
    {
        for (double value : _array)
        {
            _json.add(value);
        }
    }

    void loadVectorFromJSON(std::vector<double> &_array, JsonArray &_json)
    {
        for (JsonVariant value : _json)
        {
            _array.push_back(value.as<double>());
        }
    }
};

class Record
{
public:
    struct DiagnosticOutcome outcome;
    struct FeatureDetection peak_features;
    std::vector<double> time_data;
    std::vector<double> raw_data;
    std::vector<double> processed_data;
    std::vector<double> differential_data;

    JsonDocument toJSON()
    {
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

    void fromJSON(JsonDocument &_doc)
    {
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

    void clear()
    {
        outcome.clear();
        peak_features.clear();
        raw_data.clear();
        time_data.clear();
        processed_data.clear();
        differential_data.clear();
    }

private:
    void addVectorToJSON(std::vector<double> &_array, JsonArray &_json)
    {
        for (double value : _array)
        {
            _json.add(value);
        }
    }

    void loadVectorFromJSON(std::vector<double> &_array, JsonArray &_json)
    {
        for (JsonVariant value : _json)
        {
            _array.push_back(value.as<double>());
        }
    }
};

#endif // ALGOCLASSES_H