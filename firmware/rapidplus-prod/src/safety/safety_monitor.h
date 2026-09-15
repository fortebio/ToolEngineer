// safety_monitor.h — lớp an toàn nhiệt ĐỘC LẬP với PID.
//
// C++ THUẦN: không <Arduino.h>, không HAL, không I/O. Nhận số vào, trả quyết
// định ra. Nhờ vậy `pio test -e native` chạy được trong CI không cần phần cứng.
// Xem AGENTS.md §3 — đây là lý do tồn tại của cách chia này.
//
// LUẬT: lớp này là ranh giới an toàn CUỐI CÙNG. Nó KHÔNG biết gì về target,
// profile, hay lệnh cloud. Nó chỉ biết nhiệt độ đo được và ngưỡng cứng.
// PID sai → lớp này vẫn cắt.
#pragma once

#include "safety_limits.h"

namespace safety {

/// Hành động mà vòng điều khiển BẮT BUỘC thực hiện.
enum class Action {
    Allow,       ///< trong ngưỡng, cho phép gia nhiệt
    Warn,        ///< gần hard-limit — vẫn cho chạy nhưng phải log
    StopHeating, ///< cắt TOÀN BỘ gia nhiệt ngay
};

/// Lý do cắt — ghi log bền để truy vết sau sự cố (IEC 62304).
enum class Reason {
    None,
    BottomOverTemp,
    HotlidOverTemp,
    SensorOutOfRange,
    SensorTimeout,
};

struct Verdict {
    Action action;
    Reason reason;

    bool mustStop() const { return action == Action::StopHeating; }
};

/// Số đọc sensor tại một thời điểm.
struct Reading {
    float bottomC;
    float hotlidC;

    /// Mốc thời gian của số đọc MỚI NHẤT (millis() trên thiết bị).
    unsigned long lastUpdateMs;
};

/// Đánh giá một số đọc. Hàm thuần — cùng input luôn cho cùng output, không state.
///
/// `nowMs` truyền vào thay vì gọi millis() bên trong, để test được timeout mà
/// không phải chờ thật.
///
/// Thứ tự kiểm tra là CÓ CHỦ ĐÍCH: sensor lỗi được xét TRƯỚC ngưỡng nhiệt. Số
/// đọc rác có thể tình cờ nằm dưới ngưỡng và trông như an toàn.
Verdict evaluate(const Reading &r, unsigned long nowMs);

/// true nếu giá trị nằm trong dải sensor hợp lệ.
bool isReadingSane(float celsius);

/// Chuỗi mô tả lý do, dùng cho log. Không bao giờ trả nullptr.
const char *reasonToString(Reason reason);

}  // namespace safety
