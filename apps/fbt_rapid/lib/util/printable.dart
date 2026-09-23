/// Mở một trang HTML để **IN** — tách theo nền tảng như `platform_files.dart`.
///
/// Import file NÀY (facade), ĐỪNG import thẳng bản `_io`/`_web`.
///
/// Vì sao không dùng plugin in (`printing`/`pdf`): app chạy cả Windows desktop
/// lẫn web, và mọi máy đều đã có trình duyệt biết in A4 với hộp thoại quen
/// thuộc (chọn máy in, khổ giấy, lề, xem trước). Tờ nhãn là HTML tự chứa
/// (`services/calib_label.dart`) nên "in" = mở nó ra; trang tự gọi
/// `window.print()`.
library;

export 'printable_io.dart' if (dart.library.html) 'printable_web.dart';
