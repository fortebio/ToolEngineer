import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/test_result.dart';
import 'app_settings.dart';
import 'cloud_history_api.dart';

/// Client đọc lịch sử từ **FBT Home Server** (nguồn "Engineer Server") — server
/// FastAPI của kỹ sư (source: `../Server/app.py`, expose qua Tailscale, mặc định
/// `https://fbt.basa-luma.ts.net`).
///
/// Hợp đồng (khớp app.py; Swagger tự sinh ở `<url>/docs`):
/// - `GET /devices` → `[{id_device, sessions, last_seen}]`.
/// - `GET /sessions?device=&page=&limit=` → `{total, page, limit, items[]}`;
///   item `{id, id_device, received_at, version, type_upload, ct_value[10],
///   result[10]}` (`result` "22.3 | N" — server cũ chưa trả field này thì
///   phân loại hiện "?", mở chi tiết vẫn đủ).
/// - `GET /sessions/{id}` → `{…, payload}` = JSON firmware gốc (bỏ amplification).
/// - `GET /sessions/{id}/amplification` → `{slots:[{slot, ct_value, result,
///   points[]}]}` — `points` là mảng số server đã parse, cắm thẳng vào đồ thị.
/// - Xác thực: `Authorization: Bearer <RECEIVER_TOKEN>` (qua [headers]; server
///   chưa đặt token thì để trống).
class FbtApi implements CloudHistoryClient {
  final String baseUrl;
  final Duration timeout;
  final Map<String, String> headers;

  FbtApi(
    String url, {
    this.timeout = const Duration(seconds: 20),
    this.headers = const {},
  }) : baseUrl = url.trim().replaceAll(RegExp(r'/+$'), '');

  Uri _uri(String path, [Map<String, String>? query]) {
    final u = Uri.parse('$baseUrl$path');
    if (query == null) return u;
    return u.replace(queryParameters: {...u.queryParameters, ...query});
  }

  Future<dynamic> _get(Uri uri) async {
    if (baseUrl.isEmpty) {
      throw CloudApiException(
          'Chưa cấu hình URL Engineer Server (vào Cài đặt).');
    }
    http.Response resp;
    try {
      resp = await http
          .get(uri, headers: headers.isEmpty ? null : headers)
          .timeout(timeout);
    } catch (e) {
      throw CloudApiException('Không kết nối được Engineer Server: $e');
    }
    switch (resp.statusCode) {
      case 200:
        break;
      case 401:
        throw CloudApiException('Sai hoặc thiếu token (401).');
      case 404:
        throw CloudApiException('Không tìm thấy dữ liệu (404).');
      case 405:
        // Server chỉ có POST catch-all cho path lạ → GET vào path sai trả 405
        // (thường do URL trong Cài đặt bị THỪA path, vd .../api).
        throw CloudApiException(
            'URL Engineer Server sai (405) — chỉ nhập URL GỐC, không kèm '
            'path (vd $kDefaultEngineerUrl).');
      default:
        throw CloudApiException(
            'Engineer Server trả về HTTP ${resp.statusCode}.');
    }
    try {
      return jsonDecode(utf8.decode(resp.bodyBytes));
    } catch (_) {
      throw CloudApiException('Dữ liệu Engineer Server không phải JSON hợp lệ.');
    }
  }

  @override
  Future<List<CloudDevice>> listDevices({bool fresh = false}) async {
    final json = await _get(_uri('/devices'));
    if (json is! List) {
      throw CloudApiException('Định dạng /devices không đúng.');
    }
    return [
      for (final e in json.whereType<Map>())
        CloudDevice(
          id: (e['id_device'] ?? '').toString(),
          runCount: (e['sessions'] as num?)?.toInt() ?? 0,
          latest:
              DateTime.tryParse((e['last_seen'] ?? '').toString())?.toLocal(),
        )
    ];
  }

  @override
  Future<CloudRunsPage> listRuns(
    String deviceId, {
    int limit = 10,
    int offset = 0,
  }) async {
    // API phân trang theo `page` (1-based); màn Cloud lật trang theo offset bội
    // số của limit nên quy đổi tròn trang là đủ.
    final page = offset ~/ limit + 1;
    // deviceId rỗng = KHÔNG lọc máy (mục File JSON liệt kê mọi phiên) — gửi
    // device='' server sẽ lọc theo chuỗi rỗng và trả rỗng, nên phải bỏ param.
    final json = await _get(_uri('/sessions', {
      if (deviceId.isNotEmpty) 'device': deviceId,
      'page': '$page',
      'limit': '$limit',
    }));
    if (json is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng /sessions không đúng.');
    }
    final items = (json['items'] as List?) ?? const [];
    return CloudRunsPage(
      runs: items
          .whereType<Map>()
          .map((e) => _summary(e.cast<String, dynamic>()))
          .toList(),
      total: (json['total'] as num?)?.toInt() ?? items.length,
      offset: offset,
      limit: (json['limit'] as num?)?.toInt() ?? limit,
    );
  }

  /// JSON gốc 1 phiên (`GET /sessions/{id}`, có `payload` firmware) — mục
  /// "JSON data" (tab Thư Mục) xem THÔ, không parse thành kết quả/đồ thị.
  Future<Map<String, dynamic>> fetchSessionJson(String sessionId) async {
    final json = await _get(_uri('/sessions/$sessionId'));
    if (json is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng /sessions/{id} không đúng.');
    }
    return json;
  }

