import '../util/platform_files.dart' as pf;

/// Thư mục **gốc** để lưu file (kết quả CT, log nhiệt). Người dùng đặt trong
/// Cài đặt; trống = thư mục Documents của người dùng. Các store ghép tên thư
/// mục con (`FBT_RAPID_ketqua`, `FBT_RAPID_templog`) vào đây.
/// (Trên WEB không có filesystem → `parent` = '' và không dùng đến.)
class StoragePaths {
  static String? _custom;

  /// Đặt thư mục gốc (gọi lúc khởi động & khi đổi trong Cài đặt).
  static void setParent(String? dir) {
    final d = dir?.trim();
    _custom = (d == null || d.isEmpty) ? null : d;
  }

  static String get parent => _custom ?? pf.defaultDocumentsDir();
}
