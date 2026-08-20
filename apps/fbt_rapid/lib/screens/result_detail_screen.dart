import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

import '../models/test_result.dart';
import '../services/fbt_api.dart' show SensorError;
import '../services/result_export.dart';
import '../services/session_store.dart';
import '../util/chart_capture.dart';
import '../util/curve_processing.dart';
import '../util/format.dart';
import '../util/i18n.dart';
import '../widgets/ct_chart.dart';
import '../widgets/result_badge.dart';
import 'curve_compare_screen.dart';

class ResultDetailScreen extends StatefulWidget {
  final TestResult result;
  final int readingIntervalSec;

  /// Lỗi cảm biến máy báo về trong lần đo này. Rỗng = máy chạy sạch → KHÔNG hiện bảng.
  ///
  /// Truyền VÀO chứ không tự gọi API: màn này dùng chung cho **cả 4 nguồn** (Google Drive,
  /// RAPID ERP, Engineer Server, máy trong LAN) mà chỉ Engineer Server có endpoint lỗi.
  /// Nhét `FbtApi` vào đây là trói một màn dùng chung vào một nguồn.
  final List<SensorError> errors;

  const ResultDetailScreen({
    super.key,
    required this.result,
    required this.readingIntervalSec,
    this.errors = const [],
  });

  @override
  State<ResultDetailScreen> createState() => _ResultDetailScreenState();
}

class _ResultDetailScreenState extends State<ResultDetailScreen> {
  late final Set<int> _visible =
      widget.result.slots.map((s) => s.index).toSet();
  CurveView _view = CurveView.baselineSmoothed;
  bool _saving = false;

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? kErrorSnackBg : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  /// Hộp thoại **checklist** chọn loại đồ thị để lưu, rồi chụp + lưu.
  Future<void> _saveDialog() async {
    final r = widget.result;
    final sel = <CurveView>{...CurveView.values};
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => StatefulBuilder(
        builder: (c, setLocal) => AlertDialog(
          title: const Text('Lưu đồ thị'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text('Chọn loại đồ thị để lưu (PNG):'),
              const SizedBox(height: 4),
              for (final v in CurveView.values)
                CheckboxListTile(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  controlAffinity: ListTileControlAffinity.leading,
                  value: sel.contains(v),
                  title: Text(v.label),
                  onChanged: (on) =>
                      setLocal(() => on == true ? sel.add(v) : sel.remove(v)),
                ),
              const Divider(),
              Text(
                'Kèm data.json · slot đang chọn: '
                '${_visible.length}/${r.slots.length}',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(c, false),
                child: const Text('Hủy')),
            FilledButton(
                onPressed: sel.isEmpty ? null : () => Navigator.pop(c, true),
                child: const Text('Lưu')),
          ],
        ),
      ),
    );
    if (ok != true || sel.isEmpty) return;

