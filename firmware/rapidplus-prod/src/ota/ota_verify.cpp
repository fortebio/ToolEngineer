// ota_verify.cpp — xem ota_verify.h.
//
// KHÔNG thêm #include <Arduino.h>.

#include "ota_verify.h"

namespace ota {

namespace {

bool strEqual(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool isLowerHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

}  // namespace

bool isValidSha256Hex(const char *hex) {
    if (hex == nullptr) {
        return false;
    }
    size_t n = 0;
    while (n < 64) {
        if (hex[n] == '\0' || !isLowerHexDigit(hex[n])) {
            return false;
        }
        n++;
    }
    // Phải đúng 64 ký tự — dài hơn nghĩa là manifest sai định dạng, và ta không
    // đoán ý người gửi khi đang nói về thứ sẽ được ghi vào flash.
    return hex[64] == '\0';
}

Outcome evaluate(const Manifest &m, const char *currentVersion, const char *currentPcb) {
    // ── 1. Cert ghim ────────────────────────────────────────────────────────
    // Xét ĐẦU TIÊN: nếu kênh truyền không xác thực thì mọi thứ khác trong
    // manifest đều có thể do kẻ tấn công viết ra, kể cả hash "hợp lệ".
    if (!m.certPinned) {
        return {Decision::RejectNoPinnedCert};
    }

    // ── 2. Manifest đúng định dạng ──────────────────────────────────────────
    if (m.version == nullptr || m.pcbVersion == nullptr) {
        return {Decision::RejectMalformed};
    }
    if (!isValidSha256Hex(m.sha256Hex)) {
        return {Decision::RejectBadHash};
    }

    // ── 3. Kích thước ───────────────────────────────────────────────────────
    // Ảnh lớn hơn app slot sẽ ghi tràn sang partition kế bên. Chặn ở đây, trước
    // khi ghi byte đầu tiên.
    if (m.sizeBytes < kMinImageBytes || m.sizeBytes > kMaxImageBytes) {
        return {Decision::RejectBadSize};
    }

    // ── 4. PCB khớp ─────────────────────────────────────────────────────────
    // Nạp firmware của PCB khác = sai pin map = hỏng phần cứng.
    if (!strEqual(m.pcbVersion, currentPcb)) {
        return {Decision::RejectPcbMismatch};
    }

    // ── 5. Cùng phiên bản ───────────────────────────────────────────────────
    // Không phải lỗi, nhưng ghi lại cùng bản là tốn một chu kỳ ghi flash và một
    // lần reboot giữa phòng lab, đổi lại không được gì.
    if (strEqual(m.version, currentVersion)) {
        return {Decision::RejectSameVersion};
    }

    // Đến đây mới được phép TẢI. Sau khi tải xong phải tính sha256 thật của
    // ảnh và so lại với m.sha256Hex — hàm này chỉ kiểm ĐỊNH DẠNG hash, không
    // kiểm nội dung (nội dung chưa tồn tại lúc gọi).
    return {Decision::Accept};
}

const char *decisionToString(Decision d) {
    switch (d) {
        case Decision::Accept:             return "accept";
        case Decision::RejectBadHash:      return "reject_bad_hash";
        case Decision::RejectPcbMismatch:  return "reject_pcb_mismatch";
        case Decision::RejectNoPinnedCert: return "reject_no_pinned_cert";
        case Decision::RejectBadSize:      return "reject_bad_size";
        case Decision::RejectSameVersion:  return "reject_same_version";
        case Decision::RejectMalformed:
        default:                           return "reject_malformed";
    }
}

}  // namespace ota
