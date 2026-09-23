/// Client **ống chuẩn hiệu chuẩn** trên Engineer Server (`server/app/calib.py`, route
/// `/calib/*` trong `server/app/main.py`).
///
/// Quy trình (bàn giao 2026-09 + WI DxD Hub 07/2024): pha dãy Fluorescein → chia ống →
/// đọc số thô từng ống ở MỘT khe máy tham chiếu → server thử mọi tổ hợp 1 ống/nồng độ,
/// xếp R²/slope/LOD → tổ hợp PASS không dùng chung ống thành **bộ ống** (túi zip) có mã
/// in nhãn + vòng đời (tủ lạnh → cấp máy → hết).
///
/// Hợp đồng (Bearer như mọi route khác):
/// - `GET  /calib/template?stock_nM&…`     — khung lô mới (nguyên liệu + bảng pha).
/// - `GET|PUT /calib/limits?by=`            — ngưỡng PASS (PUT: token admin OTA).
/// - `GET  /calib/batches?status=`          — danh sách lô (tóm tắt).
/// - `PUT  /calib/batches?by=`              — tạo lô (body = khung đã sửa hoặc rỗng).
/// - `GET|PUT|DELETE /calib/batches/{id}`   — lô đầy đủ (kèm `sets`) / cập nhật một phần / xoá.
/// - `GET  /calib/batches/{id}/rank?top=`   — xếp hạng + `suggested_sets` + `blank`.
/// - `PUT  /calib/batches/{id}/sets?by=`    — đóng gói `{combos:[{tubes}], expires_days?}`.
/// - `GET  /calib/sets?status&device&batch` · `GET /calib/sets/{id}` ·
///   `PUT /calib/sets/{id}?status&device&by&note` — bộ ống + đổi trạng thái.
///
/// ⚠️ **PUT chứ không POST** cho mọi thao tác ghi — `POST /{path}` catch-all của
/// server nuốt mọi POST thành payload thiết bị.
library;

import 'dart:convert';

import 'package:http/http.dart' as http;

import 'app_settings.dart';
import 'cloud_history_api.dart' show CloudApiException;

/// Dòng tóm tắt một lô pha (`GET /calib/batches`).
class CalibBatchMeta {
  final String id;
  final String status; // prep | measure | ranked | closed
  final String createdAt;
  final String createdBy;
  final String updatedAt;
  final String stockLot;
  final String reader;
  final int stepsDone;
  final int stepsTotal;
  final int readingsDone;
  final int readingsTotal;
  final int sets;
  final String note;

  const CalibBatchMeta({
    required this.id,
    required this.status,
    required this.createdAt,
    required this.createdBy,
    required this.updatedAt,
    required this.stockLot,
    required this.reader,
    required this.stepsDone,
    required this.stepsTotal,
    required this.readingsDone,
    required this.readingsTotal,
    required this.sets,
    required this.note,
  });

  factory CalibBatchMeta.fromJson(Map<String, dynamic> j) => CalibBatchMeta(
        id: (j['id'] ?? '').toString(),
        status: (j['status'] ?? '').toString(),
        createdAt: (j['created_at'] ?? '').toString(),
        createdBy: (j['created_by'] ?? '').toString(),
        updatedAt: (j['updated_at'] ?? '').toString(),
        stockLot: (j['stock_lot'] ?? '').toString(),
        reader: (j['reader'] ?? '').toString(),
        stepsDone: (j['steps_done'] as num?)?.toInt() ?? 0,
        stepsTotal: (j['steps_total'] as num?)?.toInt() ?? 0,
        readingsDone: (j['readings_done'] as num?)?.toInt() ?? 0,
        readingsTotal: (j['readings_total'] as num?)?.toInt() ?? 0,
        sets: (j['sets'] as num?)?.toInt() ?? 0,
        note: (j['note'] ?? '').toString(),
      );
}

/// Một bộ ống (`GET /calib/sets` hoặc phần tử `items` khi đóng gói).
class CalibSet {
  final String id;
  final String batch;
  final int? rank;
  final String status; // stored | issued | used | discarded
  final Map<String, String> tubes; // nồng độ → số ống
  final Map<String, double> raw;
  final double slope;
  final double intercept;
  final double r2;
  final double? lod;
  final String verdict;
  final String createdAt;
  final String createdBy;
  final String expiresAt;
  final String device;
  final String updatedAt;
  final String limitsVer;
  final List<Map<String, dynamic>> history;

