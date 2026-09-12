import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/ate_record.dart';
import 'package:RapidPlusApp/models/user_session.dart';
import 'package:RapidPlusApp/screens/ate_run_screen.dart';
import 'package:RapidPlusApp/services/app_settings.dart';
import 'package:RapidPlusApp/services/ate_api.dart';
import 'package:RapidPlusApp/services/session_store.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

/// Nhánh giao diện của **bản web**: ba ô chọn firmware lấy tên từ kho OTA của
/// server. Test được trên VM nhờ cửa tiêm `useServerFirmware` + `binLister`.
///
/// Viết sau một lần chẩn đoán SAI (2026-09-09): thấy ô bootloader/partition
/// trống trên bản web, tôi đoán do `initialValue` của `FormField` chỉ gieo một
/// lần. Bộ test này bác bỏ giả thuyết đó (hai ca đầu xanh cả khi danh sách bin
/// về muộn); sự thật là kho trên server đã đổi sang bộ tên khác nên tên đã lưu
/// không còn tồn tại. Giữ lại vì nó khoá đúng nhánh giao diện của bản web —
/// nhánh mà `flutter test` trên VM không chạm tới nếu không có cửa tiêm.

class _Api extends AteApi {
  _Api() : super('http://test.local');

  @override
  Future<AteLimits> limits({String batch = ''}) async =>
      AteLimits.fromJson({'version': 'L1', 'batch': batch, 'source': 'chung'});
}

const _bins = ['fbt_v2.4.4.bin', 'partitions.bin', 'bootloader.bin'];

Future<void> _pump(
  WidgetTester t, {
  Duration binDelay = Duration.zero,
  List<String> bins = _bins,
}) async {
  SharedPreferences.setMockInitialValues({
    'ate_station': 'TRAM-01',
    'ate_batch': 'L2609A',
    'ate_bin_bootloader': 'bootloader.bin',
    'ate_bin_partition': 'partitions.bin',
    'ate_bin_app': 'fbt_v2.4.4.bin',
  });
  t.view.physicalSize = const Size(1280, 1600);
  t.view.devicePixelRatio = 1.0;
  addTearDown(t.view.reset);
  await t.pumpWidget(MaterialApp(
    theme: appTheme(),
    home: Scaffold(
      body: AteRunScreen(
        settings: AppSettings(),
        api: _Api(),
        // Web chưa xin quyền cổng → chưa "đủ để chạy" → phần Cấu hình trạm
        // mở sẵn, đúng cảnh thợ mở trạm đầu ca.
        portLister: () => const [],
        useServerFirmware: true,
        binLister: () async {
          if (binDelay > Duration.zero) await Future<void>.delayed(binDelay);
          return bins;
        },
      ),
    ),
  ));
  await t.pump();
  await t.pump(binDelay + const Duration(milliseconds: 50));
  await t.pumpAndSettle();
}

/// Kiểm tên bản ĐANG HIỆN trong ô chọn có nhãn [label].
///
/// Cố tình soi CHỮ TRÊN MÀN chứ không soi thuộc tính `initialValue` của widget:
/// `initialValue` luôn đúng (nó tính lại mỗi lần build), nhưng `FormField` chỉ
/// GIEO nó một lần — đúng cái khe làm màn hiện "chưa chọn" trong khi cấu hình
/// vẫn giữ tên bản. Soi thuộc tính là test tự lừa mình.
void _expectShows(WidgetTester t, String label, String name) {
  final field = find.ancestor(
    of: find.text(label),
    matching: find.byType(DropdownButtonFormField<String>),
  );
  expect(field, findsOneWidget, reason: 'không thấy ô "$label"');
  expect(find.descendant(of: field, matching: find.text(name)), findsOneWidget,
      reason: 'ô "$label" không hiện "$name"');
}

void main() {
  setUpAll(() {
    SessionStore.current = const UserSession(
      username: 'tho',
      name: 'Thợ',
      role: UserRole.operator,
      ids: [],
      allowAll: false,
    );
  });

  testWidgets('ba ô hiện ĐÚNG bản đã lưu khi danh sách bin về TRƯỚC cấu hình',
      (t) async {
    await _pump(t);
    _expectShows(t, 'Chọn bootloader', 'bootloader.bin');
    _expectShows(t, 'Chọn partition', 'partitions.bin');
    _expectShows(t, 'Chọn firmware', 'fbt_v2.4.4.bin');
  });

  testWidgets('… và cả khi danh sách bin về SAU (server chậm)', (t) async {
    // Đây là thứ tự làm lộ lỗi: ô dựng lần đầu với danh sách RỖNG.
    await _pump(t, binDelay: const Duration(milliseconds: 300));
    _expectShows(t, 'Chọn bootloader', 'bootloader.bin');
    _expectShows(t, 'Chọn partition', 'partitions.bin');
    _expectShows(t, 'Chọn firmware', 'fbt_v2.4.4.bin');
  });

  /// Chuyện đã xảy ra thật (2026-09-09): kho đổi sang bộ tên mới
  /// (`fbt_v2.4.4-bootloader.bin`…) trong khi trạm còn giữ tên cũ. Ô chọn hiện
  /// TRỐNG — nhìn y như chưa ai cấu hình, không nói được là "bản đã biến mất".
  testWidgets('bản đã lưu không còn trong kho thì NÓI RA', (t) async {
    await _pump(t, bins: const [
      'fbt_v2.4.4.bin',
      'fbt_v2.4.4-partitions.bin',
      'fbt_v2.4.4-bootloader.bin',
    ]);
    expect(find.textContaining('"bootloader.bin" KHÔNG còn trong kho'),
        findsOneWidget);
    expect(find.textContaining('"partitions.bin" KHÔNG còn trong kho'),
        findsOneWidget);
    // Bản app vẫn có trong kho → không báo động thừa.
    expect(find.textContaining('"fbt_v2.4.4.bin" KHÔNG còn'), findsNothing);
  });

  testWidgets('menu đổ đủ các bản trong kho', (t) async {
    await _pump(t);
    await t.tap(find.text('Chọn bootloader'));
    await t.pumpAndSettle();
    // Mỗi tên xuất hiện ở ô đang chọn + trong menu → chỉ cần "có mặt".
    for (final n in _bins) {
      expect(find.text(n), findsWidgets, reason: 'menu thiếu $n');
    }
  });
}
