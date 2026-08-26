import 'dart:convert';

import 'package:RapidPlusApp/models/test_result.dart';
import 'package:RapidPlusApp/services/result_export.dart';
import 'package:flutter_test/flutter_test.dart';

/// Kho "tải toàn bộ dữ liệu của 1 máy" (nút tải trên thẻ máy ở tab Lịch sử).
///
/// File này rời khỏi app đi vào tay người khác nên hình dạng của nó là **hợp
/// đồng**: đổi lặng lẽ là mọi script phân tích bên ngoài gãy.
TestResult _run(String id, {String version = 'v2.4.4', List<double>? curve}) =>
    TestResult(
      id: id,
      deviceId: 'RPL02013',
      timestamp: DateTime.utc(2026, 8, 26, 13, 53),
      version: version,
      curvesAreRaw: true,
      slots: [
        for (var i = 1; i <= 10; i++)
          SlotResult(
            index: i,
            classification: i == 1 ? Classification.positive : Classification.negative,
            ct: i == 1 ? 22.3 : null,
            curve: i == 1 ? (curve ?? const [150.0, 153.0, 160.0]) : const [],
            slope: i == 1 ? 1.5 : null,
          ),
      ],
    );

void main() {
  test('kho gói đủ mọi lần chạy + đếm đúng', () {
    final archive = ResultExport.buildDeviceArchive(
      'RPL02013',
      [_run('1'), _run('2'), _run('3')],
      exportedAt: DateTime.utc(2026, 8, 26, 14, 30),
    );

    expect(archive['deviceId'], 'RPL02013');
    expect(archive['count'], 3, reason: 'count phải khớp số lần chạy thật');
    expect((archive['runs'] as List).length, 3);
    // exportedAt để người mở file biết dữ liệu tới thời điểm nào
    expect(archive['exportedAt'], '2026-08-26T14:30:00.000Z');
  });

  test('mỗi lần chạy giữ ĐỦ dữ liệu đo, kể cả đường cong', () {
    final archive = ResultExport.buildDeviceArchive('RPL02013', [_run('1')]);
    final r = (archive['runs'] as List).first as Map<String, dynamic>;

    expect(r['deviceId'], 'RPL02013');
    expect(r['version'], 'v2.4.4');
    expect(r['time'], '2026-08-26T13:53:00.000Z');
    expect((r['slots'] as List).length, 10, reason: 'luôn đủ 10 slot');

    final slot1 = (r['slots'] as List).first as Map<String, dynamic>;
    expect(slot1['index'], 1);
    expect(slot1['ct'], 22.3);
    expect(slot1['slope'], 1.5);
    // Đường cong là phần dữ liệu đo THẬT — mất nó thì file chỉ còn là bản tóm tắt
    expect(slot1['data'], [150.0, 153.0, 160.0]);
  });

  test('kho serialize được sang JSON (không lọt kiểu lạ)', () {
    final archive = ResultExport.buildDeviceArchive('RPL02013', [_run('1'), _run('2')]);
    final text = jsonEncode(archive); // ném nếu có object không mã hoá được
    final back = jsonDecode(text) as Map<String, dynamic>;
    expect(back['count'], 2);
    expect(((back['runs'] as List).first as Map)['slots'], isA<List>());
  });

  test('máy chưa có lần chạy nào → kho rỗng hợp lệ, không nổ', () {
    final archive = ResultExport.buildDeviceArchive('RPL99999', const []);
    expect(archive['count'], 0);
    expect(archive['runs'], isEmpty);
    expect(() => jsonEncode(archive), returnsNormally);
  });

  test('tên file mang mã máy + ngày, không chứa ký tự Windows cấm', () {
    final name = ResultExport.archiveFileName('RPL02013', DateTime(2026, 8, 26));
    expect(name, 'RPL02013_toanbo_2026-08-26.json');

    // Mã máy bẩn (đã gặp thật: 'proto 1') không được sinh tên file hỏng
    final dirty = ResultExport.archiveFileName('a/b:c*d', DateTime(2026, 8, 26));
    expect(RegExp(r'[<>:"/\\|?*]').hasMatch(dirty), isFalse,
        reason: 'ký tự Windows cấm phải bị thay: $dirty');
    expect(dirty.endsWith('.json'), isTrue);

    // Mã rỗng vẫn ra tên dùng được
    expect(ResultExport.archiveFileName('', DateTime(2026, 8, 26)),
        'May_toanbo_2026-08-26.json');
  });
}
