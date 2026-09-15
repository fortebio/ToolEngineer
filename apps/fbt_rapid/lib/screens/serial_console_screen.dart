import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_libserialport/flutter_libserialport.dart';

import '../services/storage_paths.dart';
import '../theme/app_theme.dart';
import '../util/serial_ports.dart';

/// **Đọc serial** (Kỹ Thuật): console đọc/ghi **nhiều cổng COM** để nhân sự
/// kiểm tra/debug. Chọn cổng + baud → **Mở** (thêm 1 tab ở trên); mỗi cổng giữ
/// log RX + ô gửi riêng (đọc song song ngầm). Bấm tab để xem console **full màn**
/// của đúng cổng đó. Không parse — raw thuần (text hoặc HEX).
class SerialConsoleScreen extends StatefulWidget {
  /// `false` khi đang ở sub-tab Kỹ Thuật khác → tự **đóng cổng** (giữ log) để
  /// nhả phần cứng COM cho Log nhiệt / Nạp code (3 công cụ dùng chung COM).
  final bool active;
  const SerialConsoleScreen({super.key, this.active = true});

  @override
  State<SerialConsoleScreen> createState() => _SerialConsoleScreenState();
}

/// 1 phiên đọc của MỘT cổng COM (giữ port + stream + bộ đệm RX + ô gửi riêng).
class _PortSession {
  final String name;
  final int baud;
  SerialPort? port;
  StreamSubscription<Uint8List>? sub;
  String rx = ''; // bộ đệm HIỂN THỊ (đã render theo text/HEX)
  final List<int> rxBytes = <int>[]; // byte thô (nguồn để render lại khi đổi HEX)
  String? error;
  final TextEditingController send = TextEditingController();
  final ScrollController scroll = ScrollController();

  _PortSession(this.name, this.baud);

  bool get isOpen => port != null;

  void disposeAll() {
    sub?.cancel();
    final p = port;
    port = null;
    if (p != null) {
      try {
        if (p.isOpen) p.close();
      } catch (_) {}
      try {
        p.dispose();
      } catch (_) {}
    }
    send.dispose();
    scroll.dispose();
  }
}

class _SerialConsoleScreenState extends State<SerialConsoleScreen> {
  static const _bauds = [
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
  ];
  static const _endings = {
    'None': '',
    'LF (\\n)': '\n',
    'CR (\\r)': '\r',
    'CRLF (\\r\\n)': '\r\n',
  };

  List<String> _ports = usableSerialPorts();
  String? _pickPort;
  int _pickBaud = 115200;
  String _ending = 'LF (\\n)';
  bool _hex = false;
  bool _autoscroll = true;

  final List<_PortSession> _sessions = [];
  _PortSession? _active; // cổng đang xem console; null = chưa mở cổng nào

  @override
  void initState() {
    super.initState();
    _syncPick();
  }

  @override
  void didUpdateWidget(SerialConsoleScreen old) {
    super.didUpdateWidget(old);
    // Rời sang công cụ Kỹ Thuật khác → đóng mọi cổng đang mở (giữ nguyên log +
    // tab) để nhả COM cho Log nhiệt / Nạp code.
    if (old.active && !widget.active) {
      var changed = false;
      for (final s in _sessions) {
        if (s.isOpen) {
          _teardown(s);
          changed = true;
        }
      }
      if (changed && mounted) setState(() {});
    }
  }

  @override
  void dispose() {
    for (final s in _sessions) {
      s.disposeAll();
    }
    super.dispose();
  }

  // Cổng còn lại để chọn (bỏ những cổng đã mở).
  List<String> get _freePorts {
    final open = _sessions.map((s) => s.name).toSet();
    return [for (final p in _ports) if (!open.contains(p)) p];
  }

  void _syncPick() {
    final free = _freePorts;
    if (_pickPort == null || !free.contains(_pickPort)) {
      _pickPort = free.isNotEmpty ? free.first : null;
    }
  }

  // Giữ _active luôn trỏ vào 1 phiên còn trong _sessions (hoặc null nếu hết).
  void _syncActive() {
    if (!_sessions.contains(_active)) {
      _active = _sessions.isNotEmpty ? _sessions.first : null;
    }
  }

  void _refreshPorts() {
    setState(() {
      _ports = usableSerialPorts();
      _syncPick();
    });
  }

