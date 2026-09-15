import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart';
import '../services/session_store.dart';
import '../services/storage_paths.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../util/log_triage.dart';
import '../util/platform_files.dart' as pf;
import '../util/serial_link.dart';
import 'raw_uart_screen.dart';

/// Mục **Xử lý sự cố** (tab Chăm sóc KH) — ba bước cho nhân viên CSKH:
///
/// 1. **Kết nối**: cắm cáp USB máy → chọn cổng (desktop) / hộp thoại trình
///    duyệt (web) → bấm Kết nối. Cố định 115200 8N1, DTR/RTS off (không reset máy).
/// 2. **Đọc log**: log UART hiện ngay; bộ quét `util/log_triage.dart` dịch các
///    dòng ESP32 khó hiểu thành "dấu hiệu" (nguồn yếu, crash, mất WiFi…) kèm
///    gợi ý. Có lệnh nhanh (đọc tham số / hiệu chuẩn / nhiệt / khởi động lại).
/// 3. **Gửi log về kỹ thuật**: nhập mã máy (tự điền nếu thấy trong log) + mô tả
///    → `PUT /devices/{id}/logs` Engineer Server. Không mạng thì "Lưu file".
///
/// Một màn cho cả desktop lẫn web nhờ `util/serial_link.dart` (facade). Cổng COM
/// dùng CHUNG với tab Kỹ Thuật: rời tab/mục là tự ngắt (giữ log) — "đang được xem"
/// đọc từ `TickerMode` (HomeShell + AppTabScaffold cùng bọc), không luồn cờ riêng.
class SupportTroubleshootScreen extends StatefulWidget {
  final AppSettings settings;
  const SupportTroubleshootScreen({super.key, required this.settings});

  @override
  State<SupportTroubleshootScreen> createState() =>
      _SupportTroubleshootScreenState();
}

class _SupportTroubleshootScreenState extends State<SupportTroubleshootScreen> {
  static const int _baud = 115200; // chuẩn máy Forte Rapid+ (cùng Log nhiệt)
  static const int _maxLines = 5000; // ~ vài trăm KB, đủ mấy lần boot

  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  // --- kết nối ---
  List<String> _ports = const [];
  String? _pick;
  SerialLink? _link;
  StreamSubscription<Uint8List>? _sub;
  bool _connecting = false;
  String? _error;

  // --- log --- (giữ theo DÒNG, không phải một chuỗi lớn: đánh dấu "nghi lỗi"
  // một lần lúc dòng tới, thay vì chạy 12 regex trên cả buffer mỗi frame)
  final List<String> _lines = [];
  final List<bool> _flag = []; // song song _lines: dòng nghi lỗi?
  String _partial = ''; // phần cuối chưa có xuống dòng
  TriageReport _report = TriageReport.empty;
  Timer? _triageTimer;
  /// Gộp vẽ: boot log tới theo mẩu ~30 byte, vài chục mẩu/giây — setState mỗi mẩu là
  /// rebuild cả màn (và dựng lại `idx` 5000 phần tử) từng ấy lần. Vẽ tối đa ~12 lần/giây.
  Timer? _paintTimer;
  bool _onlyIssues = false;
  bool _autoscroll = true;
  final ScrollController _scroll = ScrollController();

  // --- gửi ---
  final TextEditingController _idCtl = TextEditingController();
  final TextEditingController _noteCtl = TextEditingController();
  bool _sending = false;
  String? _lastSent;

  @override
  void initState() {
    super.initState();
    if (serialLinkCanListPorts) _refreshPorts();
  }

