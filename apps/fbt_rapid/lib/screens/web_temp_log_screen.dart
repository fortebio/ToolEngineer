import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../services/temp_types.dart';
import '../theme/app_theme.dart';
import '../util/chart_capture.dart';
import '../util/i18n.dart';
import '../util/platform_files_web.dart' show downloadBytes;
import '../util/web_serial.dart';
import '../widgets/temp_chart.dart';

/// Bản WEB của màn **Log nhiệt** (Web Serial API) — parse giao thức Y HỆT
/// `temperature_serial.dart` desktop: dòng TAB `TimeRT`/`TimeRB` (giá trị cột
/// 4/5/6) ghép thành mẫu 6 kênh, trục thời gian theo Stopwatch app, tự gửi
/// lệnh `TemperatureOutput\n` khi mở (kickstart tối đa 2 lần). Đồ thị tái dùng
/// nguyên `TempChart`. "Lưu" = tải log.json + log.csv + uart.txt + chart.png
/// xuống Downloads (web không có thư mục FBT_RAPID_templog).
class WebTempLogScreen extends StatefulWidget {
  const WebTempLogScreen({super.key});

  @override
  State<WebTempLogScreen> createState() => _WebTempLogScreenState();
}

/// Đọc nhiệt từ 1 cổng Web Serial — port giữ lại được nên tự kết nối lại 2s/lần
/// khi lỗi stream (tương đương reconnect desktop).
class _WebTempReader {
  final WebSerialPort port;
  final String label;
  final void Function() onChanged;
  _WebTempReader(this.port, this.label, this.onChanged);

  static const int maxSamples = 500000;
  static const int maxRawLines = 2000000;
  static const int kBaudRate = 115200;

  final List<TempSample> samples = [];
  final List<String> rawLines = [];
  final List<double?> latest = List.filled(6, null);
  String? error;
  bool reconnecting = false;
  bool _stopped = false;

  StreamSubscription<Uint8List>? _sub;
  Timer? _kickTimer;
  Timer? _reconnectTimer;
  final Stopwatch _clock = Stopwatch();
  String _buf = '';
  List<double?>? _pendTop;
  int _pendTopAtMs = 0;
  bool _gotData = false;
  bool _outputOn = false;
  int _kickAttempts = 0;

  bool get isOpen => port.isOpen;

  Future<void> start() async {
    _stopped = false;
    _clock.start();
    await _connect();
  }

  Future<void> _connect() async {
    try {
      await port.open(baud: kBaudRate);
      _sub = port.readStream().listen(_onData, onError: (e) {
        error = '$e';
        _scheduleReconnect();
        onChanged();
      }, cancelOnError: true);
      reconnecting = false;
      error = null;
      // Bật xuất nhiệt (lệnh TOGGLE) + kickstart nếu 3s chưa có dữ liệu.
      await _writeCmd('TemperatureOutput\n');
      _outputOn = true;
      _gotData = false;
      _kickAttempts = 0;
      _kickTimer?.cancel();
      _kickTimer = Timer.periodic(const Duration(seconds: 3), (t) {
        if (_gotData || _kickAttempts >= 2) {
          t.cancel();
          return;
        }
        _kickAttempts++;
        _writeCmd('TemperatureOutput\n');
      });
      onChanged();
    } catch (e) {
      error = 'Mở cổng thất bại: $e';
      _scheduleReconnect();
      onChanged();
    }
  }

