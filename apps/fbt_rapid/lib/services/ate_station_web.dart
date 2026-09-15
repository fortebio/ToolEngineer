/// Bản **WEB** của [AteStation]: nạp bằng **esptool-js** (bundle `web/esptool.js`,
/// binding `util/esptool_js.dart`) và nói chuyện UART bằng **Web Serial API**
/// (`util/web_serial.dart`). Cùng tên lớp/API với bản desktop nên màn **Chạy
/// trạm** dùng chung, không phải viết màn thứ hai.
///
/// CHỈ chạy Chrome/Edge **desktop** (Web Serial). Ba khác biệt so với desktop mà
/// màn hình phải nói cho người dùng biết:
///
/// 1. **Firmware tải từ kho OTA của server**, không phải file trên máy: trình
///    duyệt không giữ được đường dẫn qua F5, mà bản đang chốt thì server luôn
///    giữ — đổi lại còn hết luôn rủi ro nạp nhầm file trong ổ đĩa. [AteBinPart.path]
///    ở đây là **tên file .bin trên server** ([fetchBin] tải về, nhớ lại theo tên).
/// 2. **Chưa verify lại nội dung flash**: esptool-js không có `verify_flash`.
///    [flash] trả `verified: false` → runner hạ FW-01 xuống `info`, hồ sơ ghi rõ
///    "CHƯA đối chiếu lại được". Không giả vờ đã verify.
/// 3. **`http://<ip>` tới máy bị chặn** khi app chạy HTTPS (mixed content) →
///    [dutGet] trả null và bước OPT-01 tự lùi về đường UART.
///
/// Cổng là **tài nguyên độc quyền**: esptool-js `Transport` và [WebSerialPort]
/// không được mở cùng lúc (bài học của `web_flasher_screen.dart`) — mọi hàm ở
/// đây tự đóng cổng trước khi trả về.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import '../util/esptool_js.dart';
import '../util/web_serial.dart';
import 'ate_runner.dart';

/// Nhãn cổng → object cổng thật. Màn hình chỉ cầm chuỗi nhãn (giống `COM7` bên
/// desktop), còn object JS giữ ở đây.
final Map<String, WebSerialPort> _ports = {};

String _labelFor(WebSerialPort p, int index) {
  final base = p.label;
  // Hai adapter cùng loại cho ra cùng một nhãn (`USB 10c4:ea60`) — thêm số thứ
  // tự để chọn đúng cái đang cắm máy nào.
  return index == 0 ? base : '$base #${index + 1}';
}

/// Chỉ chạy khi trình duyệt có Web Serial VÀ bundle esptool-js đã nạp — thiếu
/// một trong hai thì màn Chạy trạm không được bày ra (bấm vào chỉ để nhận lỗi
/// thì tệ hơn không bày).
bool get ateStationAvailable => webSerialSupported && esptoolJsLoaded;

/// Web lấy firmware từ **kho OTA của server** (trình duyệt không giữ được đường
/// dẫn file qua F5, mà bản đang chốt thì server luôn giữ).
bool get ateUsesServerFirmware => true;

/// Web KHÔNG liệt kê được mọi cổng, nhưng liệt kê được cổng ĐÃ cấp quyền —
/// đủ để trạm chỉ phải xin quyền một lần rồi chạy cả ca.
bool get ateCanListPorts => true;

Future<List<String>> ateListPorts() async {
  final list = await grantedSerialPorts();
  _ports.clear();
  final out = <String>[];
  for (var i = 0; i < list.length; i++) {
    final label = _labelFor(list[i], i);
    _ports[label] = list[i];
    out.add(label);
  }
  return out;
}

/// Mở hộp thoại cấp quyền của trình duyệt (phải gọi từ một cú bấm). Trả nhãn
/// cổng vừa được cấp, null nếu người dùng Hủy.
Future<String?> ateRequestPort() async {
  final p = await requestSerialPort();
  if (p == null) return null;
  final label = _labelFor(p, _ports.length);
  _ports[label] = p;
  return label;
}

class AteStationImpl implements AteStation {
  /// Nhãn cổng (lấy từ [ateListPorts] / [ateRequestPort]).
  final String port;
  final int baud;

