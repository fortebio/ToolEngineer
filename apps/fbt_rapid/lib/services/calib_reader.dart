/// Đọc số thô ống chuẩn **trực tiếp từ máy tham chiếu** qua serial — thay cho bước "mở
/// Serial Debug Assistant, gõ `0`, chép số vào sheet" của bàn giao.
///
/// Giao thức (đối chiếu firmware `firmware/rapidplus/src/ForteSetting.cpp::loop` +
/// `sensor6035.cpp::OptoCommandProcess/testShot`, 2026-09-21):
/// - Gửi **ĐÚNG MỘT byte** `'0'..'9'` = khe 1..10 (`recvLen == 1` mới vào nhánh opto —
///   kèm `\n` là thành lệnh 2 byte, firmware bỏ qua). Máy bật LED khe đó, lấy 8 mẫu
///   (~1 s) rồi in `{Green: <tổng 8 mẫu>}` — đúng con số "raw" mà sheet bàn giao ghi.
/// - Beta prototype trong WI DxD Hub in `raw,calibrated` (vd `1521,792.19`) → parser
///   nhận luôn để dùng được với máy đời đó.
/// - Firmware in thêm dòng `data received from Serial port` trước — bỏ qua.
/// - **Chế độ đọc ống chuẩn** (firmware v2.4.6, nhánh `v2.4.5at-calib-solution`): lệnh nhiều
///   ký tự kết thúc `\n` — `CalibStart[,slot]` (LCD sang màn đọc, máy ngừng quay LED),
///   `CalibSlot,n`, `CalibLabel,<text>` (chữ hiện trên LCD), `CalibShot`, `CalibEnd`. Máy trả
///   `{CalibMode: on|off}`, `{CalibSlot: n}`, `{CalibLabel: ok}`, `{CalibError: <lý do>}`. Nút ĐỎ
///   trên máy cũng đọc → máy in `{Green: N}` mà app không hỏi → phát ra [readings]. Firmware cũ
///   không có chế độ này trả "Command is not supported!" — vô hại, `readSlot` 1 byte vẫn chạy.
/// - **Khi máy đã xác nhận `{CalibMode: on}`, [readSlot] gửi `CalibShot` và CHỈ nhận `{Green: N}`**
///   (2026-09-22): firmware gom mọi byte tới trong cùng 1 s `readBytes` vào một buffer rồi **echo**
///   nguyên buffer; Enter ngay sau `CalibLabel` (app gửi sau mỗi lần đọc) → buffer `CalibLabel,…\n3`
///   → máy echo dòng `3` → parser số trần nhận `3` (= byte khe 4) làm số đo, còn lệnh đọc thật bị
///   nuốt. 11/40 ô của một lô ghi đúng số 3. Số trần chỉ còn dành cho Beta prototype (không có chế độ).
/// - **Nút TRẮNG trên máy giữa chừng** → máy in `{CalibMode: off}` và về màn chính (2026-09-22). App
///   vẫn nối: không gửi `CalibLabel` nữa (máy trả `notInMode` → hiện thành lỗi cho mọi ống kế), và lần
///   [readSlot] kế tiếp **tự vào lại**: một gói `CalibStart,<khe>` + `CalibLabel,<nhãn cuối>` +
///   `CalibShot` (firmware ≥ f89e451 tách dòng) → máy trả `{CalibMode: on}` … `{Green: N}`.
///
/// THUẦN Dart (không Flutter, không dart:io): mọi thứ chạm phần cứng đi qua
/// [SerialLink] nên test được bằng cổng giả (`test/calib_reader_test.dart`).
library;

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../util/serial_link_types.dart';

final _reFleet = RegExp(r'\{\s*Green\s*:\s*(-?\d+(?:\.\d+)?)\s*\}', caseSensitive: false);
final _reProto = RegExp(r'^\s*(-?\d+(?:\.\d+)?)\s*,\s*-?\d+(?:\.\d+)?\s*$');
final _reBare = RegExp(r'^\s*(-?\d+(?:\.\d+)?)\s*$');
final _reSlotEvt = RegExp(r'\{\s*CalibSlot\s*:\s*(\d+)\s*\}', caseSensitive: false);
final _reMode = RegExp(r'\{\s*CalibMode\s*:\s*(on|off)\s*\}', caseSensitive: false);
final _reErr = RegExp(r'\{\s*CalibError\s*:\s*([^}]*)\}', caseSensitive: false);

