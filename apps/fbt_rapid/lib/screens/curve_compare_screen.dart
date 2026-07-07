import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../models/test_result.dart';
import '../services/result_export.dart';
import '../services/session_store.dart';
import '../util/chart_capture.dart';
import '../util/curve_processing.dart';
import '../util/format.dart';
import '../widgets/ct_chart.dart';

/// Hiển thị **cả 4 dạng đồ thị** (Raw / Calibrate / Baseline / SG) cùng lúc,
/// dùng chung 1 bộ chọn slot. Có nút **Lưu** → xuất 4 PNG + data.json vào
/// folder "MãMáy_Ngày_Firmware".
class CurveCompareScreen extends StatefulWidget {
  final TestResult result;
  final int readingIntervalSec;

  const CurveCompareScreen({
    super.key,
    required this.result,
    required this.readingIntervalSec,
  });

  @override
  State<CurveCompareScreen> createState() => _CurveCompareScreenState();
}

class _CurveCompareScreenState extends State<CurveCompareScreen> {
  late final Set<int> _visible =
      widget.result.slots.map((s) => s.index).toSet();
  final Map<CurveView, GlobalKey> _keys = {};

  GlobalKey _keyFor(CurveView v) => _keys.putIfAbsent(v, () => GlobalKey());

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? Colors.red.shade700 : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  Future<void> _saveAll() async {
    final pngs = <CurveView, Uint8List>{};
    for (final v in CurveView.values) {
      // pixelRatio 4.0 = ảnh lưu to gấp đôi mặc định (2.0) cho rõ nét.
      final png = await captureBoundaryPng(_keyFor(v), pixelRatio: 4.0);
      if (png != null) pngs[v] = png;
    }
    if (pngs.isEmpty) {
      _snack('Chưa chụp được đồ thị.', error: true);
      return;
    }
    try {
      final dir = await ResultExport.saveRun(widget.result, pngs);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Đã lưu ${pngs.length} đồ thị + data.json'),
        duration: const Duration(seconds: 1),
        action: SnackBarAction(
          label: 'Mở',
          onPressed: () => ResultExport.revealInExplorer(dir),
        ),
      ));
    } catch (e) {
      _snack('Lỗi lưu: $e', error: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    final r = widget.result;
    return Scaffold(
      appBar: AppBar(
        title: Text('4 đồ thị — ${formatDateTime(r.timestamp)}'),
        actions: [
          // Lưu đồ thị: cho mọi user đã đăng nhập.
          if (SessionStore.canSaveCharts)
            IconButton(
              tooltip: 'Lưu 4 đồ thị + dữ liệu',
              onPressed: _saveAll,
              icon: const Icon(Icons.save_alt),
            ),
          TextButton(
            onPressed: () =>
                setState(() => _visible.addAll(r.slots.map((s) => s.index))),
            child: const Text('Hiện tất cả'),
          ),
          TextButton(
            onPressed: () => setState(() => _visible.clear()),
            child: const Text('Ẩn tất cả'),
          ),
        ],
      ),
      body: LayoutBuilder(builder: (context, c) {
        final twoCol = c.maxWidth >= 900;
        final chartW = twoCol ? (c.maxWidth - 32 - 16) / 2 : c.maxWidth - 32;
        return SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _SlotLegend(
                slots: r.slots,
                visible: _visible,
                onToggle: (i) => setState(() {
                  if (_visible.contains(i)) {
                    _visible.remove(i);
                  } else {
                    _visible.add(i);
                  }
                }),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 16,
                runSpacing: 16,
                children: [
                  for (final v in CurveView.values)
                    SizedBox(
                      width: chartW,
                      child: Card(
                        clipBehavior: Clip.antiAlias,
                        // Vùng chụp PNG (nền trắng + tiêu đề + chú thích + đồ thị).
                        child: RepaintBoundary(
                          key: _keyFor(v),
                          child: Container(
                            color: Theme.of(context).colorScheme.surface,
                            padding: const EdgeInsets.all(12),
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text('${v.label} — ${r.deviceId}',
                                    style:
                                        Theme.of(context).textTheme.titleMedium),
                                const SizedBox(height: 4),
                                _MiniLegend(slots: r.slots, visible: _visible),
                                const SizedBox(height: 8),
                                SizedBox(
                                  height: 260,
                                  child: CtChart(
                                    slots: r.slots,
                                    visibleIndexes: _visible,
                                    readingIntervalSec:
                                        widget.readingIntervalSec,
                                    curveSelector: (s) => computeCurveSeries(
                                      v,
                                      s.curve,
                                      s.slope,
                                      widget.readingIntervalSec,
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              Text(
                'Pipeline: raw → ÷slope → baseline → Savitzky–Golay · tham số mặc định firmware.',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
        );
      }),
    );
  }
}

/// Chú thích gọn (chấm màu + Slot n) cho các slot đang hiện — để PNG tự hiểu.
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
              Text('Slot ${s.index}', style: const TextStyle(fontSize: 10)),
            ]),
      ],
    );
  }
}

class _SlotLegend extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visible;
  final void Function(int index) onToggle;

  const _SlotLegend({
    required this.slots,
    required this.visible,
    required this.onToggle,
  });

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: slots.map((s) {
        final color = kSlotColors[(s.index - 1) % kSlotColors.length];
        return FilterChip(
          selected: visible.contains(s.index),
          onSelected: (_) => onToggle(s.index),
          avatar: CircleAvatar(backgroundColor: color, radius: 7),
          label: Text('Slot ${s.index}'),
        );
      }).toList(),
    );
  }
}
