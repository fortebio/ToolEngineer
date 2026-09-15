import 'package:flutter_test/flutter_test.dart';
import 'package:RapidPlusApp/models/test_result.dart';
import 'package:RapidPlusApp/services/firmware_history.dart';

TestResult _run(String v, DateTime t) => TestResult(
    id: t.toIso8601String(), deviceId: 'RPL02013', timestamp: t, slots: const [], version: v);

void main() {
  final t2 = DateTime(2026, 8, 1, 10); // nạp v2.4.5 lần đầu
  final t3 = DateTime(2026, 8, 10, 10); // NẠP LẠI đúng bản đó

  test('nạp lại cùng bản: số lần đo chia đúng cho từng dòng', () {
    final runs = [
      for (var i = 1; i <= 7; i++) _run('v2.4.5', t2.add(Duration(days: i))),
      for (var i = 1; i <= 3; i++) _run('v2.4.5', t3.add(Duration(hours: i))),
    ];
    final h = mergeFirmwareLog([
      FwLogEntry(version: 'v2.4.5', at: t2, confirmed: true),
      FwLogEntry(version: 'v2.4.5', at: t3, confirmed: true),
    ], buildFirmwareHistory(runs), lanDo: runs);
    expect(h.length, 2);
    expect(h.first.runs, 3, reason: 'dòng bản ĐANG CHẠY (mới nhất) phải có 3');
    expect(h.last.runs, 7, reason: 'dòng cũ cùng version chỉ giữ 7');
  });

  test('đổi bản bình thường: không đổi kết quả', () {
    final t1 = DateTime(2026, 7, 1, 8);
    final runs = [
      for (var i = 1; i <= 20; i++) _run('v2.4.4', t1.add(Duration(hours: i))),
      for (var i = 1; i <= 7; i++) _run('v2.4.5', t2.add(Duration(hours: i))),
    ];
    final h = mergeFirmwareLog([
      FwLogEntry(version: 'v2.4.4', at: t1),
      FwLogEntry(version: 'v2.4.5', at: t2),
    ], buildFirmwareHistory(runs), lanDo: runs);
    expect(h.map((s) => '${s.version}=${s.runs}').toList(),
        ['v2.4.5=7', 'v2.4.4=20']);
  });
}