/// Trích số raw từ MỘT dòng máy in ra; `null` nếu dòng không phải kết quả đo.
///
/// Thứ tự thử: `{Green: N}` (fleet Rapid+) → `raw,calibrated` (Beta prototype WI) →
/// một số trần. Dòng chữ (`data received from Serial port`, log khác) → null.
double? parseCalibRaw(String line) {
  final m = _reFleet.firstMatch(line) ?? _reProto.firstMatch(line) ?? _reBare.firstMatch(line);
  if (m == null) return null;
  return double.tryParse(m.group(1)!);
}

/// CHỈ `{Green: N}` — dùng khi máy đã ở chế độ ống chuẩn (echo/log của máy có thể chứa số trần).
double? _fleetOnly(String line) {
  final m = _reFleet.firstMatch(line);
  return m == null ? null : double.tryParse(m.group(1)!);
}

/// Lệnh 1 byte cho khe [slot] (1..10) — `'0'` là khe 1 (bảng 4 của WI: API 0 = prototype 1).
List<int> calibSlotCommand(int slot) {
  if (slot < 1 || slot > 10) throw ArgumentError.value(slot, 'slot', 'khe phải trong 1..10');
  return [0x30 + (slot - 1)];
}

/// Nhãn hiện trên LCD máy cho ô đang đọc: ASCII không dấu (font GFX mặc định của máy chỉ có
/// ASCII), ≤ 31 ký tự (buffer firmware 32). `null` = đã đọc hết.
String calibLabelFor(({String conc, int tube})? target) {
  if (target == null) return 'Da doc het';
  return '${target.conc} nM - ong ${target.tube}';
}

/// Lọc chuỗi gửi xuống máy: chỉ ASCII in được, bỏ xuống dòng/dấu phẩy đầu, cắt ≤ 31.
String calibAsciiLabel(String s) {
  final b = StringBuffer();
  for (final r in s.runes) {
    if (r >= 0x20 && r <= 0x7E) b.writeCharCode(r);
  }
  final out = b.toString().trim();
  return out.length > 31 ? out.substring(0, 31) : out;
}

class CalibReadException implements Exception {
  final String message;
  const CalibReadException(this.message);
  @override
  String toString() => message;
}

/// Bọc một [SerialLink] đã mở: gom byte thành dòng, mỗi [readSlot] gửi lệnh rồi chờ
/// dòng có số. Một lần đọc một lúc — bấm hai lần liên tiếp thì lần sau phải chờ.
class CalibReader {
  final SerialLink link;
  final Duration timeout;

  final _lines = StreamController<String>.broadcast();
  final _readings = StreamController<double>.broadcast();
  final _slots = StreamController<int>.broadcast();
  final _errors = StreamController<String>.broadcast();
  final _log = <String>[];
  final _modes = StreamController<bool>.broadcast();
  bool _modeOn = false;
  bool _modeSeen = false; // máy đã từng xác nhận chế độ → firmware có Calib*, vào lại được
  String _label = '';
  StreamSubscription<Uint8List>? _sub;
  String _buf = '';
  bool _busy = false;
  bool _closed = false;

  /// [timeout]: testShot lấy 8 mẫu cách 100 ms + LED → ~1–2 s; 6 s là dư cho máy chậm.
  CalibReader(this.link, {this.timeout = const Duration(seconds: 6)}) {
    _sub = link.stream.listen(_onBytes, onError: (_) => _end(), onDone: _end);
  }

  bool get isBusy => _busy;
  bool get isOpen => !_closed && link.isOpen;

  /// Vài dòng gần nhất máy in ra (hiện cho kỹ sư khi đọc lỗi/timeout).
  List<String> get recentLines => List.unmodifiable(_log);

