/// Tab **Hiệu chuẩn** — ống chuẩn quang (Fluorescein) cho `eSensorcalib` của Rapid+.
///
/// Ba mục: **Lô pha** (pha dung dịch → chia ống → đo → xếp hạng → đóng bộ, chi tiết ở
/// `calib_batch_screen.dart`) · **Bộ ống** (túi zip trong tủ lạnh: cấp máy / thu hồi /
/// dùng hết / huỷ) · **Ngưỡng** (R² tối thiểu, LOD tối đa, dải slope, hạn dùng — chỉ
/// nhân sự kỹ thuật sửa, `canEditLimits`).
///
/// Thay Google Sheet + Apps Script bàn giao 2026-09: cùng thuật toán (mọi tổ hợp 1
/// ống/nồng độ, hồi quy tuyến tính, xếp R²/slope) nhưng thêm LOD theo WI DxD Hub
/// (3,3·SD blank/slope), gợi ý bộ KHÔNG trùng ống, và truy vết lô/ai/khi nào/máy nào.
/// Chỉ HTTP tới Engineer Server → chạy cả web.
library;

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/calib_api.dart';
import '../services/calib_label.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/format.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'calib_batch_screen.dart';
import 'calib_label_print.dart';

class CalibScreen extends StatefulWidget {
  final AppSettings settings;
  const CalibScreen({super.key, required this.settings});

  @override
  State<CalibScreen> createState() => _CalibScreenState();
}

class _CalibScreenState extends State<CalibScreen> {
  int _seg = 0;

  @override
  Widget build(BuildContext context) {
    final tabs = <AppTab>[
      AppTab(
        icon: Icons.science_outlined,
        label: tr('calib.batches'),
        page: CalibBatchesTab(settings: widget.settings),
      ),
      AppTab(
        icon: Icons.inventory_2_outlined,
        label: tr('calib.sets'),
        page: CalibSetsTab(settings: widget.settings),
      ),
      AppTab(
        icon: Icons.rule_outlined,
        label: tr('calib.limits'),
        page: CalibLimitsTab(settings: widget.settings),
      ),
    ];
    return AppTabScaffold(
      title: tr('nav.calib'),
      subtitle: tr('calib.hint'),
      index: _seg.clamp(0, tabs.length - 1),
      onChanged: (i) => setState(() => _seg = i),
      tabs: tabs,
      lazy: true,
    );
  }
}

// ---------------------------------------------------------------------------------------
// Dùng chung
// ---------------------------------------------------------------------------------------

String calibStatusLabel(String s) {
  switch (s) {
    case 'prep':
      return 'Đang pha';
    case 'measure':
      return 'Đang đo';
    case 'ranked':
      return 'Đã đóng bộ';
    case 'closed':
      return 'Đã đóng lô';
    case 'stored':
      return 'Trong tủ lạnh';
    case 'issued':
      return 'Đã cấp máy';
    case 'used':
      return 'Đã dùng hết';
    case 'discarded':
      return 'Đã huỷ';
    default:
      return s;
  }
}

Color calibStatusColor(BuildContext c, String s) {
  final sem = AppSemantic.of(c);
  final cs = Theme.of(c).colorScheme;
  switch (s) {
    case 'prep':
    case 'measure':
      return sem.info;
    case 'ranked':
    case 'stored':
      return sem.success;
    case 'issued':
      return sem.warning;
    case 'closed':
    case 'used':
      return cs.outline;
    case 'discarded':
      return cs.error;
    default:
      return cs.outline;
  }
}

class CalibStatusChip extends StatelessWidget {
  final String status;
  const CalibStatusChip(this.status, {super.key});

  @override
  Widget build(BuildContext context) {
    final c = calibStatusColor(context, status);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(AppRadius.pill),
        border: Border.all(color: c.withValues(alpha: 0.5)),
      ),
      child: Text(calibStatusLabel(status),
          style: TextStyle(color: c, fontSize: 12, fontWeight: FontWeight.w600)),
    );
  }
}

String calibFmtTs(String iso) {
  if (iso.isEmpty) return '—';
  final d = DateTime.tryParse(iso);
  return d == null ? iso : formatDateTime(d);
}

