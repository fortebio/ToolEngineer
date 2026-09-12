// Bảng mã lỗi cảm biến trong màn chi tiết kết quả.
//
// Lỗi tới server bằng bản tin RIÊNG (`method:"error"`), không nằm trong payload lần đo — nên
// nó được TRUYỀN VÀO màn này, không phải màn tự gọi API. Màn dùng chung cho cả 4 nguồn.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/models/test_result.dart';
import 'package:RapidPlusApp/screens/result_detail_screen.dart';
import 'package:RapidPlusApp/services/fbt_api.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

TestResult _run() => TestResult(
      id: '21056',
      deviceId: 'RPL02007',
      timestamp: DateTime.parse('2026-08-03T17:45:04Z'),
      version: 'v2.4.3',
      slots: const [],
    );

Future<void> _pump(WidgetTester t, List<SensorError> errs) async {
  t.view.physicalSize = const Size(1100, 800);
  t.view.devicePixelRatio = 1.0;
  addTearDown(t.view.reset);
  await t.pumpWidget(MaterialApp(
    theme: appTheme(),
    home: ResultDetailScreen(
        result: _run(), readingIntervalSec: 20, errors: errs),
  ));
  await t.pumpAndSettle();
}

const _ba = [
  SensorError(
      slot: 'Slot 6',
      code: '1045',
      message: '[Sensor Light]- No data from sensor In Process Amplification 40 min'),
  SensorError(
      slot: 'Slot 8',
      code: '1047',
      message: '[Sensor Light]- Sensor too dark In Process Lysis 10 min'),
];

void main() {
  testWidgets('KHÔNG có lỗi → KHÔNG dựng bảng', (tester) async {
    await _pump(tester, const []);
    // Khung rỗng kèm "không có lỗi" là cấp giấy chứng nhận sạch cho một lần đo mà ta chỉ
    // biết là KHÔNG NGHE THẤY GÌ — gần cả fleet còn chưa gửi lỗi về server.
    expect(find.textContaining('lỗi cảm biến'), findsNothing);
    expect(find.byIcon(Icons.error_outline), findsNothing);
  });

  testWidgets('có lỗi → hiện đủ giếng, mã và mô tả', (tester) async {
    await _pump(tester, _ba);
    expect(find.textContaining('2 lỗi cảm biến'), findsOneWidget);
    expect(find.text('Slot 6'), findsOneWidget);
    expect(find.text('Slot 8'), findsOneWidget);
    expect(find.text('1045'), findsOneWidget);
    expect(find.text('1047'), findsOneWidget);
    expect(find.textContaining('Sensor too dark'), findsOneWidget);
  });

  testWidgets('mã lỗi in bằng font MONO, giữ nguyên 4 chữ số', (tester) async {
    // Mã này là thứ kỹ sư đọc chéo với màn TFT của máy — đổi font hay cắt số 0 đầu là làm
    // hỏng đúng công dụng của nó.
    await _pump(tester, _ba);
    final st = tester.widget<Text>(find.text('1045')).style!;
    expect(st.fontFamily, 'JetBrains Mono');
  });

  testWidgets('NÓI RÕ là ghép theo thời gian, không phải khoá ngoại',
      (tester) async {
    await _pump(tester, _ba);
    expect(find.textContaining('ghép theo thời gian'), findsOneWidget);
  });

  testWidgets('giếng rỗng hiện — chứ không để trống', (tester) async {
    await _pump(tester, const [
      SensorError(slot: '', code: '3010', message: 'Heater disconnected')
    ]);
    expect(find.text('—'), findsWidgets);
    expect(find.text('3010'), findsOneWidget);
  });

  group('FbtApi.sessionErrors', () {
    late HttpServer server;
    late FbtApi api;
    int status = 200;
    Object body = const {};

    setUpAll(() => HttpOverrides.global = null);

    setUp(() async {
      status = 200;
      server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      server.listen((req) async {
        req.response
          ..statusCode = status
          ..headers.contentType = ContentType.json
          ..write(jsonEncode(body));
        await req.response.close();
      });
      api = FbtApi('http://127.0.0.1:${server.port}',
          headers: const {'Authorization': 'Bearer t'});
    });

    tearDown(() => server.close(force: true));

    test('đọc đúng các trường', () async {
      body = {
        'id': 21056,
        'errors': [
          {
            'at': '2026-06-04T08:12:29Z',
            'session_id': 1500,
            'slot': 'Slot 6',
            'code': '1045',
            'message': 'No data from sensor'
          }
        ]
      };
      final r = await api.sessionErrors('21056');
      expect(r, hasLength(1));
      expect(r.first.slot, 'Slot 6');
      expect(r.first.code, '1045');
      expect(r.first.at, isNotNull);
    });

    test('server CHƯA deploy route (405) → rỗng, KHÔNG ném', () async {
      // Route chưa có thì catch-all `POST /{path}` khớp path nhưng sai method → 405.
      // Ném ở đây là làm hỏng cả màn chi tiết vì thiếu một bảng phụ.
      status = 405;
      body = {'detail': 'Method Not Allowed'};
      expect(await api.sessionErrors('21056'), isEmpty);
    });

    test('body lạ → rỗng, không nổ', () async {
      body = {'oops': true};
      expect(await api.sessionErrors('21056'), isEmpty);
      body = <String>['khong phai map'];
      expect(await api.sessionErrors('21056'), isEmpty);
    });
  });
}
