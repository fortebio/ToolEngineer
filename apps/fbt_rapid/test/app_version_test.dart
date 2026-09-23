import 'dart:io';

import 'package:RapidPlusApp/util/app_version.dart';
import 'package:flutter_test/flutter_test.dart';

/// Khoá số phiên bản khỏi trôi.
///
/// App hiện version ở ba chỗ (màn đăng nhập, chân thanh điều hướng, Thiết lập ›
/// Phiên bản) và cả ba đọc `util/app_version.dart`. Nhưng số PHÁT HÀNH thật lại
/// nằm trong `installer.iss` (`MyAppVersion` — tên file setup + AppVersion của
/// Windows), và lịch sử repo cho thấy hai chỗ khác nhau thì sẽ quên đồng bộ:
/// `pubspec.yaml` đã đứng yên ở 1.0.2 suốt 5 bản phát hành.
///
/// Vì vậy test đọc thẳng `installer.iss` và so với [kAppLastRelease].
void main() {
  test('kAppLastRelease khớp MyAppVersion trong installer.iss', () {
    final iss = File('installer.iss').readAsStringSync();
    final m = RegExp(r'#define\s+MyAppVersion\s+"([^"]+)"').firstMatch(iss);
    expect(m, isNotNull, reason: 'installer.iss không còn dòng #define MyAppVersion');
    expect(
      m!.group(1),
      kAppLastRelease,
      reason: 'Phát hành xong thì đặt kAppLastRelease = MyAppVersion '
          '(và nâng kAppVersion cho chu kỳ phát triển kế tiếp).',
    );
  });

  test('kAppVersion là semver và đi TRƯỚC bản phát hành gần nhất', () {
    List<int> parse(String v) {
      final p = v.split('.');
      expect(p.length, 3, reason: 'version phải dạng X.Y.Z: $v');
      return [for (final x in p) int.parse(x)];
    }

    final dev = parse(kAppVersion);
    final rel = parse(kAppLastRelease);
    // So từng bậc: dev >= rel, và nếu đang là bản dev thì phải LỚN HƠN hẳn.
    var cmp = 0;
    for (var i = 0; i < 3 && cmp == 0; i++) {
      cmp = dev[i].compareTo(rel[i]);
    }
    if (kIsDevBuild) {
      expect(cmp, greaterThan(0),
          reason: 'Bản đang phát triển ($kAppVersion) phải lớn hơn bản đã phát '
              'hành ($kAppLastRelease) — nếu không thì nhãn "-dev" nói dối.');
    } else {
      expect(cmp, 0,
          reason: 'Bản release thì kAppVersion phải bằng kAppLastRelease.');
    }
  });

  test('ngày dựng đúng dạng YYYY-MM-DD', () {
    expect(RegExp(r'^\d{4}-\d{2}-\d{2}$').hasMatch(kAppBuildDate), isTrue,
        reason: 'kAppBuildDate = $kAppBuildDate');
  });

  test('nhãn hiển thị mang đủ số version', () {
    expect(appVersionLabel, contains(kAppVersion));
    expect(appVersionFull, contains(kAppVersion));
    expect(appVersionFull, contains(kAppBuildDate));
    // Mặc định (không dart-define) là bản đang phát triển.
    expect(kIsDevBuild, isTrue);
    expect(appVersionLabel, endsWith('-dev'));
  });
}
