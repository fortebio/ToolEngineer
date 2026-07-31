/// Binding **esptool-js** (bundle IIFE `window.esptoolJS`, nạp qua thẻ
/// `<script src="esptool.js">` trong `web/index.html`; bundle vendor bằng
/// esbuild từ npm `esptool-js` — xem CLAUDE.md). CHỈ import từ code màn WEB.
///
/// Luồng dùng (khớp esptool-js v0.6): port ĐÓNG → [EspToolSession.connect]
/// (tự reset vào bootloader, detect chip, chạy stub) → writeFlash/eraseFlash/
/// flashId → [hardReset] → [disconnect] (nhả cổng cho monitor).
library;

import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

@JS('esptoolJS')
external JSObject? get _ns;

/// Bundle esptool.js đã nạp chưa (thiếu thẻ script → false).
bool get esptoolJsLoaded => _ns != null;

@JS('esptoolJS.Transport')
external JSFunction get _transportCtor;

@JS('esptoolJS.ESPLoader')
external JSFunction get _loaderCtor;

/// 1 phiên làm việc với ROM/stub loader của chip qua esptool-js.
class EspToolSession {
  final JSObject _transport;
  final JSObject _loader;

  /// Tên chip detect được (vd "ESP32-D0WD-V3 (revision v3.1)").
  final String chipName;

  EspToolSession._(this._transport, this._loader, this.chipName);

  /// Kết nối + detect chip. [jsPort] = SerialPort JS GỐC (WebSerialPort.js),
  /// phải đang ĐÓNG — Transport tự mở @115200 rồi nâng lên [baud].
  /// [onLog] nhận log terminal của esptool-js (đã kèm xuống dòng khi cần).
  static Future<EspToolSession> connect({
    required JSObject jsPort,
    required int baud,
    required void Function(String) onLog,
  }) async {
    final transport = _transportCtor.callAsConstructor<JSObject>(jsPort);
    final terminal = JSObject()
      ..setProperty('clean'.toJS, (() {}).toJS)
      ..setProperty('writeLine'.toJS, ((String s) => onLog('$s\n')).toJS)
      ..setProperty('write'.toJS, ((String s) => onLog(s)).toJS);
    final opts = JSObject()
      ..setProperty('transport'.toJS, transport)
      ..setProperty('baudrate'.toJS, baud.toJS)
      ..setProperty('romBaudrate'.toJS, 115200.toJS)
      ..setProperty('terminal'.toJS, terminal);
    final loader = _loaderCtor.callAsConstructor<JSObject>(opts);
    final chip =
        await loader.callMethod<JSPromise<JSAny?>>('main'.toJS).toDart;
    return EspToolSession._(transport, loader, '${(chip as JSString?)?.toDart}');
  }

  /// In flash id/manufacturer (tương đương lệnh `flash_id` desktop).
  Future<void> flashId() async =>
      _loader.callMethod<JSPromise<JSAny?>>('flashId'.toJS).toDart;

  /// Xóa toàn bộ flash (tương đương `erase_flash`).
  Future<void> eraseFlash() async =>
      _loader.callMethod<JSPromise<JSAny?>>('eraseFlash'.toJS).toDart;

  /// Nạp các file .bin vào offset tương ứng (tương đương `write_flash`).
  /// [flashMode]/[flashSize] = 'keep' → giữ theo header .bin (như desktop).
  Future<void> writeFlash(
    List<({int address, Uint8List data})> files, {
    String flashMode = 'keep',
    String flashSize = 'keep',
    void Function(int fileIndex, int written, int total)? onProgress,
  }) async {
    final arr = <JSObject>[
      for (final f in files)
        JSObject()
          ..setProperty('data'.toJS, f.data.toJS)
          ..setProperty('address'.toJS, f.address.toJS),
    ];
    final opts = JSObject()
      ..setProperty('fileArray'.toJS, arr.toJS)
      ..setProperty('flashMode'.toJS, flashMode.toJS)
      ..setProperty('flashFreq'.toJS, 'keep'.toJS)
      ..setProperty('flashSize'.toJS, flashSize.toJS)
      ..setProperty('eraseAll'.toJS, false.toJS)
      ..setProperty('compress'.toJS, true.toJS);
    if (onProgress != null) {
      opts.setProperty(
          'reportProgress'.toJS,
          ((int fileIndex, int written, int total) =>
              onProgress(fileIndex, written, total)).toJS);
    }
    await _loader
        .callMethod<JSPromise<JSAny?>>('writeFlash'.toJS, opts)
        .toDart;
  }

  /// Reset cứng chạy firmware mới (tương đương `--after hard_reset`).
  Future<void> hardReset() async =>
      _loader.callMethod<JSPromise<JSAny?>>('after'.toJS).toDart;

  /// Nhả cổng (đóng + release lock) để monitor/console mở lại được.
  Future<void> disconnect() async =>
      _transport.callMethod<JSPromise<JSAny?>>('disconnect'.toJS).toDart;
}
