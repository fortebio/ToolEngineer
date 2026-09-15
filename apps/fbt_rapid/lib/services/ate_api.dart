/// Client hồ sơ **trạm ATE** trên Engineer Server (`server/app/main.py`).
///
/// Hợp đồng (Bearer như mọi route khác của server đó):
/// - `PUT  /ate/records`            — đẩy 1 hồ sơ, trả `{ok, id, sn, verdict}`.
/// - `GET  /ate/records?sn&verdict&from&to&page&limit` — tra cứu (KHÔNG kèm `steps`).
/// - `GET  /ate/records/{id}`       — hồ sơ đầy đủ (`id` = tên file server trả về).
/// - `GET  /ate/sn/{sn}`            — hồ sơ khai sinh + mọi lần test lại.
/// - `GET  /ate/stats?from&to`      — FPY, sản lượng/ngày, Pareto mã bước hỏng.
/// - `GET  /ate/limits` · `PUT /ate/limits?by=` — bộ ngưỡng (PUT: quyền root).
///
/// ⚠️ **PUT chứ không POST** cho mọi thao tác ghi — `POST /{path}` catch-all của
/// server nuốt mọi POST thành payload thiết bị (trả 400 "missing id_device").
/// Cùng lý do với mục OTA và log CSKH.
library;

import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/ate_record.dart';
import 'app_settings.dart';
import 'cloud_history_api.dart' show CloudApiException;

/// Server TỪ CHỐI hồ sơ (HTTP 400) — lỗi VĨNH VIỄN, gửi lại bao nhiêu lần cũng
/// hỏng y vậy. Tách khỏi lỗi mạng vì hàng đợi phải xử lý hai thứ này ngược nhau:
/// lỗi mạng thì DỪNG và thử lại sau, hồ sơ hỏng thì ĐẨY SANG MỘT BÊN — không thì
/// một hồ sơ méo (vd chưa chạy bước nào) chặn vĩnh viễn cả hàng đợi phía sau.
class AteRejectedException extends CloudApiException {
  AteRejectedException(super.message);
}

/// Kết quả `GET /ate/stats`.
class AteStats {
  final int total;
  final int passed;
  final int failed;
  final int aborted;
  final int machines; // số máy đã thử (bỏ hồ sơ aborted)
  final int firstPass;
  final double? fpy; // null = chưa có máy nào để tính
  final List<({String code, int count})> pareto;
  final List<({String day, int passed, int failed, int aborted})> byDay;

  const AteStats({
    this.total = 0,
    this.passed = 0,
    this.failed = 0,
    this.aborted = 0,
    this.machines = 0,
    this.firstPass = 0,
    this.fpy,
    this.pareto = const [],
    this.byDay = const [],
  });

  factory AteStats.fromJson(Map<String, dynamic> j) => AteStats(
        total: (j['total'] as num?)?.toInt() ?? 0,
        passed: (j['pass'] as num?)?.toInt() ?? 0,
        failed: (j['fail'] as num?)?.toInt() ?? 0,
        aborted: (j['aborted'] as num?)?.toInt() ?? 0,
        machines: (j['machines'] as num?)?.toInt() ?? 0,
        firstPass: (j['first_pass'] as num?)?.toInt() ?? 0,
        fpy: (j['fpy'] as num?)?.toDouble(),
        pareto: [
          for (final e in ((j['pareto'] as List?) ?? const []).whereType<Map>())
            (
              code: (e['code'] ?? '').toString(),
              count: (e['count'] as num?)?.toInt() ?? 0
            )
        ],
        byDay: [
          for (final e in ((j['by_day'] as List?) ?? const []).whereType<Map>())
            (
              day: (e['day'] ?? '').toString(),
              passed: (e['pass'] as num?)?.toInt() ?? 0,
              failed: (e['fail'] as num?)?.toInt() ?? 0,
              aborted: (e['aborted'] as num?)?.toInt() ?? 0,
            )
        ],
      );
}

/// Kết quả `GET /ate/sn/{sn}` — "hồ sơ khai sinh" của một máy.
class AteSnProfile {
  final String sn;
  final List<AteRecord> records; // mới nhất trước
  final AteRecord? birth; // lần PASS đầu tiên
  const AteSnProfile({required this.sn, this.records = const [], this.birth});

  int get attempts => records.length;
}

class AteApi {
  final String baseUrl;
  final Map<String, String> headers;
  final Duration timeout;

