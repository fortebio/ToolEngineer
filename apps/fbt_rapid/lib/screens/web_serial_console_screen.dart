import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../util/platform_files_web.dart' show downloadBytes;
import '../util/web_serial.dart';

/// Bản WEB của màn **Đọc serial** (Web Serial API) — hành vi bám sát
/// `serial_console_screen.dart` desktop: đa cổng (mỗi cổng 1 tab/panel riêng),
/// raw text/HEX, ending gửi, cap buffer y hệt. Khác biệt do trình duyệt:
/// không liệt kê được cổng rảnh → nút "Mở cổng" bật hộp thoại chọn cổng của
/// browser; "Lưu log" = tải xuống Downloads.
class WebSerialConsoleScreen extends StatefulWidget {
  final bool active; // false (rời sub-tab) → đóng cổng nhưng GIỮ log
  const WebSerialConsoleScreen({super.key, this.active = true});

  @override
  State<WebSerialConsoleScreen> createState() => _WebSerialConsoleScreenState();
}

class _Session {
  WebSerialPort? port; // null = đã đóng (panel + log vẫn còn)
  final String label;
  final int baud;
  StreamSubscription<Uint8List>? sub;
  final List<int> rxBytes = []; // nguồn render lại khi đổi HEX (cap 100000)
  String rx = ''; // chuỗi hiển thị (cap 120000)
  String? error;
  final send = TextEditingController();
  final scroll = ScrollController();
  _Session(this.port, this.label, this.baud);
  bool get isOpen => port?.isOpen ?? false;
}

class _WebSerialConsoleScreenState extends State<WebSerialConsoleScreen> {
  static const _bauds = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
  static const _endings = {
    'None': '',
    'LF (\\n)': '\n',
    'CR (\\r)': '\r',
    'CRLF (\\r\\n)': '\r\n',
  };

  final List<_Session> _sessions = [];
  _Session? _active;
  int _baud = 115200;
  String _ending = 'LF (\\n)';
  bool _hex = false;
  bool _autoscroll = true;

  @override
  void didUpdateWidget(covariant WebSerialConsoleScreen old) {
    super.didUpdateWidget(old);
    // Rời sub-tab Kỹ Thuật khác → đóng MỌI cổng, giữ panel + log (như desktop).
    if (old.active && !widget.active) {
      for (final s in _sessions) {
        _teardown(s);
      }
    }
  }

  @override
  void dispose() {
    for (final s in _sessions) {
      _teardown(s);
      s.send.dispose();
      s.scroll.dispose();
    }
    super.dispose();
  }

  String _render(List<int> bytes) => _hex
      ? bytes.map((b) => '${b.toRadixString(16).padLeft(2, '0')} ').join()
      : utf8.decode(bytes, allowMalformed: true);

