/// Mô hình dữ liệu của **trạm ATE** (tab Sản xuất): bộ ngưỡng, kết quả từng
/// bước, và hồ sơ nghiệm thu một máy.
///
/// Thuần Dart (không Flutter, không dart:io) — cùng lý do với `log_triage.dart`:
/// phần chấm PASS/FAIL sai thì hỏng cả lô máy, nên nó phải test được bằng
/// `flutter test` chứ không chỉ bằng cách cắm máy thật.
///
/// Hợp đồng JSON khớp `server/app/logic.py` (`validate_ate_record` /
/// `normalize_ate_record`): `{sn, verdict, limits_ver, steps[], started_at,
/// finished_at, station, operator, fw_version, fw_sha256, pcb_version, mac,
/// calib, note, app}`.
library;

/// Kết luận của một bước hoặc của cả hồ sơ.
///
/// `skip` = bước cố ý bỏ (không cấu hình), `info` = có ghi số liệu nhưng KHÔNG
/// chấm (chưa có ngưỡng — đúng tình trạng phần lớn phép đo quang/nhiệt ở P0).
/// Hai mức này KHÔNG làm hỏng hồ sơ, nhưng vẫn nằm trong hồ sơ: sau này chốt
/// được ngưỡng thì dữ liệu cũ vẫn còn để dựng lại xu hướng.
enum AteVerdict { pass, fail, skip, info, aborted }

String ateVerdictCode(AteVerdict v) => v.name;

AteVerdict ateVerdictFrom(Object? v) {
  switch (v.toString().trim().toLowerCase()) {
    case 'pass':
      return AteVerdict.pass;
    case 'fail':
      return AteVerdict.fail;
    case 'skip':
      return AteVerdict.skip;
    case 'aborted':
      return AteVerdict.aborted;
    default:
      return AteVerdict.info;
  }
}

/// Kết quả một bước đo: **một con số kèm ngưỡng** khi đo được, và luôn kèm log
/// thô để người sau còn kiểm lại được (nguyên tắc §1 của kế hoạch).
class AteStepResult {
  final String code; // FW-01, BOOT-01… (mã ổn định, dùng vẽ Pareto)
  final String name; // tên tiếng Việt hiện trên màn
  final AteVerdict verdict;

  /// Số đo (nếu bước này ra số) + đơn vị + dải cho phép. Null = bước kiểu
  /// có/không (nạp được, khởi động sạch…).
  final num? value;
  final String unit;
  final num? min;
  final num? max;

  /// Một dòng nói CHUYỆN GÌ ĐÃ XẢY RA — hiện thẳng cho thao tác viên.
  final String detail;

  /// Log thô của bước (output esptool, log UART…). Cắt trần trước khi gửi.
  final String raw;
  final DateTime startedAt;
  final Duration took;

  const AteStepResult({
    required this.code,
    required this.name,
    required this.verdict,
    required this.startedAt,
    this.took = Duration.zero,
    this.value,
    this.unit = '',
    this.min,
    this.max,
    this.detail = '',
    this.raw = '',
  });

  bool get failed => verdict == AteVerdict.fail;

  AteStepResult copyWith({String? detail, String? raw, AteVerdict? verdict}) =>
      AteStepResult(
        code: code,
        name: name,
        verdict: verdict ?? this.verdict,
        startedAt: startedAt,
        took: took,
        value: value,
        unit: unit,
        min: min,
        max: max,
        detail: detail ?? this.detail,
        raw: raw ?? this.raw,
      );

  Map<String, dynamic> toJson({int rawLimit = 20000}) => {
        'code': code,
        'name': name,
        'verdict': ateVerdictCode(verdict),
        if (value != null) 'value': value,
        if (unit.isNotEmpty) 'unit': unit,
        if (min != null) 'min': min,
        if (max != null) 'max': max,
        if (detail.isNotEmpty) 'detail': detail,
        if (raw.isNotEmpty)
          'raw': raw.length <= rawLimit
              ? raw
              // Cắt phần GIỮA, giữ đầu (lệnh đã chạy) và đuôi (chỗ hỏng) — cắt
              // đuôi là vứt đúng phần nói vì sao bước này fail.
              : '${raw.substring(0, rawLimit ~/ 2)}\n'
                  '… [cắt bớt ${raw.length - rawLimit} ký tự] …\n'
                  '${raw.substring(raw.length - rawLimit ~/ 2)}',
        'started_at': startedAt.toUtc().toIso8601String(),
        'took_ms': took.inMilliseconds,
      };

