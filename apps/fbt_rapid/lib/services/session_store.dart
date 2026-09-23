import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/user_session.dart';

/// Giữ phiên đăng nhập **toàn cục** (đọc khắp app qua [current]) và lưu/khôi
/// phục cục bộ để lần sau mở app vào thẳng, không phải đăng nhập lại.
class SessionStore {
  static const _kKey = 'user_session_v1';

  /// Phiên đang đăng nhập; null = chưa đăng nhập.
  static UserSession? current;

  // Quyền: mọi getter fail-closed (chưa đăng nhập → false). Ý nghĩa từng quyền
  // ở `UserSession` — ở đây chỉ là lối tắt tĩnh cho màn hình.

  /// Ghi dữ liệu lâm sàng (Đồng bộ / Xoá / Lấy-từ-máy) — root + nhân viên.
  ///
  /// ⚠️ Tên cũ, GIỮ làm alias để 21 chỗ gọi không phải sửa trong một lần. Code
  /// mới dùng đúng quyền của việc mình làm ([canWriteOta], [canRunStation]…);
  /// "ghi" chung chung là thứ đã suýt trao quyền nạp firmware cả fleet cho
  /// người đứng máy ở xưởng.
  static bool get canWrite => canWriteClinical;

  static bool get canSeeClinical => current?.canSeeClinical ?? false;
  static bool get canWriteClinical => current?.canWriteClinical ?? false;
  static bool get canSupport => current?.canSupport ?? false;
  static bool get canUseTech => current?.canUseTech ?? false;
  static bool get canWriteOta => current?.canWriteOta ?? false;
  static bool get canSeeOta => current?.canSeeOta ?? false;
  static bool get canSeeProduction => current?.canSeeProduction ?? false;
  static bool get canSeeProductionStats =>
      current?.canSeeProductionStats ?? false;
  static bool get canRunStation => current?.canRunStation ?? false;
  static bool get canEditLimits => current?.canEditLimits ?? false;
  static bool get canSeeCalib => current?.canSeeCalib ?? false;
  static bool get canWriteCalib => current?.canWriteCalib ?? false;

  /// Tên đăng nhập hiện tại — ghi vào `by=` của các thao tác server (lô pha, bộ ống…).
  static String get username => current?.username ?? '';

  /// True nếu được phép **quản lý tài khoản**: root (mọi vai trò) hoặc quản lý
  /// sản xuất (chỉ thao tác viên — xem `UserSession.canManageRole`).
  static bool get canManageUsers => current?.canManageUsers ?? false;

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