  /// Số đo máy in ra mà app KHÔNG hỏi (kỹ sư bấm nút ĐỎ trên máy). Khi [readSlot] đang chờ
  /// thì dòng `{Green: N}` thuộc về nó, không phát ra đây — tránh điền một số hai lần.
  Stream<double> get readings => _readings.stream;

  /// Máy đổi khe (nút XANH trên máy, hoặc xác nhận `CalibSlot`) — số khe 1..10.
  Stream<int> get slotChanges => _slots.stream;

  /// `{CalibError: …}` máy in ra ngoài lúc [readSlot] chờ (vd bấm ĐỎ khi máy bận).
  Stream<String> get calibErrors => _errors.stream;

  /// Máy đã xác nhận đang ở chế độ đọc ống chuẩn (`{CalibMode: on}`).
  bool get modeOn => _modeOn;

  /// Firmware có chế độ ống chuẩn (đã thấy `{CalibMode: on}` ít nhất một lần trong phiên này).
  bool get modeSupported => _modeSeen;

  /// Máy vào/ra chế độ — `false` khi kỹ sư bấm nút TRẮNG trên máy (app nên báo, không phải lỗi).
  Stream<bool> get modeChanges => _modes.stream;

  void _onBytes(Uint8List b) {
    _buf += latin1.decode(b, allowInvalid: true);
    while (true) {
      final i = _buf.indexOf('\n');
      if (i < 0) break;
      final line = _buf.substring(0, i).replaceAll('\r', '');
      _buf = _buf.substring(i + 1);
      if (line.trim().isEmpty) continue;
      _log.add(line);
      if (_log.length > 20) _log.removeAt(0);
      _lines.add(line);
      _onEvent(line);
    }
  }

  /// Sự kiện máy tự phát (không thuộc một [readSlot] đang chờ).
  void _onEvent(String line) {
    final m = _reMode.firstMatch(line);
    if (m != null) {
      final on = m.group(1)!.toLowerCase() == 'on';
      if (on) _modeSeen = true;
      final changed = on != _modeOn;
      _modeOn = on;
      if (changed) _modes.add(on);
      return;
    }
    final s = _reSlotEvt.firstMatch(line);
    if (s != null) {
      final n = int.tryParse(s.group(1)!);
      if (n != null && n >= 1 && n <= 10) _slots.add(n);
      return;
    }
    if (_busy) return; // readSlot đang chờ: {Green}/{CalibError} là của nó
    final e = _reErr.firstMatch(line);
    if (e != null) {
      _errors.add(e.group(1)!.trim());
      return;
    }
    // CHỈ dạng `{Green: N}`: số trần có thể là log khác của firmware.
    final g = _reFleet.firstMatch(line);
    if (g != null) {
      final v = double.tryParse(g.group(1)!);
      if (v != null) _readings.add(v);
    }
  }

  /// Gửi một lệnh nhiều ký tự (`CalibStart,3`, `CalibLabel,300 nM - ong 1`…) kèm `\n`.
  /// Không chờ trả lời — máy in `{CalibMode…}`/`{CalibSlot…}` sẽ đi qua [slotChanges]/[modeOn].
  Future<void> sendCommand(String cmd) async {
    if (!isOpen) throw const CalibReadException('Cổng chưa mở hoặc đã mất kết nối.');
    final clean = cmd.replaceAll('\r', '').replaceAll('\n', '');
    try {
      await link.write(utf8.encode('$clean\n'));
    } on SerialLinkException catch (e) {
      throw CalibReadException('Ghi cổng lỗi: $e');
    }
  }

  Future<void> startMode(int slot) => sendCommand('CalibStart,$slot');
  Future<void> setSlot(int slot) => sendCommand('CalibSlot,$slot');
  /// Nhãn hiện trên LCD. Máy đã thoát chế độ (nút TRẮNG) thì chỉ ghi nhớ — gửi là máy trả
  /// `notInMode`; [readSlot] gửi lại nhãn này khi vào lại.
  Future<void> setLabel(String label) async {
    _label = calibAsciiLabel(label);
    if (_modeSeen && !_modeOn) return;
    await sendCommand('CalibLabel,$_label');
  }
  Future<void> endMode() => sendCommand('CalibEnd');

