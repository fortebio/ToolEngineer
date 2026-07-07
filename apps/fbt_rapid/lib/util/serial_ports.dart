import 'package:flutter_libserialport/flutter_libserialport.dart';

/// Chỉ giữ các cổng COM **dùng được với thiết bị** (đọc/ghi/nạp code): cổng
/// **USB-serial** như CP210x / CH340 / FTDI / ESP USB-JTAG… Bỏ qua cổng native
/// (COM1 nội bộ trên mainboard) và Bluetooth — không phải thiết bị FBT_RAPID.
///
/// An toàn: nếu KHÔNG cổng nào qua lọc (driver lạ báo transport != usb) thì trả
/// về TOÀN BỘ để người dùng không bị kẹt không chọn được cổng nào.
List<String> usableSerialPorts() {
  final all = _safeAvailable();
  final usb = <String>[for (final n in all) if (_isUsbSerial(n)) n];
  return usb.isEmpty ? all : usb;
}

List<String> _safeAvailable() {
  try {
    return SerialPort.availablePorts;
  } catch (_) {
    return const [];
  }
}

/// Cổng có phải USB-serial không (mở metadata, đóng ngay).
bool _isUsbSerial(String name) {
  SerialPort? p;
  try {
    p = SerialPort(name);
    // Bọc RIÊNG từng lần đọc metadata: nếu driver ném khi đọc transport,
    // KHÔNG loại cổng oan mà vẫn thử tiếp vendorId.
    try {
      if (p.transport == SerialPortTransport.usb) return true;
    } catch (_) {}
    // Vài adapter báo transport khác nhưng vẫn có Vendor ID (tức là USB).
    try {
      if (p.vendorId != null) return true;
    } catch (_) {}
    return false;
  } catch (_) {
    return false;
  } finally {
    try {
      p?.dispose();
    } catch (_) {}
  }
}