    setState(() => _saving = true);
    try {
      final pngs = await _captureViews(sel);
      if (pngs.isEmpty) {
        _snack('Chưa chụp được đồ thị.', error: true);
        return;
      }
      final dir = await ResultExport.saveRun(r, pngs);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Đã lưu ${pngs.length} đồ thị + data.json'),
        duration: const Duration(seconds: 1),
        // Web: saveRun trả '' (tải xuống Downloads) → không có thư mục để mở.
        action: dir.isEmpty
            ? null
            : SnackBarAction(
                label: 'Mở',
                onPressed: () => ResultExport.revealInExplorer(dir),
              ),
      ));
    } catch (e) {
      _snack('Lỗi lưu: $e', error: true);
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  /// Chụp các loại đồ thị đã chọn **ngoài màn hình** (không nhấp nháy UI).
  Future<Map<CurveView, Uint8List>> _captureViews(Set<CurveView> views) async {
    final r = widget.result;
    final ordered = CurveView.values.where(views.contains).toList();
    // Chiều cao đồ thị khi chụp = xấp xỉ chiều cao bảng "Kết quả bệnh"
    // (bảng xếp 2 cột; mỗi hàng card ~90px + tiêu đề). Cho đồ thị cao bằng bảng
    // để ảnh không chừa khoảng trắng phía dưới hình đồ thị.
    final resultRows = ((r.slots.length + 1) ~/ 2).clamp(1, 5);
    final captureChartH = 44.0 + resultRows * 90.0;
    final keys = {for (final v in ordered) v: GlobalKey()};
    final overlay = Overlay.of(context);
    final entry = OverlayEntry(
      builder: (_) => Positioned(
        left: -10000, // ngoài màn hình nhưng vẫn được vẽ → chụp được
        top: 0,
        child: Material(
          type: MaterialType.transparency,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              for (final v in ordered)
                RepaintBoundary(
                  key: keys[v],
                  // Theme SÁNG ghim cứng cho vùng chụp. Nền đã là `Colors.white`
                  // từ trước, nhưng chữ (tiêu đề, chú giải, nhãn trục) lấy màu
                  // theo theme đang chạy → ở chế độ tối là gần-trắng ⇒ trắng
                  // trên trắng, ảnh xuất ra MẤT hết chữ mà không báo gì.
                  child: AppExportTheme(
                    // `Builder` để lấy được context NẰM TRONG vùng chụp. Không
                    // có nó thì `Theme.of(context)` bên dưới vẫn trỏ về context
                    // của State (ở NGOÀI) → hai tiêu đề vẫn mang màu theme tối
                    // trên nền trắng, dù AppExportTheme đã lo phần còn lại.
                    child: Builder(builder: (ctxChup) => Container(
                    width: 1240,
                    color: Colors.white,
                    padding: const EdgeInsets.all(12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text('${v.label} — ${r.deviceId}',
                            style: Theme.of(ctxChup).textTheme.titleMedium),
                        const SizedBox(height: 4),
                        _MiniLegend(slots: r.slots, visible: _visible),
                        const SizedBox(height: 8),
                        // Đồ thị BÊN TRÁI + bảng "Kết quả bệnh" BÊN PHẢI.
                        // Đồ thị cao = captureChartH (≈ chiều cao bảng kết quả)
                        // → lấp hết khoảng trắng phía dưới hình đồ thị.
                        Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(
                              child: SizedBox(
                                height: captureChartH,
                                child: CtChart(
                                  slots: r.slots,
                                  visibleIndexes: _visible,
                                  readingIntervalSec: widget.readingIntervalSec,
                                  curveSelector: (s) => computeCurveSeries(v,
                                      s.curve, s.slope, widget.readingIntervalSec),
                                ),
                              ),
                            ),
                            const SizedBox(width: 16),
                            SizedBox(
                              width: 340,
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Text('Kết quả',
                                      style: Theme.of(ctxChup)
                                          .textTheme
                                          .titleMedium),
                                  const SizedBox(height: 6),
                                  _SlotGrid(
                                    slots: r.slots,
                                    visible: _visible,
                                    onToggle: (_) {},
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  )),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
    overlay.insert(entry);
    await WidgetsBinding.instance.endOfFrame;
    await Future.delayed(const Duration(milliseconds: 500)); // chờ fl_chart vẽ
    final pngs = <CurveView, Uint8List>{};
    for (final v in ordered) {
      // pixelRatio 4.0 = ảnh lưu to gấp đôi mặc định (2.0) cho rõ nét.
      final png = await captureBoundaryPng(keys[v]!, pixelRatio: 4.0);
      if (png != null) pngs[v] = png;
    }
    entry.remove();
    return pngs;
  }

  void _toggle(int i) => setState(() {
        if (_visible.contains(i)) {
          _visible.remove(i);
        } else {
          _visible.add(i);
        }
      });

  Widget _resultsHeader(BuildContext context) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        Text('Kết quả bệnh', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(width: 8),
        Expanded(
          child: Text('(bấm slot để ẩn/hiện đường)',
              style: Theme.of(context).textTheme.bodySmall,
              overflow: TextOverflow.ellipsis),
        ),
      ],
    );
  }

  /// Panel đồ thị (bên trái): tiêu đề + nút Hiện/Ẩn + chọn loại đường + đồ thị.
  Widget _chartPanel(BuildContext context, TestResult r) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text('Đồ thị', style: Theme.of(context).textTheme.titleLarge),
            Wrap(
              spacing: 8,
              children: [
                TextButton(
                  onPressed: () => setState(
                      () => _visible.addAll(r.slots.map((s) => s.index))),
                  child: const Text('Hiện tất cả'),
                ),
                TextButton(
                  onPressed: () => setState(() => _visible.clear()),
                  child: const Text('Ẩn tất cả'),
                ),
              ],
            ),
          ],
        ),
        if (r.curvesAreRaw) ...[
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: SegmentedButton<CurveView>(
              segments: CurveView.values
                  .map((v) => ButtonSegment(value: v, label: Text(v.short)))
                  .toList(),
              selected: {_view},
              showSelectedIcon: false,
              onSelectionChanged: (s) => setState(() => _view = s.first),
            ),
          ),
          const SizedBox(height: 2),
          Row(
            children: [
              Expanded(
                child: Text('${_view.label}',
                    style: Theme.of(context).textTheme.bodySmall,
                    overflow: TextOverflow.ellipsis),
              ),
              TextButton.icon(
                onPressed: () => Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (_) => CurveCompareScreen(
                      result: r,
                      readingIntervalSec: widget.readingIntervalSec,
                    ),
                  ),
                ),
                icon: const Icon(Icons.grid_view, size: 18),
                label: const Text('Xem cả 4'),
              ),
            ],
          ),
        ],
        const SizedBox(height: 8),
        // "Zone" bao đồ thị CT: khung viền bo góc, nền trắng, có đệm trong.
        Expanded(
          child: Container(
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.surface,
              border: Border.all(
                  color: Theme.of(context).colorScheme.outlineVariant),
              borderRadius: BorderRadius.circular(8),
            ),
            padding: const EdgeInsets.fromLTRB(8, 12, 12, 8),
            child: CtChart(
              slots: r.slots,
              visibleIndexes: _visible,
              readingIntervalSec: widget.readingIntervalSec,
              curveSelector: r.curvesAreRaw
                  ? (s) => computeCurveSeries(
                      _view, s.curve, s.slope, widget.readingIntervalSec)
                  : null,
            ),
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final r = widget.result;
    final machineLine =
        'Máy: ${r.deviceId.isEmpty ? "(không rõ)" : r.deviceId}'
        '${r.version.isNotEmpty ? "  ·  FW: ${r.version}" : ""}';
    return Scaffold(
      appBar: AppBar(
        toolbarHeight: 64,
        // Thông tin máy + FW nằm ngay trên cùng, cùng hàng với nút lưu đồ thị.
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text('Kết quả ${formatDateTime(r.timestamp)}',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                    fontSize: 16, fontWeight: FontWeight.w600)),
            Text(machineLine,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 12)),
          ],
        ),
        actions: [
          // Lưu đồ thị: cho mọi user đã đăng nhập (về thư mục đã chỉ định).
          if (r.curvesAreRaw && SessionStore.canSaveCharts)
            IconButton(
              tooltip: 'Lưu đồ thị (chọn loại)',
              onPressed: _saving ? null : _saveDialog,
              icon: _saving
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.save_alt),
            ),
        ],
      ),
      body: LayoutBuilder(builder: (context, c) {
              final wide = c.maxWidth >= 720;
              // Bên phải: Kết quả bệnh (mỗi slot kiêm nút bật/tắt đường).
              final results = Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _resultsHeader(context),
                  const SizedBox(height: 8),
                  if (wide)
                    Expanded(
                      child: SingleChildScrollView(
                        child: _SlotGrid(
                            slots: r.slots,
                            visible: _visible,
                            onToggle: _toggle),
                      ),
                    )
                  else
                    _SlotGrid(
                        slots: r.slots, visible: _visible, onToggle: _toggle),
                  // Bảng mã lỗi nằm NGAY DƯỚI kết quả, cùng cột với nó — lỗi cảm biến
                  // giải thích chính mấy ô kết quả phía trên (slot mất dữ liệu, quá tối),
                  // nên đọc rời hai chỗ là mất mối liên hệ.
                  if (widget.errors.isNotEmpty) ...[
                    const SizedBox(height: 16),
                    _ErrorTable(errors: widget.errors),
                  ],
                ],
              );

              if (wide) {
                // Đồ thị BÊN TRÁI, kết quả bệnh BÊN PHẢI.
                return Padding(
                  padding: const EdgeInsets.fromLTRB(16, 4, 16, 16),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Expanded(flex: 3, child: _chartPanel(context, r)),
                      const SizedBox(width: 32),
                      SizedBox(width: 360, child: results),
                    ],
                  ),
                );
              }
              // Cửa sổ hẹp: xếp dọc (đồ thị trên, kết quả dưới).
              return ListView(
                padding: const EdgeInsets.fromLTRB(16, 4, 16, 16),
                children: [
                  SizedBox(height: 380, child: _chartPanel(context, r)),
                  const SizedBox(height: 16),
                  results,
                ],
              );
      }),
    );
  }
}

