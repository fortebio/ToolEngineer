import 'web_serial.dart';

/// Web: chỉ Chrome/Edge **desktop** trên HTTPS mới có `navigator.serial`.
/// Trình duyệt điện thoại (kể cả Chrome Android) KHÔNG có → false.
bool get serialToolsAvailable => webSerialSupported;
