import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter_libserialport/flutter_libserialport.dart';

import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../util/serial_ports.dart';

/// **Nạp code** (Kỹ Thuật): kiểm tra thông tin chip + nạp firmware/bootloader/
/// partition (.bin) cho ESP32 / ESP32-S3 / ESP32-C3 / ESP8266 qua **esptool**
/// (gọi `esptool.exe` bằng Process). Output stream trực tiếp; có nút Dừng.
///
/// esptool.exe ĐI KÈM app: installer + CMake (windows/vendor/esptool.exe) đặt nó
/// NẰM CẠNH `fbt_dxd_app.exe` → app TỰ dò đường dẫn (KHÔNG hiện ô chọn). Không thấy
/// file kèm → gọi `esptool` trên PATH (dự phòng cho máy dev chưa build kèm).
class FlasherScreen extends StatefulWidget {
  /// `false` khi đang ở sub-tab Kỹ Thuật khác → dừng **theo dõi serial** để nhả
  /// COM (KHÔNG ngắt tiến trình nạp đang chạy).
  final bool active;
  const FlasherScreen({super.key, this.active = true});

  @override
  State<FlasherScreen> createState() => _FlasherScreenState();
}

/// 1 phần flash: nhãn + offset (chỉnh được) + đường dẫn .bin (rỗng = bỏ qua).
class _BinEntry {
  final String label;
  final TextEditingController offset;
  String? path;
  _BinEntry(this.label, String offset)
      : offset = TextEditingController(text: offset);
}

class _FlasherScreenState extends State<FlasherScreen> {
  // value gửi cho --chip; 'auto' = để esptool tự nhận.
  static const _chips = {
    'auto': 'Tự nhận (auto)',
    'esp32': 'ESP32',
    'esp32s3': 'ESP32-S3',
    'esp32c3': 'ESP32-C3',
    'esp8266': 'ESP8266',
  };
  // Offset mặc định [bootloader, partition, app] theo chip.
  static const _defaults = {
    'auto': ['0x1000', '0x8000', '0x10000'],
    'esp32': ['0x1000', '0x8000', '0x10000'],
    'esp32s3': ['0x0', '0x8000', '0x10000'],
    'esp32c3': ['0x0', '0x8000', '0x10000'],
    // ESP8266 thường nạp 1 file firmware hợp nhất @0x0 → đặt ở slot "Firmware
    // (app)" (slot Bootloader/Partition để trống = bỏ qua).
    'esp8266': ['', '', '0x0'],
  };
  static const _bauds = [115200, 230400, 460800, 921600, 1500000];
  // Flash mode/size — SAI mode (vd nạp QIO lên board DIO) là nguyên nhân
  // "nạp xong boot loop" rất hay gặp. 'keep' = giữ theo header trong .bin.
  static const _flashModes = ['keep', 'dio', 'qio', 'dout', 'qout'];
  static const _flashSizes = ['keep', 'detect', '1MB', '2MB', '4MB', '8MB', '16MB'];
  static const _monBauds = [9600, 74880, 115200, 230400];

  final _scroll = ScrollController();
  final _log = StringBuffer();

  List<String> _ports = usableSerialPorts();
  String? _portName;
  String _chip = 'auto';
  int _baud = 460800;
  String _flashMode = 'keep';
  String _flashSize = 'keep';
  // esptool: tự dò `esptool.exe` cạnh file thực thi (đúng vị trí sau khi cài qua
  // installer); không thấy thì gọi 'esptool' trên PATH. KHÔNG còn ô nhập đường dẫn.
  String _esptoolPath = 'esptool';
  bool _esptoolBundled = false;

  // Theo dõi serial sau khi nạp → đọc log boot / lý do reset.
  int _monBaud = 115200;
  bool _monitorAfter = true;
  SerialPort? _monPort;
  StreamSubscription<Uint8List>? _monSub;
  bool get _monitoring => _monPort != null;