  @override
  Future<TestResult> fetchRun(String sessionId) async {
    // Chi tiết + đường cong nằm ở 2 endpoint (amplification nặng, server tách
    // riêng) → gọi song song.
    final results = await Future.wait([
      _get(_uri('/sessions/$sessionId')),
      _get(_uri('/sessions/$sessionId/amplification')),
    ]);
    final detail = results[0];
    final amp = results[1];
    if (detail is! Map<String, dynamic>) {
      throw CloudApiException('Thiếu dữ liệu chi tiết phiên.');
    }
    final payload = (detail['payload'] is Map)
        ? (detail['payload'] as Map).cast<String, dynamic>()
        : const <String, dynamic>{};
    final ampSlots = (amp is Map && amp['slots'] is List)
        ? (amp['slots'] as List)
            .whereType<Map>()
            .map((e) => e.cast<String, dynamic>())
            .toList()
        : const <Map<String, dynamic>>[];

    final ctList = (payload['CT_value'] as List?) ?? const [];
    final resList = (payload['result'] as List?) ?? const [];
    final slopesList = (payload['slopes'] as List?) ?? const [];

    final slots = <SlotResult>[];
    var anyCurve = false;
    for (var i = 0; i < 10; i++) {
      // Ưu tiên slot từ /amplification (đủ points + ct + result); phiên không
      // có amplification → lấy từ payload firmware (không đường cong).
      final a = _ampSlotAt(ampSlots, i + 1);
      final rawRes =
          (a['result'] ?? (i < resList.length ? resList[i] : '') ?? '')
              .toString();
      final ct = _numOf(a['ct_value']) ??
          _numOf(i < ctList.length ? ctList[i] : null) ??
          _ctOf(rawRes);
      final curve = [
        for (final p in (a['points'] as List? ?? const []))
          p is num ? p.toDouble() : (double.tryParse('$p') ?? 0.0)
      ];
      if (curve.isNotEmpty) anyCurve = true;
      slots.add(_slot(i + 1, _letterOf(rawRes), ct, curve,
          _numOf(i < slopesList.length ? slopesList[i] : null)));
    }

    return TestResult(
      id: sessionId,
      deviceId: (detail['id_device'] ?? '').toString().trim(),
      timestamp: _timeOf(detail),
      slots: slots,
      version:
          (detail['version'] ?? payload['version'] ?? '').toString().trim(),
      curvesAreRaw: anyCurve,
    );
  }

  // --- Map item danh sách → TestResult tóm tắt (không curves) ---------------

  TestResult _summary(Map<String, dynamic> j) {
    final ctList = (j['ct_value'] as List?) ?? const [];
    final resList = (j['result'] as List?) ?? const [];
    final slots = <SlotResult>[];
    for (var i = 0; i < 10; i++) {
      final rawRes = (i < resList.length ? (resList[i] ?? '') : '').toString();
      final ct = _numOf(i < ctList.length ? ctList[i] : null) ?? _ctOf(rawRes);
      slots.add(_slot(i + 1, _letterOf(rawRes), ct, const [], null));
    }
    return TestResult(
      id: (j['id'] ?? '').toString(),
      deviceId: (j['id_device'] ?? '').toString().trim(),
      timestamp: _timeOf(j),
      slots: slots,
      version: (j['version'] ?? '').toString().trim(),
      curvesAreRaw: false,
    );
  }

  /// Slot `/amplification` theo số slot 1..10 (server trả đúng thứ tự nhưng
  /// vẫn dò theo field `slot` cho chắc). Không có → map rỗng.
  Map<String, dynamic> _ampSlotAt(List<Map<String, dynamic>> slots, int slot) {
    for (final s in slots) {
      if ((s['slot'] as num?)?.toInt() == slot) return s;
    }
    return const {};
  }

  // --- Helper parse (cùng quy ước RapidErpApi: "22.3 | N" = CT | chữ) --------

  SlotResult _slot(
      int index, String letter, double? ct, List<double> curve, double? slope) {
    final cls = Classification.fromLetter(letter);
    // CT chỉ có nghĩa với dương tính (đồng bộ quy ước fromCloudRun).
    final shownCt = (cls == Classification.negative ||
            cls == Classification.error ||
            cls == Classification.unknown)
        ? null
        : ct;
    return SlotResult(
        index: index,
        classification: cls,
        ct: shownCt,
        curve: curve,
        slope: slope);
  }

  /// `received_at` ISO (có timezone) → giờ địa phương; hỏng/thiếu → epoch 0.
  DateTime _timeOf(Map<String, dynamic> j) =>
      DateTime.tryParse((j['received_at'] ?? '').toString())?.toLocal() ??
      DateTime.fromMillisecondsSinceEpoch(0);

  /// "22.3 | N" → "N"; "N" → "N". Trả "?" nếu rỗng.
  String _letterOf(Object? x) {
    final s = (x ?? '').toString().trim();
    if (s.isEmpty) return '?';
    final part = s.contains('|') ? s.split('|').last : s;
    final c = part.trim();
    return c.isEmpty ? '?' : c[0].toUpperCase();
  }

  /// CT từ "22.3 | N" (phần trước `|`). "--"/"!"/rỗng → null.
  double? _ctOf(Object? x) {
    final s = (x ?? '').toString();
    final part = s.contains('|') ? s.split('|').first : s;
    return _numOf(part);
  }

  double? _numOf(Object? x) {
    if (x == null) return null;
    if (x is num) return x.toDouble();
    final s = x.toString().trim();
    if (s.isEmpty || s == '--' || s == '!' || s.toUpperCase() == 'N/A') {
      return null;
    }
    return double.tryParse(s);
  }
}
