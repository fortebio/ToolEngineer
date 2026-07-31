/// Thao tác file/thư mục TÁCH THEO NỀN TẢNG: desktop (dart:io) ghi file/mở
/// Explorer; web tải xuống qua trình duyệt (không có filesystem).
///
/// Import file NÀY (facade), ĐỪNG import thẳng bản `_io`/`_web` — conditional
/// import bên dưới tự chọn đúng bản khi build Windows hoặc web.
library;

export 'platform_files_io.dart'
    if (dart.library.html) 'platform_files_web.dart';
