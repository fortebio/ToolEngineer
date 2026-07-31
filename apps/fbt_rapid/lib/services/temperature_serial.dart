import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_libserialport/flutter_libserialport.dart';

import '../util/serial_ports.dart';
// Kiểu thuần (TempSample, kTempChannels) tách ra temp_types.dart cho web dùng
// chung; export lại để mọi import cũ của file này không phải đổi.
import 'temp_types.dart';

export 'temp_types.dart';

/// Đọc nhiệt từ MỘT cổng COM của máy RPL (@115200), parse dòng TimeRB/TimeRT,
/// **tự kết nối lại** khi tạm mất kết nối (rút cáp / lỗi stream).
class TempPortReader extends ChangeNotifier {
  final String portName;
  // Giữ ~toàn bộ quy trình; chỉ trim 10% cũ nhất khi vượt (rất hiếm khi chạm).
  static const int maxSamples = 500000;
  static const int maxRawLines = 2000000; // dòng UART thô giữ để xem lại
  static const int kBaudRate = 115200;

  SerialPort? _port;
  SerialPortReader? _reader;
  StreamSubscription<Uint8List>? _sub;
  Timer? _reconnectTimer;
  Timer? _watchdog;
  Timer? _kickstart; // tự gửi lệnh nếu mở cổng mà chưa thấy dữ liệu
  String _buf = '';
  bool _wantOpen = false; // người dùng MUỐN cổng này đọc (chưa bấm Dừng)
  bool _open = false; // đang kết nối thật
  bool _reconnecting = false; // mất kết nối, đang thử nối lại
  bool _outputOn = false; // app tin máy đang xuất nhiệt
  bool _gotData = false; // đã nhận được dòng nhiệt sau khi mở cổng
  int _kickAttempts = 0;
  String? _error;

  final List<TempSample> samples = [];
  final List<String> rawLines = []; // mọi dòng UART đọc được (hiển thị + xem lại)
  List<double?> latest = List.filled(6, null);
  final Stopwatch _clock = Stopwatch(); // thời gian app-elapsed → mốc trục X liên tục
  List<double?>? _pendTop;
  int _pendTopAtMs = 0; // mốc (app ms) khi nhận TimeRT, để gộp đúng cặp

  TempPortReader(this.portName);

  bool get isOpen => _open;
  bool get isReconnecting => _reconnecting;
  String? get error => _error;

  // ---- Kết nối ----

  void start() {
    if (_wantOpen) return;
    _wantOpen = true;
    if (!_clock.isRunning) _clock.start();
    _connect();
  }

  void _connect() {
    if (!_wantOpen) return;
    _error = null;
    SerialPort? p;
    try {
      if (!SerialPort.availablePorts.contains(portName)) {
        _scheduleReconnect('Chờ cổng $portName xuất hiện…');
        return;
      }
      p = SerialPort(portName);
      if (!p.openReadWrite()) {
        p.dispose();
        _scheduleReconnect('Cổng $portName đang bận, thử lại…');
        return;
      }
      final cfg = SerialPortConfig()
        ..baudRate = kBaudRate
        ..bits = 8
        ..parity = SerialPortParity.none
        ..stopBits = 1
        ..setFlowControl(SerialPortFlowControl.none)
        // GHIM DTR/RTS off: DTR→EN, RTS→GPIO0 là mạch auto-reset ESP32/Forte.
        // Không set → driver Windows tự bật 2 chân khi mở cổng → máy tự reset
        // ngay khi bắt đầu log nhiệt (giống serial_console_screen).
        ..dtr = SerialPortDtr.off
        ..rts = SerialPortRts.off;
      p.config = cfg;
      _port = p;
      _buf = '';
      _pendTop = null;
      _writeCmd('TemperatureOutput\n'); // bật xuất nhiệt (toggle)
      _outputOn = true;
      _reader = SerialPortReader(p);
      _sub = _reader!.stream.listen(
        _onData,
        onError: (Object e) => _handleLost('Mất kết nối: $e'),
        onDone: () => _handleLost('Cổng đã đóng.'),
        cancelOnError: true,
      );
      _open = true;
      _reconnecting = false;
      _error = null;
      _gotData = false;
      _kickAttempts = 0;
      _startWatchdog();
      _startKickstart();
      notifyListeners();
    } catch (e) {
      if (_port == null && p != null) {
        try {
          p.dispose();
        } catch (_) {}
      }
      _scheduleReconnect('Lỗi kết nối: $e');
    }
  }