/// "300/3 · 200/2 · 100/3 · 0/3" — nồng độ giảm dần như trên nhãn túi.
String calibTubesLabel(Map<String, String> tubes) {
  final ks = tubes.keys.toList()
    ..sort((a, b) => (double.tryParse(b) ?? 0).compareTo(double.tryParse(a) ?? 0));
  return ks.map((k) => '$k/${tubes[k]}').join(' · ');
}

String calibNum(double? v, [int digits = 3]) => v == null ? '—' : v.toStringAsFixed(digits);

void calibSnack(BuildContext context, String msg, {bool error = false}) {
  if (!context.mounted) return;
  final cs = Theme.of(context).colorScheme;
  ScaffoldMessenger.of(context).showSnackBar(SnackBar(
    content: Text(msg),
    backgroundColor: error ? cs.error : null,
  ));
}

// ---------------------------------------------------------------------------------------
// Mục Lô pha
// ---------------------------------------------------------------------------------------

class CalibBatchesTab extends StatefulWidget {
  final AppSettings settings;
  const CalibBatchesTab({super.key, required this.settings});

  @override
  State<CalibBatchesTab> createState() => _CalibBatchesTabState();
}

class _CalibBatchesTabState extends State<CalibBatchesTab> {
  late final CalibApi _api = CalibApi.of(widget.settings);
  List<CalibBatchMeta> _items = const [];
  bool _loading = false;
  String? _error;
  String _filter = '';

  @override
  void initState() {
    super.initState();
    _reload();
  }