  // Render byte theo chế độ hiện tại (text/HEX). Dùng cho cả chunk mới lẫn render
  // lại cả buffer khi đổi HEX → tránh trộn text+HEX trong cùng bộ đệm.
  String _render(List<int> bytes) => _hex
      ? (bytes.isEmpty
          ? ''
          : '${bytes.map((b) => b.toRadixString(16).padLeft(2, '0')).join(' ')} ')
      : utf8.decode(bytes, allowMalformed: true);

  void _appendRx(_PortSession s, Uint8List data) {
    if (!mounted) return;
    s.rxBytes.addAll(data);
    if (s.rxBytes.length > 100000) {
      s.rxBytes.removeRange(0, s.rxBytes.length - 100000);
    }
    final chunk = _render(data);
    setState(() {
      s.rx += chunk;
      if (s.rx.length > 120000) s.rx = s.rx.substring(s.rx.length - 120000);
    });
    if (_autoscroll) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (s.scroll.hasClients) {
          s.scroll.jumpTo(s.scroll.position.maxScrollExtent);
        }
      });
    }
  }

  void _openPort() {
    final name = _pickPort;
    if (name == null) return;
    if (_sessions.any((s) => s.name == name)) return; // đã mở
    final s = _PortSession(name, _pickBaud);
    try {
      final p = SerialPort(name);
      if (!p.openReadWrite()) {
        p.dispose();
        s.error = 'Cổng $name đang bận / mở không được.';
        setState(() {
          _sessions.add(s);
          _active = s;
        });
        return;
      }
      p.config = SerialPortConfig()
        ..baudRate = s.baud
        ..bits = 8
        ..parity = SerialPortParity.none
        ..stopBits = 1
        ..setFlowControl(SerialPortFlowControl.none)
        // GIỮ DTR/RTS = off: 2 dòng này là mạch auto-reset của ESP32/Forte
        // (DTR→EN, RTS→GPIO0). Bỏ trống → driver Windows tự bật khi mở/ghi →
        // thiết bị tự reset lúc gửi. Ghim off để KHÔNG pulse chân reset.
        ..dtr = SerialPortDtr.off
        ..rts = SerialPortRts.off;
      final reader = SerialPortReader(p);
      s.port = p;
      s.sub = reader.stream.listen(
        (data) => _appendRx(s, data),
        onError: (Object e) {
          if (!mounted) return;
          setState(() {
            s.error = 'Mất kết nối: $e';
            _teardown(s);
          });
        },
        cancelOnError: true,
      );
      setState(() {
        _sessions.add(s);
        _active = s;
        _syncPick();
      });
    } catch (e) {
      s.error = 'Lỗi mở cổng: $e';
      setState(() => _sessions.add(s));
    }
  }

  // Đóng port của 1 phiên (giữ panel để còn xem log + lỗi).
  void _teardown(_PortSession s) {
    s.sub?.cancel();
    s.sub = null;
    final p = s.port;
    s.port = null;
    if (p != null) {
      try {
        if (p.isOpen) p.close();
      } catch (_) {}
      try {
        p.dispose();
      } catch (_) {}
    }
  }

  // Đóng hẳn 1 phiên + gỡ panel.
  void _closeSession(_PortSession s) {
    _teardown(s);
    s.send.dispose();
    s.scroll.dispose();
    setState(() {
      _sessions.remove(s);
      _syncActive();
      _syncPick();
    });
  }

  void _sendTo(_PortSession s) {
    final p = s.port;
    if (p == null) return;
    final text = s.send.text + (_endings[_ending] ?? '');
    if (text.isEmpty) return;
    try {
      p.write(Uint8List.fromList(utf8.encode(text)));
      s.send.clear();
    } catch (e) {
      setState(() => s.error = 'Lỗi gửi: $e');
    }
  }

  // ---- Lưu log ra file ----
  String _stamp(DateTime n) {
    String p2(int x) => x.toString().padLeft(2, '0');
    String p3(int x) => x.toString().padLeft(3, '0');
    // Kèm mili-giây để tên file luôn duy nhất (lưu 2 lần trong cùng giây không
    // ghi đè nhau) — giống TemperatureStore._stamp.
    return '${n.year}${p2(n.month)}${p2(n.day)}_'
        '${p2(n.hour)}${p2(n.minute)}${p2(n.second)}${p3(n.millisecond)}';
  }

  // Thay ký tự Windows cấm trong tên file.
  String _safe(String s) {
    final c = s.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F]'), '_').trim();
    return c.isEmpty ? 'COM' : c;
  }

  /// Ghi bộ đệm RX của 1 cổng ra `<gốc>\FBT_RAPID_seriallog\<COM>_<thời gian>.txt`.
  Future<void> _saveLog(_PortSession s) async {
    if (s.rx.isEmpty) {
      _toast('Cổng ${s.name}: chưa có dữ liệu để lưu.');
      return;
    }
    try {
      final dir = Directory('${StoragePaths.parent}\\FBT_RAPID_seriallog');
      dir.createSync(recursive: true);
      final now = DateTime.now();
      final file =
          File('${dir.path}\\${_safe(s.name)}_${_stamp(now)}.txt');
      final header = '# FBT_RAPID — log đọc serial\n'
          '# Cổng: ${s.name} @ ${s.baud}\n'
          '# Lưu lúc: $now\n'
          '# Chế độ hiển thị: ${_hex ? 'HEX' : 'text'}\n'
          '${'-' * 50}\n';
      await file.writeAsString(header + s.rx);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Đã lưu: ${file.path}'),
        action: SnackBarAction(
          label: 'Mở thư mục',
          onPressed: () {
            try {
              // 1 arg duy nhất: explorer cần "/select,<path>" liền, KHÔNG có
              // dấu cách sau dấu phẩy (2 arg sẽ mở nhầm thư mục mặc định).
              Process.run('explorer.exe', ['/select,${file.path}']);
            } catch (_) {}
          },
        ),
      ));
    } catch (e) {
      _toast('Lỗi lưu log: $e');
    }
  }

  void _toast(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
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
            // --- Hàng thêm cổng + tuỳ chọn chung ---
            Wrap(
              spacing: 8,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                SizedBox(
                  width: 170,
                  child: DropdownButtonFormField<String>(
                    value: _pickPort,
                    isExpanded: true,
                    decoration: const InputDecoration(
                        labelText: 'Cổng COM', isDense: true),
                    items: [
                      for (final p in _freePorts)
                        DropdownMenuItem(value: p, child: Text(p)),
                    ],
                    onChanged: (v) => setState(() => _pickPort = v),
                  ),
                ),
                IconButton(
                  tooltip: 'Làm mới cổng',
                  onPressed: _refreshPorts,
                  icon: const Icon(Icons.refresh),
                ),
                SizedBox(
                  width: 130,
                  child: DropdownButtonFormField<int>(
                    value: _pickBaud,
                    isExpanded: true,
                    decoration:
                        const InputDecoration(labelText: 'Baud', isDense: true),
                    items: [
                      for (final b in _bauds)
                        DropdownMenuItem(value: b, child: Text('$b')),
                    ],
                    onChanged: (v) => setState(() => _pickBaud = v!),
                  ),
                ),
                FilledButton.icon(
                  onPressed: _pickPort == null ? null : _openPort,
                  icon: const Icon(Icons.add),
                  label: const Text('Mở cổng'),
                ),
                SizedBox(
                  width: 130,
                  child: DropdownButtonFormField<String>(
                    value: _ending,
                    isExpanded: true,
                    decoration: const InputDecoration(
                        labelText: 'Xuống dòng', isDense: true),
                    items: [
                      for (final e in _endings.keys)
                        DropdownMenuItem(value: e, child: Text(e)),
                    ],
                    onChanged: (v) => setState(() => _ending = v!),
                  ),
                ),
                FilterChip(
                  label: const Text('HEX'),
                  selected: _hex,
                  onSelected: (v) => setState(() {
                    _hex = v;
                    // Render LẠI toàn bộ buffer mỗi cổng theo chế độ mới (không
                    // còn trộn text/HEX trong cùng log).
                    for (final s in _sessions) {
                      s.rx = _render(s.rxBytes);
                      if (s.rx.length > 120000) {
                        s.rx = s.rx.substring(s.rx.length - 120000);
                      }
                    }
                  }),
                ),
                FilterChip(
                  label: const Text('Tự cuộn'),
                  selected: _autoscroll,
                  onSelected: (v) => setState(() => _autoscroll = v),
                ),
              ],
            ),
            // --- Tab các cổng đang mở: bấm để xem console cổng đó ---
            if (_sessions.isNotEmpty) ...[
              const SizedBox(height: 10),
              SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    for (final s in _sessions)
                      Padding(
                        padding: const EdgeInsets.only(right: 8),
                        child: ChoiceChip(
                          selected: identical(s, _active),
                          onSelected: (_) => setState(() => _active = s),
                          avatar: Icon(
                            s.isOpen ? Icons.usb : Icons.usb_off,
                            size: 16,
                            color: s.isOpen ? AppSemantic.of(context).success : cs.error,
                          ),
                          label: Text('${s.name} @ ${s.baud}'),
                        ),
                      ),
                  ],
                ),
              ),
            ],
            const SizedBox(height: 10),
            // --- Console cổng đang chọn (chiếm hết không gian còn lại) ---
            Expanded(
              child: _active == null
                  ? Center(
                      child: Text(
                        'Chọn cổng + baud rồi bấm “Mở cổng”.\n'
                        'Mỗi cổng mở có 1 tab ở trên — bấm để xem console cổng đó.',
                        textAlign: TextAlign.center,
                        style: TextStyle(color: cs.onSurfaceVariant),
                      ),
                    )
                  : _panel(_active!),
            ),
          ],
        ),
      ),
    );
  }

  Widget _panel(_PortSession s) {
    final cs = Theme.of(context).colorScheme;
    return Container(
      decoration: BoxDecoration(
        border: Border.all(color: cs.outlineVariant),
        borderRadius: BorderRadius.circular(8),
      ),
      padding: const EdgeInsets.all(8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Header: tên cổng + trạng thái + đóng
          Row(
            children: [
              Icon(
                s.isOpen ? Icons.usb : Icons.usb_off,
                size: 18,
                color: s.isOpen ? AppSemantic.of(context).success : cs.error,
              ),
              const SizedBox(width: 6),
              Text('${s.name} @ ${s.baud}',
                  style: const TextStyle(fontWeight: FontWeight.bold)),
              const SizedBox(width: 8),
              if (!s.isOpen)
                Text('(đã đóng)', style: TextStyle(color: cs.error, fontSize: 12)),
              const Spacer(),
              OutlinedButton.icon(
                onPressed: s.rx.isEmpty ? null : () => _saveLog(s),
                icon: const Icon(Icons.save_alt, size: 16),
                label: const Text('Lưu log'),
              ),
              const SizedBox(width: 6),
              OutlinedButton.icon(
                onPressed: () => _clearRx(s),
                icon: const Icon(Icons.delete_outline, size: 16),
                label: const Text('Xóa'),
              ),
              const SizedBox(width: 6),
              IconButton(
                tooltip: 'Đóng cổng',
                onPressed: () => _closeSession(s),
                icon: const Icon(Icons.close),
              ),
            ],
          ),
          if (s.error != null)
            Padding(
              padding: const EdgeInsets.only(top: 2, bottom: 4),
              child: Text(s.error!,
                  style: TextStyle(color: cs.error, fontSize: 12)),
            ),
          // Khung RX — chiếm hết chiều cao còn lại; nền theo theme (tự sáng/tối).
          Expanded(
            child: Container(
              width: double.infinity,
              decoration: BoxDecoration(
                color: cs.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: cs.outlineVariant),
              ),
              padding: const EdgeInsets.all(8),
              child: SingleChildScrollView(
                controller: s.scroll,
                child: SelectableText(
                  s.rx.isEmpty ? '— chưa có dữ liệu —' : s.rx,
                  style: TextStyle(
                    fontFamily: 'JetBrains Mono',
                    fontSize: 12,
                    height: 1.35,
                    color: s.rx.isEmpty ? cs.onSurfaceVariant : cs.onSurface,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 6),
          // Gửi lệnh tới cổng này
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: s.send,
                  enabled: s.isOpen,
                  onSubmitted: (_) => _sendTo(s),
                  inputFormatters: [LengthLimitingTextInputFormatter(4000)],
                  decoration: const InputDecoration(
                    hintText: 'Lệnh gửi tới cổng này…',
                    isDense: true,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              FilledButton.icon(
                onPressed: s.isOpen ? () => _sendTo(s) : null,
                icon: const Icon(Icons.send),
                label: const Text('Gửi'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  void _clearRx(_PortSession s) => setState(() {
        s.rx = '';
        s.rxBytes.clear();
      });
}
