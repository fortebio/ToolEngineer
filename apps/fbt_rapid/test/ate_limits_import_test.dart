import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/models/ate_record.dart';

/// Đọc file JSON tiêu chuẩn (mục Tiêu chuẩn › Nhập JSON).
///
/// Hàm này đứng giữa "file ai đó gửi tới" và "ngưỡng dùng để chấm máy xuất
/// xưởng", nên nó phải từ chối rõ ràng thay vì nhận bừa: một file thiếu
/// `version` mà lọt vào là hồ sơ mất khả năng truy ngược.

Object? j(String s) => jsonDecode(s);

void main() {
  test('một bộ (file do nút Xuất tạo ra)', () {
    final r = parseAteLimitsImport(j('''
      {"version": "L2609A-1", "bright_min": 800, "bright_spread_pct": 8}
    '''));
    expect(r.ok, isTrue);
    expect(r.sets.keys.toList(), ['']); // không khai lô → bộ CHUNG
    expect(r.sets['']!['version'], 'L2609A-1');
    expect(r.sets['']!['bright_min'], 800);
    expect(r.unknownKeys, isEmpty);
    expect(r.isMulti, isFalse);
  });

  test('một bộ kèm lô: lấy lô, và BỎ khoá batch khỏi nội dung', () {
    final r = parseAteLimitsImport(
        j('{"batch": "L2609A", "version": "v1", "ambient_c": 28}'));
    expect(r.sets.keys.toList(), ['L2609A']);
    expect(r.sets['L2609A']!.containsKey('batch'), isFalse); // server tự đặt
    expect(r.sets['L2609A']!['ambient_c'], 28);
  });

  test('nhiều lô một file — dạng batches', () {
    final r = parseAteLimitsImport(j('''
      {"batches": {
        "L2609A": {"version": "A-1", "bright_min": 800},
        "L2609B": {"version": "B-1", "bright_min": 900}
      }}
    '''));
    expect(r.ok, isTrue);
    expect(r.isMulti, isTrue);
    expect(r.sets.length, 2);
    expect(r.sets['L2609B']!['bright_min'], 900);
  });

  test('nhiều lô một file — dạng items', () {
    final r = parseAteLimitsImport(j('''
      {"items": [
        {"batch": "L1", "version": "1"},
        {"batch": "L2", "version": "2"}
      ]}
    '''));
    expect(r.sets.keys.toList(), ['L1', 'L2']);
  });

  test('bỏ khoá do SERVER tự quản', () {
    final r = parseAteLimitsImport(j('''
      {"version": "v1", "updated_at": "2026-09-07T00:00:00Z",
       "updated_by": "cskh", "source": "batch"}
    '''));
    final set = r.sets['']!;
    expect(set.containsKey('updated_at'), isFalse);
    expect(set.containsKey('updated_by'), isFalse);
    expect(set.containsKey('source'), isFalse);
    expect(r.unknownKeys, isEmpty); // chúng bị bỏ, KHÔNG bị coi là khoá lạ
  });

  test('khoá lạ: GIỮ nguyên nhưng phải báo', () {
    final r = parseAteLimitsImport(
        j('{"version": "v1", "opt05_r2_min": 0.99, "ghi_chu_noi_bo": "x"}'));
    expect(r.ok, isTrue);
    // Giữ: app cũ gặp tiêu chuẩn của app mới thì không được làm mất ngưỡng.
    expect(r.sets['']!['opt05_r2_min'], 0.99);
    expect(r.unknownKeys, ['ghi_chu_noi_bo', 'opt05_r2_min']); // đã sắp xếp
  });

  group('từ chối rõ ràng', () {
    test('thiếu version — nói đúng lô nào', () {
      expect(parseAteLimitsImport(j('{"bright_min": 800}')).error,
          contains('Bộ chung'));
      final r = parseAteLimitsImport(
          j('{"batches": {"L1": {"version": "1"}, "L2": {"bright_min": 5}}}'));
      expect(r.ok, isFalse);
      expect(r.error, contains('L2'));
    });

    test('không phải JSON object', () {
      expect(parseAteLimitsImport(j('[1,2,3]')).ok, isFalse);
      expect(parseAteLimitsImport(j('"chuỗi"')).ok, isFalse);
      expect(parseAteLimitsImport(null).ok, isFalse);
    });

    test('phần tử trong batches/items không phải object', () {
      expect(parseAteLimitsImport(j('{"batches": {"L1": 5}}')).ok, isFalse);
      expect(parseAteLimitsImport(j('{"items": ["x"]}')).ok, isFalse);
    });

    test('file rỗng', () {
      expect(parseAteLimitsImport(j('{}')).ok, isFalse);
    });
  });

  test('xuất rồi nhập lại ra đúng bộ cũ', () {
    // Đúng vòng đời thật: app xuất file → gửi cho xưởng khác → nhập lại.
    const exported = {
      'batch': 'L2609A',
      'version': 'L2609A-2',
      'note': 'lô dùng LED lô mới',
      'require_flash_verify': true,
      'bright_min': 780,
      'bright_spread_pct': 9,
      'ambient_c': 27.5,
    };
    final r = parseAteLimitsImport(jsonDecode(jsonEncode(exported)));
    expect(r.sets.keys.toList(), ['L2609A']);
    final back = r.sets['L2609A']!;
    expect(back['version'], 'L2609A-2');
    expect(back['ambient_c'], 27.5);
    expect(back['note'], 'lô dùng LED lô mới');
    // Bộ nhập lại phải đọc được thành AteLimits mà không mất ngưỡng nào.
    final l = AteLimits.fromJson({...back, 'batch': 'L2609A', 'source': 'batch'});
    expect(l.brightMin, 780);
    expect(l.brightSpreadPct, 9);
    expect(l.ambientC, 27.5);
    expect(l.batch, 'L2609A');
  });
}
