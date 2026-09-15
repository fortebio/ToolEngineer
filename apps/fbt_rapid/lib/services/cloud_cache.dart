import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import 'cloud_history_api.dart';

/// Cache cục bộ cho danh sách máy cloud → load tức thì + giảm số lần gọi
/// Apps Script (vì `ids` quét cả folder Drive nên rất chậm).
///
/// Cache theo từng URL cloud (đổi URL → cache riêng).
class CloudCache {
  static String _key(String url) => 'cloud_devices_${url.hashCode}';

  /// Đọc danh sách máy đã lưu (kèm thời điểm lưu). null nếu chưa có.
  Future<({List<CloudDevice> devices, DateTime savedAt})?> loadDevices(
      String url) async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString(_key(url));
    if (raw == null || raw.isEmpty) return null;
    try {
      final m = jsonDecode(raw) as Map<String, dynamic>;
      final savedAt =
          DateTime.fromMillisecondsSinceEpoch((m['savedAt'] as num).toInt());
      final devices = ((m['devices'] as List?) ?? const [])
          .map((e) => CloudDevice.fromJson(e as Map<String, dynamic>))
          .toList();
      return (devices: devices, savedAt: savedAt);
    } catch (_) {
      return null;
    }
  }

  Future<void> saveDevices(
      String url, List<CloudDevice> devices, DateTime now) async {
    final p = await SharedPreferences.getInstance();
    final m = {
      'savedAt': now.millisecondsSinceEpoch,
      'devices': devices.map((d) => d.toJson()).toList(),
    };
    await p.setString(_key(url), jsonEncode(m));
  }
}
