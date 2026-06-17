import 'package:flutter/material.dart';

/// Phân loại kết quả 1 slot, ánh xạ từ chuỗi "result" của thiết bị.
enum Classification {
  positive,
  negative,
  slightPositive,
  error,
  unknown;

  /// Map từ chuỗi thiết bị trả về (/getdata -> "result").
  /// Firmware ghi "Positive" | "Negative" | "Slide Positive" | "E".
  static Classification fromDevice(String s) {
    switch (s.trim().toLowerCase()) {
      case 'positive':
        return Classification.positive;
      case 'negative':
        return Classification.negative;
      case 'slide positive':
      case 'slight positive':
        return Classification.slightPositive;
      case 'e':
      case 'error':
        return Classification.error;
      default:
        return Classification.unknown;
    }
  }

  /// Map từ chữ cái cloud trả về (doGet -> "result"): P|N|S|E|?.
  static Classification fromLetter(String s) {
    switch (s.trim().toUpperCase()) {
      case 'P':
        return Classification.positive;
      case 'N':
        return Classification.negative;
      case 'S':
        return Classification.slightPositive;
      case 'E':
        return Classification.error;
      default:
        return Classification.unknown;
    }
  }

  String get label {
    switch (this) {
      case Classification.positive:
        return 'Dương tính';
      case Classification.negative:
        return 'Âm tính';
      case Classification.slightPositive:
        return 'Dương tính nhẹ';
      case Classification.error:
        return 'Lỗi';
      case Classification.unknown:
        return 'Không rõ';
    }
  }

  Color get color {
    switch (this) {
      case Classification.positive:
        return const Color(0xFFD32F2F); // đỏ
      case Classification.negative:
        return const Color(0xFF388E3C); // xanh lá
      case Classification.slightPositive:
        return const Color(0xFFF57C00); // cam
      case Classification.error:
        return const Color(0xFF616161); // xám
      case Classification.unknown:
        return const Color(0xFF9E9E9E);
    }
  }

  /// Tên enum để lưu/đọc JSON cục bộ.
  String get storageKey => name;

  static Classification fromStorage(String s) {
    return Classification.values.firstWhere(
      (e) => e.name == s,
      orElse: () => Classification.unknown,
    );
  }
}

/// Kết quả của 1 slot quang.
class SlotResult {
  final int index; // 1..10
  final Classification classification;
  final double? ct; // null nếu "N/A"
  final List<double> curve; // raw draw (cloud) hoặc processed_data (/getdata)
  final double? slope; // hệ số calibrate của slot (cloud); null nếu không có

  const SlotResult({
    required this.index,
    required this.classification,
    required this.ct,
    required this.curve,
    this.slope,
  });

  Map<String, dynamic> toJson() => {
        'index': index,
        'classification': classification.storageKey,
        'ct': ct,
        'curve': curve,
        'slope': slope,
      };

  factory SlotResult.fromJson(Map<String, dynamic> j) {
    return SlotResult(
      index: (j['index'] as num).toInt(),
      classification: Classification.fromStorage(j['classification'] as String),
      ct: (j['ct'] as num?)?.toDouble(),
      curve: (j['curve'] as List? ?? const [])
          .map((e) => (e as num).toDouble())
          .toList(),
      slope: (j['slope'] as num?)?.toDouble(),
    );
  }
}

/// Kết quả 1 lần chạy (gồm 10 slot).
class TestResult {
  final String id; // khóa lịch sử
  final String deviceId;
  final DateTime timestamp;
  final List<SlotResult> slots;
  final String version; // version firmware của lần chạy (rỗng nếu không có)
  final bool curvesAreRaw; // true: curve là raw draw (cloud) → tính được 4 đồ thị

  const TestResult({
    required this.id,
    required this.deviceId,
    required this.timestamp,
    required this.slots,
    this.version = '',
    this.curvesAreRaw = false,
  });

  int countOf(Classification c) =>
      slots.where((s) => s.classification == c).length;

  /// Parse từ JSON của thiết bị (GET /getdata).
  factory TestResult.fromDeviceJson(
    Map<String, dynamic> j, {
    required DateTime fetchedAt,
  }) {
    final ctList = (j['CT_value'] as List?) ?? const [];
    final resList = (j['result'] as List?) ?? const [];
    // Nếu /getdata trả thêm raw + slopes → bật 4 đồ thị (Raw/Calib/Baseline/SG)
    // như tab Cloud; không có thì dùng #1..#10 (đường cong đã xử lý) như cũ.
    final ampList = j['amplification'] as List?;
    final slopesList = (j['slopes'] as List?) ?? const [];
    final hasRaw = ampList != null && ampList.isNotEmpty;

    final slots = <SlotResult>[];
    for (var i = 0; i < 10; i++) {
      final List<double> curve;
      final double? slope;
      if (hasRaw) {
        curve = _parseRawCurve(i < ampList.length ? ampList[i] : null);
        slope = i < slopesList.length
            ? (slopesList[i] is num
                ? (slopesList[i] as num).toDouble()
                : double.tryParse('${slopesList[i]}'))
            : null;
      } else {
        final rawCurve = (j['#${i + 1}'] as List?) ?? const [];
        curve = rawCurve
            .map((e) => double.tryParse(e.toString()) ?? 0.0)
            .toList();
        slope = null;
      }

      final ctStr = i < ctList.length ? ctList[i].toString().trim() : 'N/A';
      final ct = (ctStr.isEmpty || ctStr.toUpperCase() == 'N/A')
          ? null
          : double.tryParse(ctStr);

      final resStr = i < resList.length ? resList[i].toString() : '';

      slots.add(SlotResult(
        index: i + 1,
        classification: Classification.fromDevice(resStr),
        ct: ct,
        curve: curve,
        slope: slope,
      ));
    }

    return TestResult(
      id: fetchedAt.millisecondsSinceEpoch.toString(),
      deviceId: (j['id_device'] ?? '').toString().trim(),
      timestamp: fetchedAt,
      slots: slots,
      version: (j['version'] ?? '').toString().trim(),
      curvesAreRaw: hasRaw,
    );
  }

