import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

/// Tên gợi nhớ (alias) cho từng cổng COM — để dễ nhận biết trong tab Nhiệt độ.
/// Lưu cục bộ theo tên cổng (vd COM3 → "Máy Lysis").
class ComNames {
  static const _key = 'com_names_v1';
  Map<String, String> _map = {};

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString(_key);
    if (raw == null || raw.isEmpty) return;
    try {
      final m = jsonDecode(raw) as Map<String, dynamic>;
      _map = m.map((k, v) => MapEntry(k, v.toString()));
    } catch (_) {}
  }

  String nameOf(String port) => _map[port] ?? '';

  /// Nhãn hiển thị: "Tên (COM3)" nếu có tên, ngược lại "COM3".
  String label(String port) {
    final n = _map[port];
    return (n == null || n.isEmpty) ? port : '$n ($port)';
  }

  Future<void> setName(String port, String name) async {
    final n = name.trim();
    if (n.isEmpty) {
      _map.remove(port);
    } else {
      _map[port] = n;
    }
    final p = await SharedPreferences.getInstance();
    await p.setString(_key, jsonEncode(_map));
  }
}
