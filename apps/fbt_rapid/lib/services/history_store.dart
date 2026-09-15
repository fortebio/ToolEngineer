import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/test_result.dart';

/// **CÔNG TẮC DUY NHẤT của tính năng "Lịch sử cục bộ"** (tắt 2026-08-20 theo yêu cầu chủ
/// dự án). Đổi `true` để bật lại — code và import giữ nguyên, cùng khuôn `kShowFolderTab`
/// (`home_shell.dart`) và `kShowEngineerSettings` (`user_settings_screen.dart`).
///
/// Nó gác **HAI** chỗ, và phải gác cả hai mới gọi là tắt:
///  * mục **Cục bộ** trong tab Lịch sử (`history_combined_screen.dart`) — nơi ĐỌC;
///  * nút **"Đồng bộ trang này về máy"** ở `cloud_runs_screen.dart` — nơi GHI.
///
/// ⚠️ Trước đó chỉ mục đọc bị ẩn, còn nút ghi vẫn nằm đó: người dùng bấm đồng bộ, app tải
/// từng lần chạy về (mỗi lần một request có đường cong), lưu vào `shared_preferences` —
/// rồi **không có màn nào mở ra xem được**. Tốn mạng, phình prefs, và không một dấu hiệu
/// nào cho biết dữ liệu vừa rơi vào đâu.
const bool kLocalHistoryEnabled = false;

/// Lưu lịch sử xét nghiệm cục bộ trên máy tính (v1 chưa có cloud read).
class HistoryStore {
  static const _key = 'history_v1';

  Future<List<TestResult>> load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    if (raw == null || raw.isEmpty) return [];
    try {
      final list = jsonDecode(raw) as List;
      final items = list
          .map((e) => TestResult.fromJson(e as Map<String, dynamic>))
          .toList();
      // mới nhất lên đầu
      items.sort((a, b) => b.timestamp.compareTo(a.timestamp));
      return items;
    } catch (_) {
      return [];
    }
  }

  Future<void> _saveAll(List<TestResult> items) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = jsonEncode(items.map((e) => e.toJson()).toList());
    await prefs.setString(_key, raw);
  }

  Future<void> add(TestResult result) async {
    final items = await load();
    items.removeWhere((e) => e.id == result.id);
    items.insert(0, result);
    await _saveAll(items);
  }

  /// Gộp nhiều bản ghi (vd. đồng bộ từ cloud), khử trùng theo `id` (= fileId
  /// với bản ghi cloud), giữ bản mới nhất lên đầu. Chỉ load/save 1 lần.
  Future<void> addAll(List<TestResult> results) async {
    if (results.isEmpty) return;
    final items = await load();
    final byId = {for (final e in items) e.id: e};
    for (final r in results) {
      byId[r.id] = r;
    }
    final merged = byId.values.toList()
      ..sort((a, b) => b.timestamp.compareTo(a.timestamp));
    await _saveAll(merged);
  }

  Future<void> delete(String id) async {
    final items = await load();
    items.removeWhere((e) => e.id == id);
    await _saveAll(items);
  }

  Future<void> clear() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_key);
  }
}