  Future<void> _reload() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final items = await _api.batches(status: _filter);
      if (!mounted) return;
      setState(() => _items = items);
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _open(String id) async {
    await Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => CalibBatchScreen(settings: widget.settings, batchId: id),
    ));
    _reload();
  }

  Future<void> _create() async {
    final lot = TextEditingController();
    final conc = TextEditingController(text: '52000');
    final tubes = TextEditingController(text: '10');
    final aliquot = TextEditingController(text: '25');
    final note = TextEditingController();
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Lô pha mới'),
        content: SizedBox(
          width: 420,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text(
                  'Nồng độ 300/200/100/0 nM theo firmware Rapid+. Bảng pha tính từ nồng độ stock '
                  '(C1·V1 = C2·V2): stock → 1000 nM (520 µL) → 300/200/100 nM (300 µL mỗi loại).',
                  style: TextStyle(fontSize: 12),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: lot,
                  decoration: const InputDecoration(
                      labelText: 'Lô Fluorescein (ThermoFisher F36915)', hintText: 'Lot No. trên ống'),
                ),
                const SizedBox(height: 8),
                TextField(
                  controller: conc,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(
                      labelText: 'Nồng độ stock (nM)',
                      helperText: 'Bàn giao ghi 52 mM nhưng bảng pha 52× chỉ đúng với 52 µM = 52 000 nM'),
                ),
                const SizedBox(height: 8),
                Row(children: [
                  Expanded(
                    child: TextField(
                      controller: tubes,
                      keyboardType: TextInputType.number,
                      decoration: const InputDecoration(labelText: 'Số ống / nồng độ'),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: TextField(
                      controller: aliquot,
                      keyboardType: TextInputType.number,
                      decoration: const InputDecoration(labelText: 'µL mỗi ống'),
                    ),
                  ),
                ]),
                const SizedBox(height: 8),
                TextField(
                  controller: note,
                  decoration: const InputDecoration(labelText: 'Ghi chú'),
                ),
              ],
            ),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Huỷ')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Tạo lô')),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    try {
      final doc = await _api.createBatch({
        'stock': {
          'lot': lot.text.trim(),
          if (double.tryParse(conc.text.trim()) != null) 'conc_nM': double.parse(conc.text.trim()),
        },
        if (int.tryParse(tubes.text.trim()) != null) 'tubes_per_conc': int.parse(tubes.text.trim()),
        if (double.tryParse(aliquot.text.trim()) != null) 'aliquot_ul': double.parse(aliquot.text.trim()),
        'note': note.text.trim(),
      }, by: SessionStore.username);
      if (!mounted) return;
      calibSnack(context, 'Đã tạo lô ${doc['id']}');
      await _open((doc['id'] ?? '').toString());
    } catch (e) {
      if (mounted) calibSnack(context, e.toString(), error: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final mobile = isMobileWidth(context);
    final padX = mobile ? 16.0 : 28.0;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: EdgeInsets.fromLTRB(padX, 4, padX, 8),
          child: Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              for (final f in const ['', 'prep', 'measure', 'ranked', 'closed'])
                ChoiceChip(
                  label: Text(f.isEmpty ? 'Tất cả' : calibStatusLabel(f)),
                  selected: _filter == f,
                  onSelected: (_) {
                    setState(() => _filter = f);
                    _reload();
                  },
                ),
              IconButton(
                tooltip: 'Tải lại',
                onPressed: _loading ? null : _reload,
                icon: const Icon(Icons.refresh),
              ),
              if (SessionStore.canWriteCalib)
                FilledButton.icon(
                  onPressed: _create,
                  icon: const Icon(Icons.add),
                  label: const Text('Lô mới'),
                ),
            ],
          ),
        ),
        if (_error != null)
          Padding(
            padding: EdgeInsets.symmetric(horizontal: padX),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        Expanded(
          child: _loading && _items.isEmpty
              ? const Center(child: CircularProgressIndicator())
              : _items.isEmpty
                  ? Center(
                      child: Text(_error == null ? 'Chưa có lô nào. Bấm "Lô mới" để bắt đầu.' : '',
                          style: TextStyle(color: cs.outline)))
                  : ListView.separated(
                      padding: EdgeInsets.fromLTRB(padX, 0, padX, 24),
                      itemCount: _items.length,
                      separatorBuilder: (_, __) => const SizedBox(height: 8),
                      itemBuilder: (_, i) => _batchTile(_items[i]),
                    ),
        ),
      ],
    );
  }

  /// Việc đang chờ ở lô, bằng lời thường — cùng thứ tự với 4 việc trong màn chi tiết.
  static String _batchNext(CalibBatchMeta m) {
    if (m.status == 'closed') return 'Đã đóng lô — chỉ xem';
    if (m.stepsDone < m.stepsTotal) {
      return 'Cần pha tiếp: còn ${m.stepsTotal - m.stepsDone}/${m.stepsTotal} bước';
    }
    if (m.readingsDone < m.readingsTotal) {
      return 'Cần đo tiếp: còn ${m.readingsTotal - m.readingsDone}/${m.readingsTotal} ống';
    }
    if (m.sets == 0) return 'Đo xong — cần chọn bộ ống đạt';
    return 'Xong: ${m.sets} bộ đã đóng';
  }

  Widget _batchTile(CalibBatchMeta m) {
    final cs = Theme.of(context).colorScheme;
    return Card(
      margin: EdgeInsets.zero,
      child: ListTile(
        onTap: () => _open(m.id),
        leading: const Icon(Icons.science_outlined),
        title: Row(children: [
          Text(m.id, style: const TextStyle(fontWeight: FontWeight.w700)),
          const SizedBox(width: 10),
          CalibStatusChip(m.status),
        ]),
        // Dòng 1 = việc đang chờ ở lô này (lời thường); dòng 2 = số liệu truy vết.
        subtitle: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(_batchNext(m), style: TextStyle(color: cs.onSurface, fontWeight: FontWeight.w500)),
            Text(
              [
                'pha ${m.stepsDone}/${m.stepsTotal}',
                'đo ${m.readingsDone}/${m.readingsTotal}',
                '${m.sets} bộ',
                if (m.stockLot.isNotEmpty) 'lot ${m.stockLot}',
                if (m.reader.isNotEmpty) 'máy ${m.reader}',
                '${m.createdBy.isEmpty ? '' : '${m.createdBy} · '}${calibFmtTs(m.createdAt)}',
              ].join(' · '),
              style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12),
            ),
          ],
        ),
        isThreeLine: true,
        trailing: const Icon(Icons.chevron_right),
      ),
    );
  }
}

// ---------------------------------------------------------------------------------------
// Mục Bộ ống
// ---------------------------------------------------------------------------------------

class CalibSetsTab extends StatefulWidget {
  final AppSettings settings;
  const CalibSetsTab({super.key, required this.settings});

  @override
  State<CalibSetsTab> createState() => _CalibSetsTabState();
}

class _CalibSetsTabState extends State<CalibSetsTab> {
  late final CalibApi _api = CalibApi.of(widget.settings);
  List<CalibSet> _items = const [];
  bool _loading = false;
  String? _error;
  String _filter = 'stored';
  final _device = TextEditingController();

  @override
  void initState() {
    super.initState();
    _reload();
  }

  @override
  void dispose() {
    _device.dispose();
    super.dispose();
  }

