# Changelog

Định dạng theo [Keep a Changelog](https://keepachangelog.com/vi/1.1.0/),
phiên bản theo [SemVer](https://semver.org/lang/vi/).

Mô tả theo **góc nhìn người dùng**, không phải góc nhìn code.

> **Bắt buộc với thiết bị chẩn đoán:** mọi thay đổi chạm an toàn phải có mục
> **Security/Safety** riêng, kể cả khi "chỉ đổi một hằng số". Đây là dấu vết audit
> (IEC 62304). Xem [docs/RELEASE.md](docs/RELEASE.md).

## [Unreleased]

### Added
- Khung dự án: PlatformIO 3 env (`esp32dev`, `native`, `esp32dev_test`).
- Lớp an toàn nhiệt độc lập với PID (`src/safety/`) — hard-limit bottom 99 °C,
  hot-lid 85 °C, phát hiện sensor hỏng/timeout/NaN.
- Validate + clamp remote config (`src/config/`).
- Verify OTA: sha256 + pinned cert + pcb_version (`src/ota/`).
- 26 test chạy trên host, không cần phần cứng.
- CI: build firmware + chạy test native trên mọi PR.

<!--
Nhóm dùng được (chỉ ghi nhóm thực sự có mục):
### Added            — tính năng mới
### Changed          — thay đổi hành vi có sẵn
### Deprecated       — sắp bỏ
### Removed          — đã bỏ
### Fixed            — sửa lỗi
### Security/Safety  — an toàn, bảo mật, lỗ hổng

CẢNH BÁO BẮT BUỘC nếu:
  - Đổi partition table → máy đã bán KHÔNG OTA sang được, phải nạp USB tay
  - Đổi ngưỡng an toàn  → ghi rõ giá trị cũ/mới + ai duyệt
  - Phá tương thích protocol cloud
-->
