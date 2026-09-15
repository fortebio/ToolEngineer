/// Trạng thái server đọc từ `GET /monitor` (Engineer Server).
///
/// **Mọi mục đều nullable và parse phòng thủ.** Server trả `null` cho mục nó
/// không đo được (box không phải Linux, Postgres chết) và bản server cũ thì
/// không có route này. Màn giám sát phải hiện "—" cho phần thiếu chứ không được
/// vỡ — nhưng thiếu CẢ route thì [FbtApi.monitor] NÉM lỗi để người dùng biết,
/// chứ không im lặng bày một trang trống.
library;

/// Mức dùng của một tài nguyên có trần (RAM, đĩa).
class UsageInfo {
  final int total;
  final int free;
  final double? usedPct;

  /// Chỉ ĐĨA mới có: ước tính còn bao nhiêu ngày nữa đầy, theo tốc độ phình của
  /// bảng `sessions`. `null` = server chưa đủ dữ liệu để nói (xem
  /// `monitor.days_until_full`).
  final double? daysLeft;

  const UsageInfo({
    required this.total,
    required this.free,
    this.usedPct,
    this.daysLeft,
  });

  static UsageInfo? fromJson(Object? j) {
    if (j is! Map) return null;
    final total = (j['total'] as num?)?.toInt() ?? 0;
    if (total <= 0) return null;
    // RAM gọi là `available`, đĩa gọi là `free` — cùng một ý "còn dùng được".
    final free =
        (j['free'] as num?)?.toInt() ?? (j['available'] as num?)?.toInt() ?? 0;
    return UsageInfo(
      total: total,
      free: free,
      usedPct: (j['used_pct'] as num?)?.toDouble(),
      daysLeft: (j['days_left'] as num?)?.toDouble(),
    );
  }
}

/// Cảm biến nóng nhất của box.
class TempInfo {
  final double c;
  final String sensor;
  const TempInfo(this.c, this.sensor);

  static TempInfo? fromJson(Object? j) {
    if (j is! Map) return null;
    final c = (j['c'] as num?)?.toDouble();
    if (c == null) return null;
    return TempInfo(c, (j['sensor'] ?? '').toString());
  }
}

class CpuInfo {
  final int cores;
  final double load1;
  final double? load1Pct;

  const CpuInfo({required this.cores, required this.load1, this.load1Pct});

  static CpuInfo? fromJson(Object? j) {
    if (j is! Map) return null;
    return CpuInfo(
      cores: (j['cores'] as num?)?.toInt() ?? 1,
      load1: (j['load1'] as num?)?.toDouble() ?? 0,
      load1Pct: (j['load1_pct'] as num?)?.toDouble(),
    );
  }
}

/// Một cột của biểu đồ 7 ngày.
class DayCount {
  final String day; // 'YYYY-MM-DD'
  final int count;
  const DayCount(this.day, this.count);
}

class FlowInfo {
  final int total;
  final int last24h;
  final int last7d;
  final int devices;

  /// Cỡ bảng `sessions` kể cả index/TOAST (`pg_total_relation_size`).
  final int dbBytes;
  final List<DayCount> byDay;

  const FlowInfo({
    required this.total,
    required this.last24h,
    required this.last7d,
    required this.devices,
    required this.dbBytes,
    required this.byDay,
  });

  static FlowInfo? fromJson(Object? j) {
    if (j is! Map) return null;
    return FlowInfo(
      total: (j['total'] as num?)?.toInt() ?? 0,
      last24h: (j['last24h'] as num?)?.toInt() ?? 0,
      last7d: (j['last7d'] as num?)?.toInt() ?? 0,
      devices: (j['devices'] as num?)?.toInt() ?? 0,
      dbBytes: (j['db_bytes'] as num?)?.toInt() ?? 0,
      byDay: [
        for (final e in (j['by_day'] as List? ?? const []).whereType<Map>())
          DayCount((e['d'] ?? '').toString(), (e['n'] as num?)?.toInt() ?? 0),
      ],
    );
  }

  /// Cột cao nhất — để vẽ biểu đồ theo tỉ lệ. 0 khi cả tuần không có gì.
  int get peak => byDay.fold(0, (m, e) => e.count > m ? e.count : m);
}

class ServerStatus {
  final Duration? uptime;
  final DateTime? startedAt;
  final bool dbOk;
  final String? dbError;
  final CpuInfo? cpu;
  final UsageInfo? mem;
  final UsageInfo? disk;
  final TempInfo? temp;
  final FlowInfo? flow;

  const ServerStatus({
    this.uptime,
    this.startedAt,
    this.dbOk = false,
    this.dbError,
    this.cpu,
    this.mem,
    this.disk,
    this.temp,
    this.flow,
  });

  factory ServerStatus.fromJson(Map<String, dynamic> j) {
    final svc = j['service'];
    final db = j['db'];
    return ServerStatus(
      uptime: svc is Map && svc['uptime_sec'] is num
          ? Duration(seconds: (svc['uptime_sec'] as num).toInt())
          : null,
      startedAt: svc is Map
          ? DateTime.tryParse((svc['started_at'] ?? '').toString())?.toLocal()
          : null,
      dbOk: db is Map && db['ok'] == true,
      dbError: db is Map ? (db['error'] as Object?)?.toString() : null,
      cpu: CpuInfo.fromJson(j['cpu']),
      mem: UsageInfo.fromJson(j['mem']),
      disk: UsageInfo.fromJson(j['disk']),
      temp: TempInfo.fromJson(j['temp']),
      flow: FlowInfo.fromJson(j['flow']),
    );
  }
}
