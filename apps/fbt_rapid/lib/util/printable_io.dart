// Bản DESKTOP (dart:io) của `printable.dart` — xem doc ở facade.
import 'dart:convert';
import 'dart:io';

import 'printable_types.dart';

export 'printable_types.dart';

/// Ghi [htmlText] ra file rồi mở bằng ứng dụng mặc định (trình duyệt) để in.
/// Trả về đường dẫn file — nơi gọi in ra snack để người dùng tìm lại bản in.
///
/// File nằm trong `%TEMP%\fbt_rapid_in` chứ không phải thư mục lưu của người
/// dùng: đây là bản in dùng một lần, không phải dữ liệu cần giữ (và Windows tự
/// dọn TEMP). Tên file có dấu thời gian nên in hai lần không đè nhau — trình
/// duyệt giữ tab cũ mở thì ghi đè cùng tên sẽ không thấy nội dung mới.
///
/// `start` qua `cmd /c` thay vì gọi thẳng `explorer.exe <file>`: `start` dùng
/// đúng liên kết mặc định của .html, còn explorer có máy lại mở File Explorer.
/// Tham số rỗng `''` đầu tiên là TIÊU ĐỀ cửa sổ của `start` — thiếu nó thì
/// đường dẫn (có dấu ngoặc kép) bị hiểu thành tiêu đề và không có gì mở ra.
Future<PrintOutcome> openHtmlForPrint(String htmlText,
    {String baseName = 'in'}) async {
  final dir = Directory('${Directory.systemTemp.path}${Platform.pathSeparator}fbt_rapid_in');
  dir.createSync(recursive: true);
  final now = DateTime.now();
  String two(int v) => v.toString().padLeft(2, '0');
  final stamp = '${now.year}${two(now.month)}${two(now.day)}'
      '_${two(now.hour)}${two(now.minute)}${two(now.second)}';
  final path = '${dir.path}${Platform.pathSeparator}${baseName}_$stamp.html';
  // Ghi kèm BOM UTF-8: mở file cục bộ thì trình duyệt KHÔNG có header
  // Content-Type, và tuy trang đã khai `<meta charset>`, BOM là thứ Chrome/Edge
  // tin trước tiên — thiếu nó, bản in tiếng Việt có máy ra "Ã´ng chuáº©n".
  await File(path).writeAsBytes([0xEF, 0xBB, 0xBF, ...utf8.encode(htmlText)]);
  await Process.run('cmd', ['/c', 'start', '', path]);
  return PrintOutcome(opened: true, path: path);
}
