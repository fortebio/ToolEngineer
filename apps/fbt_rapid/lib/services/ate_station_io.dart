/// Bản **DESKTOP** của [AteStation] (Windows): esptool qua `Process.start` +
/// cổng COM qua `util/serial_link.dart`.
///
/// Cùng tên lớp/API với bản web (`ate_station_web.dart`) — màn Chạy trạm import
/// facade `ate_station.dart` nên chạy được cả hai nền tảng mà không phải viết
/// hai màn hình (bài học từ tab Kỹ Thuật: hai bản song song là hai chỗ phải sửa
/// mỗi lần, và trên thực tế chúng đã lệch nhau).
///
/// Hai luật giữ nguyên từ tab Kỹ Thuật:
/// - **Cổng COM là tài nguyên độc quyền**: esptool cần cổng để nạp, nên
///   [serialCapture] luôn đóng cổng trước khi trả về.
/// - **DTR/RTS = off** khi đọc UART (mạch auto-reset ESP32) — `serial_link_io`
///   đã ghim sẵn.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import '../util/serial_link.dart';
import '../util/serial_ports.dart';
import 'ate_runner.dart';

/// Trạm chạy được trên nền tảng này không (web còn phải có Web Serial).
bool get ateStationAvailable => true;

/// Firmware lấy từ FILE trên máy (web thì lấy từ kho OTA của server).
bool get ateUsesServerFirmware => false;

/// Desktop liệt kê được cổng COM → màn hình hiện ô chọn, không cần xin quyền.
bool get ateCanListPorts => true;

/// Chỉ cổng USB-serial (bỏ COM1 native / Bluetooth) — cùng bộ lọc tab Kỹ Thuật.
Future<List<String>> ateListPorts() async => usableSerialPorts();

/// Desktop không phải xin quyền cổng (đó là chuyện của trình duyệt).
Future<String?> ateRequestPort() async => null;

class AteStationImpl implements AteStation {
  /// Cổng COM của máy đang test (vd `COM7`).
  final String port;
  final int baud;

  /// Đường dẫn esptool. Rỗng → tự dò lúc khởi tạo.
  final String esptoolPath;

  /// Không dùng ở bản desktop (firmware là file trên máy). Có mặt để cùng chữ
  /// ký với bản web — bên đó firmware tải từ kho OTA của server.
  final Future<List<int>> Function(String name)? fetchBin;

  AteStationImpl({
    required this.port,
    this.baud = 115200,
    String? esptoolPath,
    this.fetchBin,
  }) : esptoolPath = (esptoolPath == null || esptoolPath.isEmpty)
            ? resolveEsptool()
            : esptoolPath;

  Process? _proc;

  /// `esptool.exe` đi kèm app (installer đặt cạnh `fbt_dxd_app.exe`), không có
  /// thì gọi `esptool` trên PATH. Sao y `flasher_screen.dart::_resolveEsptool`.
  static String resolveEsptool() {
    try {
      final dir = File(Platform.resolvedExecutable).parent.path;
      final cand = '$dir${Platform.pathSeparator}esptool.exe';
      if (File(cand).existsSync()) return cand;
    } catch (_) {}
    return 'esptool';
  }

  @override
  void cancel() {
    try {
      _proc?.kill();
    } catch (_) {}
    _proc = null;
  }

  // ------------------------------------------------------------- esptool

  static final RegExp _macRe = RegExp(r'MAC:\s*([0-9A-Fa-f:]{17})');
  static final RegExp _chipRe = RegExp(r'Chip is\s+([^\r\n(]+)');
  static final RegExp _sizeRe =
      RegExp(r'flash size:\s*(\S+)', caseSensitive: false);

  List<String> _chipArgs(String chip) =>
      chip == 'auto' ? const [] : ['--chip', chip];

