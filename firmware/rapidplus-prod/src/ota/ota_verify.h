// ota_verify.h — quyết định CÓ ĐƯỢC PHÉP ghi flash hay không.
//
// C++ THUẦN — test được trên host. Xem AGENTS.md §3.
//
// LUẬT (AGENTS.md §6): verify sha256 + pinned cert + pcb_version. Thiếu một
// trong ba = TỪ CHỐI. Không có chế độ "bỏ qua verify cho nhanh".
//
// File này KHÔNG tải file và KHÔNG ghi flash. Nó chỉ quyết định. Việc tách bạch
// đó là để quyết định an toàn được test mà không cần mạng lẫn phần cứng.
//
// Nguồn: PRD FBT-DXD, FR-DEV-05 (đóng finding D5-01).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ota {

/// Thông tin bản phát hành, lấy từ manifest của cloud.
struct Manifest {
    const char *version;     ///< "2.4.3"
    const char *sha256Hex;   ///< 64 ký tự hex thường
    const char *pcbVersion;  ///< phải khớp FBT_PCB_VERSION của máy
    uint32_t    sizeBytes;
    bool        certPinned;  ///< true nếu kết nối đã xác thực bằng cert ghim
};

enum class Decision {
    Accept,
    RejectBadHash,
    RejectPcbMismatch,
    RejectNoPinnedCert,
    RejectBadSize,
    RejectMalformed,
    RejectSameVersion,
};

struct Outcome {
    Decision decision;

    bool accepted() const { return decision == Decision::Accept; }
};

/// Kích thước tối đa một app slot (xem partitions/default_8MB.csv).
/// Ảnh lớn hơn slot sẽ ghi tràn — phải chặn TRƯỚC khi ghi byte đầu tiên.
constexpr uint32_t kMaxImageBytes = 0x300000;  // 3 MiB
constexpr uint32_t kMinImageBytes = 64 * 1024;

/// Quyết định có nhận bản OTA này không, TRƯỚC khi tải/ghi.
///
/// `currentVersion` và `currentPcb` lấy từ include/version.h của máy đang chạy.
Outcome evaluate(const Manifest &m, const char *currentVersion, const char *currentPcb);

/// true nếu chuỗi đúng 64 ký tự hex thường — định dạng sha256.
bool isValidSha256Hex(const char *hex);

const char *decisionToString(Decision d);

}  // namespace ota
