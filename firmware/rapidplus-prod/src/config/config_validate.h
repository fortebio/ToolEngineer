// config_validate.h — validate + CLAMP config trước khi áp.
//
// C++ THUẦN — test được trên host. Xem AGENTS.md §3.
//
// LUẬT (AGENTS.md §5): KHÔNG BAO GIỜ tin config đến từ cloud hay file. Mọi giá
// trị phải qua đây. Vượt ngưỡng → clamp về biên + đánh dấu, hoặc từ chối cả gói
// nếu không clamp an toàn được. Không bao giờ áp nguyên giá trị lạ.
//
// Nguồn ngưỡng: PRD FBT-DXD, FR-DEV-03 (đóng finding D1-01, D1-02, D4-01, D4-02).
#pragma once

#include <cstdint>

namespace config {

// ─── Ngưỡng hợp lệ ──────────────────────────────────────────────────────────
// TODO(product): xác nhận lại với team sinh học trước khi phát hành.
constexpr int   kAmplificationTimeMax = 130;  // PRD: amplification_time <= 130
constexpr int   kAmplificationTimeMin = 1;
constexpr int   kLabelMaxLen          = 9;    // PRD: string <= 9 ký tự
constexpr float kKiMin                = 0.0001f;  // PRD: Ki > 0
constexpr float kKiMax                = 100.0f;
constexpr float kSlopeMinAbs          = 0.0001f;  // PRD: slope != 0

/// Kết quả của một lần validate.
enum class Status {
    Ok,       ///< mọi giá trị nằm trong ngưỡng
    Clamped,  ///< có giá trị bị kéo về biên — PHẢI log
    Rejected, ///< không clamp an toàn được — KHÔNG áp gói này
};

/// Cờ bit đánh dấu trường nào bị đụng, để log cụ thể thay vì chỉ "config sai".
enum Flag : uint32_t {
    kFlagNone             = 0,
    kFlagAmplificationTime = 1u << 0,
    kFlagLabelTruncated    = 1u << 1,
    kFlagKi                = 1u << 2,
    kFlagSlope             = 1u << 3,
};

struct RunConfig {
    int   amplificationTime;
    float ki;
    float slope;
    char  label[kLabelMaxLen + 1];  // +1 cho '\0'
};

struct Result {
    Status   status;
    uint32_t flags;  ///< OR của các Flag

    bool shouldApply() const { return status != Status::Rejected; }
    bool wasModified() const { return flags != kFlagNone; }
};

/// Validate và clamp TẠI CHỖ. `cfg` bị sửa thành giá trị an toàn để áp.
///
/// Trả Rejected khi giá trị không thể cứu an toàn được — VD slope = 0 (chia cho
/// 0 trong thuật toán) hay NaN. Với những trường hợp đó, clamp về một số tuỳ ý
/// sẽ tạo ra kết quả đo SAI mà trông vẫn hợp lệ — nguy hiểm hơn là từ chối.
Result validateAndClamp(RunConfig &cfg);

/// Cắt chuỗi an toàn về tối đa kLabelMaxLen, luôn null-terminate.
/// Trả true nếu đã phải cắt.
bool clampLabel(char *dst, unsigned long dstSize, const char *src);

}  // namespace config