  late final List<_BinEntry> _bins = [
    _BinEntry('Bootloader', '0x1000'),
    _BinEntry('Partition', '0x8000'),
    _BinEntry('Firmware (app)', '0x10000'),
  ];

  Process? _proc;
  bool get _busy => _proc != null;

  @override
  void initState() {
    super.initState();
    if (_ports.isNotEmpty) _portName = _ports.first;
    _resolveEsptool();
  }

  @override
  void didUpdateWidget(FlasherScreen old) {
    super.didUpdateWidget(old);
    // Rời sang công cụ Kỹ Thuật khác → dừng theo dõi serial để nhả COM (không
    // đụng tới tiến trình nạp đang chạy).
    if (old.active && !widget.active && _monitoring) _stopMonitor();
  }

  @override
  void dispose() {
    _proc?.kill();
    _monSub?.cancel();
    _monPort?.dispose();
    _scroll.dispose();
    for (final b in _bins) {
      b.offset.dispose();
    }
    super.dispose();
  }

  /// Tự dò `esptool.exe` cạnh file thực thi của app (vị trí cài đặt). Có →
  /// dùng đường dẫn tuyệt đối đó; không → gọi 'esptool' trên PATH.
  void _resolveEsptool() {
    try {
      final dir = File(Platform.resolvedExecutable).parent.path;
      final cand = '$dir${Platform.pathSeparator}esptool.exe';
      if (File(cand).existsSync()) {
        _esptoolPath = cand;
        _esptoolBundled = true;
        return;
      }
    } catch (_) {}
    _esptoolPath = 'esptool';
    _esptoolBundled = false;
  }