  factory AteStepResult.fromJson(Map<String, dynamic> j) => AteStepResult(
        code: (j['code'] ?? '').toString(),
        name: (j['name'] ?? '').toString(),
        verdict: ateVerdictFrom(j['verdict']),
        value: j['value'] is num ? j['value'] as num : null,
        unit: (j['unit'] ?? '').toString(),
        min: j['min'] is num ? j['min'] as num : null,
        max: j['max'] is num ? j['max'] as num : null,
        detail: (j['detail'] ?? '').toString(),
        raw: (j['raw'] ?? '').toString(),
        startedAt:
            DateTime.tryParse((j['started_at'] ?? '').toString())?.toLocal() ??
                DateTime.fromMillisecondsSinceEpoch(0),
        took: Duration(milliseconds: (j['took_ms'] as num?)?.toInt() ?? 0),
      );
}

/// Hồ sơ một lần máy qua trạm.
class AteRecord {
  final String id; // tên file trên server (rỗng khi chưa đẩy lên)
  final String sn;

  /// Lô sản xuất — khoá để tra "lô này chấm theo tiêu chuẩn nào" và để lọc FPY
  /// theo lô. Tiêu chuẩn do admin đặt cho TỪNG lô (`GET /ate/limits?batch=`).
  final String batch;
  final String mac;
  final String station;
  final String operator;
  final String fwVersion;
  final String fwSha256;
  final String pcbVersion;
  final String limitsVer;
  final DateTime startedAt;
  final DateTime finishedAt;
  final AteVerdict verdict;
  final String failCode;
  final String note;
  final List<AteStepResult> steps;

  /// Chỉ có ở bản tóm tắt server trả về (danh sách không kèm `steps`).
  final int stepsTotal;
  final int stepsFailed;

  const AteRecord({
    required this.sn,
    this.batch = '',
    required this.startedAt,
    required this.finishedAt,
    required this.verdict,
    required this.limitsVer,
    this.id = '',
    this.mac = '',
    this.station = '',
    this.operator = '',
    this.fwVersion = '',
    this.fwSha256 = '',
    this.pcbVersion = '',
    this.failCode = '',
    this.note = '',
    this.steps = const [],
    this.stepsTotal = 0,
    this.stepsFailed = 0,
  });

  Map<String, dynamic> toJson() => {
        'sn': sn,
        'batch': batch,
        'mac': mac,
        'station': station,
        'operator': operator,
        'fw_version': fwVersion,
        'fw_sha256': fwSha256,
        'pcb_version': pcbVersion,
        'limits_ver': limitsVer,
        'started_at': startedAt.toUtc().toIso8601String(),
        'finished_at': finishedAt.toUtc().toIso8601String(),
        'verdict': ateVerdictCode(verdict),
        'fail_code': failCode,
        'note': note,
        'app': 'FBT_RAPID',
        'steps': [for (final s in steps) s.toJson()],
      };

  factory AteRecord.fromJson(Map<String, dynamic> j) {
    final raw = (j['steps'] as List?) ?? const [];
    final steps = [
      for (final e in raw.whereType<Map>())
        AteStepResult.fromJson(Map<String, dynamic>.from(e))
    ];
    return AteRecord(
      id: (j['id'] ?? '').toString(),
      sn: (j['sn'] ?? '').toString(),
      batch: (j['batch'] ?? '').toString(),
      mac: (j['mac'] ?? '').toString(),
      station: (j['station'] ?? '').toString(),
      operator: (j['operator'] ?? '').toString(),
      fwVersion: (j['fw_version'] ?? '').toString(),
      fwSha256: (j['fw_sha256'] ?? '').toString(),
      pcbVersion: (j['pcb_version'] ?? '').toString(),
      limitsVer: (j['limits_ver'] ?? '').toString(),
      startedAt: DateTime.tryParse((j['started_at'] ?? '').toString())
              ?.toLocal() ??
          DateTime.fromMillisecondsSinceEpoch(0),
      finishedAt: DateTime.tryParse((j['finished_at'] ?? '').toString())
              ?.toLocal() ??
          DateTime.fromMillisecondsSinceEpoch(0),
      verdict: ateVerdictFrom(j['verdict']),
      failCode: (j['fail_code'] ?? '').toString(),
      note: (j['note'] ?? '').toString(),
      steps: steps,
      stepsTotal: (j['steps_total'] as num?)?.toInt() ?? steps.length,
      stepsFailed: (j['steps_failed'] as num?)?.toInt() ??
          steps.where((s) => s.failed).length,
    );
  }
}

/// Bộ **giới hạn** dùng để chấm. Là DỮ LIỆU CÓ VERSION, không phải hằng số
/// trong code: mọi hồ sơ ghi kèm [version] nên máy đã nghiệm thu vẫn tra lại
/// được nó bị chấm theo ngưỡng nào (kế hoạch §4.3).
///
/// Server giữ bộ hiện hành ở `GET /ate/limits`; không lấy được (mất mạng) thì
/// trạm dùng [AteLimits.fallback] và hồ sơ ghi rõ version của bộ đó.
class AteLimits {
  final String version;

