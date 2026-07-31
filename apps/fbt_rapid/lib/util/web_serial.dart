/// Binding **Web Serial API** qua `dart:js_interop` — CHỈ được import từ code
/// màn WEB (`tech_screen_web` / `web_*`); build desktop không đụng file này.
///
/// Chỉ Chrome/Edge DESKTOP + HTTPS có API này ([webSerialSupported]). Trình
/// duyệt bắt người dùng tự chọn cổng qua hộp thoại ([requestSerialPort] phải
/// gọi từ 1 cú bấm); sau khi được cấp, object port giữ được để mở/đóng lại.
library;

import 'dart:async';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

@JS('navigator.serial')
external JSObject? get _navSerial;

/// Trình duyệt có Web Serial API không (Chrome/Edge desktop, HTTPS).
bool get webSerialSupported => _navSerial != null;

@JS('navigator.serial.requestPort')
external JSPromise<JSObject> _requestPort();

/// Mở hộp thoại chọn cổng của trình duyệt. Trả `null` nếu người dùng Hủy.
Future<WebSerialPort?> requestSerialPort() async {
  try {
    return WebSerialPort._(await _requestPort().toDart);
  } catch (_) {
    return null; // NotFoundError = bấm Hủy
  }
}

/// Bao 1 `SerialPort` JS: mở/đọc/ghi/đóng kiểu Dart. Object JS gốc ([js]) đưa
/// thẳng cho esptool-js (Transport) khi nạp code — lúc đó cổng phải ĐÓNG.
class WebSerialPort {
  final JSObject js;
  WebSerialPort._(this.js);

  JSObject? _reader;
  bool _open = false;
  bool get isOpen => _open;

  /// Nhãn hiển thị (Web Serial không có tên "COM3"): "USB 303a:1001" theo
  /// vendor/product id, không có thì "Cổng serial".
  String get label {
    try {
      final info = js.callMethod<JSObject>('getInfo'.toJS).dartify();
      if (info is Map) {
        final v = info['usbVendorId'];
        final p = info['usbProductId'];
        if (v is num) {
          String hex(Object? x) =>
              x is num ? x.toInt().toRadixString(16).padLeft(4, '0') : '????';
          return 'USB ${hex(v)}:${hex(p)}';
        }
      }
    } catch (_) {}
    return 'Cổng serial';
  }

  /// Mở cổng 8N1 không flow-control (khớp config desktop). GOTCHA ESP32/Forte:
  /// DTR→EN, RTS→GPIO0 là mạch auto-reset — ghim OFF ngay sau open (tương
  /// đương `..dtr = off ..rts = off` của bản desktop), nếu không trình duyệt
  /// bật 2 chân này lúc mở/ghi → thiết bị tự reset.
  Future<void> open({required int baud, bool pinSignalsOff = true}) async {
    await js
        .callMethod<JSPromise<JSAny?>>(
            'open'.toJS,
            {
              'baudRate': baud,
              'dataBits': 8,
              'stopBits': 1,
              'parity': 'none',
              'flowControl': 'none',
            }.jsify())
        .toDart;
    if (pinSignalsOff) await setSignals(dtr: false, rts: false);
    _open = true;
  }

  Future<void> setSignals({bool? dtr, bool? rts}) async {
    await js
        .callMethod<JSPromise<JSAny?>>(
            'setSignals'.toJS,
            {
              if (dtr != null) 'dataTerminalReady': dtr,
              if (rts != null) 'requestToSend': rts,
            }.jsify())
        .toDart;
  }

  /// Stream byte nhận — chạy vòng đọc nền tới khi [close] hoặc lỗi (mất cổng).
  Stream<Uint8List> readStream() {
    final ctrl = StreamController<Uint8List>();
    _readLoop(ctrl);
    return ctrl.stream;
  }

  Future<void> _readLoop(StreamController<Uint8List> ctrl) async {
    JSObject? reader;
    try {
      final readable = js.getProperty<JSObject?>('readable'.toJS);
      if (readable == null || readable.isUndefinedOrNull) return;
      reader = readable.callMethod<JSObject>('getReader'.toJS);
      _reader = reader;
      while (true) {
        final r =
            await reader.callMethod<JSPromise<JSObject>>('read'.toJS).toDart;
        final done = r.getProperty<JSBoolean?>('done'.toJS)?.toDart ?? true;
        if (done) break;
        final value = r.getProperty<JSUint8Array?>('value'.toJS);
        if (value != null && !ctrl.isClosed) ctrl.add(value.toDart);
      }
    } catch (e) {
      if (!ctrl.isClosed) ctrl.addError(StateError('Mất kết nối: $e'));
    } finally {
      try {
        reader?.callMethod<JSAny?>('releaseLock'.toJS);
      } catch (_) {}
      _reader = null;
      if (!ctrl.isClosed) await ctrl.close();
    }
  }

  /// Ghi bytes (lấy writer theo từng lần ghi rồi nhả lock — ghi thưa).
  Future<void> write(Uint8List data) async {
    final writable = js.getProperty<JSObject?>('writable'.toJS);
    if (writable == null || writable.isUndefinedOrNull) {
      throw StateError('Cổng chưa mở');
    }
    final writer = writable.callMethod<JSObject>('getWriter'.toJS);
    try {
      await writer
          .callMethod<JSPromise<JSAny?>>('write'.toJS, data.toJS)
          .toDart;
    } finally {
      try {
        writer.callMethod<JSAny?>('releaseLock'.toJS);
      } catch (_) {}
    }
  }

  /// Đóng cổng: hủy reader (vòng đọc tự nhả lock) rồi close. Port object vẫn
  /// dùng lại được (mở lại không cần hỏi quyền).
  Future<void> close() async {
    _open = false;
    try {
      await _reader?.callMethod<JSPromise<JSAny?>>('cancel'.toJS).toDart;
    } catch (_) {}
    // chờ vòng đọc nhả lock xong (releaseLock chạy trong finally của loop)
    await Future<void>.delayed(const Duration(milliseconds: 60));
    try {
      await js.callMethod<JSPromise<JSAny?>>('close'.toJS).toDart;
    } catch (_) {}
  }
}
