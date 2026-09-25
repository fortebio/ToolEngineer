import 'dart:async';

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import 'raw_uart_screen.dart';

/// Ba trạng thái xử lý một bản log — CÙNG bộ với `_LOG_STATUSES` của server.
const kLogStatuses = ['new', 'working', 'done'];

/// Nhãn dịch của một trạng thái (`lg.status.new`…).
String logStatusLabel(String s) => tr('lg.status.$s');

/// Tên dễ đọc của một dấu hiệu bộ quét (`triage.<key>`); khoá lạ (app bản khác
/// gửi) thì hiện nguyên khoá — `tr` trả lại key khi chưa khai báo.
String triageLabel(String key) {
  final t = tr('triage.$key');
  return t == 'triage.$key' ? key : t;
}

/// Chip trạng thái dùng chung: hộp thư của kỹ thuật + hộp "Log đã gửi" của CSKH.
class LogStatusChip extends StatelessWidget {
  final String status;
  const LogStatusChip({super.key, required this.status});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final (Color c, IconData icon) = switch (status) {
      'working' => (sem.warning, Icons.build_circle_outlined),
      'done' => (sem.success, Icons.check_circle_outline),
      _ => (cs.primary, Icons.fiber_new_outlined),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: c.withValues(alpha: 0.5)),
      ),
      child: Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(icon, size: 14, color: c),
        const SizedBox(width: 4),
        Text(logStatusLabel(status),
            style: TextStyle(
                fontSize: 12, fontWeight: FontWeight.w600, color: c)),
      ]),
    );
  }
}

String _fmtTime(DateTime? d) {
  if (d == null) return '—';
  String p2(int x) => x.toString().padLeft(2, '0');
  return '${p2(d.day)}/${p2(d.month)}/${d.year} ${p2(d.hour)}:${p2(d.minute)}';
}

/// Mục **Log đã nhận** (tab Chăm sóc KH, nhân sự) — hộp thư của KỸ THUẬT: mọi bản
/// log CSKH gửi về (`GET /logs`), lọc theo trạng thái/mã máy, mở một bản để xem
/// dấu hiệu + log thô, rồi **Nhận xử lý → Đã xử lý** kèm ghi chú trả lời CSKH
/// (`PUT /logs/{file}/status`; CSKH thấy ghi chú đó trong hộp "Log đã gửi").
///
/// Nạp khi mục ĐANG ĐƯỢC XEM (`TickerMode`, mẫu `monitor_screen.dart`) và làm
/// mới nhẹ mỗi 60 s — không gọi mạng cho một trang không ai mở.
class SupportInboxScreen extends StatefulWidget {
  final AppSettings settings;

  /// Báo số bản `new` mỗi lần nạp — `SupportScreen` vẽ badge trên mục.
  final ValueChanged<int>? onNewCount;

  /// Bộ lọc mã máy do nơi khác đặt (Thống kê lỗi › bấm một máy).
  final ValueNotifier<String>? deviceFilter;

  const SupportInboxScreen(
      {super.key, required this.settings, this.onNewCount, this.deviceFilter});

  @override
  State<SupportInboxScreen> createState() => _SupportInboxScreenState();
}

class _SupportInboxScreenState extends State<SupportInboxScreen> {
  static const int _pageSize = 50;
  static const Duration _period = Duration(seconds: 60);

  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  final _deviceCtl = TextEditingController();
  String _status = 'new'; // mở ra là thấy việc chưa ai nhận
  List<DeviceLogEntry> _items = const [];
  Map<String, int> _counts = const {};
  int _total = 0;
  int _page = 1;
  bool _loading = false;
  bool _loadingMore = false;
  String? _error;
  bool _visible = false;
  bool _loadedOnce = false;
  bool _pending = false; // có yêu cầu nạp (đổi lọc) tới lúc đang nạp dở
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    widget.deviceFilter?.addListener(_onExternalFilter);
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final v = TickerMode.valuesOf(context).enabled;
    if (v == _visible) return;
    _visible = v;
    _timer?.cancel();
    if (v) {
      // Hoãn một microtask: `_load` gọi setState, mà đây đang trong pha build.
      Future.microtask(() {
        if (mounted) _load(quiet: _loadedOnce);
      });
      _timer = Timer.periodic(_period, (_) => _load(quiet: true));
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    widget.deviceFilter?.removeListener(_onExternalFilter);
    _deviceCtl.dispose();
    super.dispose();
  }

  void _onExternalFilter() {
    final d = widget.deviceFilter!.value;
    if (d.isEmpty) return; // SupportScreen đặt '' trước để lần bấm trùng máy vẫn báo
    _deviceCtl.text = d;
    _status = ''; // tra theo máy thì xem cả lịch sử của máy
    _load();
  }