  /// Lô sản xuất mà bộ ngưỡng này áp cho. Rỗng = bộ CHUNG.
  final String batch;

  /// Bộ đang dùng đến từ đâu: `batch` (của đúng lô) · `chung` · `mặc định`.
  /// Hiện thẳng trên màn trạm: tưởng đang chấm theo tiêu chuẩn của lô mà thật ra
  /// đang chạy bộ mặc định là kiểu nhầm không màn hình nào cho thấy.
  final String source;
  final int snMaxLen; // char device_id[10] của firmware → 9 ký tự
  final int bootWatchSec; // nghe UART bao lâu sau khi nạp (BOOT-01)
  final int serialBaud;
  final int ackTimeoutSec; // chờ máy trả lời một lệnh Serial
  final bool requireFlashVerify;
  final String flashSizeExpect; // vd '8MB'; rỗng = không kiểm

  // --- P1: ngưỡng tự kiểm phần cứng ---
  //
  // `null` = **CHƯA CHỐT NGƯỠNG** → bước liên quan chỉ GHI SỐ (`info`), không
  // chấm đạt/hỏng. Cố ý: đặt ngưỡng theo cảm tính còn tệ hơn không có ngưỡng —
  // nó biến hồ sơ thành một cột chữ ĐẠT không ai kiểm chứng được. Điền các ô này
  // bằng số đo của 10–20 máy tốt đã biết (golden unit), rồi PUT bộ mới.
  final num? brightMin; // OPT-03: tín hiệu sáng thấp nhất chấp nhận được
  final num? brightMax; // OPT-03: cao nhất (quang bão hoà / hở sáng)
  final num? brightSpreadPct; // OPT-03: lệch tối đa giữa 10 kênh (%)
  final num? ambientC; // TMP-01: nhiệt phòng xưởng
  final num? tempTolC; // TMP-01: lệch cho phép so với nhiệt phòng
  final num? tempSpreadC; // TMP-01: lệch cho phép giữa 6 kênh
  final int tempWindowSec; // TMP-01: nghe UART bao lâu để lấy mẫu
  final int fanWaitSec; // FAN-01: chờ bao lâu trước khi hỏi người vận hành

  final Map<String, dynamic> raw;

  const AteLimits({
    required this.version,
    this.batch = '',
    this.source = '',
    this.snMaxLen = 9,
    this.bootWatchSec = 30,
    this.serialBaud = 115200,
    this.ackTimeoutSec = 8,
    this.requireFlashVerify = true,
    this.flashSizeExpect = '',
    this.brightMin,
    this.brightMax,
    this.brightSpreadPct,
    this.ambientC,
    this.tempTolC,
    this.tempSpreadC,
    this.tempWindowSec = 8,
    this.fanWaitSec = 4,
    this.raw = const {},
  });

  /// Bộ dùng khi CHƯA gọi được server. Version nói rõ nó là bộ dự phòng để
  /// không ai nhầm hồ sơ này với hồ sơ chấm theo bộ chính thức.
  static const fallback = AteLimits(version: 'local-fallback');

  factory AteLimits.fromJson(Map<String, dynamic> j) => AteLimits(
        version: (j['version'] ?? '').toString().trim().isEmpty
            ? 'unknown'
            : (j['version']).toString(),
        batch: (j['batch'] ?? '').toString(),
        source: (j['source'] ?? '').toString(),
        snMaxLen: (j['sn_max_len'] as num?)?.toInt() ?? 9,
        bootWatchSec: (j['boot_watch_sec'] as num?)?.toInt() ?? 30,
        serialBaud: (j['serial_baud'] as num?)?.toInt() ?? 115200,
        ackTimeoutSec: (j['ack_timeout_sec'] as num?)?.toInt() ?? 8,
        requireFlashVerify: j['require_flash_verify'] != false,
        flashSizeExpect: (j['flash_size_expect'] ?? '').toString(),
        brightMin: j['bright_min'] as num?,
        brightMax: j['bright_max'] as num?,
        brightSpreadPct: j['bright_spread_pct'] as num?,
        ambientC: j['ambient_c'] as num?,
        tempTolC: j['temp_tol_c'] as num?,
        tempSpreadC: j['temp_spread_c'] as num?,
        tempWindowSec: (j['temp_window_sec'] as num?)?.toInt() ?? 8,
        fanWaitSec: (j['fan_wait_sec'] as num?)?.toInt() ?? 4,
        raw: j,
      );
}

