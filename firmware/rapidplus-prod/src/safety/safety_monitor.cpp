// safety_monitor.cpp — xem safety_monitor.h.
//
// KHÔNG thêm #include <Arduino.h> vào file này. Thêm vào là mất khả năng test
// trên host, và logic an toàn quay về trạng thái "chỉ test được bằng máy thật"
// tức là thực tế không ai test.

#include "safety_monitor.h"

namespace safety {

bool isReadingSane(float celsius) {
    // NaN thất bại cả hai phép so sánh → trả false. Đây là hành vi mong muốn:
    // NaN nghĩa là sensor hỏng, phải xử lý như nguy hiểm.
    return celsius >= kSensorMinValidC && celsius <= kSensorMaxValidC;
}

Verdict evaluate(const Reading &r, unsigned long nowMs) {
    // ── 1. Sensor timeout ────────────────────────────────────────────────────
    // Xét TRƯỚC ngưỡng nhiệt: số đọc cũ có thể vẫn "mát" trong khi máy đã nóng.
    // Trừ theo unsigned là đúng kể cả khi millis() tràn (~49.7 ngày).
    if (nowMs - r.lastUpdateMs > kSensorTimeoutMs) {
        return {Action::StopHeating, Reason::SensorTimeout};
    }

    // ── 2. Số đọc vô lý ──────────────────────────────────────────────────────
    // Sensor hở mạch/ngắn mạch cho giá trị rác có thể tình cờ dưới ngưỡng và
    // trông như an toàn. Không đọc được nhiệt độ = không được gia nhiệt.
    if (!isReadingSane(r.bottomC) || !isReadingSane(r.hotlidC)) {
        return {Action::StopHeating, Reason::SensorOutOfRange};
    }

    // ── 3. Hard-limit tuyệt đối ──────────────────────────────────────────────
    // Không có tham số target ở đây, có chủ đích: lớp này không quan tâm profile
    // đang chạy muốn gì.
    if (r.bottomC > kBottomHardLimitC) {
        return {Action::StopHeating, Reason::BottomOverTemp};
    }
    if (r.hotlidC > kHotlidHardLimitC) {
        return {Action::StopHeating, Reason::HotlidOverTemp};
    }

    // ── 4. Biên cảnh báo ─────────────────────────────────────────────────────
    // Không phải ngưỡng an toàn — chỉ là tín hiệu sớm để chẩn đoán.
    if (r.bottomC > kBottomHardLimitC - kWarnMarginC) {
        return {Action::Warn, Reason::BottomOverTemp};
    }
    if (r.hotlidC > kHotlidHardLimitC - kWarnMarginC) {
        return {Action::Warn, Reason::HotlidOverTemp};
    }

    return {Action::Allow, Reason::None};
}

const char *reasonToString(Reason reason) {
    switch (reason) {
        case Reason::BottomOverTemp:   return "bottom_over_temp";
        case Reason::HotlidOverTemp:   return "hotlid_over_temp";
        case Reason::SensorOutOfRange: return "sensor_out_of_range";
        case Reason::SensorTimeout:    return "sensor_timeout";
        case Reason::None:
        default:                       return "none";
    }
}

}  // namespace safety