  const CalibSet({
    required this.id,
    required this.batch,
    required this.rank,
    required this.status,
    required this.tubes,
    required this.raw,
    required this.slope,
    required this.intercept,
    required this.r2,
    required this.lod,
    required this.verdict,
    required this.createdAt,
    required this.createdBy,
    required this.expiresAt,
    required this.device,
    required this.updatedAt,
    required this.limitsVer,
    required this.history,
  });

  factory CalibSet.fromJson(Map<String, dynamic> j) => CalibSet(
        id: (j['id'] ?? '').toString(),
        batch: (j['batch'] ?? '').toString(),
        rank: (j['rank'] as num?)?.toInt(),
        status: (j['status'] ?? '').toString(),
        tubes: {
          for (final e in ((j['tubes'] as Map?) ?? const {}).entries)
            e.key.toString(): e.value.toString()
        },
        raw: {
          for (final e in ((j['raw'] as Map?) ?? const {}).entries)
            e.key.toString(): (e.value as num?)?.toDouble() ?? 0
        },
        slope: (j['slope'] as num?)?.toDouble() ?? 0,
        intercept: (j['intercept'] as num?)?.toDouble() ?? 0,
        r2: (j['r2'] as num?)?.toDouble() ?? 0,
        lod: (j['lod'] as num?)?.toDouble(),
        verdict: (j['verdict'] ?? '').toString(),
        createdAt: (j['created_at'] ?? '').toString(),
        createdBy: (j['created_by'] ?? '').toString(),
        expiresAt: (j['expires_at'] ?? '').toString(),
        device: (j['device'] ?? '').toString(),
        updatedAt: (j['updated_at'] ?? '').toString(),
        limitsVer: (j['limits_ver'] ?? '').toString(),
        history: [
          for (final h in (j['history'] as List?) ?? const [])
            if (h is Map) Map<String, dynamic>.from(h)
        ],
      );

  /// Hết hạn (theo `expires_at` yyyy-mm-dd, so với hôm nay).
  bool get expired {
    if (expiresAt.isEmpty) return false;
    final d = DateTime.tryParse(expiresAt);
    if (d == null) return false;
    final now = DateTime.now();
    return d.isBefore(DateTime(now.year, now.month, now.day));
  }
}

/// Một tổ hợp trong bảng xếp hạng (`GET /calib/batches/{id}/rank`).
class CalibCombo {
  final int rank;
  final Map<String, String> tubes;
  final Map<String, double> raw;
  final double slope;
  final double intercept;
  final double r2;

  /// Sai số dư của đường chuẩn (đơn vị raw) — server 2026-09-23. R² bão hoà ở
  /// 0,9999xx với 4 điểm nên nhìn không phân biệt được tổ hợp; `se` thì có.
  /// `null` khi chỉ có 2 điểm (không còn bậc tự do dư) hoặc server bản cũ.
  final double? se;
  final double? lod;
  final String status; // PASS | FAIL

  const CalibCombo({
    required this.rank,
    required this.tubes,
    required this.raw,
    required this.slope,
    required this.intercept,
    required this.r2,
    required this.se,
    required this.lod,
    required this.status,
  });

  factory CalibCombo.fromJson(Map<String, dynamic> j) => CalibCombo(
        rank: (j['rank'] as num?)?.toInt() ?? 0,
        tubes: {
          for (final e in ((j['tubes'] as Map?) ?? const {}).entries)
            e.key.toString(): e.value.toString()
        },
        raw: {
          for (final e in ((j['raw'] as Map?) ?? const {}).entries)
            e.key.toString(): (e.value as num?)?.toDouble() ?? 0
        },
        slope: (j['slope'] as num?)?.toDouble() ?? 0,
        intercept: (j['intercept'] as num?)?.toDouble() ?? 0,
        r2: (j['r2'] as num?)?.toDouble() ?? 0,
        se: (j['se'] as num?)?.toDouble(),
        lod: (j['lod'] as num?)?.toDouble(),
        status: (j['status'] ?? '').toString(),
      );

  /// Khoá so sánh hai tổ hợp (cùng bộ ống).
  String get key {
    final ks = tubes.keys.toList()..sort((a, b) => (double.tryParse(b) ?? 0).compareTo(double.tryParse(a) ?? 0));
    return ks.map((k) => '$k/${tubes[k]}').join(',');
  }
}

