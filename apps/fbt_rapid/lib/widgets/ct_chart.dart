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
    final barLabels = <String>[]; // "Slot n · tên bệnh" (v2.4.3+ mới có tên)
    double dataMaxX = 1; // phút của điểm cuối cùng (chưa làm tròn)
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
        if (xMin > dataMaxX) dataMaxX = xMin;
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
      barLabels.add(slot.label);
    }

    if (bars.isEmpty) {
      return const Center(child: Text('Không có dữ liệu đường cong để hiển thị.'));
    }

    // Trục đọc được hay không nằm ở BA điều, cả ba đều từng sai ở bản trước:
    //
    // 1. **Số mốc theo KHUNG THẬT**, không phải hằng số. Bước 5 phút cố định cho ra 2 nhãn
    //    ở lần chạy ngắn và 18 nhãn dính nhau ở lần chạy dài, trên điện thoại thì luôn dính.
    // 2. **min/max phải NẰM ĐÚNG trên mốc chia.** fl_chart LUÔN vẽ thêm nhãn tại đúng min và
    //    max (`iterateThroughAxis` mặc định `minIncluded/maxIncluded = true`) → biên lẻ như
    //    39,67 phút đẻ ra một nhãn "39.7" dí sát "35". Làm tròn biên LÊN/XUỐNG theo bước
    //    chia thì nhãn thừa đó trùng luôn với mốc, không còn chỗ nào chen chúc.
    // 3. **Lưới phải TRÙNG nhãn.** Không truyền `verticalInterval` thì fl_chart tự chọn bước
    //    riêng cho lưới (`getEfficientInterval`) → 13 đường dọc cho 9 nhãn, mắt không nối được
    //    đường kẻ với con số nào. Nay lưới dùng đúng bước của nhãn.
    return LayoutBuilder(builder: (context, box) {
      // ~80px/nhãn ngang và ~60px/nhãn dọc: thưa hơn thì trục trống trải, dày hơn thì chữ dính.
      final xInterval = _niceStep(
          dataMaxX / math.max(3, box.maxWidth ~/ 80));
      final maxX = (dataMaxX / xInterval).ceilToDouble() * xInterval;

      double? minY, maxY, yInterval;
      if (yMin != null && yMax != null) {
        // Nấc trục tung: bội số của 50 và KHÔNG BAO GIỜ nhỏ hơn 50 (chủ dự án chốt).
        // Số đo huỳnh quang đọc theo hàng chục nghìn, mốc 20 hay 5 chỉ là nhiễu thị giác;
        // và nấc luôn chia hết 50 thì mọi nhãn đọc cùng một nhịp bất kể lần chạy nào.
        yInterval = math.max(
                1,
                (_niceStep((yMax - yMin) / math.max(3, box.maxHeight ~/ 60)) / 50)
                    .ceilToDouble()) *
            50;
        // Biên bám nấc → nhãn thừa mà fl_chart vẽ tại min/max trùng luôn mốc (xem 2.).
        minY = (yMin / yInterval).floorToDouble() * yInterval;
        maxY = (yMax / yInterval).ceilToDouble() * yInterval;
        // Đường cong THẤP không được phóng to lên hết khung: dưới 100 thì khung vẫn mở tới
        // 200 (và tính từ 0). Phóng to một đường gần như phẳng chỉ biến nhiễu thành "sóng".
        if (yMax <= 100) {
          minY = math.min(minY, 0);
          maxY = math.max(maxY, 200);
        }
        // Đường phẳng tuyệt đối: min == max thì fl_chart không vẽ được gì.
        if (maxY <= minY) maxY = minY + yInterval;
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
          verticalInterval: xInterval,
          getDrawingHorizontalLine: (_) =>
              FlLine(color: cs.outlineVariant, strokeWidth: 1),
          // Dọc mảnh + mờ hơn ngang: nó chỉ để dóng thời gian, đọc giá trị là việc của ngang.
          getDrawingVerticalLine: (_) => FlLine(
              color: cs.outlineVariant.withValues(alpha: 0.6), strokeWidth: 1),
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
          // KHÔNG có tên trục ("Thời gian (phút)" / "Huỳnh quang"): đơn vị đã cố định
          // và người đọc màn này biết sẵn — chữ đó chỉ lấy chỗ của chính đường cong.
          bottomTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 26,
              interval: xInterval,
              // Bước chia là số tròn nên phút cũng in tròn; bước < 1 phút mới cần 1 chữ số.
              getTitlesWidget: (v, meta) => _tick(
                  cs, meta, xInterval >= 1 ? v.round().toString() : v.toStringAsFixed(1)),
            ),
          ),
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 48,
              interval: yInterval,
              // KHÔNG dùng nhãn mặc định: fl_chart rút gọn ≥1000 thành "1.3K" — đây là số đo
              // huỳnh quang, làm tròn kiểu đó là đọc sai giá trị.
              getTitlesWidget: (v, meta) => _tick(cs, meta, v.round().toString()),
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
                color: color.withValues(alpha: 0.7),
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
                Colors.black.withValues(alpha: 0.5), // nền tooltip mờ 50%
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
                // "Slot N · PCV : giá trị | phút" — tên kênh tô màu theo slot.
                return LineTooltipItem(
                  barLabels[s.barIndex],
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
    });
  }

  /// Nhãn một mốc trên trục — chữ mờ, nhỏ, có khoảng thở với khung.
  static Widget _tick(ColorScheme cs, TitleMeta meta, String text) =>
      SideTitleWidget(
        axisSide: meta.axisSide,
        space: 6,
        child: Text(text,
            style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
      );
}
