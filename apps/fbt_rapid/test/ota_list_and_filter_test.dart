// Ba việc của tab "Quản lý máy" (2026-08-20):
//  1. Kho .bin: bản ĐANG CHỌN lên đầu, còn lại mới-nhất-trước theo ngày tải lên.
//  2. Không xoá được bản đang chọn / đang ghim riêng cho máy nào.
//  3. Trạng thái máy: lọc theo firmware.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/user_session.dart';
import 'package:RapidPlusApp/screens/manager_machine_screen.dart';
import 'package:RapidPlusApp/services/app_settings.dart';
import 'package:RapidPlusApp/services/session_store.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

// Kho cố ý XÁO TRỘN thứ tự và cố ý để bản đang chọn KHÔNG phải bản mới nhất — nếu không thì
// "ghim lên đầu" và "mới nhất trước" cho cùng kết quả và test không phân biệt được hai luật.
//   fbt_v2.4.6.bin  22/08  <- mới nhất
//   fbt_v2.4.4.bin  12/08  <- ĐANG CHỌN (target)
//   fbt_v2.4.5.bin  19/08
//   fbt_old.bin     (không có ngày)
// Ghim riêng: RPL03003 -> fbt_v2.4.5.bin
Future<HttpServer> _fake() async {
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  server.listen((req) async {
    Object body;
    switch (req.uri.path) {
      case '/ota':
        body = {
          'target': 'fbt_v2.4.4.bin',
          'devices': {
            'RPL03003': {
              'file': 'fbt_v2.4.5.bin',
              'by': 'dat',
              'at': '2026-08-18T02:00:00Z'
            }
          },
          'files': [
            {'name': 'fbt_v2.4.5.bin', 'size': 2193408, 'modified': '2026-08-19T03:15:00Z'},
            {'name': 'fbt_old.bin', 'size': 1000},
            {'name': 'fbt_v2.4.6.bin', 'size': 2200000, 'modified': '2026-08-22T09:00:00Z'},
            {'name': 'fbt_v2.4.4.bin', 'size': 2190112, 'modified': '2026-08-12T09:00:00Z'},
          ],
        };
        break;
      case '/devices':
        body = [
          {'id_device': 'RPL03003', 'sessions': 12, 'last_seen': '2026-08-19T07:12:00Z', 'version': 'v2.4.5'},
          {'id_device': 'RPL02013', 'sessions': 7, 'last_seen': '2026-08-19T06:00:00Z', 'version': 'v2.4.5'},
          {'id_device': 'RPL01001', 'sessions': 3, 'last_seen': '2026-06-01T01:02:00Z', 'version': 'V2.3.1'},
          {'id_device': 'RPL09999', 'sessions': 1, 'last_seen': '2026-08-01T01:02:00Z', 'version': ''},
        ];
        break;
      default:
        body = {'total': 0, 'page': 1, 'limit': 10, 'items': <dynamic>[]};
    }
    req.response
      ..headers.contentType = ContentType.json
      ..write(jsonEncode(body));
    await req.response.close();
  });
  return server;
}

