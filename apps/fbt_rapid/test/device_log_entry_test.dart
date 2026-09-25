import 'package:RapidPlusApp/services/fbt_api.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('DeviceLogEntry.fromJson đọc đủ trường hộp thư (server 2026-09-25)', () {
    final e = DeviceLogEntry.fromJson({
      'file': 'RPL01015_20260925_010203_abcd1234.json',
      'device': 'RPL01015',
      'by': 'cskh01',
      'note': 'máy tắt ngang',
      'received_at': '2026-09-25T01:02:03+00:00',
      'size': 2048,
      'findings': 3,
      'fw': '2.4.5',
      'errors': 1,
      'warnings': 1,
      'keys': ['brownout', 'wifi'],
      'status': 'working',
      'status_by': 'kt01',
      'status_at': '2026-09-25T02:00:00+00:00',
      'status_note': 'đổi adapter',
    });
    expect(e.device, 'RPL01015');
    expect(e.fw, '2.4.5');
    expect(e.errors, 1);
    expect(e.keys, ['brownout', 'wifi']);
    expect(e.status, 'working');
    expect(e.statusBy, 'kt01');
    expect(e.statusNote, 'đổi adapter');
    expect(e.at!.toUtc(), DateTime.utc(2026, 9, 25, 1, 2, 3));
    expect(e.statusAt, isNotNull);
  });

  test('server cũ (thiếu trường mới) → mặc định, status "new", device dự phòng', () {
    final e = DeviceLogEntry.fromJson(
        {'file': 'x.json', 'received_at': 'hỏng', 'size': 10}, device: 'RPL1');
    expect(e.device, 'RPL1');
    expect(e.status, 'new');
    expect(e.keys, isEmpty);
    expect(e.fw, '');
    expect(e.at, isNull);
    expect(e.errors, 0);
  });
}
