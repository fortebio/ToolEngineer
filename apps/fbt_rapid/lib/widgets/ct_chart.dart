import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../models/test_result.dart';

/// Bảng màu 10 slot, đồng bộ với web UI (data/script.js).
const List<Color> kSlotColors = [
  Color(0xFF00BFFF), // #1
  Color(0xFFFF0000), // #2
  Color(0xFFFFD000), // #3 (vàng đậm hơn cho dễ thấy nền sáng)
  Color(0xFF32CD32), // #4
  Color(0xFFD2691E), // #5
  Color(0xFF00B3B3), // #6
  Color(0xFF9400D3), // #7
  Color(0xFF9ACD32), // #8
  Color(0xFF0000FF), // #9
  Color(0xFFFF69B4), // #10
];

/// Bước chia "đẹp" (1·2·5 × 10^n) gần với giá trị thô — để nhãn trục đều, dễ đọc.
double _niceStep(double rough) {
  if (rough <= 0 || rough.isNaN || rough.isInfinite) return 1;
  final mag = math.pow(10, (math.log(rough) / math.ln10).floor()).toDouble();
  final norm = rough / mag; // 1..10
  final double step = norm <= 1
      ? 1
      : norm <= 2
          ? 2
          : norm <= 5
              ? 5
              : 10;
  return step * mag;
}

/// Đồ thị đường cong khuếch đại (CT) của các slot.
class CtChart extends StatelessWidget {
  final List<SlotResult> slots;
  final Set<int> visibleIndexes; // index slot (1..10) đang hiển thị
  final int readingIntervalSec; // để quy đổi trục X sang phút

  /// Hàm lấy chuỗi giá trị để vẽ cho 1 slot. Mặc định = `slot.curve`.
  /// Dùng để vẽ các biến đổi (raw / calibrate / baseline / smoothed).
  final List<double> Function(SlotResult slot)? curveSelector;

