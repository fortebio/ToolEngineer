import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../models/test_result.dart';
import '../theme/app_theme.dart';
import '../util/chart_capture.dart';
import '../util/curve_processing.dart';
import '../util/format.dart';
import 'ct_chart.dart';
import 'result_badge.dart';

/// Render đồ thị của MỘT lần chạy thành ảnh PNG — dùng CHUNG cho nút "Lưu" ở màn
/// chi tiết và cho "tải hàng loạt" ở thẻ máy.
///
/// Tách ra đây vì hai đường xuất phải cho ra ảnh GIỐNG HỆT nhau. Để mỗi nơi tự
/// dựng layout thì chỉ vài lần sửa là ảnh của hai đường lệch nhau, mà người dùng
/// thì dán cả hai vào cùng một báo cáo.
Future<Map<CurveView, Uint8List>> captureRunCharts(
  BuildContext context, {
  required TestResult run,
  required List<CurveView> views,
  required int readingIntervalSec,
  Set<int>? visible,
}) async {
  final vis = visible ?? {for (final s in run.slots) s.index};
  final ordered = CurveView.values.where(views.contains).toList();
  if (ordered.isEmpty) return {};

  // Chiều cao đồ thị khi chụp = xấp xỉ chiều cao bảng "Kết quả bệnh"
  // (bảng xếp 2 cột; mỗi hàng card ~90px + tiêu đề). Cho đồ thị cao bằng bảng
  // để ảnh không chừa khoảng trắng phía dưới hình đồ thị.
  final resultRows = ((run.slots.length + 1) ~/ 2).clamp(1, 5);
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
                  // `Builder` để lấy được context NẰM TRONG vùng chụp. Không có
                  // nó thì `Theme.of(context)` bên dưới vẫn trỏ về context của
                  // caller (ở NGOÀI) → tiêu đề vẫn mang màu theme tối trên nền
                  // trắng, dù AppExportTheme đã lo phần còn lại.
                  child: Builder(
                    builder: (ctxChup) => Container(
                      width: 1240,
                      color: Colors.white,
                      padding: const EdgeInsets.all(12),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Text('${v.label} — ${run.deviceId}',
                              style: Theme.of(ctxChup).textTheme.titleMedium),
                          const SizedBox(height: 4),
                          RunLegend(slots: run.slots, visible: vis),
                          const SizedBox(height: 8),
                          // Đồ thị BÊN TRÁI + bảng "Kết quả bệnh" BÊN PHẢI.
                          Row(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Expanded(
                                child: SizedBox(
                                  height: captureChartH,
                                  child: CtChart(
                                    slots: run.slots,
                                    visibleIndexes: vis,
                                    readingIntervalSec: readingIntervalSec,
                                    curveSelector: (s) => computeCurveSeries(
                                        v, s.curve, s.slope, readingIntervalSec),
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
                                    RunSlotGrid(
                                      slots: run.slots,
                                      visible: vis,
                                      onToggle: (_) {},
                                    ),
                                  ],
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
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

/// Chú thích gọn (chấm màu + nhãn slot) cho slot đang hiện — để ảnh PNG tự hiểu.
class RunLegend extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visible;
  const RunLegend({super.key, required this.slots, required this.visible});

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

/// Lưới 10 slot (2 cột × 5 hàng) — bấm để bật/tắt đường trong đồ thị.
class RunSlotGrid extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visible;
  final void Function(int index) onToggle;
  const RunSlotGrid({
    super.key,
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
