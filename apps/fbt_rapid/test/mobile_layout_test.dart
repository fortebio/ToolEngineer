// Hai màn của bản WEB xem trên ĐIỆN THOẠI: "Cập nhật OTA" (Quản lý máy) và
// "Lịch sử" (Engineer Server). Người dùng báo lệch chữ / tràn chữ ở khổ ~390px.
//
// Vì sao là test chứ không phải nhìn bằng mắt: cả hai màn nằm sau đăng nhập + server
// thật, nên không chụp bằng trình duyệt ở đây được. Server giả loopback (cùng cách
// test/ota_api_test.dart đang làm) đưa dữ liệu thật vào bố cục thật.
//
// Đo HÌNH HỌC, không so ảnh golden: dòng "cập nhật <giờ>" của màn Lịch sử lấy
// DateTime.now() nên ảnh đổi theo từng phút -> golden sẽ đỏ vặt mỗi lần chạy mà
// chẳng lỗi gì. Chiều cao một dòng chữ thì ổn định, và nó chính là thứ hỏng: chữ
// vỡ xuống dòng giữa chừng.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle, FontLoader;
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/user_session.dart';
import 'package:RapidPlusApp/screens/cloud_runs_screen.dart';
import 'package:RapidPlusApp/services/cloud_history_api.dart';
import 'package:RapidPlusApp/screens/history_combined_screen.dart';
import 'package:RapidPlusApp/screens/manager_machine_screen.dart';
import 'package:RapidPlusApp/services/app_settings.dart';
import 'package:RapidPlusApp/services/session_store.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

// Khổ iPhone 14 / Pixel 7 theo chiều dọc — dưới kMobileMaxWidth (640).
const _phone = Size(390, 844);

Future<void> _loadFonts() async {
  for (final f in const {
    'DM Sans': 'assets/fonts/DMSans.ttf',
    'JetBrains Mono': 'assets/fonts/JetBrainsMono.ttf',
  }.entries) {
    final loader = FontLoader(f.key)..addFont(rootBundle.load(f.value));
    await loader.load();
  }
}

/// Server giả trả đúng hợp đồng `server/app/main.py` cho 2 endpoint 2 màn này đọc.
Future<HttpServer> _fakeServer() async {
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  server.listen((req) async {
    Object body;
    switch (req.uri.path) {
      case '/ota':
        body = {
          'target': 'fbt_v2.4.5.bin',
          'target_by': 'nguyenvandat',
          'target_at': '2026-08-19T03:20:00Z',
          'devices': {'RPL03003': {'file': 'fbt_v2.4.4.bin', 'by': 'dat', 'at': '2026-08-18T02:00:00Z'}},
          'files': [
            {'name': 'fbt_v2.4.5.bin', 'size': 2193408, 'modified': '2026-08-19T03:15:00Z'},
            {'name': 'fbt_v2.4.4_rc1.bin', 'size': 2190112, 'modified': '2026-08-12T09:00:00Z'},
          ],
        };
        break;
      case '/devices':
        body = [
          {'id_device': 'RPL03003', 'sessions': 1284, 'last_seen': '2026-08-19T07:12:00Z', 'version': 'v2.4.5'},
          {'id_device': 'RPL02013', 'sessions': 7, 'last_seen': '2026-06-01T01:02:00Z', 'version': 'v2.4.3'},
        ];
        break;
      default: // /sessions
        body = {
          'total': 128,
          'page': 1,
          'limit': 10,
          'items': [
            for (var i = 0; i < 10; i++)
              {
                'id': 900 + i,
                'id_device': 'RPL03003',
                'received_at': '2026-08-1${i}T07:12:00Z',
                'version': 'v2.4.5',
                'type_upload': 'auto',
                'ct_value': [22.3, 0, 31.1, 0, 0, 0, 0, 0, 0, 0],
                'result': ['22.3 | P', '0 | N', '31.1 | S', '0 | N', '0 | E',
                           '0 | N', '0 | N', '0 | N', '0 | N', '0 | N'],
              },
          ],
        };
    }
    req.response
      ..headers.contentType = ContentType.json
      ..write(jsonEncode(body));
    await req.response.close();
  });
  return server;
}

Widget _man(Widget child) => MaterialApp(
      theme: appTheme(),
      home: Scaffold(body: child),
    );

