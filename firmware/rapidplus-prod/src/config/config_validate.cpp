// config_validate.cpp — xem config_validate.h.
//
// KHÔNG thêm #include <Arduino.h>.

#include "config_validate.h"

#include <cmath>

namespace config {

namespace {

/// NaN và Inf phải bị bắt riêng: mọi phép so sánh với NaN đều false, nên một
/// kiểm tra `< min || > max` thông thường sẽ CHO QUA NaN.
bool isFinite(float v) { return std::isfinite(v); }

float clampFloat(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

bool clampLabel(char *dst, unsigned long dstSize, const char *src) {
    if (dst == nullptr || dstSize == 0) {
        return false;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return false;
    }

    const unsigned long maxCopy = (dstSize - 1 < kLabelMaxLen) ? dstSize - 1 : kLabelMaxLen;
    unsigned long n = 0;
    while (n < maxCopy && src[n] != '\0') {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';

    return src[n] != '\0';  // còn ký tự chưa chép = đã bị cắt
}

Result validateAndClamp(RunConfig &cfg) {
    Result res{Status::Ok, kFlagNone};

    // ── slope: KHÔNG clamp được ─────────────────────────────────────────────
    // slope dùng làm mẫu số trong thuật toán. slope = 0 → chia cho 0. Nhưng
    // clamp về 0.0001 cũng SAI: nó tạo ra kết quả đo trông hợp lệ mà thực chất
    // vô nghĩa. Với thiết bị chẩn đoán, kết quả sai âm thầm nguy hiểm hơn là
    // từ chối chạy. Nên: từ chối cả gói.
    if (!isFinite(cfg.slope) || std::fabs(cfg.slope) < kSlopeMinAbs) {
        res.status = Status::Rejected;
        res.flags |= kFlagSlope;
        return res;
    }

    // ── amplification_time ──────────────────────────────────────────────────
    if (cfg.amplificationTime < kAmplificationTimeMin ||
        cfg.amplificationTime > kAmplificationTimeMax) {
        if (cfg.amplificationTime < kAmplificationTimeMin) {
            cfg.amplificationTime = kAmplificationTimeMin;
        } else {
            cfg.amplificationTime = kAmplificationTimeMax;
        }
        res.status = Status::Clamped;
        res.flags |= kFlagAmplificationTime;
    }

    // ── Ki ──────────────────────────────────────────────────────────────────
    // Ki <= 0 làm hỏng vòng PID. Clamp được vì PID vẫn hội tụ với Ki nhỏ, và
    // hard-limit nhiệt vẫn là lưới an toàn phía sau.
    if (!isFinite(cfg.ki)) {
        cfg.ki     = kKiMin;
        res.status = Status::Clamped;
        res.flags |= kFlagKi;
    } else if (cfg.ki < kKiMin || cfg.ki > kKiMax) {
        cfg.ki     = clampFloat(cfg.ki, kKiMin, kKiMax);
        res.status = Status::Clamped;
        res.flags |= kFlagKi;
    }

    // ── label ───────────────────────────────────────────────────────────────
    // Buffer tràn ở đây từng là finding D1-02.
    //
    // KHÔNG dùng strlen(): nếu gói đến không có '\0' thì strlen đọc tràn buffer,
    // tức chính lỗi ta đang đi vá. Quét có biên bằng tay (strnlen không phải
    // hàm chuẩn C++).
    unsigned long len = 0;
    while (len < sizeof(cfg.label) && cfg.label[len] != '\0') {
        len++;
    }
    if (len >= sizeof(cfg.label)) {  // không tìm thấy '\0' trong buffer
        cfg.label[kLabelMaxLen] = '\0';
        res.status = Status::Clamped;
        res.flags |= kFlagLabelTruncated;
    }

    return res;
}

}  // namespace config