  /// Không dùng trên web (giữ cho cùng chữ ký với bản desktop).
  final String esptoolPath;

  /// Tải một file .bin từ kho OTA của server theo TÊN. Bắt buộc trên web.
  final Future<List<int>> Function(String name)? fetchBin;

  AteStationImpl({
    required this.port,
    this.baud = 115200,
    this.esptoolPath = '',
    this.fetchBin,
  });

  /// Bytes đã tải, nhớ theo tên file: một lượt chạy đọc file app hai lần (tính
  /// sha256 rồi nạp) và cả ca thì nạp cùng bản cho hàng chục máy.
  static final Map<String, List<int>> _binCache = {};

  WebSerialPort? _openPort;
  EspToolSession? _session;

  WebSerialPort? get _port => _ports[port];

  @override
  void cancel() {
    // Không có tiến trình để giết; cắt đường truyền là cách dừng duy nhất.
    unawaited(_session?.disconnect().catchError((_) {}));
    unawaited(_openPort?.close().catchError((_) {}));
    _session = null;
    _openPort = null;
  }

  // ----------------------------------------------------------- esptool-js

  static final RegExp _macRe = RegExp(r'MAC:\s*([0-9A-Fa-f:]{17})');
  static final RegExp _chipRe = RegExp(r'Chip is\s+([^\r\n(]+)');
  static final RegExp _sizeRe =
      RegExp(r'flash size:\s*(\S+)', caseSensitive: false);

  /// Mở phiên esptool-js (cổng phải ĐANG ĐÓNG) và chạy [body]; luôn reset cứng
  /// + nhả cổng khi xong, kể cả khi lỗi — không nhả là mọi bước UART sau đó chết.
  Future<AteToolResult> _withLoader(
    Future<void> Function(EspToolSession s) body, {
    required int baud,
    void Function(String)? onLog,
  }) async {
    final buf = StringBuffer();
    void add(String s) {
      buf.write(s);
      onLog?.call(s);
    }

    if (!esptoolJsLoaded) {
      add('[Lỗi] Chưa nạp được esptool.js — thiếu thẻ <script src="esptool.js"> '
          'trong web/index.html.\n');
      return AteToolResult(-1, buf.toString());
    }
    final p = _port;
    if (p == null) {
      add('[Lỗi] Chưa cấp quyền cổng cho trình duyệt (bấm "Chọn cổng").\n');
      return AteToolResult(-1, buf.toString());
    }
    try {
      if (p.isOpen) await p.close(); // Transport tự mở cổng
      final s = await EspToolSession.connect(
          jsPort: p.js, baud: baud, onLog: add);
      _session = s;
      add('Chip: ${s.chipName}\n');
      await body(s);
      await s.hardReset();
      return AteToolResult(0, buf.toString());
    } catch (e) {
      add('\n[Lỗi] $e\n');
      return AteToolResult(-1, buf.toString());
    } finally {
      try {
        await _session?.disconnect();
      } catch (_) {}
      _session = null;
    }
  }

  @override
  Future<AteChipInfo> chipInfo({void Function(String)? onLog}) async {
    // esptool-js in "Chip is …" lúc connect và "Detected flash size: …" khi
    // flashId() → parse cùng bộ regex với bản desktop.
    final r = await _withLoader((s) => s.flashId(),
        baud: 115200, onLog: onLog);
    if (!r.ok) return AteChipInfo(ok: false, raw: r.output);
    return AteChipInfo(
      ok: true,
      chip: _chipRe.firstMatch(r.output)?.group(1)?.trim() ?? '',
      mac: _macRe.firstMatch(r.output)?.group(1) ?? '',
      flashSize: _sizeRe.firstMatch(r.output)?.group(1) ?? '',
      raw: r.output,
    );
  }

