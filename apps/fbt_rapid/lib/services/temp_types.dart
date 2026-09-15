/// Kiểu dữ liệu nhiệt THUẦN (không dart:io/dart:ffi) — tách khỏi
/// `temperature_serial.dart` để `temp_chart.dart` + màn Log nhiệt bản WEB
/// (Web Serial) dùng chung mà không kéo flutter_libserialport vào build web.
library;

/// 6 kênh nhiệt theo thứ tự cố định (khớp dòng TimeRB + TimeRT của firmware).
const List<String> kTempChannels = [
  'Lysis', // BottomHeater[0]
  'Amp1', // BottomHeater[1]
  'Amp2', // BottomHeater[2]
  'Hotlid1', // TopHeater[0]
  'Hotlid2', // TopHeater[1]
  'Ambient', // TopHeater[2]
];

/// 1 mẫu nhiệt (1 chu kỳ) = thời gian (giây, theo đồng hồ máy) + 6 kênh.
class TempSample {
  final double t;
  final List<double?> v; // 6 giá trị; null nếu thiếu
  const TempSample(this.t, this.v);

  List<dynamic> toJson() => [t, ...v];

  factory TempSample.fromJson(List<dynamic> j) => TempSample(
        (j.isNotEmpty && j[0] is num) ? (j[0] as num).toDouble() : 0.0,
        [for (var i = 1; i < j.length; i++) (j[i] as num?)?.toDouble()],
      );
}