/// Kết quả xếp hạng.
class CalibRank {
  final List<CalibCombo> combos;
  final int total;
  final int pass;
  final List<CalibCombo> suggested;
  final Map<String, dynamic> blank; // {n, mean, sd, snr33}
  final Map<String, dynamic> limits;
  final List<String> tubesInSets;
  final List<String> missing;

  const CalibRank({
    required this.combos,
    required this.total,
    required this.pass,
    required this.suggested,
    required this.blank,
    required this.limits,
    required this.tubesInSets,
    required this.missing,
  });

  factory CalibRank.fromJson(Map<String, dynamic> j) => CalibRank(
        combos: [
          for (final c in (j['combos'] as List?) ?? const [])
            if (c is Map) CalibCombo.fromJson(Map<String, dynamic>.from(c))
        ],
        total: (j['total'] as num?)?.toInt() ?? 0,
        pass: (j['pass'] as num?)?.toInt() ?? 0,
        suggested: [
          for (final c in (j['suggested_sets'] as List?) ?? const [])
            if (c is Map) CalibCombo.fromJson(Map<String, dynamic>.from(c))
        ],
        blank: Map<String, dynamic>.from((j['blank'] as Map?) ?? const {}),
        limits: Map<String, dynamic>.from((j['limits'] as Map?) ?? const {}),
        tubesInSets: [for (final t in (j['tubes_in_sets'] as List?) ?? const []) t.toString()],
        missing: [for (final t in (j['missing'] as List?) ?? const []) t.toString()],
      );
}

class CalibApi {
  final String baseUrl;
  final Map<String, String> headers;
  final Duration timeout;

  CalibApi(
    String url, {
    this.headers = const {},
    this.timeout = const Duration(seconds: 20),
  }) : baseUrl = url.trim().replaceAll(RegExp(r'/+$'), '');

  /// Dựng từ Cài đặt (cùng URL + token với nguồn Engineer Server).
  factory CalibApi.of(AppSettings s) => CalibApi(
        s.cloudUrlFor(CloudSource.engineer),
        headers: s.cloudHeadersFor(CloudSource.engineer),
      );

  Uri _uri(String path, [Map<String, String>? q]) {
    final u = Uri.parse('$baseUrl$path');
    if (q == null || q.isEmpty) return u;
    return u.replace(queryParameters: {...u.queryParameters, ...q});
  }

  Future<dynamic> _send(String method, String path, {Object? body, Map<String, String>? q}) async {
    if (baseUrl.isEmpty) {
      throw CloudApiException('Chưa cấu hình URL Engineer Server (vào Cài đặt).');
    }
    final req = http.Request(method, _uri(path, q))..headers.addAll(headers);
    if (body != null) {
      req.headers['Content-Type'] = 'application/json';
      req.bodyBytes = utf8.encode(jsonEncode(body));
    }
    http.Response resp;
    try {
      resp = await http.Response.fromStream(await req.send().timeout(timeout));
    } catch (e) {
      throw CloudApiException('Không kết nối được Engineer Server: $e');
    }
    return _decode(resp);
  }

  dynamic _decode(http.Response resp) {
    switch (resp.statusCode) {
      case 200:
        break;
      case 400:
      case 409:
        throw CloudApiException(_detailOf(resp) ?? 'Server từ chối (${resp.statusCode}).');
      case 401:
        throw CloudApiException(
            'Sai hoặc thiếu token (401). Đổi ngưỡng cần token admin OTA — nhập trong Cài đặt.');
      case 404:
        throw CloudApiException(_detailOf(resp) ?? 'Không tìm thấy (404).');
      case 405:
        throw CloudApiException(
            'Server chưa có endpoint /calib (405). Cập nhật Engineer Server lên bản có '
            'ống chuẩn hiệu chuẩn, và kiểm URL trong Cài đặt là URL GỐC.');
      case 413:
        throw CloudApiException('Dữ liệu quá lớn, server từ chối (413).');
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
      if (j is Map && j['detail'] != null) return j['detail'].toString();
    } catch (_) {}
    return null;
  }

  Map<String, dynamic> _map(dynamic j, String what) {
    if (j is! Map) throw CloudApiException('Định dạng $what không đúng.');
    return Map<String, dynamic>.from(j);
  }

  // --- khung + ngưỡng ------------------------------------------------------------------