/// Lưới "Kết quả bệnh": mỗi ô tô màu đường của slot + **bấm để bật/tắt** đường
/// đó trong đồ thị (gộp luôn chức năng của chú thích cũ). Ô đang ẩn bị mờ.
class _SlotGrid extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visible;
  final void Function(int index) onToggle;
  const _SlotGrid({
    required this.slots,
    required this.visible,
    required this.onToggle,
  });

  @override
  Widget build(BuildContext context) {
    // Cột TRÁI = slot 1–5, cột PHẢI = slot 6–10 (xếp dọc theo cột).
    final left = slots.take(5).toList();
    final right = slots.skip(5).toList();
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(child: _col(context, left)),
        const SizedBox(width: 8),
        Expanded(child: _col(context, right)),
      ],
    );
  }

  Widget _col(BuildContext context, List<SlotResult> items) {
    return Column(
      children: [
        for (final s in items)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: _card(context, s),
          ),
      ],
    );
  }

  Widget _card(BuildContext context, SlotResult s) {
    // Màu = đúng màu đường của slot trong đồ thị CT (để đối chiếu nhanh).
    final color = kSlotColors[(s.index - 1) % kSlotColors.length];
    final cs = Theme.of(context).colorScheme;
    final on = visible.contains(s.index);
    return Card(
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () => onToggle(s.index), // bấm = bật/tắt đường trong đồ thị
        child: Opacity(
          opacity: on ? 1.0 : 0.4,
          child: Container(
            decoration: BoxDecoration(
              border: Border(left: BorderSide(color: color, width: 5)),
            ),
            padding: const EdgeInsets.all(8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Row(
                  children: [
                    CircleAvatar(backgroundColor: color, radius: 6),
                    const SizedBox(width: 6),
                    // Tên bệnh (firmware v2.4.3+) NẰM NGANG cạnh số slot:
                    // "Slot 1 - PCV". Chưa đặt tên thì "N/A" (đúng chữ máy ghi)
                    // chứ không bỏ trống — 10 ô phải đọc giống hệt nhau.
                    // Tên dài (ASF I177L) cắt bằng ellipsis: ô trong lưới 2 cột
                    // chỉ rộng ~150px, để tự xuống dòng là ô cao thấp so le.
                    Expanded(
                      child: Text.rich(
                        TextSpan(
                          text: 'Slot ${s.index} - ',
                          children: [
                            TextSpan(
                              text: s.name.isEmpty ? 'N/A' : s.name,
                              style: TextStyle(
                                  color: s.name.isEmpty
                                      ? cs.onSurfaceVariant
                                      : cs.primary),
                            ),
                          ],
                        ),
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(fontWeight: FontWeight.bold),
                      ),
                    ),
                    const SizedBox(width: 4),
                    Icon(on ? Icons.visibility : Icons.visibility_off,
                        size: 15, color: cs.onSurfaceVariant),
                  ],
                ),
                const SizedBox(height: 4),
                ResultBadge(classification: s.classification, dense: true),
                const SizedBox(height: 4),
                Text('CT: ${formatCt(s.ct)}'),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Chú thích gọn (chấm màu + "Slot n · tên bệnh") cho slot đang hiện — để ảnh
/// PNG tự hiểu.
class _MiniLegend extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visible;
  const _MiniLegend({required this.slots, required this.visible});

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 8,
      runSpacing: 2,
      children: [
        for (final s in slots)
          if (visible.contains(s.index))
            Row(mainAxisSize: MainAxisSize.min, children: [
              CircleAvatar(
                  backgroundColor:
                      kSlotColors[(s.index - 1) % kSlotColors.length],
                  radius: 4),
              const SizedBox(width: 3),
              Text(s.label, style: const TextStyle(fontSize: 10)),
            ]),
      ],
    );
  }
}

/// Bảng **mã lỗi cảm biến** máy tự phát hiện và gửi về.
///
/// Chỉ dựng khi CÓ lỗi — khung rỗng kèm chữ "không có lỗi" là cấp giấy chứng nhận sạch cho
/// một lần đo mà ta chỉ biết là *không nghe thấy gì*, và gần cả fleet hiện chưa gửi lỗi về
/// server (trước firmware 2026-08-05 lỗi chỉ tới Google Sheet).
class _ErrorTable extends StatelessWidget {
  final List<SensorError> errors;
  const _ErrorTable({required this.errors});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final head = TextStyle(
        fontSize: 11.5, fontWeight: FontWeight.w600, color: cs.onSurfaceVariant);

    return Container(
      decoration: BoxDecoration(
        color: cs.error.withValues(alpha: 0.06),
        borderRadius: BorderRadius.circular(AppRadius.base),
        border: Border.all(color: cs.error.withValues(alpha: 0.35)),
      ),
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Icon(Icons.error_outline, size: 17, color: cs.error),
            const SizedBox(width: 6),
            Text(tr('rd.errTitle').replaceFirst('{n}', '${errors.length}'),
                style: TextStyle(
                    fontSize: 13, fontWeight: FontWeight.w700, color: cs.error)),
          ]),
          const SizedBox(height: 8),
          Row(children: [
            SizedBox(width: 58, child: Text(tr('rd.errSlot'), style: head)),
            SizedBox(width: 50, child: Text(tr('rd.errCode'), style: head)),
            Expanded(child: Text(tr('rd.errMsg'), style: head)),
          ]),
          const Divider(height: 10),
          for (final e in errors)
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  SizedBox(
                    width: 58,
                    child: Text(e.slot.isEmpty ? '—' : e.slot,
                        style: TextStyle(fontSize: 12, color: cs.onSurface)),
                  ),
                  SizedBox(
                    width: 50,
                    // Mã in bằng font MONO và giữ nguyên 4 chữ số: đây đúng là mã hiện trên
                    // màn TFT của máy, kỹ sư đọc chéo hai nơi.
                    child: Text(e.code,
                        style: TextStyle(
                            fontFamily: 'JetBrains Mono',
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: cs.error)),
                  ),
                  Expanded(
                    child: Text(e.message,
                        style: TextStyle(fontSize: 12, color: cs.onSurface)),
                  ),
                ],
              ),
            ),
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Text(tr('rd.errNote'),
                style: TextStyle(
                    fontSize: 11, height: 1.35, color: cs.onSurfaceVariant)),
          ),
        ],
      ),
    );
  }
}
