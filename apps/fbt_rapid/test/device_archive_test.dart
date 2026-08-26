import 'dart:convert';

import 'package:RapidPlusApp/models/test_result.dart';
import 'package:RapidPlusApp/services/result_export.dart';
import 'package:flutter_test/flutter_test.dart';

/// Kho "tải toàn bộ dữ liệu của 1 máy" (nút tải trên thẻ máy ở tab Lịch sử):
/// **MỘT thư mục, mỗi lần đo MỘT file JSON**.
///
/// File rời khỏi app đi vào tay người khác nên cả nội dung LẪN cách đặt tên là
/// **hợp đồng**: đổi lặng lẽ là script phân tích bên ngoài gãy, mà tên trùng thì
/// mất bản ghi không báo gì.
///
/// Giờ để LOCAL (không `.utc`) vì tên file lấy theo giờ địa phương — dùng UTC
/// thì test đổi kết quả theo múi giờ máy chạy.
TestResult _run({
  String id = '1',
  String version = 'v2.4.4',
  DateTime? at,
  List<double>? curve,
}) =>
    TestResult(
      id: id,
      deviceId: 'RPL02013',
      timestamp: at ?? DateTime(2026, 8, 26, 13, 53, 0),
      version: version,
      curvesAreRaw: true,
      slots: [
        for (var i = 1; i <= 10; i++)
          SlotResult(
            index: i,
            classification:
                i == 1 ? Classification.positive : Classification.negative,
            ct: i == 1 ? 22.3 : null,
            curve: i == 1 ? (curve ?? const [150.0, 153.0, 160.0]) : const [],
            slope: i == 1 ? 1.5 : null,
          ),
      ],
    );

void main() {
  group('nội dung một file lần đo', () {
    test('giữ ĐỦ dữ liệu đo, kể cả đường cong', () {
      final r = ResultExport.runToJson(_run());

      expect(r['deviceId'], 'RPL02013');
      expect(r['version'], 'v2.4.4');
      expect(r['time'], startsWith('2026-08-26T13:53'));
      expect((r['slots'] as List).length, 10, reason: 'luôn đủ 10 slot');

      final slot1 = (r['slots'] as List).first as Map<String, dynamic>;
      expect(slot1['index'], 1);
      expect(slot1['ct'], 22.3);
      expect(slot1['slope'], 1.5);
      // Đường cong là dữ liệu đo THẬT — mất nó thì file chỉ còn là bản tóm tắt
      expect(slot1['data'], [150.0, 153.0, 160.0]);
    });

    test('serialize được sang JSON (không lọt kiểu lạ)', () {
      final text = jsonEncode(ResultExport.runToJson(_run()));
      final back = jsonDecode(text) as Map<String, dynamic>;
      expect((back['slots'] as List).length, 10);
      expect(back['slopes'], isA<List>());
    });
  });

  group('tên file từng lần đo', () {
    test('mang ngày_giờ_firmware để sắp theo tên là sắp theo thời gian', () {
      expect(ResultExport.runFileName(_run()), '2026-08-26_135300_v2.4.4.json');
      expect(
        ResultExport.runFileName(_run(at: DateTime(2026, 8, 26, 9, 5, 7))),
        '2026-08-26_090507_v2.4.4.json',
        reason: 'giờ/phút/giây phải pad 2 chữ số, không thì sắp sai thứ tự',
      );
    });

    test('máy chưa báo firmware → NA, không ra tên cụt', () {
      expect(ResultExport.runFileName(_run(version: '')),
          '2026-08-26_135300_NA.json');
    });

    test('version bẩn không sinh tên file hỏng', () {
      final name = ResultExport.runFileName(_run(version: 'v2/4:4*x'));
      expect(RegExp(r'[<>:"/\\|?*]').hasMatch(name), isFalse,
          reason: 'ký tự Windows cấm phải bị thay: $name');
      expect(name.endsWith('.json'), isTrue);
    });

    test('hai lần đo khác giờ → khác tên (không đè nhau)', () {
      final a = ResultExport.runFileName(_run());
      final b = ResultExport.runFileName(_run(at: DateTime(2026, 8, 26, 13, 54)));
      expect(a, isNot(b));
    });
  });

  group('tên thư mục kho', () {
    test('mang mã máy + ngày, KHÔNG có đuôi .json (là thư mục)', () {
      final d = ResultExport.archiveDirName('RPL02013', DateTime(2026, 8, 26));
      expect(d, 'RPL02013_toanbo_2026-08-26');
      expect(d.endsWith('.json'), isFalse);
    });

    test('mã máy bẩn / rỗng vẫn ra tên dùng được', () {
      // Mã bẩn đã gặp thật: 'proto 1'
      final dirty = ResultExport.archiveDirName('a/b:c*d', DateTime(2026, 8, 26));
      expect(RegExp(r'[<>:"/\\|?*]').hasMatch(dirty), isFalse,
          reason: 'ký tự Windows cấm phải bị thay: $dirty');
      expect(ResultExport.archiveDirName('', DateTime(2026, 8, 26)),
          'May_toanbo_2026-08-26');
    });
  });
}
