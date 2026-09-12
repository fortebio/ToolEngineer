import 'dart:convert';
import 'dart:io';

import 'package:file_selector/file_selector.dart';

/// Bản DESKTOP (dart:io) của `platform_files.dart` — xem doc ở facade.

/// Thư mục Documents của user (gốc lưu file khi chưa cấu hình trong Cài đặt).
String defaultDocumentsDir() {
  final home = Platform.environment['USERPROFILE'] ??
      Platform.environment['HOME'] ??
      '.';
  return '$home\\Documents';
}

void ensureDir(String path) => Directory(path).createSync(recursive: true);

Future<void> writeFileBytes(String path, List<int> bytes) =>
    File(path).writeAsBytes(bytes);

Future<void> writeFileText(String path, String text) =>
    File(path).writeAsString(text);

/// Mở thư mục trong Explorer (tạo trước nếu chưa có).
void openFolder(String path) {
  try {
    ensureDir(path);
    Process.run('explorer.exe', [path]);
  } catch (_) {}
}

/// Hộp thoại "Save as" rồi ghi [text]. Trả đường dẫn đã lưu, null nếu hủy.
/// Mặc định lọc file JSON (đa số nơi gọi xuất JSON); nhật ký trạm truyền
/// `extensions: ['txt']`, CSV rollout truyền `['csv']` để hộp thoại không gợi ý
/// sai đuôi. [label] trống = suy từ đuôi (`CSV`, `TXT`) — merge 2026-09-12: nhánh
/// Giám sát chỉ truyền `extensions`, còn nhánh ATE truyền cả `label`.
Future<String?> saveTextFileDialog(
  String suggestedName,
  String text, {
  String label = '',
  List<String> extensions = const ['json'],
}) async {
  final loc = await getSaveLocation(
    suggestedName: suggestedName,
    acceptedTypeGroups: [
      if (extensions.isNotEmpty)
        XTypeGroup(
          label: label.isEmpty ? extensions.join('/').toUpperCase() : label,
          extensions: extensions,
        ),
    ],
  );
  if (loc == null) return null;
  // Ghi BYTES chứ không writeAsString: caller có thể đã gắn sẵn BOM UTF-8 ở đầu
  // (CSV cho Excel), mà writeAsString mã hoá lại theo utf8 thì BOM vẫn giữ —
  // nhưng dùng encode tường minh cho khỏi phụ thuộc mặc định của Dart đổi về sau.
  await File(loc.path).writeAsBytes(utf8.encode(text));
  return loc.path;
}

/// Hộp thoại "Save as" rồi ghi [bytes] (file nhị phân, vd firmware .bin).
/// Trả đường dẫn đã lưu, null nếu hủy. [extensions] không có dấu chấm.
Future<String?> saveBytesFileDialog(
  String suggestedName,
  List<int> bytes, {
  String label = 'File',
  List<String> extensions = const [],
}) async {
  final loc = await getSaveLocation(
    suggestedName: suggestedName,
    acceptedTypeGroups: [
      if (extensions.isNotEmpty) XTypeGroup(label: label, extensions: extensions),
    ],
  );
  if (loc == null) return null;
  await File(loc.path).writeAsBytes(bytes);
  return loc.path;
}

/// Chỉ có nghĩa trên web (tải xuống) — desktop ghi file theo đường dẫn.
void downloadBytes(String name, List<int> bytes) =>
    throw UnsupportedError('downloadBytes chỉ dùng trên web');