  Future<void> _reload() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final items = await _api.sets(status: _filter, device: _device.text.trim());
      if (!mounted) return;
      setState(() => _items = items);
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final mobile = isMobileWidth(context);
    final padX = mobile ? 16.0 : 28.0;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: EdgeInsets.fromLTRB(padX, 4, padX, 8),
          child: Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              for (final f in const ['stored', 'issued', 'used', 'discarded', ''])
                ChoiceChip(
                  label: Text(f.isEmpty ? 'Tất cả' : calibStatusLabel(f)),
                  selected: _filter == f,
                  onSelected: (_) {
                    setState(() => _filter = f);
                    _reload();
                  },
                ),
              SizedBox(
                width: 180,
                child: TextField(
                  controller: _device,
                  decoration: const InputDecoration(
                      labelText: 'Số máy', isDense: true, prefixIcon: Icon(Icons.search)),
                  onSubmitted: (_) => _reload(),
                ),
              ),
              IconButton(
                tooltip: 'Tải lại',
                onPressed: _loading ? null : _reload,
                icon: const Icon(Icons.refresh),
              ),
              // In nhãn cho CẢ danh sách đang lọc. Đặt cạnh bộ lọc chứ không
              // nhét vào từng thẻ: đóng xong một lô là in một tờ cho cả lô, chứ
              // không ai bấm in từng túi một.
              OutlinedButton.icon(
                onPressed: _items.isEmpty
                    ? null
                    : () => showCalibLabelSheetDialog(context, _items),
                icon: const Icon(Icons.qr_code_2, size: 18),
                label: const Text('In nhãn QR'),
              ),
            ],
          ),
        ),
        if (_error != null)
          Padding(
            padding: EdgeInsets.symmetric(horizontal: padX),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        Expanded(
          child: _loading && _items.isEmpty
              ? const Center(child: CircularProgressIndicator())
              : _items.isEmpty
                  ? Center(child: Text('Không có bộ ống nào.', style: TextStyle(color: cs.outline)))
                  : ListView.separated(
                      padding: EdgeInsets.fromLTRB(padX, 0, padX, 24),
                      itemCount: _items.length,
                      separatorBuilder: (_, __) => const SizedBox(height: 8),
                      itemBuilder: (_, i) => CalibSetTile(
                        set: _items[i],
                        api: _api,
                        onChanged: _reload,
                      ),
                    ),
        ),
      ],
    );
  }
}

/// Một bộ ống + các nút đổi trạng thái (dùng ở mục Bộ ống và trong màn lô).
class CalibSetTile extends StatelessWidget {
  final CalibSet set;
  final CalibApi api;
  final VoidCallback onChanged;
  const CalibSetTile({super.key, required this.set, required this.api, required this.onChanged});