  const CtChart({
    super.key,
    required this.slots,
    required this.visibleIndexes,
    required this.readingIntervalSec,
    this.curveSelector,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final bars = <LineChartBarData>[];
    final barSlots = <int>[]; // slot.index ứng với từng bar (cho tooltip)
    double maxX = 1;
    double? yMin, yMax;

    for (final slot in slots) {
      if (!visibleIndexes.contains(slot.index)) continue;
      final curve = curveSelector?.call(slot) ?? slot.curve;
      if (curve.isEmpty) continue;

      final spots = <FlSpot>[];
      for (var i = 0; i < curve.length; i++) {
        final xMin = i * readingIntervalSec / 60.0;
        final y = curve[i];
        spots.add(FlSpot(xMin, y));
        if (xMin > maxX) maxX = xMin;
        if (yMin == null || y < yMin) yMin = y;
        if (yMax == null || y > yMax) yMax = y;
      }

      bars.add(
        LineChartBarData(
          spots: spots,
          isCurved: false,
          color: kSlotColors[(slot.index - 1) % kSlotColors.length],
          barWidth: 2,
          dotData: const FlDotData(show: false),
        ),
      );
      barSlots.add(slot.index);
    }

    if (bars.isEmpty) {
      return const Center(child: Text('Không có dữ liệu đường cong để hiển thị.'));
    }

    // Biên trục tung: nới đáy −50, đỉnh +200 rồi LÀM TRÒN VỀ BỘI SỐ CỦA 50
    // (đáy làm tròn xuống, đỉnh làm tròn lên) → min/max luôn chia hết cho 50
    // và bao trọn dữ liệu. Bước chia nhãn vẫn lấy "đẹp" để các mốc dễ đọc.
    double? minY, maxY, yInterval;
    if (yMin != null && yMax != null) {
      var lo = ((yMin - 100) / 50).floorToDouble() * 50;
      var hi = ((yMax + 150) / 50).ceilToDouble() * 50;
      if (hi - lo < 50) hi = lo + 50;
      minY = lo;
      maxY = hi;
      yInterval = _niceStep((hi - lo) / 5);
    }

    return LineChart(
      LineChartData(
        minX: 0,
        maxX: maxX,
        minY: minY,
        maxY: maxY,
        lineBarsData: bars,
        gridData: FlGridData(
          show: true,
          horizontalInterval: yInterval,
          getDrawingHorizontalLine: (_) =>
              FlLine(color: cs.outlineVariant, strokeWidth: 1),
          getDrawingVerticalLine: (_) =>
              FlLine(color: cs.outlineVariant, strokeWidth: 0.5),
        ),
        borderData: FlBorderData(
          show: true,
          border: Border.all(color: cs.outlineVariant),
        ),
        titlesData: FlTitlesData(
          topTitles:
              const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          rightTitles:
              const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          bottomTitles: const AxisTitles(
            axisNameWidget: Text('Thời gian (phút)'),
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 28,
              interval: 5,
            ),
          ),
          leftTitles: AxisTitles(
            axisNameWidget: const Text('Huỳnh quang'),
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 44,
              interval: yInterval,
            ),
          ),
        ),
        extraLinesData: ExtraLinesData(
          horizontalLines: [
            HorizontalLine(y: 0, color: cs.outline, strokeWidth: 1),
          ],
        ),
        // Tooltip: chỉ hiện ĐÚNG slot mà con trỏ trỏ vào (chọn theo khoảng cách
        // 2D đến điểm gần nhất); đường chỉ báo xuống trục hoành nét đứt mảnh, mờ.
        lineTouchData: LineTouchData(
          enabled: true,
          distanceCalculator: (touch, spot) => (touch - spot).distance,
          touchSpotThreshold: 20,
          getTouchedSpotIndicator: (barData, indexes) => indexes.map((i) {
            final color = barData.color ?? cs.onSurfaceVariant;
            return TouchedSpotIndicatorData(
              FlLine(
                color: color.withOpacity(0.7),
                strokeWidth: 0.1,
                dashArray: const [5, 4],
              ),
              FlDotData(
                show: true,
                getDotPainter: (spot, pct, bar, idx) => FlDotCirclePainter(
                    radius: 3, color: color, strokeWidth: 0),
              ),
            );
          }).toList(),
          touchTooltipData: LineTouchTooltipData(
            maxContentWidth: 280, // đủ rộng để "Slot N : giá trị | phút" 1 hàng
            getTooltipColor: (_) =>
                Colors.black.withOpacity(0.5), // nền tooltip mờ 50%
            // Giữ tooltip nằm gọn TRÊN khung đồ thị CT (không tràn sang/khuất
            // sau panel kết quả bệnh).
            fitInsideHorizontally: true,
            fitInsideVertically: true,
            getTooltipItems: (spots) {
              // Nhiều kênh tại 1 điểm → sắp xếp theo SLOT tăng dần.
              final sorted = [...spots]
                ..sort((a, b) =>
                    barSlots[a.barIndex].compareTo(barSlots[b.barIndex]));
              return sorted.map((s) {
                final slotNo = barSlots[s.barIndex];
                final color = kSlotColors[(slotNo - 1) % kSlotColors.length];
                // "Slot N : giá trị | phút" — tên kênh tô màu theo slot.
                return LineTooltipItem(
                  'Slot $slotNo',
                  TextStyle(
                      color: color, fontWeight: FontWeight.bold, fontSize: 12),
                  children: [
                    TextSpan(
                      text: ' : ${s.y.toStringAsFixed(1)}'
                          ' | ${s.x.toStringAsFixed(1)} phút',
                      style: const TextStyle(
                          color: Colors.white,
                          fontWeight: FontWeight.normal,
                          fontSize: 12),
                    ),
                  ],
                );
              }).toList();
            },
          ),
        ),
      ),
    );
  }
}