void main() {
  late HttpServer server;
  late AppSettings settings;

  setUpAll(() async {
    // flutter_test cắm HttpOverrides trả 400 cho MỌI request -> màn chỉ hiện lỗi,
    // không hiện dữ liệu, và bố cục cần soi thì không bao giờ được dựng.
    HttpOverrides.global = null;
    await _loadFonts();
  });

  setUp(() async {
    // CloudCache của màn Lịch sử đọc prefs ngay lúc dựng -> không mock là
    // MissingPluginException, không phải lỗi bố cục.
    SharedPreferences.setMockInitialValues({});
    server = await _fakeServer();
    settings = AppSettings(engineerUrl: 'http://127.0.0.1:${server.port}');
    // Root: thấy mọi máy + mọi nút ghi (đúng vai người dùng đang gặp lỗi).
    SessionStore.current = const UserSession(
      username: 'root', name: 'Root', role: UserRole.root, ids: ['*'], allowAll: true);
  });

  tearDown(() async {
    await server.close(force: true);
    SessionStore.current = null;
  });

  Future<void> pumpPhone(WidgetTester tester, Widget child,
      {Size size = _phone}) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    // HTTP thật cần THỜI GIAN THẬT: trong widget test đồng hồ là giả, pumpAndSettle
    // chỉ tua đồng hồ giả nên request chưa kịp về đã chạm timeout 20 s của FbtApi.
    // runAsync là chỗ duy nhất async thật chạy được.
    await tester.runAsync(() async {
      await tester.pumpWidget(_man(child));
      await Future<void>.delayed(const Duration(milliseconds: 400));
      await tester.pump();
    });
    await tester.pumpAndSettle();
  }

  // 360px = khổ hẹp phổ biến nhất của Android; gãy bố cục thì gãy ở đây trước.
  const hep = Size(360, 760);
  // Cửa sổ rộng phải GIỮ NGUYÊN bố cục cũ — sửa cho điện thoại mà làm hỏng desktop
  // là đổi một lỗi lấy một lỗi (đã xảy ra thật: dải trạng thái trôi vào giữa màn).
  const rong = Size(1280, 900);

  // Một dòng chữ ~14px cao 18-20px. >26 nghĩa là nó đã vỡ xuống dòng thứ hai.
  double h(WidgetTester t, Finder f) => t.getRect(f).height;

  testWidgets('OTA 360px: tên file KHÔNG vỡ dòng, hành động xuống hàng riêng',
      (tester) async {
    await pumpPhone(tester, ManagerMachineScreen(settings: settings), size: hep);
    // Tên file LÀ version ở màn này -> vỡ giữa chữ ("fbt_v2. / 4.4_rc1 / .bin") là
    // đọc nhầm bản cho cả fleet. Cả tên lẫn dòng "2.09 MB · <ngày>" phải một dòng.
    expect(h(tester, find.text('fbt_v2.4.4_rc1.bin')), lessThan(26));
    expect(h(tester, find.textContaining('2.09 MB').first), lessThan(26));
    // Dải trạng thái phải còn đủ chỗ đọc TÊN BẢN, không bị bóp còn "Máy sẽ nạ…".
    expect(tester.getRect(find.textContaining('Máy sẽ nạp')).width,
        greaterThan(200));
    expect(tester.takeException(), isNull);
  });

  testWidgets('OTA cửa sổ rộng: banner sát mép trái, nút tải lên sát mép phải',
      (tester) async {
    await pumpPhone(tester, ManagerMachineScreen(settings: settings), size: rong);
    // `Column` mặc định căn GIỮA: khi dải trạng thái đổi từ `Row` (chiếm hết bề
    // ngang) sang `Wrap` (co theo nội dung), cả hàng trôi vào giữa màn.
    expect(tester.getRect(find.textContaining('Máy sẽ nạp')).left, lessThan(120));
    expect(tester.getRect(find.textContaining('Tải firmware')).right,
        greaterThan(rong.width - 220));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Lịch sử 360px: dòng phụ thẻ máy không vỡ giữa câu', (tester) async {
    await pumpPhone(tester, HistoryCombinedScreen(settings: settings), size: hep);
    expect(h(tester, find.textContaining('1284 lần chạy').first), lessThan(26));
    expect(h(tester, find.textContaining('mới nhất').first), lessThan(26));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Lịch sử: các màn con không tràn ở khổ điện thoại', (tester) async {
    await pumpPhone(tester, HistoryCombinedScreen(settings: settings));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Lịch sử — danh sách lần chạy không tràn ở khổ điện thoại',
      (tester) async {
    await pumpPhone(
      tester,
      CloudRunsScreen(
        settings: settings,
        source: CloudSource.engineer,
        device: const CloudDevice(
            id: 'RPL03003', runCount: 1284, version: 'v2.4.5'),
      ),
    );
    expect(tester.takeException(), isNull);
  });
}
