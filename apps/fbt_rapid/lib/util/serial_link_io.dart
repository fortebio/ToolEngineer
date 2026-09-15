/// Bản DESKTOP (Windows, `flutter_libserialport`) của `serial_link.dart` — xem
/// doc ở facade. Cấu hình cổng SAO Y `serial_console_screen.dart`: 8N1, không
/// flow-control, **DTR/RTS = off** (mạch auto-reset ESP32: DTR→EN, RTS→GPIO0 —
/// bỏ trống là driver Windows tự bật lúc mở/ghi → máy reset ngay khi vừa nối).
library;

import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_libserialport/flutter_libserialport.dart';

import 'serial_link_types.dart';
import 'serial_ports.dart';

bool get serialLinkAvailable => true;
bool get serialLinkCanListPorts => true;

/// Chỉ cổng USB-serial (bỏ COM1 native / Bluetooth) — cùng bộ lọc tab Kỹ Thuật.
List<String> listSerialLinkPorts() => usableSerialPorts();

Future<SerialLink?> openSerialLink({String? name, int baud = 115200}) async {
  final n = (name ?? '').trim();
  if (n.isEmpty) throw const SerialLinkException('Chưa chọn cổng COM.');
  final p = SerialPort(n);
  if (!p.openReadWrite()) {
    try {
      p.dispose();
    } catch (_) {}
    // Hai nguyên nhân thật: cổng đang mở ở tab Kỹ Thuật (Log nhiệt giữ đọc
    // nền) hoặc cáp vừa rút. Nói cả hai để người dùng tự xử.
    throw SerialLinkException(
        'Không mở được cổng $n — cổng đang bận (đang mở ở tab Kỹ Thuật?) '
        'hoặc cáp USB đã rút. Đóng cổng ở nơi khác / cắm lại rồi thử lại.');
  }
  try {
    p.config = SerialPortConfig()
      ..baudRate = baud
      ..bits = 8
      ..parity = SerialPortParity.none
      ..stopBits = 1
      ..setFlowControl(SerialPortFlowControl.none)
      ..dtr = SerialPortDtr.off
      ..rts = SerialPortRts.off;
  } catch (e) {
    try {
      p.close();
      p.dispose();
    } catch (_) {}
    throw SerialLinkException('Không cấu hình được cổng $n: $e');
  }
  return _IoLink(p, n, baud);
}

class _IoLink implements SerialLink {
  final SerialPort _port;
  final SerialPortReader _reader;
  @override
  final String label;
  @override
  final int baud;
  bool _open = true;

  _IoLink(this._port, this.label, this.baud) : _reader = SerialPortReader(_port);

  @override
  bool get isOpen => _open;

  @override
  Stream<Uint8List> get stream => _reader.stream;

  @override
  Future<void> write(List<int> bytes) async {
    if (!_open) throw const SerialLinkException('Cổng đã đóng.');
    _port.write(Uint8List.fromList(bytes));
  }

  @override
  Future<void> close() async {
    if (!_open) return;
    _open = false;
    try {
      _reader.close();
    } catch (_) {}
    try {
      if (_port.isOpen) _port.close();
    } catch (_) {}
    try {
      _port.dispose();
    } catch (_) {}
  }
}