// ---------------------------------------------------------------------------
// Nhập bộ tiêu chuẩn từ FILE JSON
// ---------------------------------------------------------------------------

/// Mọi khoá thuộc về một bộ ngưỡng. Khoá ngoài danh sách này vẫn được GIỮ khi
/// nhập file (app mới có thể thêm khoá mà app cũ chưa biết), nhưng phải báo cho
/// người nhập biết — im lặng nuốt một khoá lạ là im lặng đánh mất một tiêu chuẩn.
const Set<String> kAteLimitKeys = {
  'version',
  'note',
  'sn_max_len',
  'boot_watch_sec',
  'serial_baud',
  'ack_timeout_sec',
  'require_flash_verify',
  'flash_size_expect',
  'bright_min',
  'bright_max',
  'bright_spread_pct',
  'ambient_c',
  'temp_tol_c',
  'temp_spread_c',
  'temp_window_sec',
  'fan_wait_sec',
};

/// Khoá do SERVER tự quản — bỏ khi nhập/xuất để file mang đúng phần "tiêu chuẩn".
const Set<String> kAteLimitServerKeys = {'updated_at', 'updated_by', 'source', 'batch'};

/// Kết quả đọc một file JSON tiêu chuẩn.
class AteLimitsImport {
  /// Lô → bộ ngưỡng. Khoá `''` = bộ CHUNG.
  final Map<String, Map<String, dynamic>> sets;

  /// Khoá không thuộc [kAteLimitKeys] (đã giữ nguyên trong [sets]).
  final List<String> unknownKeys;

  /// Lý do không đọc được; null = đọc được.
  final String? error;

  const AteLimitsImport({this.sets = const {}, this.unknownKeys = const [], this.error});

  bool get ok => error == null && sets.isNotEmpty;
  bool get isMulti => sets.length > 1;
}

/// Đọc nội dung JSON của một file tiêu chuẩn. Nhận **ba dạng**, vì thực tế file
/// đến từ ba nơi khác nhau:
///
/// 1. **Một bộ** — chính là file do nút "Xuất JSON" của app tạo ra:
///    `{"version": "L2609A-1", "bright_min": 800, …}`.
/// 2. **Một bộ kèm lô** — thêm `"batch": "L2609A"` (app tự chọn đúng lô đó).
/// 3. **Nhiều lô một file** — `{"batches": {"L2609A": {…}, "L2609B": {…}}}` hoặc
///    `{"items": [{"batch": "L2609A", …}, …]}`: cách khai cả một đợt sản xuất
///    trong một lần thay vì gõ tay từng lô.
///
/// KHÔNG tự lưu lên server: hàm này chỉ đọc. Việc ghi là một thao tác có chủ
/// đích của người dùng (một `version` = một nội dung — xem `ate_limits_screen`).
AteLimitsImport parseAteLimitsImport(Object? json) {
  if (json is! Map) {
    return const AteLimitsImport(error: 'File không phải JSON object.');
  }
  final raw = <String, Map<String, dynamic>>{};

  Map<String, dynamic>? asMap(Object? v) =>
      v is Map ? Map<String, dynamic>.from(v) : null;

  final batches = json['batches'];
  final items = json['items'];
  if (batches is Map) {
    for (final e in batches.entries) {
      final m = asMap(e.value);
      if (m == null) {
        return AteLimitsImport(error: 'Lô "${e.key}" không phải JSON object.');
      }
      raw[e.key.toString().trim()] = m;
    }
  } else if (items is List) {
    for (final e in items) {
      final m = asMap(e);
      if (m == null) {
        return const AteLimitsImport(error: 'Phần tử trong "items" phải là object.');
      }
      raw[(m['batch'] ?? '').toString().trim()] = m;
    }
  } else {
    raw[(json['batch'] ?? '').toString().trim()] = Map<String, dynamic>.from(json);
  }

  if (raw.isEmpty) {
    return const AteLimitsImport(error: 'File không có bộ ngưỡng nào.');
  }

  final unknown = <String>{};
  final out = <String, Map<String, dynamic>>{};
  for (final e in raw.entries) {
    final clean = <String, dynamic>{};
    for (final kv in e.value.entries) {
      if (kAteLimitServerKeys.contains(kv.key)) continue; // server tự đặt lại
      if (!kAteLimitKeys.contains(kv.key)) unknown.add(kv.key);
      clean[kv.key] = kv.value;
    }
    if ((clean['version'] ?? '').toString().trim().isEmpty) {
      return AteLimitsImport(
          error: e.key.isEmpty
              ? 'Bộ chung thiếu "version".'
              : 'Lô "${e.key}" thiếu "version".');
    }
    out[e.key] = clean;
  }
  return AteLimitsImport(sets: out, unknownKeys: unknown.toList()..sort());
}
