// Hộp "Lịch sử cập nhật" phải hiện **ngày cập nhật THẬT của khách** (mốc máy tự
// khai về server), không phải ngày lần đo đầu tiên báo bản đó.
//
// Vì sao là test WIDGET chứ không chỉ test hàm ghép: `mergeFirmwareLog` có test
// riêng rồi, nhưng nó đúng mà ô ngày vẫn in `firstSeen` thì tính năng vẫn sai —
// và đó đúng là kiểu lỗi đã dẫm một lần (xem CLAUDE.md: "ĐO THỨ ĐÃ RENDER").
// Ở đây đo chuỗi ngày THẬT SỰ hiện trên màn.
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

// Khách nạp v2.4.5 lúc 15/08 09:00 (máy tự khai ngay), mãi 18/08 mới chạy mẫu.
const _mocThat = '2026-08-15T09:00:00Z';
const _lanDoDau = '2026-08-18T10:00:00Z';

/// `fwLog` có trả mốc hay không — để dựng được CẢ ca server chưa deploy.
bool _coNhatKy = true;

/// Nhat ky co them mot lan NAP LAI cung ban (`how: "update"`).
bool _napLai = false;

Future<HttpServer> _fakeServer() async {
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  server.listen((req) async {
    final p = req.uri.path;
    Object body;
    var code = 200;
    if (p == '/ota') {
      body = {'target': null, 'devices': {}, 'files': <dynamic>[]};
    } else if (p == '/devices') {
      body = [
        {'id_device': 'RPL03003', 'sessions': 3,
         'last_seen': '2026-08-19T07:12:00Z', 'version': 'v2.4.5'},
      ];
    } else if (p.endsWith('/fw-log')) {
      if (!_coNhatKy) {
        // Server CHƯA deploy route: catch-all `POST /{path}` khớp path, sai
        // method -> 405. App phải sống sót, không phải hỏng cả hộp thoại.
        code = 405;
        body = {'ok': false, 'error': 'Method Not Allowed'};
      } else {
        body = [
          {'version': 'v2.4.5', 'at': _mocThat},
          if (_napLai)
            {
              'version': 'v2.4.5',
              'at': '2026-08-19T02:00:00Z',
              'how': 'update',
            },
        ];
      }
    } else {
      body = {
        'total': 4, 'page': 1, 'limit': 200,
        'items': [
          {'id': 0, 'id_device': 'RPL03003', 'received_at': '2026-03-01T10:00:00Z',
           'version': 'v2.4.3', 'type_upload': 'auto'},
          {'id': 1, 'id_device': 'RPL03003', 'received_at': '2026-06-02T10:00:00Z',
           'version': 'v2.4.4', 'type_upload': 'auto'},
          {'id': 2, 'id_device': 'RPL03003', 'received_at': _lanDoDau,
           'version': 'v2.4.5', 'type_upload': 'auto'},
          {'id': 3, 'id_device': 'RPL03003', 'received_at': '2026-08-19T07:12:00Z',
           'version': 'v2.4.5', 'type_upload': 'auto'},
        ],
      };
    }
    req.response
      ..statusCode = code
      ..headers.contentType = ContentType.json
      ..write(jsonEncode(body));
    await req.response.close();
  });
  return server;
}

/// Chỉ tìm TRONG hộp thoại: bảng phía sau cũng in version, ghi chú chân hộp
/// cũng chứa dấu `≈` -> tìm toàn màn là bắt nhầm rồi kết luận sai.
Finder _trongHop(Finder f) =>
    find.descendant(of: find.byType(AlertDialog), matching: f);

/// Nhãn `mm.histGuess` — khớp ĐÚNG chuỗi, không dùng textContaining('≈').
const _nhanSuyDoan = '≈ suy từ lần đo đầu';

String _hienThi(String iso) {
  final t = DateTime.parse(iso).toLocal();
  return '${t.day.toString().padLeft(2, '0')}/'
      '${t.month.toString().padLeft(2, '0')}/${t.year} '
      '${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}';
}

