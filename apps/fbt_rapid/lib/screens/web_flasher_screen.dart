import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import '../util/esptool_js.dart';
import '../util/i18n.dart';
import '../util/web_serial.dart';

/// Bản WEB của màn **Nạp code** — thay esptool.exe bằng **esptool-js** (bundle
/// chính thức của Espressif chạy ngay trong trình duyệt, cổng qua Web Serial).
/// UI/hành vi bám `flasher_screen.dart` desktop: 3 slot .bin + offset mặc định
/// theo chip, flash mode/size chỉ áp khi ≠ keep, xóa flash có xác nhận, theo
/// dõi serial sau nạp. Khác desktop: chọn cổng qua hộp thoại trình duyệt;
/// "Dừng" = ngắt kết nối cổng (không có tiến trình để kill).
class WebFlasherScreen extends StatefulWidget {
  final bool active; // false → dừng theo dõi serial (nhả cổng), không ngắt nạp
  const WebFlasherScreen({super.key, this.active = true});

  @override
  State<WebFlasherScreen> createState() => _WebFlasherScreenState();
}

class _BinEntry {
  final String label;
  final TextEditingController offset;
  String? fileName;
  Uint8List? bytes;
  _BinEntry(this.label, String def) : offset = TextEditingController(text: def);
}

class _WebFlasherScreenState extends State<WebFlasherScreen> {
  static const _chips = {
    'auto': 'Tự nhận (auto)',
    'esp32': 'ESP32',
    'esp32s3': 'ESP32-S3',
    'esp32c3': 'ESP32-C3',
    'esp8266': 'ESP8266',
  };
  // Offset mặc định theo chip (y hệt _defaults desktop). ESP8266: firmware
  // hợp nhất @0x0 ở slot app, 2 slot đầu trống = bỏ qua.
  static const _defaults = {
    'auto': ['0x1000', '0x8000', '0x10000'],
    'esp32': ['0x1000', '0x8000', '0x10000'],
    'esp32s3': ['0x0', '0x8000', '0x10000'],
    'esp32c3': ['0x0', '0x8000', '0x10000'],
    'esp8266': ['', '', '0x0'],
  };
  static const _bauds = [115200, 230400, 460800, 921600, 1500000];
  static const _flashModes = ['keep', 'dio', 'qio', 'dout', 'qout'];
  static const _flashSizes = ['keep', 'detect', '1MB', '2MB', '4MB', '8MB', '16MB'];
  static const _monBauds = [9600, 74880, 115200, 230400];

  WebSerialPort? _port;
  String _chip = 'auto';
  int _baud = 460800;
  String _flashMode = 'keep';
  String _flashSize = 'keep';
  int _monBaud = 115200;
  bool _monitorAfter = true;
  bool _monitoring = false;
  bool _busy = false;
  EspToolSession? _session; // phiên esptool đang chạy (để "Dừng" ngắt cổng)
  StreamSubscription<Uint8List>? _monSub;

  late final List<_BinEntry> _bins = [
    _BinEntry('Bootloader', '0x1000'),
    _BinEntry('Partition', '0x8000'),
    _BinEntry('Firmware (app)', '0x10000'),
  ];

  final StringBuffer _log = StringBuffer();
  final ScrollController _logScroll = ScrollController();

  @override
  void didUpdateWidget(covariant WebFlasherScreen old) {
    super.didUpdateWidget(old);
    // Rời sub-tab → dừng theo dõi (nhả cổng), KHÔNG đụng tiến trình nạp.
    if (old.active && !widget.active && _monitoring) _stopMonitor();
  }

  @override
  void dispose() {
    _monSub?.cancel();
    _port?.close();
    for (final b in _bins) {
      b.offset.dispose();
    }
    _logScroll.dispose();
    super.dispose();
  }