void main() {
  late HttpServer server;
  late AppSettings settings;

  setUpAll(() => HttpOverrides.global = null);

  setUp(() async {
    SharedPreferences.setMockInitialValues({});
    server = await _fake();
    settings = AppSettings(engineerUrl: 'http://127.0.0.1:${server.port}');
    SessionStore.current = const UserSession(
        username: 'root',
        name: 'Root',
        role: UserRole.root,
        ids: ['*'],
        allowAll: true);
  });

  tearDown(() async {
    await server.close(force: true);
    SessionStore.current = null;
  });

  Future<void> mo(WidgetTester tester, {String? sangTab}) async {
    tester.view.physicalSize = const Size(1280, 900);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    await tester.runAsync(() async {
      await tester.pumpWidget(MaterialApp(
        theme: appTheme(),
        home: Scaffold(body: ManagerMachineScreen(settings: settings)),
      ));
      await Future<void>.delayed(const Duration(milliseconds: 400));
      await tester.pump();
    });
    await tester.pumpAndSettle();
    if (sangTab != null) {
      await tester.tap(find.text(sangTab));
      await tester.pumpAndSettle();
    }
  }

  /// Thứ tự các tên file THEO ĐÚNG vị trí dọc trên màn.
  List<String> thuTuFile(WidgetTester t) {
    final names = <String, double>{};
    for (final n in const [
      'fbt_v2.4.4.bin', 'fbt_v2.4.5.bin', 'fbt_v2.4.6.bin', 'fbt_old.bin'
    ]) {
      final f = find.text(n);
      if (f.evaluate().isNotEmpty) names[n] = t.getTopLeft(f.first).dy;
    }
    final ks = names.keys.toList()
      ..sort((a, b) => names[a]!.compareTo(names[b]!));
    return ks;
  }

  testWidgets('kho .bin: bản ĐANG CHỌN lên đầu, còn lại mới nhất trước',
      (tester) async {
    await mo(tester);
    expect(
      thuTuFile(tester),
      // v2.4.4 đang chọn (dù là bản CŨ NHẤT có ngày) -> vẫn đứng đầu.
      // Rồi 22/08, 19/08, và bản KHÔNG có ngày xuống cuối.
      ['fbt_v2.4.4.bin', 'fbt_v2.4.6.bin', 'fbt_v2.4.5.bin', 'fbt_old.bin'],
    );
  });

  testWidgets('KHÔNG xoá được bản đang chọn và bản đang ghim riêng',
      (tester) async {
    await mo(tester);
    final nut = find.byIcon(Icons.delete_outline);
    expect(nut, findsNWidgets(4)); // nút vẫn HIỆN cho cả 4, không ẩn đi

    // `byIcon` trả về widget Icon, KHÔNG phải nút bọc nó — phải leo lên IconButton.
    bool bamDuoc(int i) => tester
            .widget<IconButton>(find
                .ancestor(of: nut.at(i), matching: find.byType(IconButton))
                .first)
            .onPressed !=
        null;

    // Thứ tự dọc: v2.4.4 (target) · v2.4.6 · v2.4.5 (ghim riêng) · old
    expect(bamDuoc(0), isFalse, reason: 'bản ĐANG CHỌN phải chặn xoá');
    expect(bamDuoc(1), isTrue, reason: 'bản rảnh vẫn xoá được');
    expect(bamDuoc(2), isFalse, reason: 'bản đang ghim riêng phải chặn xoá');
    expect(bamDuoc(3), isTrue);
  });

  testWidgets('lý do chặn NÓI RÕ chặn vì đâu, không gộp một câu', (tester) async {
    await mo(tester);
    // Hai ca khác nhau -> hai câu khác nhau, để khỏi phải đi dò "dùng ở đâu".
    expect(find.byTooltip(
        'Không xoá được: đây là bản ĐANG CHỌN cho cả fleet. Bỏ chọn trước rồi mới xoá.'),
        findsOneWidget);
    expect(find.byTooltip(
        'Không xoá được: đang ghim riêng cho 1 máy. Gỡ ghim ở cột "Trạng thái update" trước.'),
        findsOneWidget);
  });

  testWidgets('Trạng thái máy: lọc theo firmware', (tester) async {
    await mo(tester, sangTab: 'Trạng thái máy');
    expect(find.text('RPL03003'), findsOneWidget);
    expect(find.text('RPL01001'), findsOneWidget);

    await tester.tap(find.byIcon(Icons.filter_list));
    await tester.pumpAndSettle();
    // Menu liệt kê version ĐANG CÓ + số máy, nhiều nhất trước.
    expect(find.textContaining('v2.4.5  (2)'), findsOneWidget);
    expect(find.textContaining('V2.3.1  (1)'), findsOneWidget);

    await tester.tap(find.textContaining('v2.4.5  (2)'));
    await tester.pumpAndSettle();

    expect(find.text('RPL03003'), findsOneWidget);
    expect(find.text('RPL02013'), findsOneWidget);
    expect(find.text('RPL01001'), findsNothing, reason: 'V2.3.1 phải bị lọc ra');
    // Đang lọc thì PHẢI nói ra đang nhìn bao nhiêu trên tổng — và con số phải tính cả
    // bộ lọc firmware, không chỉ ô tìm.
    expect(find.text('2/4'), findsOneWidget);
  });

  testWidgets('lọc KHÔNG gộp hoa/thường và không gộp hậu tố', (tester) async {
    // `V2.3.1` (V hoa) là một mục RIÊNG, không nhập vào `v2.4.5`. Gộp cho gọn là nói dối
    // về việc fleet đang chạy gì.
    await mo(tester, sangTab: 'Trạng thái máy');
    await tester.tap(find.byIcon(Icons.filter_list));
    await tester.pumpAndSettle();
    // Khớp ĐÚNG nhãn menu (có số máy) — `textContaining('V2.3.1')` trần trụi còn trúng cả
    // ô Firmware của hàng RPL01001 trong bảng phía sau.
    expect(find.textContaining('V2.3.1  (1)'), findsOneWidget);
    expect(find.textContaining('v2.3.1  ('), findsNothing);
  });

  testWidgets('máy CHƯA báo version có mục lọc riêng', (tester) async {
    await mo(tester, sangTab: 'Trạng thái máy');
    await tester.tap(find.byIcon(Icons.filter_list));
    await tester.pumpAndSettle();
    await tester.tap(find.textContaining('chưa báo'));
    await tester.pumpAndSettle();
    expect(find.text('RPL09999'), findsOneWidget);
    expect(find.text('RPL03003'), findsNothing);
  });
}