  Future<Map<String, dynamic>> template({double? stockNM, int? tubesPerConc, double? aliquotUl}) async {
    final q = <String, String>{};
    if (stockNM != null && stockNM > 0) q['stock_nM'] = stockNM.toString();
    if (tubesPerConc != null && tubesPerConc > 0) q['tubes_per_conc'] = tubesPerConc.toString();
    if (aliquotUl != null && aliquotUl > 0) q['aliquot_ul'] = aliquotUl.toString();
    return _map(await _send('GET', '/calib/template', q: q), '/calib/template');
  }

  Future<Map<String, dynamic>> limits() async =>
      _map(await _send('GET', '/calib/limits'), '/calib/limits');

  Future<void> saveLimits(Map<String, dynamic> doc, {required String by}) async {
    await _send('PUT', '/calib/limits', body: doc, q: {'by': by});
  }

  // --- lô pha --------------------------------------------------------------------------

  Future<List<CalibBatchMeta>> batches({String status = ''}) async {
    final j = _map(await _send('GET', '/calib/batches', q: status.isEmpty ? null : {'status': status}),
        '/calib/batches');
    return [
      for (final e in (j['items'] as List?) ?? const [])
        if (e is Map) CalibBatchMeta.fromJson(Map<String, dynamic>.from(e))
    ];
  }

  Future<Map<String, dynamic>> createBatch(Map<String, dynamic> body, {required String by}) async =>
      _map(await _send('PUT', '/calib/batches', body: body, q: {'by': by}), 'lô pha');

  Future<Map<String, dynamic>> batch(String id) async =>
      _map(await _send('GET', '/calib/batches/${Uri.encodeComponent(id)}'), 'lô pha');

  Future<Map<String, dynamic>> updateBatch(String id, Map<String, dynamic> patch,
          {required String by}) async =>
      _map(await _send('PUT', '/calib/batches/${Uri.encodeComponent(id)}', body: patch, q: {'by': by}),
          'lô pha');

  Future<void> deleteBatch(String id, {required String by}) async {
    await _send('DELETE', '/calib/batches/${Uri.encodeComponent(id)}', q: {'by': by});
  }

  Future<CalibRank> rank(String id, {int top = 50}) async => CalibRank.fromJson(_map(
      await _send('GET', '/calib/batches/${Uri.encodeComponent(id)}/rank', q: {'top': '$top'}),
      'xếp hạng'));

  /// Đóng gói các tổ hợp thành bộ ống; trả các bộ vừa tạo.
  Future<List<CalibSet>> createSets(String id, List<CalibCombo> combos,
      {required String by, int? expiresDays}) async {
    final j = _map(
        await _send('PUT', '/calib/batches/${Uri.encodeComponent(id)}/sets',
            body: {
              'combos': [
                for (final c in combos) {'tubes': c.tubes, 'rank': c.rank}
              ],
              if (expiresDays != null) 'expires_days': expiresDays,
            },
            q: {'by': by}),
        'đóng gói bộ ống');
    return [
      for (final e in (j['items'] as List?) ?? const [])
        if (e is Map) CalibSet.fromJson(Map<String, dynamic>.from(e))
    ];
  }

  // --- bộ ống --------------------------------------------------------------------------

  Future<List<CalibSet>> sets({String status = '', String device = '', String batch = ''}) async {
    final j = _map(
        await _send('GET', '/calib/sets', q: {
          if (status.isNotEmpty) 'status': status,
          if (device.isNotEmpty) 'device': device,
          if (batch.isNotEmpty) 'batch': batch,
        }),
        '/calib/sets');
    return [
      for (final e in (j['items'] as List?) ?? const [])
        if (e is Map) CalibSet.fromJson(Map<String, dynamic>.from(e))
    ];
  }

  Future<CalibSet> set(String id) async =>
      CalibSet.fromJson(_map(await _send('GET', '/calib/sets/${Uri.encodeComponent(id)}'), 'bộ ống'));

  Future<CalibSet> setStatus(String id, String status,
          {String device = '', required String by, String note = ''}) async =>
      CalibSet.fromJson(_map(
          await _send('PUT', '/calib/sets/${Uri.encodeComponent(id)}', q: {
            'status': status,
            if (device.isNotEmpty) 'device': device,
            'by': by,
            if (note.isNotEmpty) 'note': note,
          }),
          'bộ ống'));
}