  /// [quiet] = làm mới nền: không bật spinner, lỗi mạng giữ danh sách cũ.
  Future<void> _load({bool quiet = false}) async {
    if (_loading) {
      // Đổi bộ lọc giữa lúc vòng làm mới nền đang chạy: đừng nuốt — nạp lại sau.
      if (!quiet) _pending = true;
      return;
    }
    _loading = true;
    if (!quiet) {
      setState(() => _error = null);
    }
    try {
      final r = await _api.listAllLogs(
        device: _deviceCtl.text,
        status: _status,
        limit: _pageSize,
      );
      if (!mounted) return;
      setState(() {
        _items = r.items;
        _counts = r.counts;
        _total = r.total;
        _page = 1;
        _error = null;
        _loadedOnce = true;
      });
      // Badge chỉ đúng khi KHÔNG lọc máy (counts tính sau lọc máy).
      if (_deviceCtl.text.trim().isEmpty) {
        widget.onNewCount?.call(r.counts['new'] ?? 0);
      }
    } on CloudApiException catch (e) {
      if (mounted && !(quiet && _loadedOnce)) setState(() => _error = e.message);
    } catch (e) {
      if (mounted && !(quiet && _loadedOnce)) setState(() => _error = '$e');
    } finally {
      _loading = false;
      if (mounted && !quiet) setState(() {});
      if (_pending && mounted) {
        _pending = false;
        _load();
      }
    }
  }

  Future<void> _loadMore() async {
    if (_loadingMore) return;
    setState(() => _loadingMore = true);
    try {
      final r = await _api.listAllLogs(
        device: _deviceCtl.text,
        status: _status,
        page: _page + 1,
        limit: _pageSize,
      );
      if (!mounted) return;
      setState(() {
        _items = [..._items, ...r.items];
        _page += 1;
        _total = r.total;
      });
    } on CloudApiException catch (e) {
      _toast(e.message);
    } finally {
      if (mounted) setState(() => _loadingMore = false);
    }
  }

  void _toast(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _openDetail(DeviceLogEntry e) async {
    final changed = await showDialog<bool>(
      context: context,
      builder: (_) => _LogDetailDialog(api: _api, entry: e),
    );
    if (changed == true) _load(quiet: true);
  }

  // ------------------------------------------------------------------- UI

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(4, 4, 4, 8),
          child: Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              for (final s in ['', ...kLogStatuses])
                ChoiceChip(
                  label: Text(s.isEmpty
                      ? '${tr('lg.all')} ${_counts.values.fold<int>(0, (a, b) => a + b)}'
                      : '${logStatusLabel(s)} ${_counts[s] ?? 0}'),
                  selected: _status == s,
                  onSelected: (_) {
                    setState(() => _status = s);
                    _load();
                  },
                ),
              SizedBox(
                width: 200,
                child: TextField(
                  controller: _deviceCtl,
                  decoration: InputDecoration(
                    isDense: true,
                    labelText: tr('lg.filterDevice'),
                    prefixIcon: const Icon(Icons.search, size: 18),
                    suffixIcon: _deviceCtl.text.isEmpty
                        ? null
                        : IconButton(
                            icon: const Icon(Icons.clear, size: 18),
                            onPressed: () {
                              _deviceCtl.clear();
                              _load();
                            },
                          ),
                  ),
                  onSubmitted: (_) => _load(),
                ),
              ),
              IconButton(
                tooltip: tr('common.refresh'),
                onPressed: _loading ? null : () => _load(),
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
        ),
        Expanded(child: _body(cs)),
      ],
    );
  }

  Widget _body(ColorScheme cs) {
    if (_error != null && _items.isEmpty) {
      return Center(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          Text(_error!, style: TextStyle(color: cs.error)),
          const SizedBox(height: 8),
          OutlinedButton(onPressed: _load, child: Text(tr('common.retry'))),
        ]),
      );
    }
    if (!_loadedOnce) {
      return const Center(child: CircularProgressIndicator());
    }
    if (_items.isEmpty) {
      return Center(
        child: Text(
          _status == 'new' ? tr('lg.emptyNew') : tr('lg.empty'),
          style: TextStyle(color: cs.onSurfaceVariant),
        ),
      );
    }
    final more = _items.length < _total;
    return ListView.separated(
      itemCount: _items.length + (more ? 1 : 0),
      separatorBuilder: (_, __) => const SizedBox(height: 6),
      itemBuilder: (_, i) {
        if (i == _items.length) {
          return Center(
            child: TextButton(
              onPressed: _loadingMore ? null : _loadMore,
              child: Text(tr('lg.more')
                  .replaceFirst('{n}', '${_total - _items.length}')),
            ),
          );
        }
        return _LogCard(entry: _items[i], onTap: () => _openDetail(_items[i]));
      },
    );
  }
}