  void _appendRx(_Session s, Uint8List data) {
    s.rxBytes.addAll(data);
    if (s.rxBytes.length > 100000) {
      s.rxBytes.removeRange(0, s.rxBytes.length - 100000);
    }
    s.rx += _render(data);
    if (s.rx.length > 120000) s.rx = s.rx.substring(s.rx.length - 120000);
    setState(() {});
    if (_autoscroll && s.scroll.hasClients) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (s.scroll.hasClients) {
          s.scroll.jumpTo(s.scroll.position.maxScrollExtent);
        }
      });
    }
  }

  Future<void> _openPort() async {
    final port = await requestSerialPort();
    if (port == null) return; // user hủy
    final s = _Session(port, port.label, _baud);
    try {
      await port.open(baud: _baud); // 8N1 + DTR/RTS off (chống auto-reset)
      s.sub = port.readStream().listen(
        (d) => _appendRx(s, d),
        onError: (e) {
          s.error = '$e';
          _teardown(s);
          if (mounted) setState(() {});
        },
        cancelOnError: true,
      );
    } catch (e) {
      s.error = 'Mở cổng thất bại: $e';
      s.port = null;
    }
    setState(() {
      _sessions.add(s);
      _active = s;
    });
  }

  /// Đóng cổng nhưng GIỮ panel + log (khớp _teardown desktop).
  void _teardown(_Session s) {
    s.sub?.cancel();
    s.sub = null;
    final p = s.port;
    s.port = null;
    p?.close();
  }

  void _closeSession(_Session s) {
    _teardown(s);
    s.send.dispose();
    s.scroll.dispose();
    setState(() {
      _sessions.remove(s);
      if (_active == s) _active = _sessions.isEmpty ? null : _sessions.first;
    });
  }

  Future<void> _sendTo(_Session s) async {
    final text = s.send.text + (_endings[_ending] ?? '');
    if (text.isEmpty) return;
    try {
      await s.port?.write(Uint8List.fromList(utf8.encode(text)));
      s.send.clear(); // KHÔNG echo ra log (như desktop)
    } catch (e) {
      setState(() => s.error = 'Lỗi gửi: $e');
    }
  }

  String _stamp() {
    final n = DateTime.now();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${n.year}${p2(n.month)}${p2(n.day)}_${p2(n.hour)}${p2(n.minute)}${p2(n.second)}${n.millisecond.toString().padLeft(3, '0')}';
  }

  void _saveLog(_Session s) {
    if (s.rx.isEmpty) return;
    final safe = s.label.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F ]'), '_');
    final header = '# FBT_RAPID — log đọc serial\n'
        '# Cổng: ${s.label} @ ${s.baud}\n'
        '# Lưu lúc: ${DateTime.now()}\n'
        '# Chế độ hiển thị: ${_hex ? 'HEX' : 'text'}\n'
        '${'-' * 50}\n';
    downloadBytes('${safe}_${_stamp()}.txt', utf8.encode(header + s.rx));
    ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(tr('techweb.downloaded'))));
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
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
                SizedBox(
                  width: 130,
                  child: DropdownButtonFormField<int>(
                    initialValue: _baud,
                    decoration: const InputDecoration(labelText: 'Baud'),
                    items: [
                      for (final b in _bauds)
                        DropdownMenuItem(value: b, child: Text('$b')),
                    ],
                    onChanged: (v) => setState(() => _baud = v ?? 115200),
                  ),
                ),
                FilledButton.icon(
                  onPressed: _openPort,
                  icon: const Icon(Icons.add),
                  label: Text(tr('techweb.openPort')),
                ),
                SizedBox(
                  width: 140,
                  child: DropdownButtonFormField<String>(
                    initialValue: _ending,
                    decoration:
                        const InputDecoration(labelText: 'Xuống dòng'),
                    items: [
                      for (final k in _endings.keys)
                        DropdownMenuItem(value: k, child: Text(k)),
                    ],
                    onChanged: (v) => setState(() => _ending = v ?? 'LF (\\n)'),
                  ),
                ),
                FilterChip(
                  label: const Text('HEX'),
                  selected: _hex,
                  onSelected: (v) => setState(() {
                    _hex = v;
                    for (final s in _sessions) {
                      s.rx = _render(s.rxBytes); // render lại toàn bộ buffer
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
            if (_sessions.isNotEmpty) ...[
              const SizedBox(height: 8),
              SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    for (final s in _sessions)
                      Padding(
                        padding: const EdgeInsets.only(right: 8),
                        child: ChoiceChip(
                          avatar: Icon(
                            s.isOpen ? Icons.usb : Icons.usb_off,
                            size: 18,
                            color: s.isOpen
                                ? AppSemantic.of(context).success
                                : cs.error,
                          ),
                          label: Text('${s.label} @ ${s.baud}'),
                          selected: _active == s,
                          onSelected: (_) => setState(() => _active = s),
                        ),
                      ),
                  ],
                ),
              ),
            ],
            const SizedBox(height: 8),
            Expanded(
              child: _active == null
                  ? Center(
                      child: Text(
                        tr('techweb.serialHint'),
                        textAlign: TextAlign.center,
                        style: TextStyle(color: cs.onSurfaceVariant),
                      ),
                    )
                  : _panel(_active!, cs),
            ),
          ],
        ),
      ),
    );
  }

  Widget _panel(_Session s, ColorScheme cs) {
    return Container(
      decoration: BoxDecoration(
        border: Border.all(color: cs.outlineVariant),
        borderRadius: BorderRadius.circular(8),
      ),
      padding: const EdgeInsets.all(8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Icon(s.isOpen ? Icons.usb : Icons.usb_off,
                  size: 18,
                  color: s.isOpen
                      ? AppSemantic.of(context).success
                      : cs.error),
              const SizedBox(width: 6),
              Text('${s.label} @ ${s.baud}',
                  style: const TextStyle(fontWeight: FontWeight.bold)),
              if (!s.isOpen)
                Text(' (đã đóng)',
                    style: TextStyle(color: cs.onSurfaceVariant)),
              const Spacer(),
              OutlinedButton.icon(
                onPressed: s.rx.isEmpty ? null : () => _saveLog(s),
                icon: const Icon(Icons.save_alt, size: 18),
                label: Text(tr('techweb.saveLog')),
              ),
              IconButton(
                tooltip: 'Xóa',
                icon: const Icon(Icons.delete_outline),
                onPressed: () => setState(() {
                  s.rx = '';
                  s.rxBytes.clear();
                }),
              ),
              IconButton(
                tooltip: 'Đóng cổng',
                icon: const Icon(Icons.close),
                onPressed: () => _closeSession(s),
              ),
            ],
          ),
          if (s.error != null)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Text(s.error!,
                  style: TextStyle(color: cs.error, fontSize: 12)),
            ),
          Expanded(
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.all(8),
              color: cs.surfaceContainerHighest,
              child: SingleChildScrollView(
                controller: s.scroll,
                child: SelectableText(
                  s.rx.isEmpty ? '— chưa có dữ liệu —' : s.rx,
                  style: const TextStyle(
                      fontFamily: 'JetBrains Mono', fontSize: 12, height: 1.35),
                ),
              ),
            ),
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: s.send,
                  enabled: s.isOpen,
                  inputFormatters: [LengthLimitingTextInputFormatter(4000)],
                  decoration: const InputDecoration(
                      hintText: 'Lệnh gửi tới cổng này…'),
                  onSubmitted: (_) => _sendTo(s),
                ),
              ),
              const SizedBox(width: 8),
              FilledButton.icon(
                onPressed: s.isOpen ? () => _sendTo(s) : null,
                icon: const Icon(Icons.send, size: 18),
                label: const Text('Gửi'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
