/// Mục **Thống kê** (tab Sản xuất): FPY, sản lượng theo ngày, Pareto mã bước
/// hỏng — đọc từ `GET /ate/stats` (server tính, app chỉ vẽ).
///
/// FPY ở đây là **First Pass Yield**: máy ĐẠT NGAY LẦN THỬ ĐẦU / máy đã thử.
/// Cố ý KHÔNG dùng "tỉ lệ hồ sơ PASS" — con số đó đẹp lên mỗi khi thao tác viên
/// bấm chạy lại, tức là nó thưởng cho đúng thứ cần phát hiện.
///
/// Chỉ HTTP → chạy cả trên bản web.
library;

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/ate_api.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';

class AteStatsScreen extends StatefulWidget {
  final AppSettings settings;

  /// Client thay thế — CHỈ dùng cho test + chụp ảnh tài liệu (bỏ trống = thật).
  final AteApi? api;

  const AteStatsScreen({super.key, required this.settings, this.api});

  @override
  State<AteStatsScreen> createState() => _AteStatsScreenState();
}

class _AteStatsScreenState extends State<AteStatsScreen> {
  late final AteApi _api = widget.api ?? AteApi.of(widget.settings);
  final TextEditingController _from = TextEditingController();
  final TextEditingController _to = TextEditingController();
  AteStats? _stats;
  bool _loading = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    // Mặc định 30 ngày gần nhất: mở màn ra là có số, không phải khai ngày trước.
    final now = DateTime.now();
    _from.text = _ymd(now.subtract(const Duration(days: 30)));
    _to.text = _ymd(now);
    _load();
  }

  @override
  void dispose() {
    _from.dispose();
    _to.dispose();
    super.dispose();
  }

  static String _ymd(DateTime d) =>
      '${d.year}-${d.month.toString().padLeft(2, '0')}-'
      '${d.day.toString().padLeft(2, '0')}';

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final s = await _api.stats(from: _from.text.trim(), to: _to.text.trim());
      if (!mounted) return;
      setState(() {
        _stats = s;
        _loading = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _loading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final s = _stats;
    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        Wrap(spacing: 12, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
          SizedBox(width: 150, child: _dateField(_from, tr('ate.from'))),
          SizedBox(width: 150, child: _dateField(_to, tr('ate.to'))),
          FilledButton.icon(
            onPressed: _loading ? null : _load,
            icon: const Icon(Icons.refresh),
            label: Text(tr('history.jsonRefresh')),
          ),
        ]),
        if (_loading)
          const Padding(
              padding: EdgeInsets.all(24),
              child: Center(child: CircularProgressIndicator())),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(top: 16),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        if (s != null && !_loading) ...[
          const SizedBox(height: 14),
          _kpiRow(s, cs),
          const SizedBox(height: 14),
          _paretoCard(s, cs),
          const SizedBox(height: 14),
          _byDayCard(s, cs),
        ],
      ],
    );
  }

  Widget _dateField(TextEditingController c, String label) => TextField(
        controller: c,
        decoration: InputDecoration(
          labelText: label,
          hintText: 'YYYY-MM-DD',
          border: const OutlineInputBorder(),
          isDense: true,
        ),
        style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 13),
        onSubmitted: (_) => _load(),
      );

  Widget _kpiRow(AteStats s, ColorScheme cs) {
    final sem = AppSemantic.of(context);
    return Wrap(spacing: 12, runSpacing: 12, children: [
      _kpi(
        tr('ate.fpy'),
        s.fpy == null ? '—' : '${(s.fpy! * 100).toStringAsFixed(1)}%',
        '${s.firstPass}/${s.machines} ${tr('ate.machines')}',
        s.fpy == null
            ? cs.onSurfaceVariant
            : s.fpy! >= 0.95
                ? sem.success
                : s.fpy! >= 0.85
                    ? sem.warning
                    : cs.error,
      ),
      _kpi(tr('ate.recordsTotal'), '${s.total}',
          '${tr('ate.pass')} ${s.passed} · ${tr('ate.fail')} ${s.failed}', cs.primary),
      _kpi(tr('ate.fail'), '${s.failed}', tr('ate.failHint'), cs.error),
      _kpi(tr('ate.aborted'), '${s.aborted}', tr('ate.abortedHint'), sem.warning),
    ]);
  }

  Widget _kpi(String label, String value, String sub, Color color) => SizedBox(
        width: 210,
        child: AppCard(
          padding: const EdgeInsets.all(16),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(label,
                style: TextStyle(
                    fontSize: 12.5,
                    color: Theme.of(context).colorScheme.onSurfaceVariant)),
            const SizedBox(height: 4),
            Text(value,
                style: TextStyle(
                    fontSize: 28,
                    fontWeight: FontWeight.w800,
                    color: color,
                    fontFeatures: const [FontFeature.tabularFigures()])),
            Text(sub, style: const TextStyle(fontSize: 11.5)),
          ]),
        ),
      );

  Widget _paretoCard(AteStats s, ColorScheme cs) {
    final maxN = s.pareto.isEmpty
        ? 1
        : s.pareto.map((e) => e.count).reduce((a, b) => a > b ? a : b);
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(tr('ate.pareto'),
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        Text(tr('ate.paretoHint'),
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
        const SizedBox(height: 10),
        if (s.pareto.isEmpty)
          Text(tr('ate.noFail'), style: TextStyle(color: cs.onSurfaceVariant))
        else
          for (final e in s.pareto)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Row(children: [
                SizedBox(
                  width: 90,
                  child: Text(e.code,
                      style: const TextStyle(
                          fontFamily: 'JetBrains Mono', fontSize: 12.5)),
                ),
                Expanded(
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(AppRadius.sm),
                    child: LinearProgressIndicator(
                      value: e.count / maxN,
                      minHeight: 14,
                      backgroundColor: cs.surfaceContainerHighest,
                      color: cs.error,
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                SizedBox(
                  width: 40,
                  child: Text('${e.count}',
                      textAlign: TextAlign.right,
                      style: const TextStyle(
                          fontFamily: 'JetBrains Mono',
                          fontSize: 12.5,
                          fontFeatures: [FontFeature.tabularFigures()])),
                ),
              ]),
            ),
      ]),
    );
  }

  Widget _byDayCard(AteStats s, ColorScheme cs) {
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(tr('ate.byDay'),
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        const SizedBox(height: 8),
        if (s.byDay.isEmpty)
          Text(tr('ate.noData'), style: TextStyle(color: cs.onSurfaceVariant))
        else
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: DataTable(
              columnSpacing: 28,
              headingRowHeight: 34,
              dataRowMinHeight: 30,
              dataRowMaxHeight: 38,
              columns: [
                DataColumn(label: Text(tr('ate.day'))),
                DataColumn(label: Text(tr('ate.pass'))),
                DataColumn(label: Text(tr('ate.fail'))),
                DataColumn(label: Text(tr('ate.aborted'))),
              ],
              rows: [
                for (final d in s.byDay)
                  DataRow(cells: [
                    DataCell(Text(d.day,
                        style: const TextStyle(
                            fontFamily: 'JetBrains Mono', fontSize: 12.5))),
                    DataCell(Text('${d.passed}')),
                    DataCell(Text('${d.failed}')),
                    DataCell(Text('${d.aborted}')),
                  ]),
              ],
            ),
          ),
      ]),
    );
  }
}
