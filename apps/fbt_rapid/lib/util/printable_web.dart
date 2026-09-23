// Bản WEB của `printable.dart` — xem doc ở facade.
// (dart:html deprecated nhưng cả app đang dùng nó, xem `platform_files_web.dart`.)
// ignore_for_file: avoid_web_libraries_in_flutter, deprecated_member_use
import 'dart:convert';
import 'dart:html' as html;

import 'printable_types.dart';

export 'printable_types.dart';

/// In [htmlText] qua một **iframe ẩn** ngay trong trang; hỏng thì tải file xuống.
///
/// Vì sao KHÔNG `window.open` (bản đầu đã làm vậy và hỏng, 2026-09-23): tab mới
/// là pop-up, mà pop-up bị chặn ở rất nhiều máy — và khi bị chặn thì `dart:html`
/// KHÔNG trả null như kiểu `WindowBase` hứa, nó ném thẳng
/// `Attempting to use a null window opened in Window.open` (gặp thật trong
/// khung xem trước của Claude). Iframe không phải pop-up nên không có ai chặn.
///
/// Trang nhãn tự gọi `window.print()` trong `onload` của chính nó; gọi trong
/// iframe thì Chrome/Edge in ĐÚNG nội dung iframe. Không gọi thêm `print()` từ
/// Dart nữa — làm vậy là hộp thoại in bật hai lần.
///
/// Iframe phải sống đến khi người dùng đóng hộp thoại in (Chrome huỷ lệnh in
/// nếu khung bị gỡ giữa chừng) → dọn sau 2 phút.
Future<PrintOutcome> openHtmlForPrint(String htmlText,
    {String baseName = 'in'}) async {
  final blob = html.Blob(<Object>[utf8.encode(htmlText)], 'text/html;charset=utf-8');
  final url = html.Url.createObjectUrlFromBlob(blob);
  try {
    final frame = html.IFrameElement()
      ..style.position = 'fixed'
      ..style.right = '0'
      ..style.bottom = '0'
      ..style.width = '0'
      ..style.height = '0'
      ..style.border = '0'
      ..style.visibility = 'hidden'
      ..src = url;
    html.document.body!.append(frame);
    Future.delayed(const Duration(minutes: 2), () {
      frame.remove();
      html.Url.revokeObjectUrl(url);
    });
    return const PrintOutcome(opened: true);
  } catch (_) {
    // Không in được tại chỗ → tải file để người dùng tự mở và Ctrl+P.
    final name = '$baseName.html';
    final a = html.AnchorElement(href: url)..download = name;
    html.document.body?.append(a);
    a.click();
    a.remove();
    Future.delayed(const Duration(seconds: 30), () => html.Url.revokeObjectUrl(url));
    return PrintOutcome(path: name, downloaded: true);
  }
}
