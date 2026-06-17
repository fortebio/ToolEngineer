import 'dart:io';

/// Thư mục **gốc** để lưu file (kết quả CT, log nhiệt). Người dùng đặt trong
/// Cài đặt; trống = thư mục Documents của người dùng. Các store ghép tên thư
/// mục con (`FBT_RAPID_ketqua`, `FBT_RAPID_templog`) vào đây.
class StoragePaths {
  static String? _custom;

  /// Đặt thư mục gốc (gọi lúc khởi động & khi đổi trong Cài đặt).
  static void setParent(String? dir) {
    final d = dir?.trim();
    _custom = (d == null || d.isEmpty) ? null : d;
  }

  static String get parent {
    if (_custom != null) return _custom!;
    final home = Platform.environment['USERPROFILE'] ??
        Platform.environment['HOME'] ??
        '.';
    return '$home\\Documents';
  }
}