  // Watchdog: cổng bị rút sẽ biến mất khỏi danh sách → coi như mất kết nối.
  void _startWatchdog() {
    _watchdog?.cancel();
    _watchdog = Timer.periodic(const Duration(seconds: 2), (_) {
      if (!_wantOpen || !_open) return;
      try {
        if (!SerialPort.availablePorts.contains(portName)) {
          _handleLost('Cổng $portName bị ngắt — đang kết nối lại…');
        }
      } catch (_) {}
    });
  }

  // Mở cổng nhưng chưa thấy dữ liệu → lệnh xuất nhiệt (toggle) có thể đang TẮT
  // → tự gửi lại để máy bắt đầu xuất nhiệt (tối đa 2 lần, cách nhau 3s).
  void _startKickstart() {
    _kickstart?.cancel();
    _kickstart = Timer.periodic(const Duration(seconds: 3), (_) {
      if (!_open) return;
      if (_gotData || _kickAttempts >= 2) {
        _kickstart?.cancel();
        _kickstart = null;
        return;
      }
      _kickAttempts++;
      _writeCmd('TemperatureOutput\n'); // chưa có dữ liệu → gửi lệnh lấy nhiệt
      _outputOn = !_outputOn;
    });
  }

  void _handleLost(String msg) {
    if (!_wantOpen) return; // đã dừng chủ động → bỏ qua
    _teardownPort(); // cổng đã chết → không gửi lệnh tắt
    _scheduleReconnect(msg);
  }

