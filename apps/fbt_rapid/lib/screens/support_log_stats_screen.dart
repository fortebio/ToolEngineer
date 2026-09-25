import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import 'support_inbox_screen.dart' show triageLabel;

/// Mục **Thống kê lỗi** (tab Chăm sóc KH, nhân sự) — gom mọi log CSKH gửi về
/// (`GET /logs/stats`) để thấy lỗi nào LẶP LẠI trên cả đội máy: dấu hiệu hay gặp,
/// máy gửi nhiều log, theo phiên bản firmware, số log theo ngày.
///
/// Dữ liệu là dấu hiệu bộ quét của APP (`log_triage.dart`) chụp lúc CSKH gửi —
/// không phải chẩn đoán của kỹ thuật; log gửi trước 2026-09-25 không có `fw`.
class SupportLogStatsScreen extends StatefulWidget {
  final AppSettings settings;

  /// Bấm một máy → mở hộp thư lọc theo máy đó (`SupportScreen` nối).
  final ValueChanged<String>? onOpenDevice;

  const SupportLogStatsScreen(
      {super.key, required this.settings, this.onOpenDevice});

  @override
  State<SupportLogStatsScreen> createState() => _SupportLogStatsScreenState();
}

class _SupportLogStatsScreenState extends State<SupportLogStatsScreen> {
  static const _periods = [7, 30, 90, 365, 0]; // 0 = mọi lúc

  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  int _days = 90;
  Map<String, dynamic>? _data;
  String? _error;
  bool _loading = false;
  bool _visible = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final v = TickerMode.valuesOf(context).enabled;
    if (v == _visible) return;
    _visible = v;
    // Mỗi lần mở mục là số mới — thống kê không cần chạy nền.
    if (v) {
      Future.microtask(() {
        if (mounted) _load();
      });
    }
  }

  Future<void> _load() async {
    if (_loading) return;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final d = await _api.logStats(days: _days);
      if (mounted) setState(() => _data = d);
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  String _periodLabel(int d) =>
      d == 0 ? tr('ls.all') : tr('ls.days').replaceFirst('{n}', '$d');

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final d = _data;
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
              for (final p in _periods)
                ChoiceChip(
                  label: Text(_periodLabel(p)),
                  selected: _days == p,
                  onSelected: (_) {
                    setState(() => _days = p);
                    _load();
                  },
                ),
              IconButton(
                tooltip: tr('common.refresh'),
                onPressed: _loading ? null : _load,
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
        ),
        if (_loading) const LinearProgressIndicator(minHeight: 2),
        Expanded(
          child: _error != null
              ? Center(
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                    Text(_error!, style: TextStyle(color: cs.error)),
                    const SizedBox(height: 8),
                    OutlinedButton(
                        onPressed: _load, child: Text(tr('common.retry'))),
                  ]),
                )
              : d == null
                  ? const Center(child: CircularProgressIndicator())
                  : _content(d),
        ),
      ],
    );
  }

  List<Map> _list(Map<String, dynamic> d, String k) =>
      (d[k] as List? ?? const []).whereType<Map>().toList();

  int _n(Object? v) => (v as num?)?.toInt() ?? 0;

  Widget _content(Map<String, dynamic> d) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final total = _n(d['total']);
    if (total == 0) {
      return Center(
          child: Text(tr('ls.empty'),
              style: TextStyle(color: cs.onSurfaceVariant)));
    }
    final counts = (d['counts'] as Map?) ?? const {};
    final signs = _list(d, 'signs');
    final devices = _list(d, 'devices');
    final fws = _list(d, 'firmware');
    final daily = _list(d, 'daily');
    final withErr = daily.fold<int>(0, (a, r) => a + _n(r['with_errors']));

    return ListView(
      padding: const EdgeInsets.only(bottom: 16),
      children: [
        Wrap(spacing: 10, runSpacing: 10, children: [
          _Kpi(label: tr('ls.total'), value: '$total'),
          _Kpi(
              label: tr('ls.pending'),
              value: '${_n(counts['new']) + _n(counts['working'])}',
              color: _n(counts['new']) > 0 ? cs.primary : null),
          _Kpi(
              label: tr('ls.withErrors'),
              value: '$withErr',
              sub: '${(100 * withErr / total).round()}%',
              color: withErr > 0 ? cs.error : null),
          _Kpi(label: tr('ls.devices'), value: '${_n(d['device_count'])}'),
          _Kpi(
              label: tr('ls.clean'),
              value: '${_n(d['clean'])}',
              color: sem.success),
        ]),
        const SizedBox(height: 16),
        _Section(
          title: tr('ls.signs'),
          hint: tr('ls.signsHint'),
          child: signs.isEmpty
              ? Text(tr('ls.noSigns'),
                  style: TextStyle(color: cs.onSurfaceVariant))
              : Column(children: [
                  for (final s in signs)
                    _BarRow(
                      label: triageLabel('${s['key']}'),
                      value: _n(s['logs']),
                      max: _n(signs.first['logs']),
                      color: s['level'] == 'error' ? cs.error : sem.warning,
                      trailing: tr('ls.signTrail')
                          .replaceFirst('{logs}', '${s['logs']}')
                          .replaceFirst('{dev}', '${s['devices']}'),
                    ),
                ]),
        ),
        _Section(
          title: tr('ls.byDevice'),
          hint: tr('ls.byDeviceHint'),
          child: Column(children: [
            for (final r in devices)
              ListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                title: Text('${r['device']}',
                    style: const TextStyle(fontWeight: FontWeight.w600)),
                subtitle: Text(tr('ls.deviceSub')
                    .replaceFirst('{logs}', '${r['logs']}')
                    .replaceFirst('{err}', '${r['with_errors']}')
                    .replaceFirst('{new}', '${r['new']}')
                    .replaceFirst('{last}', _day('${r['last']}'))),
                trailing: widget.onOpenDevice == null
                    ? null
                    : const Icon(Icons.chevron_right),
                onTap: widget.onOpenDevice == null
                    ? null
                    : () => widget.onOpenDevice!('${r['device']}'),
              ),
          ]),
        ),
        _Section(
          title: tr('ls.byFw'),
          hint: tr('ls.byFwHint'),
          child: Column(children: [
            for (final r in fws)
              _BarRow(
                label: '${r['fw']}'.isEmpty ? tr('ls.fwUnknown') : 'fw ${r['fw']}',
                value: _n(r['with_errors']),
                max: _n(r['logs']),
                color: cs.error,
                trailing: tr('ls.fwTrail')
                    .replaceFirst('{err}', '${r['with_errors']}')
                    .replaceFirst('{logs}', '${r['logs']}')
                    .replaceFirst('{dev}', '${r['devices']}'),
              ),
          ]),
        ),
        _Section(
          title: tr('ls.daily'),
          hint: tr('ls.dailyHint'),
          child: _DailyBars(rows: daily.reversed.take(60).toList().reversed.toList()),
        ),
      ],
    );
  }

  String _day(String iso) {
    final t = DateTime.tryParse(iso)?.toLocal();
    if (t == null) return '—';
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${p2(t.day)}/${p2(t.month)}';
  }
}

