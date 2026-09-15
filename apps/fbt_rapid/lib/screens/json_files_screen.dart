import 'dart:convert';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/test_result.dart';
import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';

/// Mục "JSON data" (tab Thư Mục): danh sách file JSON thiết bị đã đẩy lên
/// **Engineer Server** (server lưu file-first — mỗi phiên `/sessions` = 1 file
/// JSON gốc), bấm từng file để XEM **JSON THÔ** (không parse thành đồ thị).
/// Kèm nút "Mở file JSON" xem thô 1 file trên máy.
class JsonFilesScreen extends StatefulWidget {
  final AppSettings settings;
  const JsonFilesScreen({super.key, required this.settings});

  @override
  State<JsonFilesScreen> createState() => _JsonFilesScreenState();
}

class _JsonFilesScreenState extends State<JsonFilesScreen> {
  static const int _pageSize = 50;

  // FbtApi trực tiếp (không qua buildCloudClient) vì cần fetchSessionJson
  // (xem thô) — mục này gắn CHẶT nguồn engineer, không đổi nguồn.
  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  final List<TestResult> _items = []; // đã lọc theo quyền canSee
  int _fetched = 0; // số item ĐÃ tải (trước lọc) — để phân trang đúng offset
  int _total = 0;
  bool _loading = false;
  String? _error;
  String? _openingId; // phiên đang tải chi tiết (hiện spinner ở dòng đó)

  @override
  void initState() {
    super.initState();
    _load(reset: true);
  }