  void _end() {
    _closed = true;
    if (!_lines.isClosed) _lines.close();
  }

  /// Gửi lệnh đọc khe [slot], trả số raw. Ném [CalibReadException] khi cổng đóng,
  /// đang bận, hết giờ, hoặc máy báo lỗi cảm biến.
  ///
  /// Máy ở chế độ ống chuẩn ([modeOn]) → `CalibShot` + chỉ nhận `{Green: N}`; máy khác → 1 byte
  /// `'0'..'9'` + nhận cả `raw,calibrated`/số trần (xem chú thích đầu file).
  Future<double> readSlot(int slot) async {
    if (!isOpen) throw const CalibReadException('Cổng chưa mở hoặc đã mất kết nối.');
    if (_busy) throw const CalibReadException('Đang đọc dở — chờ máy trả lời rồi bấm lại.');
    _busy = true;
    // Firmware có chế độ → luôn đi đường CalibShot (kể cả khi máy vừa thoát: vào lại trước).
    final strict = _modeOn || _modeSeen;
    try {
      _buf = ''; // bỏ rác còn dở từ trước — kết quả phải là dòng in SAU lệnh này
      final done = Completer<double>();
      final sub = _lines.stream.listen((line) {
        if (done.isCompleted) return;
        if (line.toLowerCase().contains('opto sensor error')) {
          done.completeError(const CalibReadException('Máy báo lỗi cảm biến quang — tắt/bật lại máy.'));
          return;
        }
        final err = _reErr.firstMatch(line);
        if (err != null) {
          done.completeError(CalibReadException('Máy từ chối đọc: ${err.group(1)!.trim()}'));
          return;
        }
        final v = strict ? _fleetOnly(line) : parseCalibRaw(line);
        if (v != null) done.complete(v);
      }, onDone: () {
        if (!done.isCompleted) done.completeError(const CalibReadException('Mất kết nối cổng.'));
      });
      try {
        if (strict) {
          final cmd = StringBuffer();
          if (!_modeOn) {
            // Nút TRẮNG trên máy đã thoát chế độ: vào lại + nhãn + đọc trong MỘT gói.
            cmd.write('CalibStart,$slot\n');
            if (_label.isNotEmpty) cmd.write('CalibLabel,$_label\n');
          }
          cmd.write('CalibShot\n');
          await link.write(utf8.encode(cmd.toString()));
        } else {
          await link.write(calibSlotCommand(slot));
        }
        return await done.future.timeout(timeout, onTimeout: () {
          throw CalibReadException(
              'Máy không trả số sau ${timeout.inSeconds} s. Máy phải ở màn hình chính (không '
              'đang chạy), đúng cổng COM và baud 115200.'
              '${_log.isEmpty ? '' : ' Máy in: ${_log.last}'}');
        });
      } finally {
        await sub.cancel();
      }
    } on SerialLinkException catch (e) {
      throw CalibReadException('Ghi cổng lỗi: $e');
    } finally {
      _busy = false;
    }
  }

  Future<void> close() async {
    _end();
    for (final c in [_readings, _slots, _errors, _modes]) {
      if (!c.isClosed) await c.close();
    }
    await _sub?.cancel();
    try {
      await link.close();
    } catch (_) {}
  }
}

/// Thứ tự đọc ống theo bàn giao: hết ống 1..N của nồng độ đầu rồi sang nồng độ kế.
/// Trả vị trí kế tiếp của ([conc], [tube]) trong lưới, `null` khi đã hết.
({String conc, int tube})? nextCalibCell(List<String> concs, int tubesPerConc, String conc, int tube) {
  final ci = concs.indexOf(conc);
  if (ci < 0) return concs.isEmpty ? null : (conc: concs.first, tube: 1);
  if (tube < tubesPerConc) return (conc: conc, tube: tube + 1);
  if (ci + 1 < concs.length) return (conc: concs[ci + 1], tube: 1);
  return null;
}