void main() {
  late HttpServer server;
  late AppSettings settings;

  setUpAll(() => HttpOverrides.global = null);

  setUp(() async {
    _coNhatKy = true;
    _napLai = false;
    SharedPreferences.setMockInitialValues({});
    server = await _fakeServer();
    settings = AppSettings(engineerUrl: 'http://127.0.0.1:${server.port}');
    SessionStore.current = const UserSession(
        username: 'root', name: 'Root', role: UserRole.root,
        ids: ['*'], allowAll: true);
  });

  tearDown(() async {
    await server.close(force: true);
    SessionStore.current = null;
  });

  /// Mở tab, sang mục "Trạng thái máy", bấm nút lịch sử của máy đầu tiên.
  Future<void> moHopThoai(WidgetTester tester) async {
    tester.view.physicalSize = const Size(1280, 900);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    // HTTP thật cần thời gian THẬT — đồng hồ giả của pumpAndSettle không đủ.
    await tester.runAsync(() async {
      await tester.pumpWidget(MaterialApp(
        theme: appTheme(),
        home: Scaffold(body: ManagerMachineScreen(settings: settings)),
      ));
      await Future<void>.delayed(const Duration(milliseconds: 400));
      await tester.pump();
    });
    await tester.pumpAndSettle();

    await tester.tap(find.text('Trạng thái máy'));
    await tester.pumpAndSettle();

    await tester.runAsync(() async {
      await tester.tap(find.byIcon(Icons.history).first);
      await tester.pump();
      await Future<void>.delayed(const Duration(milliseconds: 600));
      await tester.pump();
    });
    await tester.pumpAndSettle();
  }

  testWidgets('ngày hiện ra là mốc THẬT của khách, không phải lần đo đầu',
      (tester) async {
    await moHopThoai(tester);

    // Đúng thứ chủ dự án hỏi: 15/08 (lúc nạp), KHÔNG phải 18/08 (lúc chạy mẫu).
    expect(_trongHop(find.text(_hienThi(_mocThat))), findsOneWidget);
    expect(_trongHop(find.text(_hienThi(_lanDoDau))), findsNothing);

    // Dòng v2.4.5 có mốc thật -> KHÔNG dấu; v2.4.4 vẫn suy đoán -> CÓ dấu;
    // v2.4.3 cũ nhất mang nhãn riêng. Đúng một dấu `≈`.
    expect(_trongHop(find.text(_nhanSuyDoan)), findsOneWidget);
    expect(_trongHop(find.text('bản đầu tiên ghi nhận')), findsOneWidget);
    // Một lần cập nhật = MỘT dòng. Ghép hỏng là hiện hai dòng v2.4.5 hai ngày.
    expect(_trongHop(find.text('v2.4.5')), findsOneWidget);
    expect(_trongHop(find.text('v2.4.4')), findsOneWidget);
    // Số lần đo phải gả sang dòng chính xác, không rơi về "0 lần đo".
    expect(_trongHop(find.textContaining('2 lần đo')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('server CHƯA deploy (/fw-log 405) → vẫn hiện lịch sử suy đoán',
      (tester) async {
    _coNhatKy = false;
    await moHopThoai(tester);

    // Rơi về hành vi cũ: ngày = lần đo đầu tiên báo bản đó, và tự nhận là suy đoán.
    expect(_trongHop(find.text(_hienThi(_lanDoDau))), findsOneWidget);
    // Không có nhật ký -> CẢ v2.4.5 lẫn v2.4.4 đều là suy đoán.
    expect(_trongHop(find.text(_nhanSuyDoan)), findsNWidgets(2));
    expect(_trongHop(find.text('v2.4.5')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('nạp LẠI cùng một bản hiện thành DÒNG RIÊNG, không bị gộp',
      (tester) async {
    // Mắt xích cuối: server ghi `how: "update"` -> `FbtApi.fwLog` đọc ra -> hộp thoại hiện
    // đủ số lần. Ba tầng, và test hàm thuần không chạm được tầng đọc JSON ở giữa.
    _napLai = true;
    await moHopThoai(tester);

    // Hai dòng v2.4.5 với hai ngày khác nhau = hai lần nạp, đúng yêu cầu
    // "cập nhật bao nhiêu lần thì lưu bấy nhiêu".
    expect(_trongHop(find.text('v2.4.5')), findsNWidgets(2));
    expect(_trongHop(find.text(_hienThi('2026-08-19T02:00:00Z'))), findsOneWidget);
    expect(_trongHop(find.text(_hienThi(_mocThat))), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
