/// Bản WEB (Web Serial API, Chrome/Edge desktop) của `serial_link.dart` — xem
/// doc ở facade. Trình duyệt KHÔNG liệt kê cổng: `openSerialLink` bật hộp thoại
/// chọn cổng của browser (phải gọi từ một cú bấm), người dùng Hủy → `null`.
/// `WebSerialPort.open` đã ghim DTR/RTS off (chống auto-reset ESP32).
library;

import 'dart:async';
import 'dart:typed_data';

import 'serial_link_types.dart';
import 'web_serial.dart';

bool get serialLinkAvailable => webSerialSupported;
bool get serialLinkCanListPorts => false;
List<String> listSerialLinkPorts() => const [];

Future<SerialLink?> openSerialLink({String? name, int baud = 115200}) async {
  if (!webSerialSupported) {
    throw const SerialLinkException(
        'Trình duyệt này không hỗ trợ Web Serial — dùng Chrome/Edge trên máy tính.');
  }
  final port = await requestSerialPort();
  if (port == null) return null; // người dùng bấm Hủy
  try {
    await port.open(baud: baud);
  } catch (e) {
    throw SerialLinkException(
        'Không mở được cổng — cổng đang bận ở tab/ứng dụng khác hoặc cáp đã rút ($e).');
  }
  return _WebLink(port, baud);
}

class _WebLink implements SerialLink {
  final WebSerialPort _port;
  @override
  final int baud;
  Stream<Uint8List>? _stream;

  _WebLink(this._port, this.baud);

  @override
  String get label => _port.label;

  @override
  bool get isOpen => _port.isOpen;

  @override
  Stream<Uint8List> get stream => _stream ??= _port.readStream();

  @override
  Future<void> write(List<int> bytes) => _port.write(Uint8List.fromList(bytes));

  @override
  Future<void> close() => _port.close();
}
