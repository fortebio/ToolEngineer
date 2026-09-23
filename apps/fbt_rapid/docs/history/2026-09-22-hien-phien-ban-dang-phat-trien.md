# 2026-09-22 — Hiện phiên bản đang phát triển trong app

Yêu cầu chủ dự án: *"bổ sung lên app thông tin version hiện tại đang phát triển"*.

Trước đó app **không in số version ở đâu cả**. Số phát hành nằm trong `installer.iss`
(`MyAppVersion` = 1.0.7) còn `pubspec.yaml` đứng yên ở 1.0.2 suốt 5 bản — nên người cầm máy
không có cách nào biết mình đang chạy bản nào, và câu đầu tiên khi hỗ trợ từ xa ("bản nào?")
không ai trả lời được.

## Một chỗ duy nhất: `lib/util/app_version.dart`

| Hằng | Giá trị mặc định | Ý nghĩa |
|---|---|---|
| `kAppVersion` | `1.1.0` | version ĐANG PHÁT TRIỂN (tab Hiệu chuẩn đi sau 1.0.7) |
| `kAppChannel` | `dev` | `--dart-define=FBT_CHANNEL=release` khi dựng bản phát hành |
| `kAppBuildDate` | `2026-09-22` | `--dart-define=FBT_BUILD_DATE=…` |
| `kAppBuildRev` | rỗng | `--dart-define=FBT_BUILD_REV=<sha>` |
| `kAppLastRelease` | `1.0.7` | = `MyAppVersion` trong `installer.iss` |
| `kDevHighlights` | 2 dòng | việc đang thêm so với bản phát hành |

Nhãn dẫn xuất: `appVersionLabel` → `v1.1.0-dev` · `appVersionFull` → `v1.1.0-dev · 2026-09-22 · <sha>`.

Vì sao không đọc `pubspec.yaml`: đọc được lúc chạy thì phải thêm `package_info_plus`, mà chính
`pubspec.yaml` mới là chỗ đã trôi khỏi thực tế. Version phát hành thật nằm ở `installer.iss`, nên
file này giữ version dev và **test** neo nó vào `installer.iss`.

## Bày ở ba chỗ

1. **Màn đăng nhập** — dòng nhỏ dưới nút Đăng nhập: `v1.1.0-dev · 2026-09-22`. Đây là màn duy nhất
   ai cũng đi qua.
2. **Chân thanh điều hướng** (`_RailVersion`) — rail desktop CHỈ khi bung (thu 64px không nhét được
   chữ) và chân ngăn kéo điện thoại. Bản dev in màu `kWarning` + icon 🔧, tooltip nói rõ "chưa phát
   hành"; bản release in chữ phụ, im lặng. Đệm đáy `_AccountMenu` giảm 16→6 khi bung để dòng version
   không dính sát cạnh cửa sổ.
3. **Thiết lập › Phiên bản** (`_VersionCard`, đặt ngay sau Nhà cung cấp, KHÔNG gác quyền) — version,
   kênh, ngày dựng, commit (nếu có), phát hành gần nhất, ô cảnh báo vàng "bản này chưa phát hành",
   danh sách *Đang thêm trong bản này*, và nút **Chép thông tin phiên bản** để dán vào tin nhắn gửi
   kỹ thuật.

Chuỗi mới nằm trong `util/i18n.dart` nhóm `ver.*` (vi/en).

## Test — `test/app_version_test.dart` (4 test, pass)

- `kAppLastRelease` **phải khớp** `MyAppVersion` đọc thẳng từ `installer.iss` (regex) → phát hành
  xong mà quên đồng bộ là đỏ ngay, đúng lỗi `pubspec.yaml` đã mắc.
- `kAppVersion` là semver và **lớn hơn hẳn** `kAppLastRelease` khi đang ở kênh dev (nếu không thì
  nhãn `-dev` nói dối); bằng nhau khi kênh release.
- `kAppBuildDate` đúng dạng `YYYY-MM-DD`; nhãn hiển thị mang đủ số version.

## Quy trình phát hành (ghi cả vào `CLAUDE.md`)

Nâng `MyAppVersion` trong `installer.iss` lên `kAppVersion` → đặt `kAppLastRelease` bằng nó → dọn
`kDevHighlights` → nâng `kAppVersion` cho chu kỳ kế tiếp → dựng với
`--dart-define=FBT_CHANNEL=release --dart-define=FBT_BUILD_DATE=… --dart-define=FBT_BUILD_REV=…`.

## Đã kiểm

`flutter analyze lib/` — không error/warning (10 `info` có sẵn từ trước). `flutter test
test/app_version_test.dart` 4/4. Bản web dựng với `mock-server.js` và xem thật trên trình duyệt:
màn đăng nhập, rail bung, ngăn kéo điện thoại 375px, thẻ Thiết lập › Phiên bản — cả bốn đúng như mô tả.

⚠️ `test/widget_test.dart` đang ĐỎ từ TRƯỚC thay đổi này (tìm tab `Cloud` không còn tồn tại; app giờ
mở ở màn đăng nhập) — không liên quan, chưa sửa.
