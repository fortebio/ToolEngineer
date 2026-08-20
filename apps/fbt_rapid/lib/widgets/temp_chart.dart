import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

// Chỉ cần kiểu thuần (TempSample/kTempChannels) — KHÔNG import
// temperature_serial.dart (kéo dart:ffi) để file này tái dùng được trên WEB.
import '../services/temp_types.dart';

/// Màu cho 6 kênh nhiệt (Lysis, Amp1, Amp2, Hotlid1, Hotlid2, Ambient).
const List<Color> kTempColors = [
  Color(0xFFD32F2F), // Lysis   - đỏ
  Color(0xFFF57C00), // Amp1    - cam
  Color(0xFFFBC02D), // Amp2    - vàng
  Color(0xFF1976D2), // Hotlid1 - xanh dương
  Color(0xFF7B1FA2), // Hotlid2 - tím
  Color(0xFF388E3C), // Ambient - xanh lá
];

/// Đồ thị 6 đường nhiệt theo thời gian (phút). Dùng cho cả realtime lẫn xem lại.
class TempChart extends StatelessWidget {
  final List<TempSample> samples;

  /// Các kênh (0..5) đang hiển thị. `null` = hiện tất cả 6 kênh.
  final Set<int>? visibleChannels;

  const TempChart({super.key, required this.samples, this.visibleChannels});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    if (samples.isEmpty) {
      return Center(
        child: Text('Chưa có dữ liệu.',
            style: TextStyle(color: cs.onSurfaceVariant)),
      );
    }
    final t0 = samples.first.t;
    // Decimate khi quá nhiều mẫu để vẽ mượt (~3000 điểm/đường); dữ liệu lưu ra
    // file vẫn ĐẦY ĐỦ, chỉ phần hiển thị lấy thưa.
    const maxPoints = 3000;
    final stride =
        samples.length > maxPoints ? (samples.length / maxPoints).ceil() : 1;
    double maxX = 0;
    double? yMax;
    final bars = <LineChartBarData>[];
    final barChannels = <int>[]; // kênh ứng với từng bar (cho tooltip)
    for (var ch = 0; ch < 6; ch++) {
      if (visibleChannels != null && !visibleChannels!.contains(ch)) continue;
      final spots = <FlSpot>[];
      for (var si = 0; si < samples.length; si += stride) {
        final s = samples[si];
        final v = s.v.length > ch ? s.v[ch] : null;
        if (v == null) continue;
        final x = (s.t - t0) / 60.0;
        if (x > maxX) maxX = x;
        if (yMax == null || v > yMax) yMax = v;
        spots.add(FlSpot(x, v));
      }
      if (spots.isEmpty) continue;
      bars.add(LineChartBarData(
        spots: spots,
        isCurved: false,
        color: kTempColors[ch],
        barWidth: 2,
        dotData: const FlDotData(show: false),
      ));
      barChannels.add(ch);
    }
    final xMax = maxX <= 0 ? 1.0 : maxX;
    // Bước phút CHẴN (số nguyên: 1, 2, 3…), ~6 mốc, tối thiểu 1 phút.
    final xInterval = math.max(1, (xMax / 6).ceil()).toDouble();
    // Trục tung: −10 → (nhiệt độ MAX + 10); nấc CHÍNH 5/10 (có nhãn) + nấc PHỤ ở giữa.
    final dataMax = yMax ?? 100.0;
    final yInterval = (dataMax + 20) > 60 ? 10.0 : 5.0;
    final yMinor = yInterval / 2; // nấc phụ giữa 2 nấc chính
    final yTop = ((dataMax + 10) / yInterval).ceilToDouble() * yInterval;
    bool isMajorY(double v) =>
        (v / yInterval - (v / yInterval).round()).abs() < 0.001;
    return LineChart(LineChartData(
      minX: 0,
      maxX: xMax,
      minY: -10.0,
      maxY: yTop,
      lineBarsData: bars,
      gridData: FlGridData(
        show: true,
        verticalInterval: xInterval,
        horizontalInterval: yMinor,
        getDrawingHorizontalLine: (value) => isMajorY(value)
            ? FlLine(color: cs.outline, strokeWidth: 1)
            : FlLine(color: cs.outlineVariant, strokeWidth: 0.5),
        getDrawingVerticalLine: (_) =>
            FlLine(color: cs.outlineVariant, strokeWidth: 0.5),
      ),
      borderData:
          FlBorderData(show: true, border: Border.all(color: cs.outlineVariant)),
      titlesData: FlTitlesData(
        topTitles:
            const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        rightTitles:
            const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        bottomTitles: AxisTitles(
          axisNameWidget: const Text('Thời gian (phút)'),
          sideTitles: SideTitles(
            showTitles: true,
            reservedSize: 28,
            interval: xInterval,
            getTitlesWidget: (value, meta) => SideTitleWidget(
              axisSide: meta.axisSide,
              child: Text('${value.round()}',
                  style: const TextStyle(fontSize: 11)),
            ),
          ),
        ),
        leftTitles: AxisTitles(
          axisNameWidget: const Text('Nhiệt độ (°C)'),
          sideTitles: SideTitles(
            showTitles: true,
            reservedSize: 40,
            interval: yInterval,
          ),
        ),
      ),
      // Tooltip: chỉ hiện ĐÚNG kênh con trỏ trỏ vào; đường chỉ báo xuống trục
      // hoành nét đứt mảnh, mờ; giá trị làm tròn 1 số lẻ + kèm thời gian.
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
              getDotPainter: (spot, pct, bar, idx) =>
                  FlDotCirclePainter(radius: 3, color: color, strokeWidth: 0),
            ),
          );
        }).toList(),
        touchTooltipData: LineTouchTooltipData(
          maxContentWidth: 280, // đủ rộng để "Kênh : giá trị | phút" 1 hàng
          getTooltipColor: (_) =>
              Colors.black.withValues(alpha: 0.5), // nền tooltip mờ 50%
          fitInsideHorizontally: true,
          fitInsideVertically: true,
          getTooltipItems: (spots) {
            // Nhiều kênh tại 1 điểm → sắp xếp theo KÊNH tăng dần.
            final sorted = [...spots]
              ..sort((a, b) =>
                  barChannels[a.barIndex].compareTo(barChannels[b.barIndex]));
            return sorted.map((s) {
              final ch = barChannels[s.barIndex];
              // "Kênh : giá trị | phút" — tên kênh tô màu theo kênh.
              return LineTooltipItem(
                kTempChannels[ch],
                TextStyle(
                    color: kTempColors[ch],
                    fontWeight: FontWeight.bold,
                    fontSize: 12),
                children: [
                  TextSpan(
                    text: ' : ${s.y.toStringAsFixed(1)}°C'
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
    ));
  }
}

/// Chú thích màu các kênh nhiệt. `visible` = các kênh đang hiện (null = tất cả).
class TempLegend extends StatelessWidget {
  final Set<int>? visible;
  const TempLegend({super.key, this.visible});

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 12,
      runSpacing: 4,
      children: [
        for (var i = 0; i < 6; i++)
          if (visible == null || visible!.contains(i))
            Row(mainAxisSize: MainAxisSize.min, children: [
              CircleAvatar(backgroundColor: kTempColors[i], radius: 6),
              const SizedBox(width: 4),
              Text(kTempChannels[i], style: const TextStyle(fontSize: 12)),
            ]),
      ],
    );
  }
}

/// Hàng chip **bật/tắt** hiển thị từng kênh nhiệt trên đồ thị (bấm = ẩn/hiện
/// đường của kênh đó). Tùy chọn kèm giá trị mới nhất (live).
class TempChannelBar extends StatelessWidget {
  final Set<int> visible;
  final ValueChanged<int> onToggle;

  /// Giá trị mới nhất mỗi kênh để hiển thị kèm tên; null = không hiện giá trị.
  final List<double?>? latest;

  const TempChannelBar({
    super.key,
    required this.visible,
    required this.onToggle,
    this.latest,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Wrap(
      spacing: 8,
      runSpacing: 4,
      children: List.generate(6, (i) {
        final on = visible.contains(i);
        final v = (latest != null && i < latest!.length) ? latest![i] : null;
        final valueText = latest == null
            ? ''
            : ': ${v == null ? '--' : '${v.toStringAsFixed(1)}°'}';
        return FilterChip(
          selected: on,
          showCheckmark: false,
          visualDensity: VisualDensity.compact,
          tooltip: on
              ? 'Bấm để ẩn ${kTempChannels[i]}'
              : 'Bấm để hiện ${kTempChannels[i]}',
          avatar: CircleAvatar(
            backgroundColor: on ? kTempColors[i] : cs.outlineVariant,
            radius: 7,
          ),
          label: Text('${kTempChannels[i]}$valueText'),
          onSelected: (_) => onToggle(i),
        );
      }),
    );
  }
}