  Future<void> _change(BuildContext context, String status) async {
    String device = set.device;
    String note = '';
    if (status == 'issued') {
      final dev = TextEditingController();
      final nt = TextEditingController();
      final ok = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: Text('Cấp bộ ${set.id} cho máy'),
          content: Column(mainAxisSize: MainAxisSize.min, children: [
            TextField(
              controller: dev,
              autofocus: true,
              decoration: const InputDecoration(labelText: 'Số máy nhận (SN)'),
            ),
            const SizedBox(height: 8),
            TextField(controller: nt, decoration: const InputDecoration(labelText: 'Ghi chú')),
          ]),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Huỷ')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Cấp')),
          ],
        ),
      );
      if (ok != true) return;
      device = dev.text.trim();
      note = nt.text.trim();
      if (device.isEmpty) {
        if (context.mounted) calibSnack(context, 'Phải ghi số máy nhận.', error: true);
        return;
      }
    } else if (status == 'discarded') {
      final ok = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: Text('Huỷ bộ ${set.id}?'),
          content: const Text('Bộ đã huỷ không dùng lại được. Ống trong bộ vẫn giữ trong hồ sơ lô.'),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Không')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Huỷ bộ')),
          ],
        ),
      );
      if (ok != true) return;
    }
    try {
      await api.setStatus(set.id, status, device: device, by: SessionStore.username, note: note);
      if (context.mounted) calibSnack(context, '${set.id} → ${calibStatusLabel(status)}');
      onChanged();
    } catch (e) {
      if (context.mounted) calibSnack(context, e.toString(), error: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final canWrite = SessionStore.canWriteCalib;
    final actions = <Widget>[];
    // Nhãn QR: KHÔNG gác `canWriteCalib` — in nhãn không đổi gì trong hồ sơ, và
    // người đi dán túi trong kho thường chỉ có quyền xem. Nhưng chỉ bộ còn dùng
    // được mới có nút: dán nhãn đẹp lên túi FAIL/đã huỷ là mời dùng nhầm một
    // vật chuẩn không đạt (xem `services/calib_label.dart`).
    if (calibPrintableSets([set]).isNotEmpty) {
      actions.add(OutlinedButton.icon(
        onPressed: () => showCalibLabelDialog(context, set),
        icon: const Icon(Icons.qr_code_2, size: 18),
        label: const Text('Nhãn QR'),
      ));
    }
    if (canWrite) {
      if (set.status == 'stored') {
        actions.add(FilledButton.tonalIcon(
          onPressed: () => _change(context, 'issued'),
          icon: const Icon(Icons.outbox_outlined, size: 18),
          label: const Text('Cấp máy'),
        ));
        actions.add(TextButton(
          onPressed: () => _change(context, 'discarded'),
          child: const Text('Huỷ'),
        ));
      } else if (set.status == 'issued') {
        actions.add(FilledButton.tonalIcon(
          onPressed: () => _change(context, 'used'),
          icon: const Icon(Icons.check, size: 18),
          label: const Text('Dùng hết'),
        ));
        actions.add(TextButton(
          onPressed: () => _change(context, 'stored'),
          child: const Text('Thu hồi'),
        ));
        actions.add(TextButton(
          onPressed: () => _change(context, 'discarded'),
          child: const Text('Huỷ'),
        ));
      }
    }
    final expired = set.expired && (set.status == 'stored' || set.status == 'issued');
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Wrap(
              spacing: 10,
              runSpacing: 6,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                SelectableText(set.id, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15)),
                CalibStatusChip(set.status),
                if (set.verdict == 'FAIL')
                  Text('FAIL theo ngưỡng ${set.limitsVer}',
                      style: TextStyle(color: cs.error, fontSize: 12, fontWeight: FontWeight.w600)),
                if (expired)
                  Text('HẾT HẠN ${set.expiresAt}',
                      style: TextStyle(color: sem.warning, fontSize: 12, fontWeight: FontWeight.w700)),
              ],
            ),
            const SizedBox(height: 6),
            Text('Ống: ${calibTubesLabel(set.tubes)}',
                style: const TextStyle(fontFamily: 'monospace', fontSize: 13)),
            Text(
              'slope ${calibNum(set.slope)} · intercept ${calibNum(set.intercept, 1)} · '
              'R² ${calibNum(set.r2, 5)} · LOD ${set.lod == null ? '—' : '${calibNum(set.lod, 1)} nM'}',
              style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12),
            ),
            Text(
              [
                if (set.device.isNotEmpty) 'máy ${set.device}',
                if (set.expiresAt.isNotEmpty) 'hạn ${set.expiresAt}',
                'đóng ${calibFmtTs(set.createdAt)}${set.createdBy.isEmpty ? '' : ' bởi ${set.createdBy}'}',
              ].join(' · '),
              style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12),
            ),
            if (actions.isNotEmpty) ...[
              const SizedBox(height: 8),
              Wrap(spacing: 6, children: actions),
            ],
          ],
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------------------
// Mục Ngưỡng
// ---------------------------------------------------------------------------------------

class CalibLimitsTab extends StatefulWidget {
  final AppSettings settings;
  const CalibLimitsTab({super.key, required this.settings});

  @override
  State<CalibLimitsTab> createState() => _CalibLimitsTabState();
}

