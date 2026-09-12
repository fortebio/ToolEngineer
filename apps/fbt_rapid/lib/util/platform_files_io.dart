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
/// `extensions: ['txt']` để hộp thoại không gợi ý sai đuôi.
Future<String?> saveTextFileDialog(
  String suggestedName,
  String text, {
  String label = 'JSON',
  List<String> extensions = const ['json'],
}) async {
  final loc = await getSaveLocation(
    suggestedName: suggestedName,
    acceptedTypeGroups: [
      if (extensions.isNotEmpty) XTypeGroup(label: label, extensions: extensions),
    ],
  );
  if (loc == null) return null;
  await File(loc.path).writeAsString(text);
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