  @override
  Future<AteChipInfo> chipInfo({void Function(String)? onLog}) async {
    final r = await _esptool(['flash_id'], onLog: onLog);
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
    final log = StringBuffer();
    final chipArgs = _chipArgs(req.chip);

    if (req.erase) {
      final er = await _esptool([...chipArgs, 'erase_flash'], onLog: onLog);
      log.writeln(er.output);
      if (!er.ok) {
        return AteFlashResult(ok: false, output: log.toString(), stage: 'erase');
      }
    }

    final w = await _esptool([
      ...chipArgs,
      '--baud', '${req.baud}',
      'write_flash',
      if (req.flashMode != 'keep') ...['--flash_mode', req.flashMode],
      if (req.flashSize != 'keep') ...['--flash_size', req.flashSize],
      for (final p in req.parts) ...[p.offset, p.path],
    ], onLog: onLog);
    log.writeln(w.output);
    if (!w.ok) {
      return AteFlashResult(ok: false, output: log.toString(), stage: 'write');
    }

    if (req.verify && req.appBin.isNotEmpty) {
      final v = await _esptool(
          [...chipArgs, 'verify_flash', req.appOffset, req.appBin],
          onLog: onLog);
      log.writeln(v.output);
      if (!v.ok) {
        return AteFlashResult(
            ok: false, output: log.toString(), stage: 'verify');
      }
    }
    return AteFlashResult(
        ok: true,
        output: log.toString(),
        // Desktop verify bằng `esptool verify_flash`; không bật verify thì cũng
        // không nhận là đã verify.
        verified: req.verify && req.appBin.isNotEmpty);
  }

  /// Chạy esptool một lần. KHÔNG ném: lỗi khởi chạy trả exit code khác 0 kèm
  /// lời giải thích — runner coi đó là FAIL của bước và vẫn ghi hồ sơ.
  Future<AteToolResult> _esptool(List<String> args,
      {void Function(String)? onLog}) async {
    // `--port` đứng đầu: tuỳ chọn chung phải nằm TRƯỚC lệnh con của esptool.
    final full = <String>['--port', port, ...args];
    final buf = StringBuffer();
    void add(String s) {
      buf.write(s);
      onLog?.call(s);
    }

    add('\$ $esptoolPath ${full.join(' ')}\n');
    StreamSubscription<String>? outSub;
    StreamSubscription<String>? errSub;
    try {
      final p = await Process.start(esptoolPath, full, runInShell: true);
      _proc = p;
      outSub =
          p.stdout.transform(const Utf8Decoder(allowMalformed: true)).listen(add);
      errSub =
          p.stderr.transform(const Utf8Decoder(allowMalformed: true)).listen(add);
      final code = await p.exitCode;
      // Chờ stream xả hết: exitCode về TRƯỚC khi mọi byte stdout tới nơi, mà
      // FW-02 parse chính output đó (MAC, dung lượng flash).
      await outSub.asFuture<void>().catchError((_) {});
      await errSub.asFuture<void>().catchError((_) {});
      return AteToolResult(code, buf.toString());
    } catch (e) {
      add('\n[Lỗi] Không chạy được esptool: $e\n'
          '→ Thiếu esptool.exe đi kèm app — cài lại bản đầy đủ (hoặc đặt '
          'esptool.exe cạnh fbt_dxd_app.exe).\n');
      return AteToolResult(-1, buf.toString());
    } finally {
      _proc = null;
      await outSub?.cancel();
      await errSub?.cancel();
    }
  }

  // -------------------------------------------------------------- UART

  @override
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  }) async {
    SerialLink? link;
    StreamSubscription<Uint8List>? sub;
    final done = Completer<void>();
    final buf = StringBuffer();
    Timer? timer;
    try {
      link = await openSerialLink(name: port, baud: baud);
      if (link == null) return '';
      sub = link.stream.listen(
        (bytes) {
          final s = const Utf8Decoder(allowMalformed: true).convert(bytes);
          buf.write(s);
          onLog?.call(s);
          if (until != null && !done.isCompleted && until(buf.toString())) {
            done.complete();
          }
        },
        onError: (_) {
          if (!done.isCompleted) done.complete(); // cáp rút → dừng sớm, giữ log
        },
        onDone: () {
          if (!done.isCompleted) done.complete();
        },
      );
      if (send != null && send.isNotEmpty) {
        await link.write(utf8.encode('$send\n'));
      }
      timer = Timer(window, () {
        if (!done.isCompleted) done.complete();
      });
      await done.future;
      return buf.toString();
    } finally {
      timer?.cancel();
      await sub?.cancel();
      await link?.close();
    }
  }

  @override
  Future<List<int>> readFile(String path) => File(path).readAsBytes();

  @override
  Future<String?> dutGet(String ip, String path,
      {Duration timeout = const Duration(seconds: 5)}) async {
    // KHÔNG ném: ở P1 máy thường chưa vào WiFi xưởng, và "chưa có mạng" phải là
    // một trạng thái bình thường mà bước tự lùi về đường UART, không phải FAIL.
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
