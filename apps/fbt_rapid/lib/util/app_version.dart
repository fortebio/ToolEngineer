/// Phiên bản ứng dụng — **một chỗ duy nhất** trong code Dart.
///
/// Version PHÁT HÀNH nằm ở `installer.iss` (`MyAppVersion`, khớp tag commit
/// `vX.Y.Z`), KHÔNG phải `pubspec.yaml` (vẫn 1.0.2 — xem `CLAUDE.md`). File này
/// giữ version ĐANG PHÁT TRIỂN ([kAppVersion]) — luôn đi trước bản phát hành
/// gần nhất ([kAppLastRelease]). `test/app_version_test.dart` đọc `installer.iss`
/// và so với [kAppLastRelease] để hai chỗ không trôi khỏi nhau.
///
/// **Lúc phát hành**: nâng `MyAppVersion` trong `installer.iss` lên
/// [kAppVersion], đặt [kAppLastRelease] bằng nó, dọn [kDevHighlights], rồi dựng
/// với `--dart-define=FBT_CHANNEL=release`.
///
/// **Kênh** ([kAppChannel]) mặc định `dev` = bản ĐANG PHÁT TRIỂN, chưa phát
/// hành. Khi dựng bản phát hành thì truyền dart-define:
///
/// ```
/// flutter build windows --release --dart-define=FBT_CHANNEL=release \
///   --dart-define=FBT_BUILD_DATE=2026-10-01 --dart-define=FBT_BUILD_REV=0650dee
/// ```
///
/// Không truyền gì thì app hiện đúng cái nó là: `v1.1.0-dev · 2026-09-22`.
library;

/// Số version đang phát triển (semver, không kèm hậu tố kênh).
const String kAppVersion = '1.1.0';

/// Kênh phát hành: `dev` (đang phát triển) hoặc `release` (đã đóng gói).
const String kAppChannel =
    String.fromEnvironment('FBT_CHANNEL', defaultValue: 'dev');

/// Ngày dựng bản này (YYYY-MM-DD). Mặc định = ngày sửa version gần nhất.
const String kAppBuildDate =
    String.fromEnvironment('FBT_BUILD_DATE', defaultValue: '2026-09-22');

/// Commit dựng bản này (rỗng nếu build tay, không truyền dart-define).
const String kAppBuildRev =
    String.fromEnvironment('FBT_BUILD_REV', defaultValue: '');

/// Bản PHÁT HÀNH gần nhất = `MyAppVersion` trong `installer.iss` (test giữ khớp).
const String kAppLastRelease = '1.0.7';

/// `true` khi đây KHÔNG phải bản phát hành (mặc định lúc chạy từ nguồn).
bool get kIsDevBuild => kAppChannel != 'release';

/// Nhãn ngắn dán được ở chân rail / màn đăng nhập: `v1.1.0-dev`.
String get appVersionLabel => kIsDevBuild ? 'v$kAppVersion-$kAppChannel' : 'v$kAppVersion';

/// Nhãn đầy đủ cho màn Thiết lập: `v1.1.0-dev · 2026-09-22 · 0650dee`.
String get appVersionFull {
  final parts = <String>[appVersionLabel, kAppBuildDate];
  if (kAppBuildRev.isNotEmpty) {
    parts.add(kAppBuildRev.length > 7 ? kAppBuildRev.substring(0, 7) : kAppBuildRev);
  }
  return parts.join(' · ');
}

/// Những gì version đang phát triển này thêm so với [kAppLastRelease].
///
/// Giữ NGẮN (một dòng một việc, tiếng Việt thường) — đây là thứ nhân viên đọc
/// để biết bản trên máy mình có tính năng đang bàn hay chưa, không phải
/// changelog đầy đủ (changelog nằm ở `docs/history/`).
const List<String> kDevHighlights = [
  'Tab Hiệu chuẩn: lô pha ống chuẩn quang, đo ống, xếp hạng & đóng bộ (2026-09-21)',
  'Tab Hiệu chuẩn: bố cục theo việc — thanh tiến độ, "việc tiếp theo", nút ĐỌC to (2026-09-22)',
];