/// Một dòng hộp thư: giờ · máy · người gửi · fw, mô tả, các dấu hiệu, trạng thái.
class _LogCard extends StatelessWidget {
  final DeviceLogEntry entry;
  final VoidCallback onTap;
  const _LogCard({required this.entry, required this.onTap});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final e = entry;
    final accent = e.errors > 0
        ? cs.error
        : e.warnings > 0
            ? sem.warning
            : cs.outlineVariant;
    return Card(
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Container(
          decoration: BoxDecoration(
            border: Border(left: BorderSide(color: accent, width: 4)),
          ),
          padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(children: [
                Expanded(
                  child: Text.rich(
                    TextSpan(children: [
                      TextSpan(
                          text: e.device,
                          style: const TextStyle(fontWeight: FontWeight.w700)),
                      TextSpan(
                        text: '  ·  ${_fmtTime(e.at)}  ·  '
                            '${e.by.isEmpty ? '—' : e.by}'
                            '${e.fw.isEmpty ? '' : '  ·  fw ${e.fw}'}',
                        style: TextStyle(color: cs.onSurfaceVariant),
                      ),
                    ]),
                    style: const TextStyle(
                        fontSize: 13,
                        fontFeatures: [FontFeature.tabularFigures()]),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                const SizedBox(width: 8),
                LogStatusChip(status: e.status),
              ]),
              if (e.note.isNotEmpty) ...[
                const SizedBox(height: 4),
                Text(e.note, maxLines: 2, overflow: TextOverflow.ellipsis),
              ],
              if (e.keys.isNotEmpty) ...[
                const SizedBox(height: 6),
                Wrap(spacing: 6, runSpacing: 4, children: [
                  for (final k in e.keys.take(6))
                    Text('• ${triageLabel(k)}',
                        style: TextStyle(fontSize: 12, color: accent)),
                ]),
              ],
              if (e.statusNote.isNotEmpty || e.statusBy.isNotEmpty) ...[
                const SizedBox(height: 4),
                Text(
                  '${e.statusBy.isEmpty ? '' : '${e.statusBy}: '}${e.statusNote}',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                      fontSize: 12,
                      fontStyle: FontStyle.italic,
                      color: cs.onSurfaceVariant),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

/// Chi tiết một bản log + hành động của kỹ thuật. Trả `true` khi đã đổi/xoá.
class _LogDetailDialog extends StatefulWidget {
  final FbtApi api;
  final DeviceLogEntry entry;
  const _LogDetailDialog({required this.api, required this.entry});

  @override
  State<_LogDetailDialog> createState() => _LogDetailDialogState();
}

class _LogDetailDialogState extends State<_LogDetailDialog> {
  late DeviceLogEntry _e = widget.entry;
  late final _noteCtl = TextEditingController(text: widget.entry.statusNote);
  bool _busy = false;
  bool _changed = false;
  List<Map>? _findings; // từ bản đầy đủ (có `count`), nạp khi mở

  @override
  void initState() {
    super.initState();
    _loadDoc();
  }

  @override
  void dispose() {
    _noteCtl.dispose();
    super.dispose();
  }

  Map<String, dynamic>? _doc;

  Future<void> _loadDoc() async {
    try {
      final d = await widget.api.fetchDeviceLog(_e.file);
      if (!mounted) return;
      setState(() {
        _doc = d;
        _findings = (d['findings'] as List? ?? const []).whereType<Map>().toList();
      });
    } catch (_) {
      if (mounted) setState(() => _findings = const []);
    }
  }

  Future<void> _setStatus(String s) async {
    setState(() => _busy = true);
    try {
      final r = await widget.api.setLogStatus(
        _e.file,
        s,
        by: SessionStore.current?.username ?? '',
        note: _noteCtl.text.trim(),
      );
      if (!mounted) return;
      setState(() {
        _e = r;
        _changed = true;
      });
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(tr('lg.statusSaved')
              .replaceFirst('{s}', logStatusLabel(s)))));
    } on CloudApiException catch (err) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(err.message)));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _delete() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('lg.deleteTitle')),
        content: Text(tr('lg.deleteBody').replaceFirst('{file}', _e.file)),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              style: FilledButton.styleFrom(
                  backgroundColor: Theme.of(c).colorScheme.error),
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('common.delete'))),
        ],
      ),
    );
    if (ok != true) return;
    setState(() => _busy = true);
    try {
      await widget.api.deleteLog(_e.file);
      if (mounted) Navigator.pop(context, true);
    } on CloudApiException catch (err) {
      if (mounted) {
        setState(() => _busy = false);
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(err.message)));
      }
    }
  }

  void _openRaw() {
    final d = _doc;
    if (d == null) return;
    final head = StringBuffer()
      ..writeln('# ${_e.file}')
      ..writeln('# ${tr('sp.by')}: ${_e.by}')
      ..writeln('# ${tr('sp.note')}: ${_e.note}')
      ..writeln('-' * 50);
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => RawUartScreen(
          title: _e.file, text: '$head${(d['text'] ?? '').toString()}'),
    ));
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    Widget kv(String k, String v) => Padding(
          padding: const EdgeInsets.only(bottom: 4),
          child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            SizedBox(
                width: 110,
                child: Text(k, style: TextStyle(color: cs.onSurfaceVariant))),
            Expanded(child: SelectableText(v.isEmpty ? '—' : v)),
          ]),
        );
    final findings = _findings;
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) Navigator.pop(context, _changed);
      },
      child: AlertDialog(
        title: Row(children: [
          Expanded(child: Text('${tr('lg.detail')} · ${_e.device}')),
          LogStatusChip(status: _e.status),
        ]),
        content: SizedBox(
          width: 560,
          child: SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                kv(tr('lg.receivedAt'), _fmtTime(_e.at)),
                kv(tr('sp.by'), _e.by),
                kv('Firmware', _e.fw),
                kv(tr('sp.note'), _e.note),
                kv(tr('lg.size'), '${(_e.size / 1024).toStringAsFixed(1)} KB'),
                const Divider(),
                Text(tr('lg.signs'),
                    style: const TextStyle(fontWeight: FontWeight.w700)),
                const SizedBox(height: 6),
                if (findings == null)
                  const LinearProgressIndicator()
                else if (findings.isEmpty)
                  Text(tr('lg.noSigns'),
                      style: TextStyle(color: cs.onSurfaceVariant))
                else
                  for (final f in findings)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 2),
                      child: Row(children: [
                        Icon(
                          f['level'] == 'error'
                              ? Icons.error_outline
                              : f['level'] == 'warning'
                                  ? Icons.warning_amber_outlined
                                  : Icons.info_outline,
                          size: 16,
                          color: f['level'] == 'error'
                              ? cs.error
                              : f['level'] == 'warning'
                                  ? sem.warning
                                  : cs.onSurfaceVariant,
                        ),
                        const SizedBox(width: 6),
                        Expanded(
                            child: Text(triageLabel('${f['key']}'))),
                        if (f['count'] != null)
                          Text('×${f['count']}',
                              style: TextStyle(color: cs.onSurfaceVariant)),
                      ]),
                    ),
                const Divider(),
                if (_e.statusBy.isNotEmpty || _e.statusAt != null)
                  kv(tr('lg.handledBy'),
                      '${_e.statusBy} · ${_fmtTime(_e.statusAt)}'),
                TextField(
                  controller: _noteCtl,
                  minLines: 2,
                  maxLines: 4,
                  decoration: InputDecoration(
                    labelText: tr('lg.replyLabel'),
                    helperText: tr('lg.replyHelp'),
                    border: const OutlineInputBorder(),
                  ),
                ),
              ],
            ),
          ),
        ),
        actionsOverflowButtonSpacing: 8,
        actions: [
          TextButton.icon(
            onPressed: _busy ? null : _delete,
            icon: Icon(Icons.delete_outline, color: cs.error),
            label: Text(tr('common.delete'), style: TextStyle(color: cs.error)),
          ),
          TextButton.icon(
            onPressed: _doc == null ? null : _openRaw,
            icon: const Icon(Icons.article_outlined),
            label: Text(tr('lg.openRaw')),
          ),
          if (_e.status != 'new')
            TextButton(
              onPressed: _busy ? null : () => _setStatus('new'),
              child: Text(tr('lg.reopen')),
            ),
          if (_e.status != 'working')
            OutlinedButton(
              onPressed: _busy ? null : () => _setStatus('working'),
              child: Text(tr('lg.take')),
            ),
          FilledButton(
            // Đã xử lý rồi thì nút này = lưu lại ghi chú (giữ trạng thái done).
            onPressed: _busy ? null : () => _setStatus('done'),
            child: Text(_e.status == 'done' ? tr('lg.saveNote') : tr('lg.markDone')),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, _changed),
            child: Text(tr('common.close')),
          ),
        ],
      ),
    );
  }
}
