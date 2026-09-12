/// Kiểu dùng chung của `serial_link.dart` — thuần Dart, KHÔNG dart:io/ffi/html,
/// để cả bản desktop (`serial_link_io.dart`) lẫn web (`serial_link_web.dart`)
/// cùng implement và màn hình chỉ nhìn thấy một giao diện.
library;

import 'dart:typed_data';

/// Một cổng serial ĐÃ MỞ, tối giản cho màn **Xử lý sự cố**: đọc luồng byte,
/// ghi lệnh, đóng. Không HEX, không đa cổng — những thứ đó ở tab Kỹ Thuật.
abstract class SerialLink {
  /// Nhãn hiển thị: `COM5` (desktop) hoặc `USB 10c4:ea60` (web).
  String get label;
  int get baud;
  bool get isOpen;

  /// Byte nhận từ máy. Kết thúc (done) hoặc lỗi khi mất cổng.
  Stream<Uint8List> get stream;

  Future<void> write(List<int> bytes);
  Future<void> close();
}

/// Lỗi mở/ghi cổng có thông điệp tiếng Việt sẵn để hiện thẳng cho người dùng.
class SerialLinkException implements Exception {
  final String message;
  const SerialLinkException(this.message);
  @override
  String toString() => message;
}