class _Kpi extends StatelessWidget {
  final String label;
  final String value;
  final String? sub;
  final Color? color;
  const _Kpi({required this.label, required this.value, this.sub, this.color});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Container(
      width: 150,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: cs.surfaceContainerHighest.withValues(alpha: 0.5),
        borderRadius: BorderRadius.circular(AppRadius.card),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(label,
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
        const SizedBox(height: 4),
        Row(crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic, children: [
          Text(value,
              style: TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.w700,
                  color: color ?? cs.onSurface,
                  fontFeatures: const [FontFeature.tabularFigures()])),
          if (sub != null) ...[
            const SizedBox(width: 6),
            Text(sub!, style: TextStyle(color: cs.onSurfaceVariant)),
          ],
        ]),
      ]),
    );
  }
}

class _Section extends StatelessWidget {
  final String title;
  final String hint;
  final Widget child;
  const _Section({required this.title, required this.hint, required this.child});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Padding(
      padding: const EdgeInsets.only(bottom: 20),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(title,
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        const SizedBox(height: 2),
        Text(hint, style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
        const SizedBox(height: 10),
        child,
      ]),
    );
  }
}

/// Một thanh ngang: nhãn · thanh tỉ lệ [value]/[max] · số bên phải.
class _BarRow extends StatelessWidget {
  final String label;
  final int value;
  final int max;
  final Color color;
  final String trailing;
  const _BarRow(
      {required this.label,
      required this.value,
      required this.max,
      required this.color,
      required this.trailing});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final frac = max <= 0 ? 0.0 : (value / max).clamp(0.0, 1.0);
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(children: [
        SizedBox(
          width: 220,
          child: Text(label, maxLines: 2, overflow: TextOverflow.ellipsis),
        ),
        Expanded(
          child: ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: LinearProgressIndicator(
              value: frac,
              minHeight: 10,
              color: color,
              backgroundColor: cs.surfaceContainerHighest,
            ),
          ),
        ),
        const SizedBox(width: 10),
        SizedBox(
          width: 150,
          child: Text(trailing,
              textAlign: TextAlign.right,
              style: TextStyle(
                  fontSize: 12,
                  color: cs.onSurfaceVariant,
                  fontFeatures: const [FontFeature.tabularFigures()])),
        ),
      ]),
    );
  }
}

/// Cột theo ngày (tối đa 60 ngày có log gần nhất): chiều cao = số log, phần đỏ =
/// số log có lỗi. Rê chuột/nhấn giữ để xem số.
class _DailyBars extends StatelessWidget {
  final List<Map> rows;
  const _DailyBars({required this.rows});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    if (rows.isEmpty) return const SizedBox.shrink();
    final max = rows
        .map((r) => (r['logs'] as num?)?.toInt() ?? 0)
        .fold<int>(1, (a, b) => b > a ? b : a);
    const h = 90.0;
    return SizedBox(
      height: h + 20,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          for (final r in rows)
            Expanded(
              child: Tooltip(
                message: tr('ls.dayTip')
                    .replaceFirst('{day}', '${r['day']}')
                    .replaceFirst('{logs}', '${r['logs']}')
                    .replaceFirst('{err}', '${r['with_errors']}'),
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 1),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.end,
                    children: [
                      Container(
                        height: h *
                            (((r['logs'] as num?) ?? 0) -
                                ((r['with_errors'] as num?) ?? 0)) /
                            max,
                        decoration: BoxDecoration(
                          color: cs.primary.withValues(alpha: 0.45),
                          borderRadius: const BorderRadius.vertical(
                              top: Radius.circular(2)),
                        ),
                      ),
                      Container(
                        height: h * ((r['with_errors'] as num?) ?? 0) / max,
                        color: cs.error,
                      ),
                    ],
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}
