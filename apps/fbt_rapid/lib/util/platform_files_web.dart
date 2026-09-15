// Bản WEB của `platform_files.dart` — trình duyệt không có filesystem: mọi
// "lưu file" đều là TẢI XUỐNG (vào thư mục Downloads); thao tác thư mục no-op.
// (dart:html deprecated nhưng khỏi thêm dependency package:web — đổi khi cần wasm.)
// ignore_for_file: avoid_web_libraries_in_flutter, deprecated_member_use
import 'dart:convert';
import 'dart:html' as html;

String defaultDocumentsDir() => '';

void ensureDir(String path) {}

Future<void> writeFileBytes(String path, List<int> bytes) async =>
    throw UnsupportedError('web: dùng downloadBytes');

Future<void> writeFileText(String path, String text) async =>
    throw UnsupportedError('web: dùng downloadBytes');

void openFolder(String path) {}

/// Web: không có hộp thoại "Save as" — tải thẳng xuống Downloads.
/// [label]/[extensions] chỉ có nghĩa trên desktop (bộ lọc hộp thoại), giữ ở đây
/// cho khớp chữ ký.
Future<String?> saveTextFileDialog(
  String suggestedName,
  String text, {
  String label = '',
  List<String> extensions = const ['json'],
}) async {
  downloadBytes(suggestedName, utf8.encode(text));
  return suggestedName;
}

/// Web: không có hộp thoại "Save as" — tải thẳng xuống Downloads.
Future<String?> saveBytesFileDialog(
  String suggestedName,
  List<int> bytes, {
  String label = 'File',
  List<String> extensions = const [],
}) async {
  downloadBytes(suggestedName, bytes);
  return suggestedName;
}

/// Tải [bytes] xuống dưới tên [name] qua thẻ <a download> + data URI.
void downloadBytes(String name, List<int> bytes) {
  final a = html.AnchorElement(
      href: 'data:application/octet-stream;base64,${base64Encode(bytes)}')
    ..download = name;
  html.document.body?.append(a);
  a.click();
  a.remove();
}
