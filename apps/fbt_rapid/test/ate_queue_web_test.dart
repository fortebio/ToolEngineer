import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/ate_record.dart';
import 'package:RapidPlusApp/services/ate_api.dart';
import 'package:RapidPlusApp/services/ate_queue_web.dart';

/// Hàng đợi hồ sơ ATE của **bản web** (localStorage). Test chạy được trên VM vì
/// file này không đụng `dart:html` — nó đi qua `shared_preferences`, thứ có sẵn
/// bản giả cho test. Chỗ đáng test không phải "ghi rồi đọc lại được" mà là ba
/// chỗ hàng đợi ĐƯỢC PHÉP mất dữ liệu: lược log, đầy trần, server từ chối. Mất
/// mà im lặng thì hồ sơ nghiệm thu hết giá trị.

AteRecord _rec(String sn, {String raw = 'log dài', String note = ''}) =>
    AteRecord(
      sn: sn,
      startedAt: DateTime.utc(2026, 9, 8, 8, 15, 30),
      finishedAt: DateTime.utc(2026, 9, 8, 8, 17, 0),
      verdict: AteVerdict.pass,
      limitsVer: 'L2609A-1',
      note: note,
      steps: [
        AteStepResult(
          code: 'FW-01',
          name: 'Nạp firmware',
          verdict: AteVerdict.pass,
          startedAt: DateTime.utc(2026, 9, 8, 8, 15, 31),
          raw: raw,
        ),
        AteStepResult(
          code: 'BOOT-01',
          name: 'Khởi động',
          verdict: AteVerdict.pass,
          startedAt: DateTime.utc(2026, 9, 8, 8, 15, 40),
          raw: raw,
        ),
      ],
    );

/// API giả: `fail` quyết định lần gửi thứ mấy thì hỏng và hỏng kiểu gì.
class _FakeApi extends AteApi {
  _FakeApi({this.rejectSn = const {}, this.networkAfter = -1})
      : super('http://queue.test');

  /// Những SN mà server TỪ CHỐI (400) — hỏng vĩnh viễn.
  final Set<String> rejectSn;

  /// Sau bao nhiêu lần gửi thành công thì rớt mạng (-1 = không rớt).
  final int networkAfter;

  final List<String> sentSn = [];

  @override
  Future<String> putRecordJson(Map<String, dynamic> body) async {
    final sn = (body['sn'] ?? '').toString();
    if (rejectSn.contains(sn)) throw AteRejectedException('hồ sơ méo');
    if (networkAfter >= 0 && sentSn.length >= networkAfter) {
      throw Exception('mất mạng');
    }
    sentSn.add(sn);
    return 'id_$sn';
  }
}

Future<List<Map<String, dynamic>>> _queued() async {
  final p = await SharedPreferences.getInstance();
  return [
    for (final s in p.getStringList('ate_queue_v1') ?? const [])
      jsonDecode(s) as Map<String, dynamic>,
  ];
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('lược log khi xếp hàng', () {
    test('bỏ raw của từng bước và NÓI RA trong ghi chú', () async {
      await AteQueue.save(_rec('RPL02017'));
      final j = (await _queued()).single;

      final steps = (j['steps'] as List).cast<Map>();
      expect(steps.length, 2); // vẫn đủ bước, chỉ mất log thô
      expect(steps.every((s) => s.containsKey('raw')), isFalse);
      expect(j['note'], contains('2 bước bị lược'));
    });

    test('giữ ghi chú cũ, không đè', () async {
      await AteQueue.save(_rec('RPL02017', note: 'chạy lại lần 2'));
      expect((await _queued()).single['note'], startsWith('chạy lại lần 2 · '));
    });

    test('hồ sơ vốn không có log thô thì không thêm ghi chú thừa', () async {
      await AteQueue.save(_rec('RPL02017', raw: ''));
      expect((await _queued()).single['note'], '');
    });
  });

  group('trần hàng đợi', () {
    test('đầy thì đẩy hồ sơ CŨ NHẤT ra, giữ đúng 60 bản mới', () async {
      for (var i = 0; i < 63; i++) {
        await AteQueue.save(_rec('RPL${i.toString().padLeft(5, '0')}'));
      }
      final items = await _queued();
      expect(items.length, 60);
      expect(items.first['sn'], 'RPL00003'); // 3 bản đầu bị đẩy ra
      expect(await AteQueue.pendingCount(), 60);
    });

    test('bản bị đẩy ra được ĐẾM vào rejected của lần gửi kế, không im lặng',
        () async {
      for (var i = 0; i < 62; i++) {
        await AteQueue.save(_rec('RPL${i.toString().padLeft(5, '0')}'));
      }
      final api = _FakeApi();
      final r = await AteQueue.flush(api);
      expect(r.sent, 60);
      expect(r.rejected, 2); // 2 bản mất vì đầy trần
      expect(r.left, 0);

      // Đã báo một lần thì thôi, không cộng dồn sang lần sau.
      await AteQueue.save(_rec('RPL09999'));
      expect((await AteQueue.flush(_FakeApi())).rejected, 0);
    });
  });

  group('gửi lại', () {
    test('lỗi mạng thì DỪNG và giữ nguyên phần chưa gửi', () async {
      for (final sn in ['A', 'B', 'C']) {
        await AteQueue.save(_rec(sn));
      }
      final r = await AteQueue.flush(_FakeApi(networkAfter: 1));
      expect(r.sent, 1);
      expect(r.left, 2);
      expect(r.error, contains('mất mạng'));
      expect([for (final j in await _queued()) j['sn']], ['B', 'C']);
    });

    test('server từ chối thì BỎ hồ sơ đó và đi tiếp — không chặn cả hàng',
        () async {
      for (final sn in ['A', 'B', 'C']) {
        await AteQueue.save(_rec(sn));
      }
      final api = _FakeApi(rejectSn: {'B'});
      final r = await AteQueue.flush(api);
      expect(api.sentSn, ['A', 'C']);
      expect(r.sent, 2);
      expect(r.rejected, 1);
      expect(await AteQueue.pendingCount(), 0);
    });

    test('mục rác trong localStorage bị loại, không làm kẹt hàng đợi', () async {
      final p = await SharedPreferences.getInstance();
      await p.setStringList('ate_queue_v1', ['["không phải hồ sơ"]']);
      await AteQueue.save(_rec('A'));

      final api = _FakeApi();
      final r = await AteQueue.flush(api);
      expect(r.rejected, 1);
      expect(api.sentSn, ['A']);
    });
  });
}
