/// Bản **WEB** của hàng đợi hồ sơ ATE — cùng API với `ate_queue_io.dart`, nhưng
/// chỗ chứa là **localStorage** (qua `shared_preferences`) chứ không phải file.
///
/// Ba khác biệt phải nói với xưởng nếu trạm chạy trên web:
///
/// 1. **Log thô bị lược khi xếp hàng.** localStorage cỡ ~5MB cho cả origin, mà
///    log esptool + UART của MỘT máy có thể vài trăm KB. Hồ sơ gửi thẳng lên
///    server thì vẫn đủ log; chỉ bản nằm chờ trong hàng đợi là bị cắt phần `raw`.
///    Cắt có chủ đích và ghi rõ trong ghi chú của hồ sơ, hơn là để đầy bộ nhớ
///    rồi mất sạch cả hàng đợi.
/// 2. **Xoá dữ liệu duyệt = mất hàng đợi.** Bản desktop ghi file nên không dính.
/// 3. **Trần số hồ sơ chờ** ([_maxPending]): đầy thì hồ sơ CŨ NHẤT bị đẩy ra và
///    đếm vào `rejected` của lần gửi kế — thà mất bản cũ nhất một cách có báo còn
///    hơn `QuotaExceededError` làm hỏng cả lần lưu.
library;

import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/ate_record.dart';
import 'ate_api.dart';

class AteQueue {
  static const _key = 'ate_queue_v1';
  static const _keyDropped = 'ate_queue_dropped_v1';

  /// Đủ cho một ca mất mạng (30–50 máy) mà không chạm trần localStorage.
  static const int _maxPending = 60;

  /// Không có thư mục trên web — trả chuỗi mô tả để màn hình nói cho người dùng.
  static String get baseDir => 'localStorage (trình duyệt)';
  static String get pendingDir => baseDir;
  static String get sentDir => baseDir;
  static String get rejectedDir => baseDir;

  static String fileNameFor(AteRecord r) {
    final t = r.startedAt.toUtc();
    String two(int v) => v.toString().padLeft(2, '0');
    return '${r.sn}_${t.year}${two(t.month)}${two(t.day)}_'
        '${two(t.hour)}${two(t.minute)}${two(t.second)}';
  }

  /// Bản hồ sơ để XẾP HÀNG: bỏ `raw` của từng bước (xem ghi chú đầu file).
  static Map<String, dynamic> _slim(AteRecord r) {
    final j = r.toJson();
    final steps = (j['steps'] as List).cast<Map<String, dynamic>>();
    var trimmed = 0;
    for (final s in steps) {
      if (s.remove('raw') != null) trimmed++;
    }
    if (trimmed > 0) {
      final note = (j['note'] ?? '').toString();
      j['note'] = [
        if (note.isNotEmpty) note,
        '[bản web: log thô của $trimmed bước bị lược khi xếp hàng offline]',
      ].join(' · ');
    }
    return j;
  }

  static Future<List<String>> _read() async {
    final p = await SharedPreferences.getInstance();
    return p.getStringList(_key) ?? const [];
  }

  static Future<void> _write(List<String> items) async {
    final p = await SharedPreferences.getInstance();
    await p.setStringList(_key, items);
  }

  /// Ghi hồ sơ vào hàng đợi. Trả "đường dẫn" (ở đây là khoá hiển thị).
  static Future<String> save(AteRecord r) async {
    final items = [...await _read(), jsonEncode(_slim(r))];
    var dropped = 0;
    while (items.length > _maxPending) {
      items.removeAt(0);
      dropped++;
    }
    if (dropped > 0) {
      final p = await SharedPreferences.getInstance();
      await p.setInt(_keyDropped, (p.getInt(_keyDropped) ?? 0) + dropped);
    }
    await _write(items);
    return '$baseDir · ${fileNameFor(r)}';
  }

  static Future<int> pendingCount() async => (await _read()).length;

  /// Gửi lại toàn bộ hàng đợi — cùng luật với bản desktop: lỗi mạng thì DỪNG,
  /// server từ chối (400) thì bỏ hồ sơ đó ra và ĐI TIẾP.
  static Future<({int sent, int left, int rejected, String? error})> flush(
      AteApi api) async {
    final items = [...await _read()];
    var sent = 0;
    var rejected = 0;

    // Hồ sơ bị đẩy ra vì đầy hàng đợi cũng phải hiện lên một lần, không im lặng.
    final prefs = await SharedPreferences.getInstance();
    final dropped = prefs.getInt(_keyDropped) ?? 0;
    if (dropped > 0) {
      rejected += dropped;
      await prefs.remove(_keyDropped);
    }

    while (items.isNotEmpty) {
      final raw = items.first;
      try {
        final body = jsonDecode(raw);
        if (body is! Map<String, dynamic>) {
          items.removeAt(0);
          rejected++;
          continue;
        }
        await api.putRecordJson(body);
        items.removeAt(0);
        sent++;
      } on AteRejectedException {
        items.removeAt(0);
        rejected++;
      } catch (e) {
        await _write(items);
        return (sent: sent, left: items.length, rejected: rejected, error: '$e');
      }
    }
    await _write(items);
    return (sent: sent, left: 0, rejected: rejected, error: null);
  }
}