class _CalibLimitsTabState extends State<CalibLimitsTab> {
  late final CalibApi _api = CalibApi.of(widget.settings);
  final _version = TextEditingController();
  final _r2 = TextEditingController();
  final _lod = TextEditingController();
  final _slopeMin = TextEditingController();
  final _slopeMax = TextEditingController();
  final _shelf = TextEditingController();
  String _source = '';
  String _updated = '';
  bool _busy = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _reload();
  }

  @override
  void dispose() {
    for (final c in [_version, _r2, _lod, _slopeMin, _slopeMax, _shelf]) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> _reload() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final j = await _api.limits();
      if (!mounted) return;
      setState(() {
        _version.text = (j['version'] ?? '').toString();
        _r2.text = (j['r2_min'] ?? '').toString();
        _lod.text = (j['lod_max'] ?? '').toString();
        _slopeMin.text = (j['slope_min'] ?? '').toString();
        _slopeMax.text = (j['slope_max'] ?? '').toString();
        _shelf.text = (j['shelf_days'] ?? '').toString();
        _source = (j['source'] ?? '').toString();
        final at = (j['updated_at'] ?? '').toString();
        final by = (j['updated_by'] ?? '').toString();
        _updated = at.isEmpty ? '' : '${calibFmtTs(at)}${by.isEmpty ? '' : ' bởi $by'}';
      });
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _save() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      await _api.saveLimits({
        'version': _version.text.trim(),
        'r2_min': double.tryParse(_r2.text.trim()) ?? 0,
        'lod_max': double.tryParse(_lod.text.trim()) ?? 0,
        'slope_min': double.tryParse(_slopeMin.text.trim()) ?? 0,
        'slope_max': double.tryParse(_slopeMax.text.trim()) ?? 0,
        'shelf_days': int.tryParse(_shelf.text.trim()) ?? 0,
      }, by: SessionStore.username);
      if (mounted) calibSnack(context, 'Đã lưu ngưỡng ${_version.text.trim()}');
      await _reload();
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final mobile = isMobileWidth(context);
    final padX = mobile ? 16.0 : 28.0;
    final canEdit = SessionStore.canEditLimits;
    Widget num(TextEditingController c, String label, String helper) => TextField(
          controller: c,
          enabled: canEdit && !_busy,
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          decoration: InputDecoration(labelText: label, helperText: helper, helperMaxLines: 3),
        );
    return SingleChildScrollView(
      padding: EdgeInsets.fromLTRB(padX, 4, padX, 24),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 560),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Ngưỡng PASS cho tổ hợp ống. WI gốc (DxD Hub 07/2024) chấp nhận máy khi R² > 0,95 và '
              'LOD < 20 nM (LOD = 3,3·SD blank / slope); chọn ống chuẩn siết R² hơn vì bộ ống là '
              'vật chuẩn cho cả fleet. Một version = một nội dung — sửa số thì đổi version.',
              style: TextStyle(color: cs.onSurfaceVariant, fontSize: 13),
            ),
            const SizedBox(height: 6),
            Text(
              'Đang áp dụng: ${_source == 'file' ? 'bộ đã lưu' : 'mặc định'}'
              '${_updated.isEmpty ? '' : ' · $_updated'}'
              '${canEdit ? '' : ' · chỉ nhân sự kỹ thuật sửa được'}',
              style: TextStyle(color: cs.outline, fontSize: 12),
            ),
            const SizedBox(height: 14),
            TextField(
              controller: _version,
              enabled: canEdit && !_busy,
              decoration: const InputDecoration(labelText: 'Version bộ ngưỡng', hintText: 'vd 2026-09-v1'),
            ),
            const SizedBox(height: 10),
            num(_r2, 'R² tối thiểu', 'Tổ hợp dưới mức này = FAIL (0..1).'),
            const SizedBox(height: 10),
            num(_lod, 'LOD tối đa (nM)', '3,3·SD(ống 0 nM)/slope. 0 = không xét.'),
            const SizedBox(height: 10),
            Row(children: [
              Expanded(child: num(_slopeMin, 'Slope tối thiểu', '0 = không giới hạn')),
              const SizedBox(width: 10),
              Expanded(child: num(_slopeMax, 'Slope tối đa', '0 = không giới hạn')),
            ]),
            const SizedBox(height: 10),
            num(_shelf, 'Hạn dùng bộ ống (ngày)', 'WI: tín hiệu tăng ~20 %/12 tháng do bay hơi. 0 = không đặt hạn.'),
            const SizedBox(height: 14),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Text(_error!, style: TextStyle(color: cs.error)),
              ),
            Row(children: [
              OutlinedButton.icon(
                onPressed: _busy ? null : _reload,
                icon: const Icon(Icons.refresh),
                label: const Text('Tải lại'),
              ),
              const SizedBox(width: 10),
              if (canEdit)
                FilledButton.icon(
                  onPressed: _busy ? null : _save,
                  icon: const Icon(Icons.save_outlined),
                  label: const Text('Lưu ngưỡng'),
                ),
            ]),
          ],
        ),
      ),
    );
  }
}