  void _scheduleReconnect() {
    if (_stopped) return;
    reconnecting = true;
    _sub?.cancel();
    _sub = null;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 2), () async {
      if (_stopped) return;
      try {
        await port.close();
      } catch (_) {}
      await _connect();
    });
  }

  Future<void> _writeCmd(String s) async {
    try {
      await port.write(Uint8List.fromList(s.codeUnits));
    } catch (_) {}
  }

  /// Nút "Gửi lệnh": đảo trạng thái xuất nhiệt (lệnh toggle của firmware).
  Future<void> sendToggle() async {
    _outputOn = !_outputOn;
    await _writeCmd('TemperatureOutput\n');
  }

  // Byte→char thô (String.fromCharCodes như desktop, KHÔNG decode UTF-8),
  // ghép buffer, cắt dòng theo \n.
  void _onData(Uint8List data) {
    _buf += String.fromCharCodes(data);
    var i = _buf.indexOf('\n');
    while (i >= 0) {
      final line = _buf.substring(0, i).trim();
      _buf = _buf.substring(i + 1);
      if (line.isNotEmpty) {
        rawLines.add(line);
        if (rawLines.length > maxRawLines) {
          rawLines.removeRange(0, maxRawLines ~/ 10);
        }
        _parseLine(line);
      }
      i = _buf.indexOf('\n');
    }
    if (_buf.length > 8192) _buf = _buf.substring(_buf.length - 2048);
    onChanged();
  }

  // Dòng TAB tối thiểu 7 cột; parts[1] phải là số; giá trị nhiệt ở cột 4/5/6.
  // TimeRT = top heater (giữ tạm <3s), TimeRB = bottom heater → chốt mẫu
  // [b0,b1,b2,t0,t1,t2]. Thời gian = Stopwatch app (millis máy reset về 0
  // khi rút cáp nên không dùng).
  void _parseLine(String line) {
    final parts = line.split('\t');
    if (parts.length < 7) return;
    if (double.tryParse(parts[1]) == null) return;
    _gotData = true;
    final a = double.tryParse(parts[4]);
    final b = double.tryParse(parts[5]);
    final c = double.tryParse(parts[6]);
    final nowMs = _clock.elapsedMilliseconds;
    final tag = parts[0];
    if (tag == 'TimeRT') {
      _pendTop = [a, b, c];
      _pendTopAtMs = nowMs;
      return;
    }
    if (tag != 'TimeRB') return;
    final top = (nowMs - _pendTopAtMs < 3000) ? _pendTop : null;
    final v = <double?>[a, b, c, ...(top ?? const [null, null, null])];
    samples.add(TempSample(nowMs / 1000.0, v));
    if (samples.length > maxSamples) {
      samples.removeRange(0, maxSamples ~/ 10);
    }
    for (var i = 0; i < 6; i++) {
      latest[i] = v[i] ?? latest[i];
    }
  }

  void clearData() {
    samples.clear();
    rawLines.clear();
    onChanged();
  }

  Future<void> stop() async {
    _stopped = true;
    _kickTimer?.cancel();
    _reconnectTimer?.cancel();
    if (_outputOn && isOpen) {
      await _writeCmd('TemperatureOutput\n'); // TẮT xuất nhiệt khi dừng chủ động
      _outputOn = false;
    }
    _sub?.cancel();
    _sub = null;
    await port.close();
  }

  String toCsv() {
    final b = StringBuffer('time_s,${kTempChannels.join(',')}\n');
    for (final s in samples) {
      b.write(s.t.toStringAsFixed(2));
      for (final v in s.v) {
        b.write(',');
        if (v != null) b.write(v.toStringAsFixed(2));
      }
      b.write('\n');
    }
    return b.toString();
  }
}

class _WebTempLogScreenState extends State<WebTempLogScreen> {
  final List<_WebTempReader> _readers = [];
  _WebTempReader? _active;
  final Set<int> _visibleCh = {0, 1, 2, 3, 4, 5};
  bool _showUart = false;
  final GlobalKey _chartKey = GlobalKey();
  final ScrollController _uartScroll = ScrollController();

  @override
  void dispose() {
    for (final r in _readers) {
      r.stop();
    }
    _uartScroll.dispose();
    super.dispose();
  }