  /// Mục này có đang được xem không (tab cấp trên VÀ mục con đều đang chọn).
  /// `TickerMode` lồng nhau: cha tắt là con đọc ra tắt — đúng cả hai tầng.
  bool _visible = true;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final v = TickerMode.valuesOf(context).enabled;
    if (v == _visible) return;
    _visible = v;
    // Rời mục/tab → nhả cổng (GIỮ log) — cùng luật với "Đọc serial" ở tab Kỹ
    // Thuật: bốn công cụ dùng chung phần cứng COM. Hoãn một microtask vì
    // didChangeDependencies chạy trong pha build (_disconnect gọi setState).
    if (!v && _link != null) Future.microtask(_disconnect);
  }

  @override
  void dispose() {
    _triageTimer?.cancel();
    _paintTimer?.cancel();
    _sub?.cancel();
    _link?.close();
    _scroll.dispose();
    _idCtl.dispose();
    _noteCtl.dispose();
    super.dispose();
  }

  bool get _connected => _link != null;
  bool get _hasLog => _lines.isNotEmpty || _partial.isNotEmpty;

  /// Toàn bộ log dạng chuỗi — chỉ dựng khi cần (quét, gửi, lưu).
  String get _text {
    final sb = StringBuffer();
    for (final l in _lines) {
      sb.writeln(l);
    }
    sb.write(_partial);
    return sb.toString();
  }

  // ---------------------------------------------------------------- kết nối

  void _refreshPorts() {
    setState(() {
      _ports = listSerialLinkPorts();
      if (_pick == null || !_ports.contains(_pick)) {
        _pick = _ports.isEmpty ? null : _ports.first;
      }
    });
  }

  Future<void> _connect() async {
    if (_connecting || _connected) return;
    setState(() {
      _connecting = true;
      _error = null;
    });
    SerialLink? link;
    try {
      link = await openSerialLink(name: _pick, baud: _baud);
    } on SerialLinkException catch (e) {
      if (mounted) {
        setState(() {
          _connecting = false;
          _error = e.message;
        });
      }
      return;
    } catch (e) {
      if (mounted) {
        setState(() {
          _connecting = false;
          _error = tr('sp.openFail').replaceFirst('{err}', '$e');
        });
      }
      return;
    }
    if (!mounted) {
      await link?.close();
      return;
    }
    if (link == null) {
      // Web: người dùng bấm Hủy ở hộp thoại chọn cổng — không phải lỗi.
      setState(() => _connecting = false);
      return;
    }
    if (!_visible) {
      // Chuyển tab TRONG LÚC hộp thoại chọn cổng đang mở: lúc rời tab `_link` còn
      // null nên không có gì để nhả; giữ cổng khi đã ẩn là tab Kỹ Thuật mở cổng
      // thất bại mà không rõ ai đang giữ. Đóng ngay, không nhận.
      await link.close();
      setState(() => _connecting = false);
      return;
    }
    _link = link;
    _sub = link.stream.listen(
      _onData,
      onError: (Object e) => _onLost('$e'),
      onDone: () => _onLost(null),
      cancelOnError: true,
    );
    setState(() => _connecting = false);
  }

  /// Cổng tự rơi (rút cáp, máy tắt) — KHÁC chủ động ngắt: báo cho người dùng.
  void _onLost(String? why) {
    if (_link == null) return; // đang chủ động ngắt (_disconnect đã null trước)
    final l = _link;
    _link = null;
    _sub?.cancel();
    _sub = null;
    l?.close();
    if (!mounted) return;
    setState(() =>
        _error = why == null ? tr('sp.lost') : '${tr('sp.lost')} ($why)');
  }

  Future<void> _disconnect() async {
    final l = _link;
    if (l == null) return;
    _link = null; // đặt null TRƯỚC để onDone của stream không hiểu là mất kết nối
    await _sub?.cancel();
    _sub = null;
    await l.close();
    if (mounted) setState(() {});
  }

  // -------------------------------------------------------------------- log

  void _onData(Uint8List d) {
    if (!mounted) return;
    _partial += utf8.decode(d, allowMalformed: true);
    final parts = _partial.split('\n');
    _partial = parts.removeLast();
    for (final p in parts) {
      final line = p.endsWith('\r') ? p.substring(0, p.length - 1) : p;
      _lines.add(line);
      _flag.add(isSuspiciousLine(line));
    }
    if (_lines.length > _maxLines) {
      final n = _lines.length - _maxLines;
      _lines.removeRange(0, n);
      _flag.removeRange(0, n);
    }
    _scheduleTriage();
    _schedulePaint();
  }

  /// Vẽ lại sau ≤ 80 ms kể từ mẩu đầu tiên của đợt (không đẩy lùi khi mẩu mới tới —
  /// đẩy lùi là log dồn dập thì màn đứng im tới khi ngừng).
  void _schedulePaint() {
    if (_paintTimer != null) return;
    _paintTimer = Timer(const Duration(milliseconds: 80), () {
      _paintTimer = null;
      if (!mounted) return;
      setState(() {});
      if (_autoscroll) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (_scroll.hasClients) {
            _scroll.jumpTo(_scroll.position.maxScrollExtent);
          }
        });
      }
    });
  }

  /// Quét lại toàn bộ log sau khi dữ liệu ngừng tới 600ms (boot log tới theo
  /// từng đợt vài chục byte — quét mỗi đợt là phí).
  void _scheduleTriage() {
    _triageTimer?.cancel();
    _triageTimer = Timer(const Duration(milliseconds: 600), () {
      if (!mounted) return;
      final r = triageLog(_text);
      setState(() {
        _report = r;
        // Mã máy in trong log → điền hộ (chỉ khi ô đang trống, không ghi đè
        // thứ nhân viên đã gõ).
        if (_idCtl.text.trim().isEmpty && r.deviceId != null) {
          _idCtl.text = r.deviceId!;
        }
      });
    });
  }

  Future<void> _sendCmd(String cmd) async {
    final l = _link;
    if (l == null) return;
    try {
      await l.write(utf8.encode('$cmd\n'));
      _toast(tr('sp.cmdSent').replaceFirst('{cmd}', cmd));
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = tr('sp.openFail').replaceFirst('{err}', '$e'));
    }
  }

  Future<void> _confirmReset() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('sp.resetTitle')),
        content: Text(tr('sp.resetBody')),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('sp.cmdReset'))),
        ],
      ),
    );
    if (ok == true) await _sendCmd('Res');
  }

  void _clear() => setState(() {
        _lines.clear();
        _flag.clear();
        _partial = '';
        _report = TriageReport.empty;
      });

  String _stamp() {
    final n = DateTime.now();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${n.year}${p2(n.month)}${p2(n.day)}_'
        '${p2(n.hour)}${p2(n.minute)}${p2(n.second)}';
  }

  String _safeId() {
    final id = _idCtl.text.trim().replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '_');
    return id.isEmpty ? 'may' : id;
  }

  String _header() => '# FBT_RAPID — log máy (Chăm sóc KH › Xử lý sự cố)\n'
      '# Mã máy: ${_idCtl.text.trim()}\n'
      '# Cổng: ${_link?.label ?? ''} @ $_baud\n'
      '# Người lấy: ${SessionStore.current?.username ?? ''}\n'
      '# Lúc: ${DateTime.now()}\n'
      '# Mô tả: ${_noteCtl.text.trim()}\n'
      '${'-' * 50}\n';

  /// Lưu cục bộ (desktop: `<gốc>\FBT_RAPID_supportlog\`; web: tải xuống) — đường
  /// lùi khi không có mạng, hoặc để đính kèm email.
  Future<void> _saveLocal() async {
    if (!_hasLog) {
      _toast(tr('sp.noData'));
      return;
    }
    final name = '${_safeId()}_${_stamp()}.txt';
    final content = _header() + _text;
    try {
      if (kIsWeb) {
        pf.downloadBytes(name, utf8.encode(content));
        _toast(tr('techweb.downloaded'));
        return;
      }
      final dir = '${StoragePaths.parent}\\FBT_RAPID_supportlog';
      pf.ensureDir(dir);
      final path = '$dir\\$name';
      await pf.writeFileText(path, content);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(tr('sp.saved').replaceFirst('{path}', path)),
        action: SnackBarAction(
          label: tr('sp.openFolder'),
          onPressed: () => pf.openFolder(dir),
        ),
      ));
    } catch (e) {
      _toast(tr('sp.sendFail').replaceFirst('{err}', '$e'));
    }
  }

  // ------------------------------------------------------------------- gửi

  Future<void> _upload() async {
    if (_sending) return;
    if (!_hasLog) {
      _toast(tr('sp.sendNeedText'));
      return;
    }
    final id = _idCtl.text.trim();
    if (id.isEmpty) {
      _toast(tr('sp.sendNeedId'));
      return;
    }
    setState(() => _sending = true);
    try {
      final r = await _api.uploadDeviceLog(id, {
        'by': SessionStore.current?.username ?? '',
        'note': _noteCtl.text.trim(),
        'port': _link?.label ?? '',
        'baud': _baud,
        'captured_at': DateTime.now().toIso8601String(),
        'app': 'FBT_RAPID',
        'findings': [
          for (final f in _report.findings)
            {'level': f.level.name, 'key': f.key, 'count': f.count},
        ],
        'text': _text,
      });
      if (!mounted) return;
      setState(() => _lastSent = r.file);
      _toast(tr('sp.sent').replaceFirst('{file}', r.file));
    } on CloudApiException catch (e) {
      _toast(tr('sp.sendFail').replaceFirst('{err}', e.message));
    } catch (e) {
      _toast(tr('sp.sendFail').replaceFirst('{err}', '$e'));
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  Future<void> _showSent() async {
    final id = _idCtl.text.trim();
    if (id.isEmpty) {
      _toast(tr('sp.sendNeedId'));
      return;
    }
    await showDialog<void>(
      context: context,
      builder: (_) => _SentLogsDialog(api: _api, device: id),
    );
  }

  void _toast(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  // ------------------------------------------------------------------- UI

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    if (!serialLinkAvailable) {
      // Web điện thoại / trình duyệt không có Web Serial: nói rõ thay vì bày
      // nút Kết nối chết.
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.usb_off, size: 48, color: cs.onSurfaceVariant),
              const SizedBox(height: 12),
              Text(tr('techweb.unsupported'),
                  textAlign: TextAlign.center,
                  style: TextStyle(color: cs.onSurfaceVariant)),
            ],
          ),
        ),
      );
    }
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Padding(
        padding: const EdgeInsets.fromLTRB(4, 4, 4, 8),
        child: LayoutBuilder(builder: (context, c) {
          // Dưới ~900px hai cột (log + bảng gửi) chen nhau: xếp dọc, cuộn cả trang.
          final narrow = c.maxWidth < 900;
          final side = _sidePanel(context);
          if (narrow) {
            return ListView(
              children: [
                _connectBar(context),
                const SizedBox(height: 10),
                SizedBox(height: 360, child: _console(context)),
                const SizedBox(height: 10),
                side,
              ],
            );
          }
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _connectBar(context),
              const SizedBox(height: 10),
              Expanded(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Expanded(child: _console(context)),
                    const SizedBox(width: 12),
                    SizedBox(
                      width: 360,
                      child: SingleChildScrollView(child: side),
                    ),
                  ],
                ),
              ),
            ],
          );
        }),
      ),
    );
  }

  /// Bước 1 — hàng kết nối + lệnh nhanh.
  Widget _connectBar(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final on = _connected;
    return AppCard(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Wrap(
            spacing: 10,
            runSpacing: 10,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              _StepBadge(1, tr('sp.step1')),
              if (serialLinkCanListPorts) ...[
                SizedBox(
                  width: 170,
                  // `initialValue` chỉ áp lúc dựng → key theo danh sách cổng để
                  // "Làm mới cổng" (đổi danh sách + _pick) dựng lại ô chọn.
                  child: DropdownButtonFormField<String>(
                    key: ValueKey('ports_${_ports.join('|')}'),
                    initialValue: _pick,
                    isExpanded: true,
                    decoration: InputDecoration(
                        labelText: tr('sp.port'), isDense: true),
                    items: [
                      for (final p in _ports)
                        DropdownMenuItem(value: p, child: Text(p)),
                    ],
                    onChanged: on ? null : (v) => setState(() => _pick = v),
                  ),
                ),
                IconButton(
                  tooltip: tr('sp.refreshPorts'),
                  onPressed: on ? null : _refreshPorts,
                  icon: const Icon(Icons.refresh),
                ),
              ],
              if (on)
                OutlinedButton.icon(
                  onPressed: _disconnect,
                  icon: const Icon(Icons.usb_off, size: 18),
                  label: Text(tr('sp.disconnect')),
                )
              else
                FilledButton.icon(
                  onPressed: _connecting ||
                          (serialLinkCanListPorts && _pick == null)
                      ? null
                      : _connect,
                  icon: _connecting
                      ? const SizedBox(
                          width: 16,
                          height: 16,
                          child: CircularProgressIndicator(strokeWidth: 2))
                      : const Icon(Icons.usb, size: 18),
                  label: Text(_connecting ? tr('sp.connecting') : tr('sp.connect')),
                ),
              Chip(
                avatar: Icon(Icons.circle,
                    size: 12, color: on ? sem.success : cs.outline),
                label: Text(on
                    ? '${tr('sp.connected')} · ${_link!.label} @ $_baud'
                    : tr('sp.notConnected')),
                backgroundColor: on ? cs.primaryContainer : null,
                side: BorderSide.none,
              ),
            ],
          ),
          if (!on && serialLinkCanListPorts && _ports.isEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(tr('sp.noPorts'),
                  style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
            ),
          if (!on && !serialLinkCanListPorts)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(tr('sp.webPick'),
                  style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
            ),
          if (_error != null)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(_error!,
                  style: TextStyle(fontSize: 12.5, color: cs.error)),
            ),
          const SizedBox(height: 10),
          // Lệnh nhanh — chỉ những lệnh AN TOÀN (tra ở data/machine_info_content:
          // KHÔNG có "P" vì nó treo firmware). Mờ khi chưa kết nối.
          Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              Text(tr('sp.quick'),
                  style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
              _Cmd(tr('sp.cmdPara'), 'ParaRead', on ? () => _sendCmd('ParaRead') : null),
              _Cmd(tr('sp.cmdCal'), 'M', on ? () => _sendCmd('M') : null),
              _Cmd(tr('sp.cmdTemp'), 'TemperatureOutput',
                  on ? () => _sendCmd('TemperatureOutput') : null),
              _Cmd(tr('sp.cmdReset'), 'Res', on ? _confirmReset : null,
                  danger: true),
            ],
          ),
        ],
      ),
    );
  }

  /// Bước 2 — khung log.
  Widget _console(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    // KHÔNG copy `_lines`/`_flag` mỗi lần dựng (5000 phần tử × mỗi lần vẽ): dòng dở
    // `_partial` là phần tử cuối "ảo" — đọc qua hai hàm nhỏ bên dưới.
    final hasPartial = _partial.isNotEmpty;
    final partialBad = hasPartial && isSuspiciousLine(_partial);
    final total = _lines.length + (hasPartial ? 1 : 0);
    String lineAt(int i) => i < _lines.length ? _lines[i] : _partial;
    bool badAt(int i) => i < _flag.length ? _flag[i] : partialBad;
    final idx = <int>[
      for (var i = 0; i < total; i++)
        if (!_onlyIssues || badAt(i)) i
    ];
    var issues = 0;
    for (final f in _flag) {
      if (f) issues++;
    }
    return AppCard(
      padding: EdgeInsets.zero,
      child: ClipRRect(
        borderRadius: BorderRadius.circular(AppRadius.card),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(14, 10, 8, 8),
              child: Wrap(
                spacing: 8,
                runSpacing: 6,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  _StepBadge(2, tr('sp.step2')),
                  Text(
                    tr('sp.lines').replaceFirst('{n}', '$total'),
                    style: TextStyle(
                        fontSize: 12.5,
                        color: cs.onSurfaceVariant,
                        fontFeatures: const [FontFeature.tabularFigures()]),
                  ),
                  FilterChip(
                    label: Text(issues > 0
                        ? '${tr('sp.onlyIssues')} ($issues)'
                        : tr('sp.onlyIssues')),
                    selected: _onlyIssues,
                    onSelected: (v) => setState(() => _onlyIssues = v),
                  ),
                  FilterChip(
                    label: Text(tr('sp.autoscroll')),
                    selected: _autoscroll,
                    onSelected: (v) => setState(() => _autoscroll = v),
                  ),
                  IconButton(
                    tooltip: tr('sp.saveLocal'),
                    onPressed: _hasLog ? _saveLocal : null,
                    icon: const Icon(Icons.save_alt),
                  ),
                  IconButton(
                    tooltip: tr('sp.clear'),
                    onPressed: _hasLog ? _clear : null,
                    icon: const Icon(Icons.delete_outline),
                  ),
                ],
              ),
            ),
            Divider(height: 1, color: cs.outlineVariant),
            Expanded(
              child: Container(
                color: cs.surfaceContainerHighest,
                child: total == 0
                    ? Center(
                        child: Padding(
                          padding: const EdgeInsets.all(20),
                          child: Text(
                            _connected ? tr('sp.waiting') : tr('sp.noData'),
                            textAlign: TextAlign.center,
                            style: TextStyle(color: cs.onSurfaceVariant),
                          ),
                        ),
                      )
                    // SelectionArea + Text từng dòng: tô đỏ dòng nghi lỗi mà
                    // vẫn bôi đen/copy xuyên nhiều dòng được.
                    : SelectionArea(
                        child: ListView.builder(
                          controller: _scroll,
                          padding: const EdgeInsets.all(10),
                          itemCount: idx.length,
                          itemBuilder: (_, k) {
                            final i = idx[k];
                            final bad = badAt(i);
                            return Text(
                              lineAt(i),
                              style: TextStyle(
                                fontFamily: 'JetBrains Mono',
                                fontSize: 12,
                                height: 1.35,
                                color: bad ? cs.error : cs.onSurface,
                                fontWeight:
                                    bad ? FontWeight.w600 : FontWeight.w400,
                              ),
                            );
                          },
                        ),
                      ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Cột phải: dấu hiệu + bước 3 gửi + hướng dẫn.
  Widget _sidePanel(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _triageCard(context),
        const SizedBox(height: 10),
        _sendCard(context),
        const SizedBox(height: 10),
        _guideCard(context),
      ],
    );
  }

  Widget _triageCard(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    Color colorOf(TriageLevel l) => switch (l) {
          TriageLevel.error => cs.error,
          TriageLevel.warning => sem.warning,
          TriageLevel.info => sem.info,
        };
    IconData iconOf(TriageLevel l) => switch (l) {
          TriageLevel.error => Icons.error_outline,
          TriageLevel.warning => Icons.warning_amber_outlined,
          TriageLevel.info => Icons.info_outline,
        };
    // Kết luận MỘT DÒNG cho nhân viên CSKH trước khi đi vào từng dấu hiệu:
    // có lỗi máy → chuyển kỹ thuật; chỉ cảnh báo → khách tự thử được; không
    // có gì → nói thẳng. Đây là câu họ sẽ nói với khách.
    final hasWarn =
        _report.findings.any((f) => f.level == TriageLevel.warning);
    final (Color bannerBg, Color bannerFg, IconData bannerIcon, String bannerKey) =
        _report.hasError
            ? (cs.errorContainer, cs.onErrorContainer, Icons.report_outlined,
                'sp.statusError')
            : hasWarn
                ? (sem.warning.withValues(alpha: 0.14), cs.onSurface,
                    Icons.tips_and_updates_outlined, 'sp.statusWarn')
                : (sem.success.withValues(alpha: 0.14), cs.onSurface,
                    Icons.check_circle_outline, 'sp.statusOk');
    return AppCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppSectionTitle(tr('sp.triageTitle')),
          if (!_hasLog)
            Text(tr('sp.triageNoLog'),
                style: TextStyle(fontSize: 13, color: cs.onSurfaceVariant))
          else ...[
            Container(
              width: double.infinity,
              margin: const EdgeInsets.only(bottom: 12),
              padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
              decoration: BoxDecoration(
                color: bannerBg,
                borderRadius: BorderRadius.circular(AppRadius.base),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(bannerIcon, size: 20, color: bannerFg),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(tr(bannerKey),
                        style: TextStyle(
                            fontSize: 13.5,
                            height: 1.4,
                            fontWeight: FontWeight.w600,
                            color: bannerFg)),
                  ),
                ],
              ),
            ),
            for (final f in _report.findings)
              Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Icon(iconOf(f.level), size: 18, color: colorOf(f.level)),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            '${tr(f.titleKey)}'
                            '${f.count > 1 ? ' ×${f.count}' : ''}',
                            style: TextStyle(
                                fontSize: 13.5,
                                fontWeight: FontWeight.w600,
                                color: cs.onSurface),
                          ),
                          Text(tr(f.hintKey),
                              style: TextStyle(
                                  fontSize: 12.5,
                                  height: 1.4,
                                  color: cs.onSurfaceVariant)),
                          if (f.sample.isNotEmpty)
                            Padding(
                              padding: const EdgeInsets.only(top: 3),
                              child: Text(f.sample,
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                  style: TextStyle(
                                      fontFamily: 'JetBrains Mono',
                                      fontSize: 11,
                                      color: cs.onSurfaceVariant)),
                            ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
          ],
          if (_report.version != null || _report.deviceId != null) ...[
            const SizedBox(height: 4),
            Wrap(spacing: 8, runSpacing: 6, children: [
              if (_report.deviceId != null)
                Chip(
                    visualDensity: VisualDensity.compact,
                    label: Text(tr('sp.detectedId')
                        .replaceFirst('{id}', _report.deviceId!))),
              if (_report.version != null)
                Chip(
                    visualDensity: VisualDensity.compact,
                    label: Text(tr('sp.detectedVersion')
                        .replaceFirst('{v}', _report.version!))),
            ]),
          ],
        ],
      ),
    );
  }

  Widget _sendCard(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final who = SessionStore.current?.username ?? '';
    return AppCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(children: [
            _StepBadge(3, tr('sp.step3')),
          ]),
          const SizedBox(height: 12),
          TextField(
            controller: _idCtl,
            textCapitalization: TextCapitalization.characters,
            inputFormatters: [
              LengthLimitingTextInputFormatter(32),
              // Chỉ chữ/số/_-: mã máy đi vào URL + tên file trên server.
              FilteringTextInputFormatter.allow(RegExp(r'[A-Za-z0-9_\-]')),
            ],
            style: const TextStyle(fontFamily: 'JetBrains Mono'),
            decoration: InputDecoration(
              labelText: tr('sp.deviceId'),
              hintText: 'RPL02013',
              helperText: tr('sp.deviceIdHint'),
              helperMaxLines: 2,
              isDense: true,
              prefixIcon: const Icon(Icons.qr_code_2_outlined),
            ),
            onChanged: (_) => setState(() {}),
          ),
          const SizedBox(height: 10),
          TextField(
            controller: _noteCtl,
            minLines: 2,
            maxLines: 4,
            inputFormatters: [LengthLimitingTextInputFormatter(2000)],
            decoration: InputDecoration(
              labelText: tr('sp.note'),
              hintText: tr('sp.noteHint'),
              alignLabelWithHint: true,
            ),
          ),
          const SizedBox(height: 8),
          Text('${tr('sp.by')}: ${who.isEmpty ? '—' : who}',
              style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
          const SizedBox(height: 12),
          FilledButton.icon(
            onPressed: (_sending || !_hasLog) ? null : _upload,
            icon: _sending
                ? const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.cloud_upload_outlined, size: 18),
            label: Text(_sending ? tr('sp.sending') : tr('sp.send')),
          ),
          const SizedBox(height: 8),
          Row(children: [
            Expanded(
              child: OutlinedButton.icon(
                onPressed: _hasLog ? _saveLocal : null,
                icon: const Icon(Icons.save_alt, size: 18),
                label: Text(tr('sp.saveLocal')),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton.icon(
                onPressed: _idCtl.text.trim().isEmpty ? null : _showSent,
                icon: const Icon(Icons.history, size: 18),
                label: Text(tr('sp.viewSent')),
              ),
            ),
          ]),
          if (_lastSent != null)
            Padding(
              padding: const EdgeInsets.only(top: 10),
              child: Row(children: [
                Icon(Icons.check_circle_outline,
                    size: 16, color: AppSemantic.of(context).success),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    tr('sp.sent').replaceFirst('{file}', _lastSent!),
                    style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant),
                  ),
                ),
              ]),
            ),
        ],
      ),
    );
  }

  Widget _guideCard(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final steps = [
      tr('sp.guide1'),
      tr('sp.guide2'),
      tr('sp.guide3'),
      tr('sp.guide4'),
    ];
    return AppCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppSectionTitle(tr('sp.guideTitle')),
          for (var i = 0; i < steps.length; i++)
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('${i + 1}. ',
                      style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w700,
                          color: cs.primary)),
                  Expanded(
                    child: Text(steps[i],
                        style: TextStyle(
                            fontSize: 13, height: 1.4, color: cs.onSurface)),
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

/// Nhãn "Bước N — …" đầu mỗi khối, để ba khối đọc ra thành một trình tự.
class _StepBadge extends StatelessWidget {
  final int n;
  final String text;
  const _StepBadge(this.n, this.text);

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        CircleAvatar(
          radius: 11,
          backgroundColor: cs.primary,
          child: Text('$n',
              style: TextStyle(
                  fontSize: 12, fontWeight: FontWeight.w700, color: cs.onPrimary)),
        ),
        const SizedBox(width: 8),
        Text(text,
            style: TextStyle(
                fontSize: 14, fontWeight: FontWeight.w700, color: cs.onSurface)),
      ],
    );
  }
}