  Future<void> _load({bool reset = false}) async {
    if (_loading) return;
    setState(() {
      _loading = true;
      _error = null;
      if (reset) {
        _items.clear();
        _fetched = 0;
        _total = 0;
      }
    });
    try {
      // deviceId rỗng = KHÔNG lọc máy → server trả mọi phiên (mọi file JSON).
      // User hạn chế quyền: 1 trang có thể toàn máy KHÔNG được xem → lọc canSee
      // ra rỗng. Nếu vậy mà VẪN còn trang, tự tải tiếp cho tới khi có item xem
      // được hoặc hết (không thì màn hiện "rỗng" mà không có nút Tải thêm — user
      // kẹt dù máy họ nằm ở trang sau). Cap vòng lặp để không quét vô hạn.
      final session = SessionStore.current;
      var guard = 0;
      while (true) {
        final page =
            await _api.listRuns('', limit: _pageSize, offset: _fetched);
        if (!mounted) return;
        final visible =
            page.runs.where((r) => session?.canSee(r.deviceId) ?? false);
        setState(() {
          _fetched += page.runs.length;
          _total = page.total;
          _items.addAll(visible);
        });
        // Dừng khi: có item mới hiện được, hoặc hết trang, hoặc trang rỗng.
        if (visible.isNotEmpty || _fetched >= _total || page.runs.isEmpty) break;
        if (++guard >= 40) break; // ~2000 phiên/lần bấm — tránh quét cả kho
      }
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  /// Mở 1 phiên trên server: tải JSON gốc rồi xem THÔ (pretty-print).
  Future<void> _viewRun(TestResult item) async {
    if (_openingId != null) return;
    setState(() => _openingId = item.id);
    Map<String, dynamic> j;
    try {
      j = await _api.fetchSessionJson(item.id);
    } on CloudApiException catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(e.message),
          backgroundColor: kErrorSnackBg,
        ));
      }
      return;
    } finally {
      if (mounted) setState(() => _openingId = null);
    }
    if (!mounted) return;
    _push('${item.deviceId} · #${item.id}', j);
  }

  /// Mở 1 file JSON trên máy — xem thô như file trên server.
  Future<void> _openLocalFile() async {
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'JSON', extensions: ['json'])
    ]);
    if (f == null) return;
    Object? j;
    try {
      j = jsonDecode(await f.readAsString());
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(tr('history.openJsonError')),
        backgroundColor: kErrorSnackBg,
      ));
      return;
    }
    if (!mounted) return;
    _push(f.name, j);
  }

  void _push(String title, Object? json) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => _JsonViewScreen(
          title: title,
          text: const JsonEncoder.withIndent('  ').convert(json),
        ),
      ),
    );
  }

  String _fmt(DateTime d) {
    final l = d.toLocal();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${p2(l.day)}-${p2(l.month)}-${l.year} ${p2(l.hour)}:${p2(l.minute)}';
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 4, 12, 4),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  '${widget.settings.cloudUrlFor(CloudSource.engineer)}'
                  '${_total > 0 ? ' · $_total file' : ''}',
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12),
                ),
              ),
              IconButton(
                tooltip: tr('history.jsonRefresh'),
                icon: const Icon(Icons.refresh, size: 20),
                onPressed: _loading ? null : () => _load(reset: true),
              ),
              OutlinedButton.icon(
                onPressed: _openLocalFile,
                icon: const Icon(Icons.file_open_outlined, size: 18),
                label: Text(tr('history.openJson')),
              ),
            ],
          ),
        ),
        Expanded(child: _body(cs)),
      ],
    );
  }

  Widget _body(ColorScheme cs) {
    if (_error != null) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(_error!, textAlign: TextAlign.center),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: () => _load(reset: true),
              icon: const Icon(Icons.refresh, size: 18),
              label: Text(tr('history.jsonRefresh')),
            ),
          ],
        ),
      );
    }
    if (_items.isEmpty) {
      return _loading
          ? const Center(child: CircularProgressIndicator())
          : Center(
              child: Text(
                tr('history.jsonEmpty'),
                style: TextStyle(color: cs.onSurfaceVariant),
              ),
            );
    }
    final hasMore = _fetched < _total;
    return ListView.builder(
      itemCount: _items.length + (hasMore ? 1 : 0),
      itemBuilder: (_, i) {
        if (i == _items.length) {
          return Padding(
            padding: const EdgeInsets.all(12),
            child: Center(
              child: _loading
                  ? const CircularProgressIndicator()
                  : OutlinedButton(
                      onPressed: _load,
                      child: Text(tr('history.jsonLoadMore')),
                    ),
            ),
          );
        }
        final r = _items[i];
        return AppFadeIn(
          index: i,
          child: ListTile(
            dense: true,
            leading: Icon(Icons.description_outlined, color: cs.primary),
            // Mã máy + ID phiên = định danh → mono + tabular.
            title: Text('${r.deviceId} · ${_fmt(r.timestamp)}',
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontFeatures: [FontFeature.tabularFigures()],
                )),
            subtitle: Text(
              '#${r.id}${r.version.isEmpty ? '' : ' · ${r.version}'}',
              style: TextStyle(
                  color: cs.onSurfaceVariant,
                  fontFeatures: const [FontFeature.tabularFigures()]),
            ),
            trailing: _openingId == r.id
                ? const SizedBox(
                    width: 18, height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2))
                : Icon(Icons.chevron_right, color: cs.onSurfaceVariant),
            onTap: () => _viewRun(r),
          ),
        );
      },
    );
  }
}

/// Xem JSON THÔ: chữ mono, bôi đen chọn được, nút copy toàn bộ.
class _JsonViewScreen extends StatelessWidget {
  final String title;
  final String text;
  const _JsonViewScreen({required this.title, required this.text});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(title, overflow: TextOverflow.ellipsis),
        actions: [
          IconButton(
            tooltip: tr('common.copy'),
            icon: const Icon(Icons.copy_outlined),
            onPressed: () {
              Clipboard.setData(ClipboardData(text: text));
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(content: Text(tr('common.copied'))),
              );
            },
          ),
        ],
      ),
      body: SelectionArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Text(
            text,
            style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 13),
          ),
        ),
      ),
    );
  }
}