  void _refresh() {
    if (!mounted) return;
    setState(() {});
    if (_showUart && _uartScroll.hasClients) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_uartScroll.hasClients) {
          _uartScroll.jumpTo(_uartScroll.position.maxScrollExtent);
        }
      });
    }
  }

  Future<void> _addPort() async {
    final port = await requestSerialPort();
    if (port == null) return;
    final r = _WebTempReader(port, port.label, _refresh);
    setState(() {
      _readers.add(r);
      _active = r;
    });
    await r.start();
  }

  Future<void> _stop(_WebTempReader r) async {
    await r.stop();
    setState(() {
      _readers.remove(r);
      if (_active == r) _active = _readers.isEmpty ? null : _readers.first;
    });
  }

  String _stamp() {
    final n = DateTime.now();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${n.year}${p2(n.month)}${p2(n.day)}_${p2(n.hour)}${p2(n.minute)}${p2(n.second)}${n.millisecond.toString().padLeft(3, '0')}';
  }

  /// "Lưu" = tải bundle như desktop (log.json + log.csv + uart.txt + chart.png)
  /// xuống Downloads, tên tiền tố `<cổng>_<timestamp>_...`.
  Future<void> _save(_WebTempReader r) async {
    if (r.samples.isEmpty) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text(tr('techweb.noData'))));
      return;
    }
    final safe = r.label.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F ]'), '_');
    final prefix = '${safe}_${_stamp()}';
    downloadBytes(
        '${prefix}_log.json',
        utf8.encode(jsonEncode({
          'port': r.label,
          'savedAt': DateTime.now().toIso8601String(),
          'channels': kTempChannels,
          'samples': [for (final s in r.samples) s.toJson()],
        })));
    downloadBytes('${prefix}_log.csv', utf8.encode(r.toCsv()));
    if (r.rawLines.isNotEmpty) {
      downloadBytes('${prefix}_uart.txt', utf8.encode(r.rawLines.join('\n')));
    }
    final png = await captureBoundaryPng(_chartKey);
    if (png != null) downloadBytes('${prefix}_chart.png', png);
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text(tr('techweb.downloaded'))));
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Scaffold(
      appBar: AppBar(
        title: Text(tr('tech.templog')),
        actions: [
          IconButton(
            tooltip: 'Dừng tất cả',
            icon: const Icon(Icons.stop_circle_outlined),
            onPressed: _readers.isEmpty
                ? null
                : () async {
                    for (final r in List.of(_readers)) {
                      await _stop(r);
                    }
                  },
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                FilledButton.icon(
                  onPressed: _addPort,
                  icon: const Icon(Icons.add),
                  label: Text(tr('techweb.openPort')),
                ),
                for (final r in _readers)
                  ChoiceChip(
                    avatar: Icon(
                      r.isOpen ? Icons.usb : Icons.usb_off,
                      size: 18,
                      color: r.isOpen ? AppSemantic.of(context).success : cs.error,
                    ),
                    label: Text('${r.label} · ${r.samples.length} mẫu'),
                    selected: _active == r,
                    onSelected: (_) => setState(() => _active = r),
                  ),
              ],
            ),
            const SizedBox(height: 8),
            Expanded(
              child: _active == null
                  ? Center(
                      child: Text(
                        tr('techweb.tempHint'),
                        textAlign: TextAlign.center,
                        style: TextStyle(color: cs.onSurfaceVariant),
                      ),
                    )
                  : _activeView(_active!, cs),
            ),
          ],
        ),
      ),
    );
  }

  Widget _activeView(_WebTempReader r, ColorScheme cs) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Icon(Icons.thermostat,
                size: 20,
                color: r.isOpen
                    ? AppSemantic.of(context).success
                    : (r.reconnecting ? AppSemantic.of(context).warning : cs.onSurfaceVariant)),
            const SizedBox(width: 6),
            Text(r.label, style: const TextStyle(fontWeight: FontWeight.bold)),
            const SizedBox(width: 12),
            Text('${r.samples.length} mẫu',
                style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12)),
            if (r.reconnecting) ...[
              const SizedBox(width: 8),
              const SizedBox(
                  width: 14,
                  height: 14,
                  child: CircularProgressIndicator(strokeWidth: 2)),
            ],
            if (r.error != null) ...[
              const SizedBox(width: 8),
              Expanded(
                child: Text(r.error!,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(color: cs.error, fontSize: 12)),
              ),
            ] else
              const Spacer(),
          ],
        ),
        const SizedBox(height: 4),
        TempChannelBar(
          visible: _visibleCh,
          latest: r.latest,
          onToggle: (i) => setState(() {
            if (!_visibleCh.remove(i)) _visibleCh.add(i);
          }),
        ),
        const SizedBox(height: 4),
        Expanded(
          flex: 3,
          child: RepaintBoundary(
            key: _chartKey,
            child: Container(
              color: Theme.of(context).scaffoldBackgroundColor,
              padding: const EdgeInsets.all(4),
              child: Column(
                children: [
                  const TempLegend(),
                  Expanded(
                    child: TempChart(
                        samples: r.samples, visibleChannels: _visibleCh),
                  ),
                ],
              ),
            ),
          ),
        ),
        if (_showUart) ...[
          const SizedBox(height: 8),
          Expanded(
            flex: 2,
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.all(8),
              color: cs.surfaceContainerHighest,
              child: SingleChildScrollView(
                controller: _uartScroll,
                child: SelectableText(
                  r.rawLines.isEmpty
                      ? '— chưa có dữ liệu UART —'
                      // chỉ render 500 dòng cuối cho nhẹ; dữ liệu đủ vẫn trong RAM
                      : r.rawLines
                          .sublist(
                              r.rawLines.length > 500
                                  ? r.rawLines.length - 500
                                  : 0)
                          .join('\n'),
                  style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 12),
                ),
              ),
            ),
          ),
        ],
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton.icon(
              onPressed: r.isOpen ? r.sendToggle : null,
              icon: const Icon(Icons.send, size: 18),
              label: const Text('Gửi lệnh'),
            ),
            OutlinedButton.icon(
              onPressed: () => setState(() => _showUart = !_showUart),
              icon: const Icon(Icons.terminal, size: 18),
              label: Text(_showUart ? 'Ẩn UART' : 'Hiển thị UART'),
            ),
            OutlinedButton.icon(
              onPressed: () => _save(r),
              icon: const Icon(Icons.save_alt, size: 18),
              label: Text(tr('techweb.saveLog')),
            ),
            OutlinedButton.icon(
              onPressed: r.clearData,
              icon: const Icon(Icons.delete_outline, size: 18),
              label: const Text('Xóa dữ liệu'),
            ),
            OutlinedButton.icon(
              onPressed: () => _stop(r),
              icon: const Icon(Icons.stop, size: 18),
              label: const Text('Dừng cổng'),
            ),
          ],
        ),
      ],
    );
  }
}
