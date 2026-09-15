// safety_limits.h — NGƯỠNG AN TOÀN TUYỆT ĐỐI.
//
// ╔════════════════════════════════════════════════════════════════════════╗
// ║  FILE NÀY LÀ RANH GIỚI AN TOÀN CUỐI CÙNG CỦA THIẾT BỊ.                 ║
// ║                                                                        ║
// ║  Mọi giá trị ở đây là COMPILE-TIME CONSTANT. Chúng KHÔNG BAO GIỜ:      ║
// ║    - đọc từ config                                                     ║
// ║    - nhận từ cloud                                                     ║
// ║    - sửa lúc chạy                                                      ║
// ║                                                                        ║
// ║  Đổi bất kỳ số nào trong file này = thay đổi an toàn. Bắt buộc:        ║
// ║    1. Team nhiệt/sinh học duyệt bằng văn bản                          ║
// ║    2. Ghi vào CHANGELOG.md mục Security/Safety                        ║
// ║    3. Test trên máy thật trước khi phát hành                          ║
// ╚════════════════════════════════════════════════════════════════════════╝
//
// Nguồn: PRD FBT-DXD, FR-DEV-04 (đóng finding D6-01).
#pragma once

namespace safety {

// ─── Hard-limit nhiệt tuyệt đối ─────────────────────────────────────────────
// Vượt ngưỡng → cắt TOÀN BỘ gia nhiệt, bất kể target/profile/lệnh cloud.
//
// TODO(safety): PRD Q3 ghi ngưỡng chính xác "cần team nhiệt/sinh học chốt".
// Giá trị dưới đây theo FR-DEV-04. XÁC NHẬN LẠI trước khi phát hành production.
constexpr float kBottomHardLimitC = 99.0f;
constexpr float kHotlidHardLimitC = 85.0f;

// ─── Tính hợp lệ của số đọc sensor ──────────────────────────────────────────
// Ngoài dải này = sensor hỏng/hở mạch, KHÔNG phải nhiệt độ thật.
// Xử lý như nguy hiểm: không đọc được nhiệt độ thì không được phép gia nhiệt.
constexpr float kSensorMinValidC = -40.0f;
constexpr float kSensorMaxValidC = 200.0f;

// Không có số đọc mới quá lâu = mất sensor. Cắt gia nhiệt.
// Vòng PID chạy 100 ms → 2000 ms là đã lỡ ~20 chu kỳ, quá đủ để kết luận.
constexpr unsigned long kSensorTimeoutMs = 2000;

// ─── Biên cảnh báo ──────────────────────────────────────────────────────────
// Dưới hard-limit một khoảng: cảnh báo sớm để chẩn đoán trước khi phải cắt.
// Đây KHÔNG phải ngưỡng an toàn, chỉ là tín hiệu sớm.
constexpr float kWarnMarginC = 3.0f;

}  // namespace safety