  void _scheduleReconnect(String msg) {
    _open = false;
    _reconnecting = true;
    _error = msg;
    notifyListeners();
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 2), () {
      if (_wantOpen) _connect();
    });
  }

  /// Gửi lại lệnh bật/tắt (toggle) — dùng khi không thấy dữ liệu.
  void sendToggle() {
    _outputOn = !_outputOn;
    _writeCmd('TemperatureOutput\n');
  }

  void _writeCmd(String s) {
    try {
      _port?.write(Uint8List.fromList(s.codeUnits));
    } catch (_) {}
  }

  // ---- Đọc & parse ----

  void _onData(Uint8List data) {
    _buf += String.fromCharCodes(data);
    var changed = false;
    int idx;
    while ((idx = _buf.indexOf('\n')) >= 0) {
      final line = _buf.substring(0, idx).trim();
      _buf = _buf.substring(idx + 1);
      if (line.isNotEmpty) {
        rawLines.add(line); // giữ MỌI dòng UART (kể cả dòng không phải nhiệt)
        changed = true;
      }
      _parseLine(line);
    }
    if (rawLines.length > maxRawLines) {
      rawLines.removeRange(0, maxRawLines ~/ 10); // bỏ 10% cũ nhất (amortized)
    }
    if (_buf.length > 8192) _buf = _buf.substring(_buf.length - 2048);
    if (changed) notifyListeners();
  }

  bool _parseLine(String line) {
    if (line.isEmpty) return false;
    final parts = line.split('\t');
    if (parts.length < 7) return false;
    final tag = parts[0];
    if (double.tryParse(parts[1]) == null) return false; // dòng Time* hợp lệ
    _gotData = true; // máy đang xuất nhiệt → khỏi cần kickstart
    final a = double.tryParse(parts[4]);
    final b = double.tryParse(parts[5]);
    final c = double.tryParse(parts[6]);
    // Mốc thời gian = app-elapsed (liên tục qua reconnect/máy-reset), không
    // dùng đồng hồ máy (millis máy nhảy về 0 khi rút cáp).
    final nowMs = _clock.elapsedMilliseconds;

    if (tag == 'TimeRT') {
      _pendTop = [a, b, c];
      _pendTopAtMs = nowMs;
      return false; // chờ TimeRB để gộp thành 1 mẫu
    }
    if (tag == 'TimeRB') {
      // gộp top của TimeRT nếu vừa nhận (< 3s); nếu mất dòng thì bỏ top.
      final useTop = _pendTop != null && (nowMs - _pendTopAtMs) < 3000;
      final top = useTop ? _pendTop! : const <double?>[null, null, null];
      final sample =
          TempSample(nowMs / 1000.0, [a, b, c, top[0], top[1], top[2]]);
      _pendTop = null;
      samples.add(sample);
      if (samples.length > maxSamples) {
        samples.removeRange(0, maxSamples ~/ 10); // bỏ 10% cũ nhất (amortized)
      }
      latest = sample.v;
      return true;
    }
    return false;
  }

  // ---- Dừng / dọn ----

  void stop() {
    _wantOpen = false;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _reconnecting = false;
    _error = null;
    _teardownPort(sendOff: true);
    notifyListeners();
  }

  void _teardownPort({bool sendOff = false}) {
    _watchdog?.cancel();
    _watchdog = null;
    _kickstart?.cancel();
    _kickstart = null;
    try {
      _sub?.cancel();
    } catch (_) {}
    try {
      _reader?.close();
    } catch (_) {}
    final port = _port;
    _port = null;
    _reader = null;
    _sub = null;
    _open = false;
    _buf = '';
    _pendTop = null;
    if (sendOff && port != null && _outputOn) {
      try {
        port.write(Uint8List.fromList('TemperatureOutput\n'.codeUnits));
      } catch (_) {}
    }
    _outputOn = false;
    // Hoãn đóng + giải phóng native để isolate đọc kịp dừng (tránh use-after-free).
    if (port != null) {
      Future.delayed(const Duration(milliseconds: 350), () {
        try {
          if (port.isOpen) port.close();
        } catch (_) {}
        try {
          port.dispose();
        } catch (_) {}
      });
    }
  }

  void clearData() {
    samples.clear();
    rawLines.clear();
    latest = List.filled(6, null);
    notifyListeners();
  }

  /// CSV: time_s,Lysis,Amp1,Amp2,Hotlid1,Hotlid2,Ambient
  String toCsv() {
    final sb = StringBuffer('time_s,${kTempChannels.join(',')}\n');
    for (final s in samples) {
      sb.write(s.t.toStringAsFixed(2));
      for (final v in s.v) {
        sb.write(',');
        if (v != null) sb.write(v.toStringAsFixed(2));
      }
      sb.write('\n');
    }
    return sb.toString();
  }

  @override
  void dispose() {
    stop();
    super.dispose();
  }
}

/// Quản lý nhiều cổng COM cùng lúc.
class TemperatureLogController extends ChangeNotifier {
  final Map<String, TempPortReader> readers = {};
  List<String> available = [];

  void refreshPorts() {
    try {
      available = usableSerialPorts(); // chỉ cổng USB-serial (đọc/ghi/nạp)
    } catch (_) {
      available = [];
    }
    notifyListeners();
  }

  bool isReading(String port) => readers.containsKey(port);

  void start(String port) {
    if (readers.containsKey(port)) return;
    readers[port] = TempPortReader(port)..start();
    notifyListeners();
  }

  void stop(String port) {
    readers.remove(port)?.dispose();
    notifyListeners();
  }

  void toggle(String port) =>
      readers.containsKey(port) ? stop(port) : start(port);

  void stopAll() {
    for (final r in readers.values) {
      r.dispose();
    }
    readers.clear();
    notifyListeners();
  }

  @override
  void dispose() {
    stopAll();
    super.dispose();
  }
}