  /// Parse từ JSON của cloud (Apps Script doGet).
  ///
  /// - `action=runs` trả tóm tắt (KHÔNG có `curves`) → đường cong rỗng.
  /// - `action=run` trả thêm `curves` (10 mảng số) để vẽ đồ thị CT.
  ///
  /// Khóa lịch sử `id` = `fileId` của file Drive (ổn định, chống trùng).
  factory TestResult.fromCloudRun(Map<String, dynamic> j) {
    final ctList = (j['ct'] as List?) ?? const [];
    final resList = (j['result'] as List?) ?? const [];
    final curvesList = (j['curves'] as List?) ?? const [];
    final slopesList = (j['slopes'] as List?) ?? const [];

    final slots = <SlotResult>[];
    for (var i = 0; i < 10; i++) {
      final cls = i < resList.length
          ? Classification.fromLetter(resList[i].toString())
          : Classification.unknown;

      // CT chỉ có ý nghĩa với dương tính; âm/lỗi/không rõ hiển thị N/A.
      double? ct =
          i < ctList.length ? double.tryParse(ctList[i].toString()) : null;
      if (cls == Classification.negative ||
          cls == Classification.error ||
          cls == Classification.unknown) {
        ct = null;
      }

      final rawCurve = (i < curvesList.length ? curvesList[i] : null) as List?;
      final curve = (rawCurve ?? const [])
          .map((e) => e is num ? e.toDouble() : (double.tryParse('$e') ?? 0.0))
          .toList();

      final slope = i < slopesList.length
          ? (slopesList[i] is num
              ? (slopesList[i] as num).toDouble()
              : double.tryParse('${slopesList[i]}'))
          : null;

      slots.add(SlotResult(
        index: i + 1,
        classification: cls,
        ct: ct,
        curve: curve,
        slope: slope,
      ));
    }

    final ts = _parseRunTime((j['time'] ?? '').toString()) ??
        _parseRunTime((j['created'] ?? '').toString()) ??
        DateTime.fromMillisecondsSinceEpoch(0);

    return TestResult(
      id: (j['fileId'] ?? '').toString(),
      deviceId: (j['id_device'] ?? '').toString().trim(),
      timestamp: ts,
      slots: slots,
      version: (j['version'] ?? '').toString().trim(),
      curvesAreRaw: curvesList.isNotEmpty, // curves cloud = raw draw
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'deviceId': deviceId,
        'timestamp': timestamp.toIso8601String(),
        'version': version,
        'curvesAreRaw': curvesAreRaw,
        'slots': slots.map((s) => s.toJson()).toList(),
      };

  factory TestResult.fromJson(Map<String, dynamic> j) {
    return TestResult(
      id: j['id'] as String,
      deviceId: (j['deviceId'] ?? '') as String,
      timestamp: DateTime.parse(j['timestamp'] as String),
      version: (j['version'] ?? '') as String,
      curvesAreRaw: (j['curvesAreRaw'] ?? false) as bool,
      slots: (j['slots'] as List? ?? const [])
          .map((e) => SlotResult.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }
}

/// Parse thời gian lần chạy: chấp nhận ISO ("2025-07-30T11:18:59") HOẶC
/// "dd-mm-yyyy HH:mm:ss" (firmware cũ). Trả null nếu không hợp lệ.
DateTime? _parseRunTime(String s) {
  s = s.trim();
  if (s.isEmpty) return null;
  final iso = DateTime.tryParse(s);
  if (iso != null) return iso;
  final m = RegExp(r'^(\d{2})-(\d{2})-(\d{4})[ T](\d{2}):(\d{2}):(\d{2})')
      .firstMatch(s);
  if (m != null) {
    return DateTime(int.parse(m[3]!), int.parse(m[2]!), int.parse(m[1]!),
        int.parse(m[4]!), int.parse(m[5]!), int.parse(m[6]!));
  }
  return null;
}

/// Parse 1 đường cong raw từ /getdata: chấp nhận mảng số HOẶC chuỗi "1,2,3,".
List<double> _parseRawCurve(dynamic e) {
  if (e == null) return const [];
  if (e is List) {
    return e
        .map((x) => x is num ? x.toDouble() : (double.tryParse('$x') ?? 0.0))
        .toList();
  }
  return e
      .toString()
      .split(',')
      .where((s) => s.trim().isNotEmpty)
      .map((s) => double.tryParse(s.trim()) ?? 0.0)
      .toList();
}