/// Chip lệnh nhanh: chỉ nhãn tiếng Việt; lệnh thật (`ParaRead`…) nằm trong
/// tooltip — nhân viên CSKH không cần thấy mã lệnh, kỹ thuật rê chuột là biết.
class _Cmd extends StatelessWidget {
  final String label;
  final String cmd;
  final VoidCallback? onTap;
  final bool danger;
  const _Cmd(this.label, this.cmd, this.onTap, {this.danger = false});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Tooltip(
      message: tr('sp.cmdTip').replaceFirst('{cmd}', cmd),
      waitDuration: const Duration(milliseconds: 500),
      child: ActionChip(
        onPressed: onTap,
        avatar: Icon(danger ? Icons.restart_alt : Icons.send_outlined,
            size: 16, color: danger ? cs.error : null),
        label: Text(label),
      ),
    );
  }
}

/// Hộp thoại "Log đã gửi của máy này": tải danh sách từ server, bấm một dòng
/// → mở nội dung đầy đủ (màn xem thô `RawUartScreen`).
class _SentLogsDialog extends StatefulWidget {
  final FbtApi api;
  final String device;
  const _SentLogsDialog({required this.api, required this.device});

  @override
  State<_SentLogsDialog> createState() => _SentLogsDialogState();
}

class _SentLogsDialogState extends State<_SentLogsDialog> {
  List<DeviceLogEntry>? _items;
  String? _error;
  String? _opening;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _items = null;
      _error = null;
    });
    try {
      final r = await widget.api.listDeviceLogs(widget.device);
      if (mounted) setState(() => _items = r);
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    }
  }

  Future<void> _open(DeviceLogEntry e) async {
    if (_opening != null) return;
    setState(() => _opening = e.file);
    try {
      final doc = await widget.api.fetchDeviceLog(e.file);
      if (!mounted) return;
      final text = (doc['text'] ?? '').toString();
      final head = StringBuffer()
        ..writeln('# ${e.file}')
        ..writeln('# ${tr('sp.by')}: ${e.by}')
        ..writeln('# ${tr('sp.note')}: ${e.note}')
        ..writeln('-' * 50);
      await Navigator.of(context).push(MaterialPageRoute(
        builder: (_) => RawUartScreen(title: e.file, text: '$head$text'),
      ));
    } on CloudApiException catch (err) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
            content: Text(tr('sp.sentLoadFail').replaceFirst('{err}', err.message))));
      }
    } finally {
      if (mounted) setState(() => _opening = null);
    }
  }

  String _fmt(DateTime? d) {
    if (d == null) return '—';
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${p2(d.day)}/${p2(d.month)}/${d.year} ${p2(d.hour)}:${p2(d.minute)}';
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final items = _items;
    return AlertDialog(
      title: Text('${tr('sp.sentList')} · ${widget.device}'),
      content: SizedBox(
        width: 520,
        height: 380,
        child: _error != null
            ? Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(_error!, style: TextStyle(color: cs.error)),
                    const SizedBox(height: 8),
                    OutlinedButton(
                        onPressed: _load, child: Text(tr('common.retry'))),
                  ],
                ),
              )
            : items == null
                ? const Center(child: CircularProgressIndicator())
                : items.isEmpty
                    ? Center(
                        child: Text(tr('sp.sentEmpty'),
                            style: TextStyle(color: cs.onSurfaceVariant)))
                    : ListView.separated(
                        itemCount: items.length,
                        separatorBuilder: (_, __) =>
                            Divider(height: 1, color: cs.outlineVariant),
                        itemBuilder: (_, i) {
                          final e = items[i];
                          return ListTile(
                            dense: true,
                            contentPadding: EdgeInsets.zero,
                            leading: _opening == e.file
                                ? const SizedBox(
                                    width: 20,
                                    height: 20,
                                    child: CircularProgressIndicator(
                                        strokeWidth: 2))
                                : Icon(Icons.description_outlined,
                                    color: e.findings > 0
                                        ? AppSemantic.of(context).warning
                                        : cs.onSurfaceVariant),
                            title: Text(
                              '${_fmt(e.at)} · ${e.by.isEmpty ? '—' : e.by}',
                              style: const TextStyle(
                                  fontWeight: FontWeight.w600,
                                  fontFeatures: [FontFeature.tabularFigures()]),
                            ),
                            subtitle: Text(
                              e.note.isEmpty ? e.file : e.note,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                            ),
                            trailing: Text(
                              '${(e.size / 1024).toStringAsFixed(1)} KB',
                              style: TextStyle(
                                  fontSize: 11,
                                  color: cs.onSurfaceVariant,
                                  fontFeatures: const [
                                    FontFeature.tabularFigures()
                                  ]),
                            ),
                            onTap: () => _open(e),
                          );
                        },
                      ),
      ),
      actions: [
        TextButton(
            onPressed: _load, child: Text(tr('common.refresh'))),
        TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(tr('common.close'))),
      ],
    );
  }
}
