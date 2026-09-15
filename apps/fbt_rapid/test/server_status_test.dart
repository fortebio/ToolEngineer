import 'package:RapidPlusApp/models/server_status.dart';
import 'package:RapidPlusApp/util/format.dart';
import 'package:flutter_test/flutter_test.dart';

/// Tab Giám sát đọc `GET /monitor`. Server trả `null` cho mục nó không đo được
/// (box không phải Linux, Postgres chết) — màn hình phải hiện "—" chứ KHÔNG
/// được vỡ, nên phần parse là chỗ đáng test nhất của tính năng này.
void main() {
  Map<String, dynamic> full() => {
        'ok': true,
        'service': {'started_at': '2026-08-28T01:00:00+00:00', 'uptime_sec': 93600},
        'db': {'ok': true, 'error': null},
        'cpu': {'cores': 4, 'load1': 0.42, 'load5': 0.3, 'load15': 0.2, 'load1_pct': 10.5},
        'mem': {'total': 4000000000, 'available': 1000000000, 'used_pct': 75.0},
        'disk': {
          'total': 113000000000,
          'free': 90000000000,
          'used_pct': 20.4,
          'days_left': 973752.6,
        },
        'temp': {'c': 57.0, 'sensor': 'TCPU'},
        'flow': {
          'total': 3372,
          'last24h': 12,
          'last7d': 88,
          'devices': 9,
          'db_bytes': 17170432,
          'by_day': [
            {'d': '2026-08-27', 'n': 0},
            {'d': '2026-08-28', 'n': 12},
          ],
        },
      };

  group('parse đầy đủ', () {
    test('đọc đúng mọi mục', () {
      final st = ServerStatus.fromJson(full());

      expect(st.uptime, const Duration(seconds: 93600));
      expect(st.startedAt, isNotNull);
      expect(st.dbOk, isTrue);
      expect(st.cpu!.cores, 4);
      expect(st.cpu!.load1, 0.42);
      expect(st.disk!.usedPct, 20.4);
      expect(st.flow!.total, 3372);
      expect(st.flow!.dbBytes, 17170432);
      expect(st.temp!.c, 57.0);
      expect(st.temp!.sensor, 'TCPU');
      expect(st.disk!.daysLeft, 973752.6);
    });

    test('RAM dùng khoá "available", đĩa dùng "free" — cùng nghĩa còn trống', () {
      final st = ServerStatus.fromJson(full());
      expect(st.mem!.free, 1000000000, reason: 'phải đọc được từ "available"');
      expect(st.disk!.free, 90000000000);
    });

    test('peak = cột cao nhất, để vẽ biểu đồ theo tỉ lệ', () {
      expect(ServerStatus.fromJson(full()).flow!.peak, 12);
    });
  });

  group('parse phòng thủ', () {
    test('server chưa có nhiệt độ / cỡ DB → null + 0, không ném', () {
      // Server cũ chưa deploy phần này: thiếu 'temp', thiếu 'db_bytes'.
      final j = full()..remove('temp');
      (j['flow'] as Map).remove('db_bytes');
      (j['disk'] as Map).remove('days_left');
      final st = ServerStatus.fromJson(j);
      expect(st.temp, isNull);
      expect(st.flow!.dbBytes, 0, reason: 'UI ẩn dòng cỡ DB khi = 0');
      expect(st.disk!.daysLeft, isNull, reason: 'không đoán khi server không nói');
      expect(st.disk!.usedPct, 20.4, reason: 'phần còn lại vẫn đọc được');
    });

    test('server cũ trả rỗng → mọi mục null, KHÔNG ném', () {
      final st = ServerStatus.fromJson({});
      expect(st.uptime, isNull);
      expect(st.cpu, isNull);
      expect(st.mem, isNull);
      expect(st.disk, isNull);
      expect(st.flow, isNull);
      expect(st.dbOk, isFalse, reason: 'thiếu tin thì coi như CHƯA ổn, không phải ổn');
    });

    test('mục không đo được (null) không làm hỏng các mục khác', () {
      final j = full()..addAll({'cpu': null, 'mem': null, 'disk': null});
      final st = ServerStatus.fromJson(j);
      expect(st.cpu, isNull);
      expect(st.flow!.last24h, 12, reason: 'phần còn lại vẫn phải đọc được');
    });

    test('total = 0 → coi như không đo được (tránh chia 0 lúc vẽ)', () {
      final st = ServerStatus.fromJson(
          full()..['disk'] = {'total': 0, 'free': 0, 'used_pct': 0});
      expect(st.disk, isNull);
    });

    test('db lỗi → dbOk false + giữ tên loại lỗi', () {
      final st = ServerStatus.fromJson(
          full()..['db'] = {'ok': false, 'error': 'OperationalError'});
      expect(st.dbOk, isFalse);
      expect(st.dbError, 'OperationalError');
    });

    test('by_day méo (thiếu khoá) không làm ném', () {
      final st = ServerStatus.fromJson(full()
        ..['flow'] = {
          'total': 1,
          'by_day': [
            {'d': '2026-08-28'},
            {'n': 3},
            'rác',
          ],
        });
      expect(st.flow!.byDay.length, 2, reason: 'phần tử không phải Map bị bỏ');
      expect(st.flow!.byDay.first.count, 0);
      expect(st.flow!.peak, 3);
    });
  });

  group('format', () {
    test('"đầy sau" cắt ngọn ở 1 năm, không in số ngày giả-chính-xác', () {
      // Box thật cho ra 973752 ngày — in nguyên là tỏ vẻ chính xác về một ước lượng thô
      expect(formatDaysLeft(973752.6), contains('năm'));
      expect(formatDaysLeft(400), contains('năm'));
      expect(formatDaysLeft(90), contains('3'), reason: '~3 tháng');
      expect(formatDaysLeft(12), contains('12'));
    });

    test('dung lượng theo bội số 1024', () {
      expect(formatBytes(0), '0 B');
      expect(formatBytes(1023), '1023 B');
      expect(formatBytes(1024), '1.0 KB');
      expect(formatBytes(1536), '1.5 KB');
      expect(formatBytes(5 * 1024 * 1024), '5.00 MB');
      // Đĩa của box là 113GB — bản _size() cũ dừng ở MB nên sẽ ra "107765.63 MB"
      expect(formatBytes(113 * 1024 * 1024 * 1024), '113.00 GB');
    });

    test('uptime chỉ hai đơn vị lớn nhất', () {
      expect(formatUptime(const Duration(seconds: 47)), contains('47'));
      expect(formatUptime(const Duration(minutes: 5)), contains('5'));
      final h = formatUptime(const Duration(hours: 5, minutes: 12));
      expect(h, contains('5'));
      expect(h, contains('12'));
      final d = formatUptime(const Duration(days: 3, hours: 4, minutes: 30));
      expect(d, contains('3'));
      expect(d, contains('4'));
      expect(d.contains('30'), isFalse, reason: 'có ngày thì bỏ phút');
    });
  });
}
