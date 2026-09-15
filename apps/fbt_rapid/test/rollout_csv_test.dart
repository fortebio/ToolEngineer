import 'package:RapidPlusApp/services/cloud_history_api.dart' show CloudDevice;
import 'package:RapidPlusApp/services/rollout_csv.dart';
import 'package:flutter_test/flutter_test.dart';

/// CSV tiến độ triển khai OTA (nút "Tải CSV" trong pop-up Tiến độ).
///
/// File này đi vào báo cáo/Excel của người khác nên hình dạng là **hợp đồng**:
/// lệch cột hoặc hỏng tiếng Việt là hỏng ở phía người nhận, không ai báo lại.
CloudDevice _dev(String id,
        {String version = 'v2.4.3', int runs = 10, DateTime? at}) =>
    CloudDevice(
      id: id,
      runCount: runs,
      version: version,
      latest: at ?? DateTime(2026, 8, 26, 14, 26),
    );

List<String> _lines(String csv) => csv.split('\r\n');

void main() {
  group('escape ô CSV', () {
    test('ô có dấu phẩy / nháy / xuống dòng phải được bọc', () {
      expect(csvCell('RPL03005'), 'RPL03005');
      expect(csvCell('proto 1'), 'proto 1', reason: 'dấu cách không cần bọc');
      expect(csvCell('a,b'), '"a,b"');
      expect(csvCell('say "hi"'), '"say ""hi"""');
      expect(csvCell('hai\ndòng'), '"hai\ndòng"');
      expect(csvCell(null), '');
      expect(csvCell(12), '12');
    });
  });

  group('nội dung file', () {
    test('mở đầu bằng BOM + sep=, để Excel tiếng Việt đọc đúng', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: [_dev('RPL03005')],
        onTarget: (_) => false,
        exportedAt: DateTime(2026, 8, 26, 15, 0),
      );
      // BOM UTF-8: thiếu nó Excel đọc tiếng Việt ra ký tự rác
      expect(csv.codeUnitAt(0), 0xFEFF);
      // sep=, : Windows tiếng Việt lấy ';' làm list separator → thiếu dòng này
      // là cả file dồn vào MỘT cột
      expect(_lines(csv).first.endsWith('sep=,'), isTrue);
    });

    test('ghi bản mục tiêu + thời điểm xuất vào chính file', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: [_dev('RPL03005')],
        onTarget: (_) => true,
        exportedAt: DateTime(2026, 8, 26, 15, 0),
      );
      expect(csv, contains('fbt_v2.4.4.bin'));
      expect(csv, contains('2026-08-26T15:00'));
    });

    test('mỗi máy một dòng, đủ 5 cột, đúng thứ tự', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: [
          _dev('RPL03005', version: 'v2.4.4', runs: 47),
          _dev('RPL02001', version: 'v2.4.3', runs: 101),
        ],
        onTarget: (d) => d.version == 'v2.4.4',
      );
      final lines = _lines(csv).where((l) => l.startsWith('RPL')).toList();
      expect(lines.length, 2);

      final first = lines.first.split(',');
      expect(first.length, 5, reason: 'Ma may, Firmware, Trang thai, So phien, Lan gui cuoi');
      expect(first[0], 'RPL03005');
      expect(first[1], 'v2.4.4');
      expect(first[2], 'Da cap nhat');
      expect(first[3], '47');

      expect(lines[1].split(',')[2], 'Chua cap nhat');
    });

    test('không kết luận được → "Khong xac dinh", KHÔNG phải "chưa cập nhật"', () {
      // Nhầm null thành false là báo sai: máy có thể đã nạp mà chưa đo lại.
      expect(rolloutStatusLabel(null), 'Khong xac dinh');
      expect(rolloutStatusLabel(true), 'Da cap nhat');
      expect(rolloutStatusLabel(false), 'Chua cap nhat');

      final csv = buildRolloutCsv(
        target: 'firmware.bin', // tên không mang version
        devices: [_dev('RPL03005', version: '')],
        onTarget: (_) => null,
      );
      expect(csv, contains('Khong xac dinh'));
    });

    test('mã máy có dấu phẩy không làm lệch cột', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: [_dev('may,lo 1')],
        onTarget: (_) => true,
      );
      final row = _lines(csv).firstWhere((l) => l.contains('may,lo 1'));
      expect(row.startsWith('"may,lo 1",'), isTrue,
          reason: 'phải bọc nháy, không thì thành 2 cột: $row');
    });

    test('máy chưa báo version / chưa gửi lần nào → ô rỗng, không nổ', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: [CloudDevice(id: 'RPL99999', runCount: 0)],
        onTarget: (_) => null,
      );
      final row = _lines(csv).firstWhere((l) => l.startsWith('RPL99999'));
      expect(row, 'RPL99999,,Khong xac dinh,0,');
    });

    test('danh sách rỗng vẫn ra file hợp lệ (chỉ có phần đầu)', () {
      final csv = buildRolloutCsv(
        target: 'fbt_v2.4.4.bin',
        devices: const [],
        onTarget: (_) => null,
      );
      expect(csv, contains('Ma may'));
      expect(_lines(csv).any((l) => l.startsWith('RPL')), isFalse);
    });
  });

  group('tên file', () {
    test('bỏ đuôi .bin, mang tên bản + ngày', () {
      expect(rolloutCsvFileName('fbt_v2.4.4.bin', DateTime(2026, 8, 26)),
          'tiendo_fbt_v2.4.4_2026-08-26.csv');
    });

    test('tên bản bẩn không sinh tên file hỏng', () {
      final n = rolloutCsvFileName('a/b:c*d.bin', DateTime(2026, 8, 5));
      expect(RegExp(r'[<>:"/\\|?*]').hasMatch(n), isFalse, reason: n);
      expect(n, endsWith('.csv'));
      expect(n, contains('2026-08-05'), reason: 'ngày phải pad 2 chữ số');
    });
  });
}