  AteApi(
    String url, {
    this.headers = const {},
    this.timeout = const Duration(seconds: 20),
  }) : baseUrl = url.trim().replaceAll(RegExp(r'/+$'), '');

  /// Dựng từ Cài đặt (cùng URL + token với nguồn Engineer Server).
  factory AteApi.of(AppSettings s) => AteApi(
        s.cloudUrlFor(CloudSource.engineer),
        headers: s.cloudHeadersFor(CloudSource.engineer),
      );

  Uri _uri(String path, [Map<String, String>? q]) {
    final u = Uri.parse('$baseUrl$path');
    if (q == null || q.isEmpty) return u;
    return u.replace(queryParameters: {...u.queryParameters, ...q});
  }

  Future<dynamic> _get(String path, [Map<String, String>? q]) async {
    _requireUrl();
    http.Response resp;
    try {
      resp = await http
          .get(_uri(path, q), headers: headers.isEmpty ? null : headers)
          .timeout(timeout);
    } catch (e) {
      throw CloudApiException('Không kết nối được Engineer Server: $e');
    }
    return _decode(resp);
  }

  Future<dynamic> _put(String path, Object body, [Map<String, String>? q]) async {
    _requireUrl();
    final req = http.Request('PUT', _uri(path, q))..headers.addAll(headers);
    req.headers['Content-Type'] = 'application/json';
    req.bodyBytes = utf8.encode(jsonEncode(body));
    http.Response resp;
    try {
      resp = await http.Response.fromStream(await req.send().timeout(timeout));
    } catch (e) {
      throw CloudApiException('Không kết nối được Engineer Server: $e');
    }
    return _decode(resp);
  }

  void _requireUrl() {
    if (baseUrl.isEmpty) {
      throw CloudApiException('Chưa cấu hình URL Engineer Server (vào Cài đặt).');
    }
  }

  dynamic _decode(http.Response resp) {
    switch (resp.statusCode) {
      case 200:
        break;
      case 400:
        throw AteRejectedException(_detailOf(resp) ?? 'Hồ sơ không hợp lệ (400).');
      case 401:
        throw CloudApiException(
            'Sai hoặc thiếu token (401). Token cấp lúc ĐĂNG NHẬP — đăng xuất rồi '
            'đăng nhập lại, hoặc nhập token trạm trong Cài đặt.');
      case 404:
        throw CloudApiException('Không tìm thấy hồ sơ (404).');
      case 405:
        // Catch-all `POST /{path}` làm mọi path chưa khai báo trả 405 chứ không
        // 404 → gần như luôn là "server chưa deploy phần ATE".
        throw CloudApiException(
            'Server chưa có endpoint /ate (405). Cập nhật Engineer Server lên bản '
            'có trạm ATE, và kiểm URL trong Cài đặt là URL GỐC.');
      case 413:
        throw CloudApiException('Hồ sơ quá lớn, server từ chối (413).');
      default:
        throw CloudApiException('Engineer Server trả về HTTP ${resp.statusCode}.');
    }
    try {
      return jsonDecode(utf8.decode(resp.bodyBytes));
    } catch (_) {
      throw CloudApiException('Dữ liệu Engineer Server không phải JSON hợp lệ.');
    }
  }

  String? _detailOf(http.Response resp) {
    try {
      final j = jsonDecode(utf8.decode(resp.bodyBytes));
      final d = (j is Map) ? j['detail'] : null;
      return (d == null || '$d'.isEmpty) ? null : '$d';
    } catch (_) {
      return null;
    }
  }

  /// Bộ ngưỡng ÁP DỤNG cho [batch]. Server lùi dần: bộ của lô → bộ chung → bộ
  /// mặc định, và nói rõ nguồn trong `AteLimits.source`.
  ///
  /// Lỗi mạng → ném; người gọi tự quyết dùng [AteLimits.fallback] hay dừng.
  Future<AteLimits> limits({String batch = ''}) async {
    final j = await _get(
        '/ate/limits', batch.trim().isEmpty ? null : {'batch': batch.trim()});
    if (j is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng /ate/limits không đúng.');
    }
    return AteLimits.fromJson(j);
  }

