import 'dart:convert';
import 'dart:io';

import 'package:RapidPlusApp/services/cloud_history_api.dart';
import 'package:RapidPlusApp/screens/manager_machine_screen.dart';
import 'package:RapidPlusApp/services/fbt_api.dart';
import 'package:flutter_test/flutter_test.dart';

/// Kiểm phần OTA của [FbtApi] bằng HTTP server giả chạy trong tiến trình —
/// chốt lại đúng hợp đồng với `server/app/main.py` (PUT body thô, DELETE,
/// GET /ota) mà không cần server thật.
void main() {
  late HttpServer server;
  late FbtApi api;
  final calls = <String>[]; // "METHOD path" theo thứ tự
  final uris = <String>[]; // URI đầy đủ KÈM query — ?device= nằm ở đây
  final bodies = <String, List<int>>{};
  var nextStatus = 200;
  var nextBody = '{"ok":true}';

  setUp(() async {
    calls.clear();
    uris.clear();
    bodies.clear();
    nextStatus = 200;
    nextBody = '{"ok":true}';
    server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    server.listen((req) async {
      calls.add('${req.method} ${req.uri.path}');
      uris.add(req.uri.toString());
      bodies['${req.method} ${req.uri.path}'] =
          await req.fold<List<int>>([], (b, d) => b..addAll(d));
      req.response.statusCode = nextStatus;
      req.response.headers.contentType = ContentType.json;
      req.response.write(nextBody);
      await req.response.close();
    });
    api = FbtApi('http://127.0.0.1:${server.port}',
        headers: const {'Authorization': 'Bearer tok'});
  });

  tearDown(() => server.close(force: true));

  test('listOta parse target + files, target rỗng coi như chưa chọn', () async {
    nextBody = jsonEncode({
      'target': 'fw_v2.bin',
      'files': [
        {'name': 'fw_v2.bin', 'size': 2048, 'modified': '2026-08-17T10:00:00Z'},
        {'name': 'fw_v1.bin', 'size': 1024},
      ],
    });
    final s = await api.listOta();
    expect(calls.single, 'GET /ota');
    expect(s.target, 'fw_v2.bin');
    expect(s.files.map((f) => f.name), ['fw_v2.bin', 'fw_v1.bin']);
    expect(s.files.first.size, 2048);
    expect(s.files.first.modified, isNotNull);
    expect(s.files.last.modified, isNull); // thiếu field → null, không nổ

    nextBody = jsonEncode({'target': '', 'files': []});
    expect((await api.listOta()).target, isNull);
  });

  test('uploadOta gửi PUT với NGUYÊN bytes vào /ota/<tên>', () async {
    final bytes = List<int>.generate(300, (i) => i % 256);
    await api.uploadOta('fw v2.bin', bytes);
    expect(calls.single, 'PUT /ota/fw%20v2.bin'); // tên có dấu cách → encode
    expect(bodies['PUT /ota/fw%20v2.bin'], bytes);
  });

  test('chọn / huỷ chọn / xoá dùng đúng method + path', () async {
    await api.setOtaTarget('fw_v1.bin');
    await api.clearOtaTarget();
    await api.deleteOta('fw_v1.bin');
    expect(calls, [
      'PUT /ota/target/fw_v1.bin',
      'DELETE /ota/target',
      'DELETE /ota/fw_v1.bin',
    ]);
  });

  test('lỗi HTTP thành CloudApiException, 400 lấy `detail` của server', () async {
    // expectLater + await: các assert dưới đổi `nextStatus`, không await thì
    // cả 3 cùng chạy sau khi biến đã bị ghi đè (lần đầu viết đã dính đúng bẫy này).
    nextStatus = 400;
    nextBody = jsonEncode({'detail': 'tên file phải là .bin hợp lệ'});
    await expectLater(
      () => api.uploadOta('fw.txt', [1]),
      throwsA(isA<CloudApiException>()
          .having((e) => e.message, 'message', contains('.bin hợp lệ'))),
    );

    nextStatus = 401;
    nextBody = '{}';
    await expectLater(
        () => api.listOta(),
        throwsA(isA<CloudApiException>()
            .having((e) => e.message, 'm', contains('401'))
            // Thông báo phải NÓI CÁCH CHỮA. Nguyên nhân gần như luôn là token cũ còn nằm
            // trong phiên khôi phục (`_AuthGate` không gọi `/auth`), mà "sai hoặc thiếu
            // token" thì người dùng không có manh mối nào — đã mất một lượt hỏi-đáp vì đúng
            // chỗ này (bản web điện thoại, tài khoản root, 2026-08-20).
            .having((e) => e.message, 'chi cach chua',
                contains('đăng nhập lại'))));

    nextStatus = 413;
    await expectLater(
        () => api.uploadOta('fw.bin', [1]),
        throwsA(isA<CloudApiException>()
            .having((e) => e.message, 'm', contains('quá lớn'))));
  });

  test('URL trống → báo chưa cấu hình, không gọi mạng', () async {
    final empty = FbtApi('');
    await expectLater(() => empty.listOta(), throwsA(isA<CloudApiException>()));
    await expectLater(
        () => empty.uploadOta('a.bin', [1]), throwsA(isA<CloudApiException>()));
    expect(calls, isEmpty);
  });

  test('listDevices lấy version firmware từ /devices', () async {
    nextBody = jsonEncode([
      {
        'id_device': 'RPL02013',
        'sessions': 7,
        'last_seen': '2026-08-17T09:00:00Z',
        'version': 'v2.4.2'
      },
      {'id_device': 'RPL00001', 'sessions': 1}, // server cũ chưa trả version
    ]);
    final ds = await api.listDevices();
    expect(ds.first.version, 'v2.4.2');
    expect(ds.first.runCount, 7);
    expect(ds.last.version, '');
  });
  // --- Ghim bản riêng cho TỪNG MÁY -----------------------------------------

  test('setOtaTarget/clearOtaTarget gắn ?device= đúng hợp đồng server', () async {
    await api.setOtaTarget('fbt_v2.4.5.bin', device: 'RPL03003', by: 'khang');
    expect(uris.last, '/ota/target/fbt_v2.4.5.bin?device=RPL03003&by=khang');

    // Không đăng nhập / không biết tên -> vẫn ghim được, chỉ là không có `by`.
    await api.setOtaTarget('fbt_v2.4.5.bin', device: 'RPL03003');
    expect(uris.last, '/ota/target/fbt_v2.4.5.bin?device=RPL03003');

    // Không có device -> đặt bản CHUNG. Gửi `?device=` rỗng sẽ khiến server ghim vào
    // khoá "" thay vì đổi bản chung, tức là im lặng không làm gì cho cả fleet.
    await api.setOtaTarget('fbt_v2.4.4.bin');
    expect(uris.last, '/ota/target/fbt_v2.4.4.bin');

    // Bản CHUNG cũng phải mang `by`: đẩy cho cả fleet mà không ghi ai đẩy thì
    // "ai set firmware?" không có chỗ nào trả lời (đúng lỗi đã gặp).
    await api.setOtaTarget('fbt_v2.4.4.bin', by: 'khang');
    expect(uris.last, '/ota/target/fbt_v2.4.4.bin?by=khang');

    await api.clearOtaTarget(device: 'RPL03003');
    expect(uris.last, '/ota/target?device=RPL03003');
    expect(calls.last, 'DELETE /ota/target');

    await api.clearOtaTarget();
    expect(uris.last, '/ota/target');
  });

  test('listOta đọc ghim ở CẢ HAI dạng; server chưa nâng cấp vẫn chạy', () async {
    // Dạng hiện tại: {file, by, at} — `by` nuôi cột "Người thiết lập".
    nextBody = jsonEncode({
      'target': 'fbt_v2.4.4.bin',
      'target_by': 'nam',
      'target_at': '2026-08-18T08:00:00Z',
      'devices': {
        'RPL03003': {
          'file': 'fbt_v2.4.5.bin',
          'by': 'khang',
          'at': '2026-08-18T09:00:00Z',
        },
      },
      'files': [],
    });
    var st = await api.listOta();
    var pin = st.devices['RPL03003']!;
    expect(pin.file, 'fbt_v2.4.5.bin');
    expect(pin.by, 'khang');
    expect(pin.at, isNotNull);
    expect(st.targetBy, 'nam'); // ai đặt bản CHUNG
    expect(st.targetAt, isNotNull);

    // Dạng CŨ (chuỗi trần) — target.json trên box đã từng ở dạng này.
    nextBody = jsonEncode({
      'target': null,
      'devices': {'RPL03003': 'fbt_v2.4.5.bin'},
      'files': [],
    });
    pin = (await api.listOta()).devices['RPL03003']!;
    expect(pin.file, 'fbt_v2.4.5.bin');
    expect(pin.by, isEmpty, reason: 'chưa biết ai đặt → cột hiện "?" chứ không nổ');

    nextBody = jsonEncode({'target': 'fbt_v2.4.4.bin', 'files': []});
    st = await api.listOta();
    expect(st.devices, isEmpty,
        reason: 'thiếu field devices = server cũ, coi như chưa ghim máy nào');
    expect(st.targetBy, isEmpty, reason: 'server cũ chưa trả target_by → không nổ');
    expect(st.targetAt, isNull);
  });

  test('supportsPerDevicePin: mốc v2.4.4, dưới mốc KHÔNG cho ghim', () {
    // Ghim cho máy cũ hơn v2.4.4 hỏng HOÀN TOÀN IM LẶNG: server lưu, máy không bao giờ
    // hỏi. Nên mặc định phải là "không cho", kể cả khi không đọc được version.
    for (final v in ['v2.4.4', '2.4.4', 'v2.4.4AT', 'v2.4.5', 'v2.5.0', 'v3.0', 'v2.4.10']) {
      expect(supportsPerDevicePin(v), isTrue, reason: v);
    }
    for (final v in ['v2.4.3', 'v2.4.3ATZ', 'V2.3.1', 'V2.2.5', 'v2.4', 'v2', '', 'vtest', '  ']) {
      expect(supportsPerDevicePin(v), isFalse, reason: '"$v"');
    }
  });
  test('matchesQuery: KHÔNG phân biệt hoa/thường, rỗng = không lọc', () {
    // Fleet thật có cả `V2.3.1` (V hoa) lẫn `v2.4.3` (thường). Lọc phân biệt hoa/thường sẽ
    // GIẤU MẤT một nửa số máy mà không báo gì — đây là cả lý do hàm này tồn tại.
    expect(matchesQuery('v2.3', ['RPL01007', 'V2.3.1']), isTrue);
    expect(matchesQuery('V2.4', ['RPL02013', 'v2.4.4']), isTrue);
    expect(matchesQuery('rpl020', ['RPL02013', 'v2.4.4']), isTrue);

    // Khớp trên BẤT KỲ trường nào: mã máy hoặc version.
    expect(matchesQuery('2.4.4', ['RPL02013', 'v2.4.4']), isTrue);
    expect(matchesQuery('RPL02013', ['RPL02013', 'v2.4.4']), isTrue);

    expect(matchesQuery('v9', ['RPL02013', 'v2.4.4']), isFalse);

    // Rỗng / chỉ khoảng trắng = không lọc, KHÔNG phải "không khớp gì".
    expect(matchesQuery('', ['bất kỳ']), isTrue);
    expect(matchesQuery('   ', ['bất kỳ']), isTrue);
    // Khoảng trắng thừa quanh từ khoá bị bỏ (người ta hay dán kèm dấu cách).
    expect(matchesQuery('  2.4.5  ', ['fbt_v2.4.5.bin']), isTrue);
  });

}
