import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/test_result.dart';
import 'history_store.dart';

/// Sao lưu / khôi phục **lịch sử xét nghiệm + cài đặt** ra/vào 1 file JSON.
class BackupService {
  static const _historyKey = 'history_v1';
  static const _settingKeys = [
    'device_ip',
    'reading_interval_sec',
    'user_name',
    'user_org',
    'cloud_api_url',
    'com_names_v1',
    'save_dir',
  ];

  /// Tạo nội dung JSON sao lưu từ dữ liệu hiện tại.
  static Future<String> buildJson() async {
    final p = await SharedPreferences.getInstance();
    final settings = <String, dynamic>{};
    for (final k in _settingKeys) {
      final v = p.get(k);
      if (v != null) settings[k] = v;
    }
    List<dynamic> history = const [];
    final raw = p.getString(_historyKey);
    if (raw != null && raw.isNotEmpty) {
      try {
        history = jsonDecode(raw) as List;
      } catch (_) {}
    }
    return const JsonEncoder.withIndent('  ').convert({
      'app': 'FBT_RAPID',
      'backupVersion': 1,
      'exportedAt': DateTime.now().toIso8601String(),
      'history': history,
      'settings': settings,
    });
  }

  static bool isValid(Map<String, dynamic> b) =>
      b['app'] == 'FBT_RAPID' &&
      (b.containsKey('history') || b.containsKey('settings'));

  /// Khôi phục. [mergeHistory] = true → **gộp** (không mất dữ liệu hiện có);
  /// false → **thay thế** toàn bộ lịch sử. Trả về số lần chạy trong file.
  static Future<int> restore(
    Map<String, dynamic> backup, {
    required bool mergeHistory,
  }) async {
    final p = await SharedPreferences.getInstance();

    // Cài đặt
    final s = (backup['settings'] as Map?) ?? const {};
    for (final entry in s.entries) {
      final k = entry.key.toString();
      final v = entry.value;
      if (v is int) {
        await p.setInt(k, v);
      } else if (v is double) {
        await p.setDouble(k, v);
      } else if (v is bool) {
        await p.setBool(k, v);
      } else if (v != null) {
        await p.setString(k, v.toString());
      }
    }

    // Lịch sử
    final hist = (backup['history'] as List?) ?? const [];
    final incoming = hist
        .map((e) => TestResult.fromJson(e as Map<String, dynamic>))
        .toList();
    if (mergeHistory) {
      await HistoryStore().addAll(incoming);
    } else {
      await p.setString(
          _historyKey, jsonEncode(incoming.map((e) => e.toJson()).toList()));
    }
    return incoming.length;
  }
}