  Future<void> _pickBin(_BinEntry b) async {
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'Firmware', extensions: ['bin'])
    ]);
    if (f == null) return;
    setState(() => b.path = f.path);
  }

  void _refreshPorts() => setState(() {
        _ports = usableSerialPorts();
        if (_portName == null || !_ports.contains(_portName)) {
          _portName = _ports.isNotEmpty ? _ports.first : null;
        }
      });

  void _applyChipDefaults(String chip) {
    final d = _defaults[chip]!;
    for (var i = 0; i < _bins.length && i < d.length; i++) {
      _bins[i].offset.text = d[i];
    }
  }

  void _appendLog(String s) {
    if (!mounted) return;
    setState(() => _log.write(s));
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scroll.hasClients) {
        _scroll.jumpTo(_scroll.position.maxScrollExtent);
      }
    });
  }

  String get _exe => _esptoolPath;

  List<String> _chipArgs() => _chip == 'auto' ? [] : ['--chip', _chip];

  // --- Theo dõi serial (đọc log boot để tìm lý do reset) ---
  Future<void> _startMonitor() async {
    if (_monitoring || _busy) return;
    final name = _portName;
    if (name == null) return;
    try {
      final p = SerialPort(name);
      if (!p.openReadWrite()) {
        p.dispose();
        _appendLog('\n[Theo dõi] Mở cổng $name thất bại (đang bận?).\n');
        return;
      }
      p.config = SerialPortConfig()
        ..baudRate = _monBaud
        ..bits = 8
        ..parity = SerialPortParity.none
        ..stopBits = 1
        ..setFlowControl(SerialPortFlowControl.none);
      final reader = SerialPortReader(p);
      _monSub = reader.stream.listen(
        (data) => _appendLog(utf8.decode(data, allowMalformed: true)),
        onError: (Object e) {
          _appendLog('\n[Theo dõi] Mất kết nối: $e\n');
          _stopMonitor();
        },
        cancelOnError: true,
      );
      setState(() => _monPort = p);
      _appendLog('\n[Theo dõi] Đang đọc serial @ $_monBaud — xem log boot/reset…\n');
    } catch (e) {
      _appendLog('\n[Theo dõi] Lỗi: $e\n');
    }
  }

  void _stopMonitor() {
    _monSub?.cancel();
    _monSub = null;
    final p = _monPort;
    _monPort = null;
    if (p != null) {
      try {
        if (p.isOpen) p.close();
      } catch (_) {}
      try {
        p.dispose();
      } catch (_) {}
    }
    if (mounted) setState(() {});
  }

  /// Chạy esptool với [args]; stream stdout/stderr vào log. Trả exit code.
  Future<void> _run(List<String> args) async {
    if (_busy) return;
    _stopMonitor(); // nhả cổng cho esptool
    if (_portName == null) {
      _appendLog('\n[Lỗi] Chưa chọn cổng COM.\n');
      return;
    }
    final full = [..._chipArgs(), '--port', _portName!, ...args];
    _appendLog('\n\$ $_exe ${full.join(' ')}\n');
    StreamSubscription<String>? outSub;
    StreamSubscription<String>? errSub;
    try {
      final p = await Process.start(_exe, full, runInShell: true);
      if (!mounted) {
        p.kill(); // màn đã huỷ trong lúc khởi động tiến trình
        return;
      }
      setState(() => _proc = p);
      outSub =
          p.stdout.transform(const Utf8Decoder(allowMalformed: true)).listen(_appendLog);
      errSub =
          p.stderr.transform(const Utf8Decoder(allowMalformed: true)).listen(_appendLog);
      final code = await p.exitCode;
      _appendLog('\n[Kết thúc] exit code = $code\n');
    } catch (e) {
      _appendLog('\n[Lỗi] Không chạy được esptool: $e\n'
          '→ Thiếu esptool.exe đi kèm app — cài lại bản đầy đủ '
          '(hoặc đặt esptool.exe cạnh fbt_dxd_app.exe).\n');
    } finally {
      // Huỷ stream để không gọi _appendLog sau khi tiến trình kết thúc/màn huỷ.
      await outSub?.cancel();
      await errSub?.cancel();
      if (mounted) {
        setState(() => _proc = null);
      } else {
        _proc = null;
      }
    }
  }

  void _readChipInfo() => _run(['flash_id']);

  void _eraseFlash() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Xóa toàn bộ flash?'),
        content: const Text('Sẽ xóa SẠCH firmware hiện có trên thiết bị. '
            'Cần nạp lại sau đó.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              style: FilledButton.styleFrom(
                  backgroundColor: Theme.of(c).colorScheme.error),
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Xóa')),
        ],
      ),
    );
    if (ok == true && mounted) _run(['erase_flash']);
  }

  Future<void> _writeFlash() async {
    final parts = <String>[];
    for (final b in _bins) {
      final path = b.path;
      final off = b.offset.text.trim();
      if (path == null || path.isEmpty) continue;
      if (off.isEmpty) {
        _appendLog('\n[Lỗi] "${b.label}" có file nhưng thiếu offset.\n');
        return;
      }
      parts.addAll([off, path]);
    }
    if (parts.isEmpty) {
      _appendLog('\n[Lỗi] Chưa chọn file .bin nào để nạp.\n');
      return;
    }
    final wf = <String>['write_flash'];
    if (_flashMode != 'keep') wf.addAll(['--flash_mode', _flashMode]);
    if (_flashSize != 'keep') wf.addAll(['--flash_size', _flashSize]);
    await _run(['--baud', '$_baud', ...wf, ...parts]);
    // Tự mở serial đọc log boot ngay sau nạp → thấy chip reset/boot thế nào.
    if (_monitorAfter && mounted) await _startMonitor();
  }

  @override
  Widget build(BuildContext context) {
    final c = Theme.of(context).colorScheme;
    // Nền trong suốt + KHÔNG appBar: màn này luôn là mục con của `AppTabScaffold`
    // trong tab Kỹ Thuật, vốn đã có tiêu đề + dải chọn mục. Một AppBar nữa ở đây
    // là tiêu đề thứ ba nói cùng một điều. Nút của nó chuyển xuống cạnh chính
    // thứ nó điều khiển (hàng chọn cổng).
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Padding(
        padding: const EdgeInsets.fromLTRB(4, 8, 4, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // --- Cổng + chip + baud ---
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                _box(160, DropdownButtonFormField<String>(
                  value: _portName,
                  isExpanded: true,
                  decoration: _dec('Cổng COM'),
                  items: [
                    for (final p in _ports)
                      DropdownMenuItem(value: p, child: Text(p)),
                  ],
                  onChanged: _busy ? null : (v) => setState(() => _portName = v),
                )),
                IconButton(
                  tooltip: 'Làm mới cổng',
                  onPressed: _busy ? null : _refreshPorts,
                  icon: const Icon(Icons.refresh),
                ),
                _box(170, DropdownButtonFormField<String>(
                  value: _chip,
                  isExpanded: true,
                  decoration: _dec('Loại chip'),
                  items: [
                    for (final e in _chips.entries)
                      DropdownMenuItem(value: e.key, child: Text(e.value)),
                  ],
                  onChanged: _busy
                      ? null
                      : (v) => setState(() {
                            _chip = v!;
                            _applyChipDefaults(v);
                          }),
                )),
                _box(150, DropdownButtonFormField<int>(
                  value: _baud,
                  isExpanded: true,
                  decoration: _dec('Baud nạp'),
                  items: [
                    for (final b in _bauds)
                      DropdownMenuItem(value: b, child: Text('$b')),
                  ],
                  onChanged: _busy ? null : (v) => setState(() => _baud = v!),
                )),
                _box(140, DropdownButtonFormField<String>(
                  value: _flashMode,
                  isExpanded: true,
                  decoration: _dec('Flash mode'),
                  items: [
                    for (final m in _flashModes)
                      DropdownMenuItem(value: m, child: Text(m)),
                  ],
                  onChanged: _busy ? null : (v) => setState(() => _flashMode = v!),
                )),
                _box(140, DropdownButtonFormField<String>(
                  value: _flashSize,
                  isExpanded: true,
                  decoration: _dec('Flash size'),
                  items: [
                    for (final s in _flashSizes)
                      DropdownMenuItem(value: s, child: Text(s)),
                  ],
                  onChanged: _busy ? null : (v) => setState(() => _flashSize = v!),
                )),
              ],
            ),
            const SizedBox(height: 8),
            // --- Trạng thái esptool (đường dẫn tự dò cạnh app — không cho sửa) ---
            Row(
              children: [
                Icon(
                  _esptoolBundled ? Icons.check_circle_outline
                      : Icons.info_outline,
                  size: 16,
                  color: _esptoolBundled ? AppSemantic.of(context).success : c.onSurfaceVariant,
                ),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    _esptoolBundled
                        ? 'esptool: đã tích hợp sẵn cùng app'
                        : 'esptool: dùng PATH hệ thống (chưa kèm trong app)',
                    style: TextStyle(fontSize: 12, color: c.onSurfaceVariant),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            // --- 3 file .bin + offset ---
            for (final b in _bins) ...[
              Row(
                children: [
                  // 200px: 140 làm NHÃN BỊ CẮT ("Bootloader @ of…"). Offset là
                  // số hex → mono cho dễ đọc/đối chiếu (sai offset = boot loop).
                  _box(200, TextField(
                    controller: b.offset,
                    enabled: !_busy,
                    style: const TextStyle(fontFamily: 'JetBrains Mono'),
                    decoration: _dec('${b.label} @ offset'),
                  )),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      b.path ?? '— chưa chọn file —',
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                          color: b.path == null ? c.onSurfaceVariant : null),
                    ),
                  ),
                  if (b.path != null)
                    IconButton(
                      tooltip: 'Bỏ file',
                      onPressed: _busy ? null : () => setState(() => b.path = null),
                      icon: const Icon(Icons.clear, size: 18),
                    ),
                  OutlinedButton(
                    onPressed: _busy ? null : () => _pickBin(b),
                    child: const Text('Chọn .bin'),
                  ),
                ],
              ),
              const SizedBox(height: 6),
            ],
            const SizedBox(height: 4),
            // --- Hành động ---
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                OutlinedButton.icon(
                  onPressed: _busy ? null : _readChipInfo,
                  icon: const Icon(Icons.info_outline),
                  label: const Text('Kiểm tra chip'),
                ),
                FilledButton.icon(
                  onPressed: _busy ? null : _writeFlash,
                  icon: const Icon(Icons.bolt),
                  label: const Text('Nạp'),
                ),
                // Thao tác PHÁ HUỶ → tô theo `error` để không lẫn với nút
                // thường ("Xóa log" bên cạnh chỉ xoá text, giữ trung tính).
                OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(
                    foregroundColor: c.error,
                    side: BorderSide(color: c.error.withValues(alpha: 0.5)),
                  ),
                  onPressed: _busy ? null : _eraseFlash,
                  icon: const Icon(Icons.delete_sweep_outlined),
                  label: const Text('Xóa flash'),
                ),
                if (_busy)
                  FilledButton.icon(
                    style: FilledButton.styleFrom(backgroundColor: c.error),
                    onPressed: () => _proc?.kill(),
                    icon: const Icon(Icons.stop),
                    label: const Text('Dừng'),
                  ),
                OutlinedButton.icon(
                  onPressed: () => setState(_log.clear),
                  icon: const Icon(Icons.delete_outline),
                  label: const Text('Xóa log'),
                ),
              ],
            ),
            const SizedBox(height: 6),
            // --- Theo dõi serial (đọc log boot để tìm lý do reset) ---
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                OutlinedButton.icon(
                  onPressed: _busy
                      ? null
                      : (_monitoring ? _stopMonitor : _startMonitor),
                  icon: Icon(_monitoring ? Icons.stop_circle_outlined
                      : Icons.monitor_heart_outlined),
                  label: Text(_monitoring ? 'Dừng theo dõi' : 'Theo dõi serial'),
                ),
                _box(140, DropdownButtonFormField<int>(
                  value: _monBaud,
                  isExpanded: true,
                  decoration: _dec('Baud theo dõi'),
                  items: [
                    for (final b in _monBauds)
                      DropdownMenuItem(value: b, child: Text('$b')),
                  ],
                  onChanged: _monitoring
                      ? null
                      : (v) => setState(() => _monBaud = v!),
                )),
                FilterChip(
                  label: const Text('Theo dõi sau khi nạp'),
                  selected: _monitorAfter,
                  onSelected: (v) => setState(() => _monitorAfter = v),
                ),
              ],
            ),
            const SizedBox(height: 8),
            // --- Output ---
            Expanded(
              child: Container(
                width: double.infinity,
                decoration: BoxDecoration(
                  // Log esptool: nền LUÔN tối như terminal (không theo theme).
                  color: AppSemantic.of(context).consoleBg,
                  borderRadius: BorderRadius.circular(AppRadius.base),
                ),
                padding: const EdgeInsets.all(10),
                child: SingleChildScrollView(
                  controller: _scroll,
                  child: SelectableText(
                    _log.isEmpty ? '— log esptool sẽ hiện ở đây —' : _log.toString(),
                    style: TextStyle(
                      fontFamily: 'JetBrains Mono',
                      fontSize: 12,
                      height: 1.35,
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

  Widget _box(double w, Widget child) => SizedBox(width: w, child: child);

  // Viền/bo lấy từ `inputDecorationTheme` (app_theme.dart) — không đè.
  InputDecoration _dec(String label) =>
      InputDecoration(labelText: label, isDense: true);
}