  @override
  Future<AteFlashResult> flash(AteFlashRequest req,
      {void Function(String)? onLog}) async {
    // Tải firmware TRƯỚC khi mở cổng: mạng chậm mà giữ cổng mở thì cổng treo vô
    // ích, và lỗi tải phải nói rõ là lỗi mạng chứ không phải lỗi nạp.
    final files = <({int address, Uint8List data})>[];
    final log = StringBuffer();
    for (final part in req.parts) {
      try {
        final bytes = await readFile(part.path);
        files.add((
          address: _offset(part.offset),
          data: Uint8List.fromList(bytes),
        ));
        log.writeln('Đã tải ${part.path} (${bytes.length} byte) → ${part.offset}');
      } catch (e) {
        log.writeln('[Lỗi] Không tải được ${part.path} từ kho firmware: $e');
        return AteFlashResult(
            ok: false, output: log.toString(), stage: 'write');
      }
    }
    onLog?.call(log.toString());

    final r = await _withLoader(
      (s) async {
        if (req.erase) await s.eraseFlash();
        await s.writeFlash(
          files,
          flashMode: req.flashMode,
          flashSize: req.flashSize,
          onProgress: (i, written, total) {
            if (total > 0 && written % 65536 < 4096) {
              onLog?.call('Nạp phần ${i + 1}: ${(written * 100 / total).round()}%\n');
            }
          },
        );
      },
      baud: req.baud,
      onLog: (s) {
        log.write(s);
        onLog?.call(s);
      },
    );
    return AteFlashResult(
      ok: r.ok,
      output: log.toString(),
      stage: r.ok ? '' : 'write',
      // esptool-js chưa có verify_flash → NÓI THẲNG là chưa đối chiếu, đừng để
      // hồ sơ ghi "verify khớp" cho một thứ không ai kiểm.
      verified: false,
    );
  }

  /// '0x10000' / '65536' → số. Sai định dạng thì coi như 0 và để esptool-js báo.
  static int _offset(String s) =>
      int.tryParse(s.trim().replaceFirst(RegExp('^0x', caseSensitive: false), ''),
          radix: s.trim().toLowerCase().startsWith('0x') ? 16 : 10) ??
      0;

  // ------------------------------------------------------------ Web Serial

  @override
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  }) async {
    final p = _port;
    if (p == null) return '';
    final done = Completer<void>();
    final buf = StringBuffer();
    StreamSubscription<Uint8List>? sub;
    Timer? timer;
    try {
      // pinSignalsOff: DTR/RTS = off, nếu không Chrome bật hai chân đó và ESP32
      // tự reset ngay khi mở cổng (gotcha đã ghim ở web_serial.dart).
      await p.open(baud: baud, pinSignalsOff: true);
      _openPort = p;
      sub = p.readStream().listen(
        (bytes) {
          final s = const Utf8Decoder(allowMalformed: true).convert(bytes);
          buf.write(s);
          onLog?.call(s);
          if (until != null && !done.isCompleted && until(buf.toString())) {
            done.complete();
          }
        },
        onError: (_) {
          if (!done.isCompleted) done.complete();
        },
        onDone: () {
          if (!done.isCompleted) done.complete();
        },
      );
      if (send != null && send.isNotEmpty) {
        await p.write(Uint8List.fromList(utf8.encode('$send\n')));
      }
      timer = Timer(window, () {
        if (!done.isCompleted) done.complete();
      });
      await done.future;
      return buf.toString();
    } catch (e) {
      return buf.toString();
    } finally {
      timer?.cancel();
      await sub?.cancel();
      try {
        await p.close();
      } catch (_) {}
      _openPort = null;
    }
  }

  @override
  Future<List<int>> readFile(String path) async {
    final cached = _binCache[path];
    if (cached != null) return cached;
    final get = fetchBin;
    if (get == null) {
      throw StateError('Bản web phải lấy firmware từ kho OTA của server '
          '(chưa cấu hình nguồn firmware).');
    }
    final bytes = await get(path);
    _binCache[path] = bytes;
    return bytes;
  }

  @override
  Future<String?> dutGet(String ip, String path,
      {Duration timeout = const Duration(seconds: 5)}) async {
    // App web chạy HTTPS thì trình duyệt CHẶN gọi http:// (mixed content) —
    // trả null để bước OPT-01 lùi về đường UART, không phải FAIL oan.
    try {
      final uri =
          Uri.parse('http://$ip${path.startsWith('/') ? path : '/$path'}');
      final resp = await http.get(uri).timeout(timeout);
      return resp.statusCode == 200 ? utf8.decode(resp.bodyBytes) : null;
    } catch (_) {
      return null;
    }
  }
}
