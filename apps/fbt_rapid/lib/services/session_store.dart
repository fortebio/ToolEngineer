import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/user_session.dart';

/// Giữ phiên đăng nhập **toàn cục** (đọc khắp app qua [current]) và lưu/khôi
/// phục cục bộ để lần sau mở app vào thẳng, không phải đăng nhập lại.
class SessionStore {
  static const _kKey = 'user_session_v1';

  /// Phiên đang đăng nhập; null = chưa đăng nhập.
  static UserSession? current;

  /// True nếu được phép ghi mạnh (Đồng bộ, Xóa, Lấy-từ-máy) — **nhân sự**
  /// (root + admin/nhân viên).
  static bool get canWrite => current?.isStaff ?? false;

  /// True nếu được phép **quản lý tài khoản** (thêm/xóa nhân viên + khách hàng)
  /// — chỉ **root**.
  static bool get canManageUsers => current?.isRoot ?? false;

  /// True nếu được phép **lưu ảnh đồ thị** về thư mục đã chỉ định — cho **mọi
  /// user đã đăng nhập** (cả user lẫn admin).
  static bool get canSaveCharts => current != null;

  /// Khôi phục phiên đã lưu (gọi lúc khởi động). Trả về phiên (hoặc null).
  static Future<UserSession?> load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString(_kKey);
    if (raw == null || raw.isEmpty) {
      current = null;
      return null;
    }
    try {
      current = UserSession.fromJson(jsonDecode(raw) as Map<String, dynamic>);
    } catch (_) {
      current = null;
    }
    return current;
  }

  static Future<void> save(UserSession s) async {
    current = s;
    final p = await SharedPreferences.getInstance();
    await p.setString(_kKey, jsonEncode(s.toJson()));
  }

  static Future<void> clear() async {
    current = null;
    final p = await SharedPreferences.getInstance();
    await p.remove(_kKey);
  }
}