  void _appendLog(String s) {
    if (!mounted) return;
    setState(() => _log.write(s));
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_logScroll.hasClients) {
        _logScroll.jumpTo(_logScroll.position.maxScrollExtent);
      }
    });
  }

  Future<void> _pickPort() async {
    final p = await requestSerialPort();
    if (p == null) return;
    await _stopMonitor();
    await _port?.close();
    setState(() => _port = p);
    _appendLog('[Cổng] Đã chọn ${p.label}\n');
  }

  void _applyChipDefaults(String chip) {
    final d = _defaults[chip]!;
    for (var i = 0; i < _bins.length; i++) {
      _bins[i].offset.text = d[i];
    }
  }

  Future<void> _pickBin(_BinEntry b) async {
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'BIN', extensions: ['bin'])
    ]);
    if (f == null) return;
    final bytes = await f.readAsBytes();
    setState(() {
      b.fileName = f.name;
      b.bytes = bytes;
    });
  }

  int? _parseOffset(String s) {
    final t = s.trim().toLowerCase();
    if (t.isEmpty) return null;
    return t.startsWith('0x') ? int.tryParse(t.substring(2), radix: 16) : int.tryParse(t);
  }

  /// Khung chạy chung (tương đương _run desktop): dừng monitor nhả cổng →
  /// connect esptool-js (tự reset vào bootloader + detect chip) → [action] →
  /// hard reset → disconnect. LUÔN log chi tiết.
  Future<void> _run(String title, Future<void> Function(EspToolSession) action,
      {bool monitorAfter = false}) async {
    if (_busy) return;
    if (!esptoolJsLoaded) {
      _appendLog('[Lỗi] Chưa nạp được esptool.js (tải lại trang?).\n');
      return;
    }
    final port = _port;
    if (port == null) {
      _appendLog('[Lỗi] Chưa chọn cổng.\n');
      return;
    }
    await _stopMonitor(); // nhả cổng cho esptool
    await port.close(); // Transport tự mở — cổng phải đang đóng
    setState(() => _busy = true);
    _appendLog('\$ esptool-js $title (baud $_baud'
        '${_chip == 'auto' ? '' : ', chip $_chip'})\n');
    EspToolSession? s;
    try {
      s = await EspToolSession.connect(
          jsPort: port.js, baud: _baud, onLog: _appendLog);
      _session = s;
      _appendLog('[Chip] ${s.chipName}\n');
      await action(s);
      await s.hardReset();
      _appendLog('[Kết thúc] OK\n');
    } catch (e) {
      _appendLog('[Lỗi] $e\n');
    } finally {
      try {
        await s?.disconnect();
      } catch (_) {}
      _session = null;
      if (mounted) setState(() => _busy = false);
    }
    if (monitorAfter && _monitorAfter && mounted) await _startMonitor();
  }

  Future<void> _readChipInfo() =>
      _run('flash_id', (s) => s.flashId());

  Future<void> _eraseFlash() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Xóa toàn bộ flash?'),
        content: const Text(
            'Toàn bộ firmware + dữ liệu trên chip sẽ bị xóa sạch. Tiếp tục?'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              style: FilledButton.styleFrom(
                  backgroundColor: Theme.of(c).colorScheme.error),
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Xóa flash')),
        ],
      ),
    );
    if (ok != true) return;
    await _run('erase_flash', (s) => s.eraseFlash());
  }

  Future<void> _writeFlash() async {
    final files = <({int address, Uint8List data})>[];
    for (final b in _bins) {
      if (b.bytes == null) continue; // slot trống = bỏ qua (như desktop)
      final off = _parseOffset(b.offset.text);
      if (off == null) {
        _appendLog('[Lỗi] ${b.label}: offset trống/sai (${b.offset.text}).\n');
        return;
      }
      files.add((address: off, data: b.bytes!));
    }
    if (files.isEmpty) {
      _appendLog('[Lỗi] Chưa chọn file .bin nào.\n');
      return;
    }
    await _run('write_flash', (s) async {
      int lastPct = -1;
      await s.writeFlash(
        files,
        flashMode: _flashMode,
        flashSize: _flashSize,
        onProgress: (i, written, total) {
          final pct = total == 0 ? 0 : (written * 100 ~/ total);
          if (pct != lastPct) {
            lastPct = pct;
            _appendLog('[Nạp] file ${i + 1}/${files.length}: $pct%\n');
          }
        },
      );
    }, monitorAfter: true);
  }

  Future<void> _startMonitor() async {
    if (_monitoring || _busy) return;
    final port = _port;
    if (port == null) return;
    try {
      await port.open(baud: _monBaud);
      _monSub = port.readStream().listen(
        (d) => _appendLog(utf8.decode(d, allowMalformed: true)),
        onError: (e) {
          _appendLog('[Theo dõi] Mất kết nối: $e\n');
          _stopMonitor();
        },
        cancelOnError: true,
      );
      setState(() => _monitoring = true);
      _appendLog('[Theo dõi] Đang đọc serial @ $_monBaud…\n');
    } catch (e) {
      _appendLog('[Theo dõi] Mở cổng thất bại: $e\n');
      try {
        await port.close();
      } catch (_) {}
    }
  }

  Future<void> _stopMonitor() async {
    if (!_monitoring) return;
    _monSub?.cancel();
    _monSub = null;
    try {
      await _port?.close();
    } catch (_) {}
    if (mounted) setState(() => _monitoring = false);
  }

  /// "Dừng": không có tiến trình để kill — ngắt cổng để thao tác đang chạy
  /// văng lỗi và dừng lại.
  Future<void> _abort() async {
    _appendLog('[Dừng] Ngắt kết nối cổng…\n');
    try {
      await _session?.disconnect();
    } catch (_) {}
    try {
      await _port?.close();
    } catch (_) {}
  }

  Widget _box(double w, Widget child) => SizedBox(width: w, child: child);

  @override
  Widget build(BuildContext context) {
    final c = Theme.of(context).colorScheme;
    // Nền trong suốt + KHÔNG appBar — mục con của `AppTabScaffold` (tab Kỹ Thuật
    // bản web) đã có tiêu đề rồi. Giữ y bản desktop để hai bản không lệch nhau.
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Padding(
        padding: const EdgeInsets.fromLTRB(4, 8, 4, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                OutlinedButton.icon(
                  onPressed: _busy ? null : _pickPort,
                  icon: const Icon(Icons.usb, size: 18),
                  label: Text(_port == null
                      ? tr('techweb.pickPort')
                      : _port!.label),
                ),
                _box(
                  170,
                  DropdownButtonFormField<String>(
                    initialValue: _chip,
                    decoration: const InputDecoration(labelText: 'Loại chip'),
                    items: [
                      for (final e in _chips.entries)
                        DropdownMenuItem(value: e.key, child: Text(e.value)),
                    ],
                    onChanged: _busy
                        ? null
                        : (v) => setState(() {
                              _chip = v ?? 'auto';
                              _applyChipDefaults(_chip);
                            }),
                  ),
                ),
                _box(
                  150,
                  DropdownButtonFormField<int>(
                    initialValue: _baud,
                    decoration: const InputDecoration(labelText: 'Baud nạp'),
                    items: [
                      for (final b in _bauds)
                        DropdownMenuItem(value: b, child: Text('$b')),
                    ],
                    onChanged:
                        _busy ? null : (v) => setState(() => _baud = v ?? 460800),
                  ),
                ),
                _box(
                  140,
                  DropdownButtonFormField<String>(
                    initialValue: _flashMode,
                    decoration: const InputDecoration(labelText: 'Flash mode'),
                    items: [
                      for (final m in _flashModes)
                        DropdownMenuItem(value: m, child: Text(m)),
                    ],
                    onChanged: _busy
                        ? null
                        : (v) => setState(() => _flashMode = v ?? 'keep'),
                  ),
                ),
                _box(
                  140,
                  DropdownButtonFormField<String>(
                    initialValue: _flashSize,
                    decoration: const InputDecoration(labelText: 'Flash size'),
                    items: [
                      for (final m in _flashSizes)
                        DropdownMenuItem(value: m, child: Text(m)),
                    ],
                    onChanged: _busy
                        ? null
                        : (v) => setState(() => _flashSize = v ?? 'keep'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            for (final b in _bins)
              Padding(
                padding: const EdgeInsets.only(bottom: 6),
                child: Row(
                  children: [
                    // 200px: 140 làm NHÃN BỊ CẮT ("Bootloader @ of…"). Offset là
                    // số hex → mono (bám desktop `flasher_screen.dart`).
                    _box(
                      200,
                      TextField(
                        controller: b.offset,
                        enabled: !_busy,
                        style: const TextStyle(fontFamily: 'JetBrains Mono'),
                        decoration:
                            InputDecoration(labelText: '${b.label} @ offset'),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        b.fileName ?? '— chưa chọn file —',
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                            color: b.fileName == null
                                ? c.onSurfaceVariant
                                : null),
                      ),
                    ),
                    if (b.fileName != null)
                      IconButton(
                        tooltip: 'Bỏ file',
                        icon: const Icon(Icons.clear, size: 18),
                        onPressed: _busy
                            ? null
                            : () => setState(() {
                                  b.fileName = null;
                                  b.bytes = null;
                                }),
                      ),
                    OutlinedButton(
                      onPressed: _busy ? null : () => _pickBin(b),
                      child: const Text('Chọn .bin'),
                    ),
                  ],
                ),
              ),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                OutlinedButton.icon(
                  onPressed: _busy ? null : _readChipInfo,
                  icon: const Icon(Icons.search, size: 18),
                  label: const Text('Kiểm tra chip'),
                ),
                FilledButton.icon(
                  onPressed: _busy ? null : _writeFlash,
                  icon: const Icon(Icons.flash_on, size: 18),
                  label: const Text('Nạp'),
                ),
                // Thao tác PHÁ HUỶ → tô theo `error` (bám desktop).
                OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(
                    foregroundColor: c.error,
                    side: BorderSide(color: c.error.withValues(alpha: 0.5)),
                  ),
                  onPressed: _busy ? null : _eraseFlash,
                  icon: const Icon(Icons.delete_forever, size: 18),
                  label: const Text('Xóa flash'),
                ),
                if (_busy)
                  FilledButton.icon(
                    style: FilledButton.styleFrom(backgroundColor: c.error),
                    onPressed: _abort,
                    icon: const Icon(Icons.stop, size: 18),
                    label: const Text('Dừng'),
                  ),
                OutlinedButton.icon(
                  onPressed: () => setState(_log.clear),
                  icon: const Icon(Icons.clear_all, size: 18),
                  label: const Text('Xóa log'),
                ),
                OutlinedButton.icon(
                  onPressed: _busy
                      ? null
                      : (_monitoring ? _stopMonitor : _startMonitor),
                  icon: Icon(_monitoring ? Icons.pause : Icons.monitor_heart,
                      size: 18),
                  label:
                      Text(_monitoring ? 'Dừng theo dõi' : 'Theo dõi serial'),
                ),
                _box(
                  140,
                  DropdownButtonFormField<int>(
                    initialValue: _monBaud,
                    decoration:
                        const InputDecoration(labelText: 'Baud theo dõi'),
                    items: [
                      for (final b in _monBauds)
                        DropdownMenuItem(value: b, child: Text('$b')),
                    ],
                    onChanged: _monitoring
                        ? null
                        : (v) => setState(() => _monBaud = v ?? 115200),
                  ),
                ),
                FilterChip(
                  label: const Text('Theo dõi sau khi nạp'),
                  selected: _monitorAfter,
                  onSelected: (v) => setState(() => _monitorAfter = v),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Expanded(
              child: Container(
                width: double.infinity,
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(
                  // Nền LUÔN tối như terminal — giống hệt bản desktop.
                  color: AppSemantic.of(context).consoleBg,
                  borderRadius: BorderRadius.circular(AppRadius.base),
                ),
                child: SingleChildScrollView(
                  controller: _logScroll,
                  child: SelectableText(
                    _log.isEmpty ? '— log esptool sẽ hiện ở đây —' : '$_log',
                    style: TextStyle(
                      fontFamily: 'JetBrains Mono',
                      fontSize: 12,
                      color: AppSemantic.of(context).consoleFg,
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