  /// Đặt bộ ngưỡng cho một lô ([batch] rỗng = bộ CHUNG). Server gác bằng token
  /// admin; trong app chỉ nhân sự kỹ thuật thấy đường này.
  ///
  /// ⚠️ Server TỪ CHỐI (400) nếu dùng lại `version` cũ cho nội dung khác — sửa
  /// ngưỡng thì phải đổi version, vì hồ sơ chỉ ghi chuỗi `limits_ver`.
  Future<String> putLimits(Map<String, dynamic> limits,
      {String by = '', String batch = ''}) async {
    final j = await _put('/ate/limits', limits, {
      if (by.isNotEmpty) 'by': by,
      if (batch.trim().isNotEmpty) 'batch': batch.trim(),
    });
    return (j is Map ? (j['version'] ?? '').toString() : '');
  }

  /// Các lô đã có bộ tiêu chuẩn riêng (kèm bộ chung — `batch` rỗng).
  Future<List<({String batch, String version, String updatedAt, String updatedBy})>>
      listLimits() async {
    final j = await _get('/ate/limits/list');
    final items = (j is Map ? j['items'] : null) as List? ?? const [];
    return [
      for (final e in items.whereType<Map>())
        (
          batch: (e['batch'] ?? '').toString(),
          version: (e['version'] ?? '').toString(),
          updatedAt: (e['updated_at'] ?? '').toString(),
          updatedBy: (e['updated_by'] ?? '').toString(),
        )
    ];
  }

  /// Đẩy một hồ sơ. Trả `id` (tên file trên server) — gửi lại đúng hồ sơ đó cho
  /// cùng một `id`, không sinh bản ghi trùng.
  Future<String> putRecord(AteRecord record) async {
    final j = await _put('/ate/records', record.toJson());
    if (j is! Map || j['ok'] != true) {
      throw CloudApiException('Server không nhận hồ sơ.');
    }
    return (j['id'] ?? '').toString();
  }

  /// Đẩy một hồ sơ đã ở dạng JSON (dùng cho hàng đợi gửi lại).
  Future<String> putRecordJson(Map<String, dynamic> body) async {
    final j = await _put('/ate/records', body);
    if (j is! Map || j['ok'] != true) {
      throw CloudApiException('Server không nhận hồ sơ.');
    }
    return (j['id'] ?? '').toString();
  }

  /// Tra cứu hồ sơ (mới nhất trước). Trả `(total, items)`.
  Future<({int total, List<AteRecord> items})> listRecords({
    String sn = '',
    String batch = '',
    String verdict = '',
    String from = '',
    String to = '',
    int page = 1,
    int limit = 50,
  }) async {
    final j = await _get('/ate/records', {
      if (sn.trim().isNotEmpty) 'sn': sn.trim(),
      if (batch.trim().isNotEmpty) 'batch': batch.trim(),
      if (verdict.isNotEmpty) 'verdict': verdict,
      if (from.isNotEmpty) 'from': from,
      if (to.isNotEmpty) 'to': to,
      'page': '$page',
      'limit': '$limit',
    });
    if (j is! Map) throw CloudApiException('Định dạng /ate/records không đúng.');
    final items = (j['items'] as List?) ?? const [];
    return (
      total: (j['total'] as num?)?.toInt() ?? items.length,
      items: [
        for (final e in items.whereType<Map>())
          AteRecord.fromJson(Map<String, dynamic>.from(e))
      ],
    );
  }

  Future<AteRecord> getRecord(String id) async {
    final j = await _get('/ate/records/${Uri.encodeComponent(id)}');
    if (j is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng hồ sơ không đúng.');
    }
    return AteRecord.fromJson({...j, 'id': id});
  }

  Future<AteSnProfile> snProfile(String sn) async {
    final j = await _get('/ate/sn/${Uri.encodeComponent(sn.trim())}');
    if (j is! Map) throw CloudApiException('Định dạng /ate/sn không đúng.');
    final recs = [
      for (final e in ((j['records'] as List?) ?? const []).whereType<Map>())
        AteRecord.fromJson(Map<String, dynamic>.from(e))
    ];
    final b = j['birth'];
    return AteSnProfile(
      sn: (j['sn'] ?? sn).toString(),
      records: recs,
      birth: b is Map ? AteRecord.fromJson(Map<String, dynamic>.from(b)) : null,
    );
  }

  Future<AteStats> stats(
      {String from = '', String to = '', String batch = ''}) async {
    final j = await _get('/ate/stats', {
      if (from.isNotEmpty) 'from': from,
      if (to.isNotEmpty) 'to': to,
      if (batch.trim().isNotEmpty) 'batch': batch.trim(),
    });
    if (j is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng /ate/stats không đúng.');
    }
    return AteStats.fromJson(j);
  }
}
