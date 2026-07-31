import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/test_result.dart';
import 'app_settings.dart';
import 'cloud_history_api.dart';
import 'fbt_api.dart';
import 'session_store.dart';

/// Client đọc lịch sử từ **API RAPID ERP** (server NGOÀI, REST) — nguồn cloud
/// thứ 3, song song Google Apps Script và server tự host.
///
/// Hợp đồng (xem docs/api-guide-external-vi.md):
/// - `GET /external/device/{id}/results?limit&offset` → `{items[], total, …}`.
///   Mỗi item có `result_codes` = map `{"0".."9": "22.3 | N"}` (CT + phân loại).
/// - `GET /external/results/{result_id}/detail` → 10 kênh + `amplification_data`.
/// - Xác thực: header `X-API-Key` (set qua [headers]).
///
/// KHÁC 2 nguồn kia ở 2 điểm quan trọng:
/// 1. KHÔNG có endpoint liệt kê máy → [listDevices] dùng [deviceIds] (admin nhập
///    tay ở Cài đặt); nếu trống thì suy từ mã máy được cấp trong phiên
///    ([SessionStore.current.ids]).
/// 2. Phản hồi là JSON REST thuần (KHÔNG bọc `{ok:true,…}`).
class RapidErpApi implements CloudHistoryClient {
  /// URL gốc, vd `https://api.fortebio.tech/api/v1/results` (bỏ `/` cuối).
  final String baseUrl;
  final Duration timeout;

  /// Header gửi kèm MỌI request — thường là `{'X-API-Key': '<key>'}`.
  final Map<String, String> headers;

  /// Mã máy admin nhập tay (nguồn của danh sách máy). Rỗng → fallback session.
  final List<String> deviceIds;

  RapidErpApi(
    String url, {
    this.timeout = const Duration(seconds: 20),
    this.headers = const {},
    this.deviceIds = const [],
  }) : baseUrl = url.trim().replaceAll(RegExp(r'/+$'), '');

  Uri _uri(String path, [Map<String, String>? query]) {
    final u = Uri.parse('$baseUrl$path');
    if (query == null) return u;
    return u.replace(queryParameters: {...u.queryParameters, ...query});
  }

  Future<Map<String, dynamic>> _getMap(Uri uri) async {
    if (baseUrl.isEmpty) {
      throw CloudApiException('Chưa cấu hình URL RAPID ERP (vào Cài đặt).');
    }
    http.Response resp;
    try {
      resp = await http
          .get(uri, headers: headers.isEmpty ? null : headers)
          .timeout(timeout);
    } catch (e) {
      throw CloudApiException('Không kết nối được RAPID ERP: $e');
    }
    switch (resp.statusCode) {
      case 200:
        break;
      case 401:
        throw CloudApiException('Sai hoặc thiếu API key (401).');
      case 404:
        throw CloudApiException('Không tìm thấy kết quả (404).');
      case 422:
        throw CloudApiException('Tham số không hợp lệ (422).');
      default:
        throw CloudApiException('RAPID ERP trả về HTTP ${resp.statusCode}.');
    }
    Object? decoded;
    try {
      decoded = jsonDecode(utf8.decode(resp.bodyBytes));
    } catch (_) {
      throw CloudApiException('Dữ liệu RAPID ERP không phải JSON hợp lệ.');
    }
    if (decoded is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng phản hồi RAPID ERP không đúng.');
    }
    return decoded;
  }

  /// Danh sách máy: ƯU TIÊN [deviceIds] (admin nhập tay), nếu trống thì suy từ
  /// mã máy được cấp trong phiên (API không có endpoint liệt kê). `"*"`/rỗng bị
  /// bỏ; dedupe không phân biệt hoa thường. Mỗi máy gọi 1 trang `limit=1` để
  /// lấy số lần chạy + lần mới nhất.
  @override
  Future<List<CloudDevice>> listDevices({bool fresh = false}) async {
    final source = deviceIds.isNotEmpty
        ? deviceIds
        : (SessionStore.current?.ids ?? const <String>[]);
    final seen = <String>{};
    final ids = <String>[];
    for (final raw in source) {
      final id = raw.trim();
      if (id.isEmpty || id == '*') continue;
      if (seen.add(id.toLowerCase())) ids.add(id);
    }
    if (ids.isEmpty) return const [];

    // Danh sách dài → BỎ hydrate (mỗi máy 1 request sẽ quá nặng); số lần chạy
    // sẽ hiện khi mở từng máy.
    if (ids.length > 60) {
      return [for (final id in ids) CloudDevice(id: id, runCount: 0)];
    }

    // Hydrate runCount/latest/version theo LÔ 8 (không bắn tất cả request 1 lúc).
    final out = <CloudDevice>[];
    for (var i = 0; i < ids.length; i += 8) {
      final end = (i + 8 < ids.length) ? i + 8 : ids.length;
      out.addAll(await Future.wait(ids.sublist(i, end).map(_deviceMeta)));
    }
    return out;
  }

  /// Lấy số lần chạy + lần mới nhất của 1 máy qua 1 trang `limit=1`.
  Future<CloudDevice> _deviceMeta(String id) async {
    try {
      final page = await listRuns(id, limit: 1, offset: 0);
      final first = page.runs.isNotEmpty ? page.runs.first : null;
      return CloudDevice(
        id: id,
        runCount: page.total,
        latest: first?.timestamp,
        version: first?.version ?? '',
      );
    } catch (_) {
      // Máy lỗi/chưa có dữ liệu → vẫn hiện thẻ (số lần chạy = 0).
      return CloudDevice(id: id, runCount: 0);
    }
  }

  @override
  Future<CloudRunsPage> listRuns(
    String deviceId, {
    int limit = 10,
    int offset = 0,
  }) async {
    final json = await _getMap(_uri(
      '/external/device/${Uri.encodeComponent(deviceId)}/results',
      {'limit': '$limit', 'offset': '$offset'},
    ));
    final items = (json['items'] as List?) ?? const [];
    return CloudRunsPage(
      runs: items
          .whereType<Map>()
          .map((e) => _summaryFromItem(e.cast<String, dynamic>()))
          .toList(),
      total: (json['total'] as num?)?.toInt() ?? items.length,
      offset: (json['offset'] as num?)?.toInt() ?? offset,
      limit: (json['limit'] as num?)?.toInt() ?? limit,
    );
  }

  @override
  Future<TestResult> fetchRun(String resultId) async {
    final json = await _getMap(
        _uri('/external/results/${Uri.encodeComponent(resultId)}/detail'));
    return _fromDetail(json, resultId);
  }

  // --- Map item danh sách (`result_codes`) → TestResult tóm tắt (không curves).

  TestResult _summaryFromItem(Map<String, dynamic> j) {
    final codes = (j['result_codes'] as Map?) ?? const {};
    final slots = <SlotResult>[];
    for (var i = 0; i < 10; i++) {
      final raw = (codes['$i'] ?? codes[i] ?? '').toString();
      slots.add(_slot(i + 1, _letterOf(raw), _ctOf(raw), const [], null));
    }
    return TestResult(
      id: (j['id'] ?? '').toString(),
      deviceId: (j['device_id_raw'] ?? '').toString().trim(),
      timestamp: _bestTime(j),
      slots: slots,
      version: (j['firmware_version'] ?? '').toString().trim(),
      curvesAreRaw: false,
    );
  }

  // --- Map chi tiết (`/detail`) → TestResult kèm đường cong (nếu có) ---------
  //
  // Shape của `/detail` CHƯA được tài liệu chốt cấu trúc (chỉ liệt kê tên field)
  // → parse PHÒNG THỦ: thử (a) mảng/map kênh tường minh, (b) raw_payload kiểu
  // firmware. Lấy được ít nhất CT + phân loại; có `amplification_data` thì bật
  // đường cong (curvesAreRaw=true) cho 4 đồ thị như nguồn Google.
  TestResult _fromDetail(Map<String, dynamic> j, String resultId) {
    var channels = _channelMaps(j);

    // Fallback: chưa có đường cong nào → thử raw_payload (JSON gốc firmware).
    final hasCurve =
        channels.any((c) => _curveOf(c['amplification_data']).isNotEmpty);
    if (!hasCurve && j['raw_payload'] is Map) {
      final fromRaw =
          _channelsFromFirmwareRaw((j['raw_payload'] as Map).cast<String, dynamic>());
      if (fromRaw.any((c) => _curveOf(c['amplification_data']).isNotEmpty)) {
        channels = fromRaw;
      }
    }

    // Lưới phòng cuối: detail vẫn kèm `result_codes` như item danh sách.
    final codes = (j['result_codes'] as Map?) ?? const {};

    final slots = <SlotResult>[];
    var anyCurve = false;
    for (var i = 0; i < 10; i++) {
      final Map<String, dynamic> ch =
          i < channels.length ? channels[i] : const {};
      final curve = _curveOf(ch['amplification_data']);
      if (curve.isNotEmpty) anyCurve = true;

      String letter;
      double? ct;
      if (ch.containsKey('result_code') || ch.containsKey('ct_value')) {
        letter = _letterOf((ch['result_code'] ?? '').toString());
        ct = _numOf(ch['ct_value']);
        // CT chỉ có nghĩa với dương tính (đồng bộ quy ước fromCloudRun).
        final cls = Classification.fromLetter(letter);
        if (cls == Classification.negative ||
            cls == Classification.error ||
            cls == Classification.unknown) {
          ct = null;
        }
      } else {
        final raw = (codes['$i'] ?? codes[i] ?? '').toString();
        letter = _letterOf(raw);
        ct = _ctOf(raw);
      }

      slots.add(_slot(
        i + 1,
        letter,
        ct,
        curve,
        _numOf(ch['calibration_slope'] ?? ch['slope']),
      ));
    }

    return TestResult(
      id: resultId,
      deviceId:
          (j['device_id_raw'] ?? j['device_id'] ?? '').toString().trim(),
      timestamp: _bestTime(j),
      slots: slots,
      version: (j['firmware_version'] ?? j['version'] ?? '').toString().trim(),
      curvesAreRaw: anyCurve,
    );
  }

  /// Chuẩn hoá phần "kênh" của detail về `List<Map>` theo thứ tự kênh 0..9.
  /// Thử lần lượt: mảng/map dưới khoá quen thuộc → dạng cột (mảng song song).
  List<Map<String, dynamic>> _channelMaps(Map<String, dynamic> j) {
    for (final key in const [
      'channels',
      'results',
      'channel_results',
      'data'
    ]) {
      final v = j[key];
      if (v is List && v.isNotEmpty) {
        return v
            .whereType<Map>()
            .map((e) => e.cast<String, dynamic>())
            .toList();
      }
      if (v is Map && v.isNotEmpty) {
        return _mapByIndex(v.cast<String, dynamic>());
      }
    }
    // Dạng "cột": các mảng song song ngay ở top-level.
    if (j['ct_value'] is List ||
        j['result_code'] is List ||
        j['amplification_data'] is List) {
      return _columnar(j);
    }
    return const [];
  }

  /// Map `{"0":{…},"1":{…}}` → list theo key số tăng dần.
  List<Map<String, dynamic>> _mapByIndex(Map<String, dynamic> m) {
    final keys = m.keys.where((k) => int.tryParse(k) != null).toList()
      ..sort((a, b) => int.parse(a).compareTo(int.parse(b)));
    return [
      for (final k in keys)
        if (m[k] is Map) (m[k] as Map).cast<String, dynamic>()
    ];
  }

  /// Các mảng song song (`ct_value:[…]`, `amplification_data:[[…]]`,…) → kênh.
  List<Map<String, dynamic>> _columnar(Map<String, dynamic> j) {
    List asList(Object? x) => x is List ? x : const [];
    final ct = asList(j['ct_value']);
    final res = asList(j['result_code']);
    final amp = asList(j['amplification_data']);
    final slope = asList(j['calibration_slope']);
    final n = [ct.length, res.length, amp.length]
        .fold<int>(0, (m, e) => e > m ? e : m);
    return [
      for (var i = 0; i < n; i++)
        {
          if (i < ct.length) 'ct_value': ct[i],
          if (i < res.length) 'result_code': res[i],
          if (i < amp.length) 'amplification_data': amp[i],
          if (i < slope.length) 'calibration_slope': slope[i],
        }
    ];
  }

  /// raw_payload kiểu firmware (`amplification` chuỗi "a,b,…", `CT_value`,
  /// `result` "x | N", `slopes`) → list kênh dùng chung khoá với detail.
  List<Map<String, dynamic>> _channelsFromFirmwareRaw(Map<String, dynamic> raw) {
    List asList(Object? x) => x is List ? x : const [];
    final amp = asList(raw['amplification']);
    final ct = asList(raw['CT_value']);
    final res = asList(raw['result']);
    final slope = asList(raw['slopes']);
    final n = [amp.length, ct.length, res.length]
        .fold<int>(0, (m, e) => e > m ? e : m);
    return [
      for (var i = 0; i < n; i++)
        {
          if (i < amp.length) 'amplification_data': amp[i],
          if (i < ct.length) 'ct_value': ct[i],
          if (i < res.length) 'result_code': res[i],
          if (i < slope.length) 'calibration_slope': slope[i],
        }
    ];
  }

  SlotResult _slot(
    int index,
    String letter,
    double? ct,
    List<double> curve,
    double? slope,
  ) {
    final cls = Classification.fromLetter(letter);
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
      slope: slope,
    );
  }

  // --- Helper parse ----------------------------------------------------------

  /// Thời gian tốt nhất: `test_timestamp` (giờ máy) → `received_at` (giờ server,
  /// ISO có timezone) → epoch. ISO UTC quy đổi về giờ địa phương để hiển thị.
  DateTime _bestTime(Map<String, dynamic> j) {
    for (final k in const ['test_timestamp', 'received_at', 'time', 'created']) {
      final v = (j[k] ?? '').toString().trim();
      if (v.isEmpty) continue;
      final d = DateTime.tryParse(v);
      if (d != null) return d.toLocal();
    }
    return DateTime.fromMillisecondsSinceEpoch(0);
  }

  /// "22.3 | N" → "N"; "! | E" → "E"; "N" → "N". Trả "?" nếu rỗng.
  String _letterOf(Object? x) {
    final s = (x ?? '').toString().trim();
    if (s.isEmpty) return '?';
    final part = s.contains('|') ? s.split('|').last : s;
    final c = part.trim();
    return c.isEmpty ? '?' : c[0].toUpperCase();
  }

  /// CT từ "22.3 | N" (phần trước `|`). "! | E"/rỗng/"N/A" → null.
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

  /// Đường cong từ mảng số HOẶC chuỗi "1,2,3,". Rỗng nếu không có.
  List<double> _curveOf(Object? e) {
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
}

/// Tạo client cloud đúng theo [source] (nguồn → lớp triển khai). Các màn Cloud
/// gọi hàm này thay vì `new` thẳng → thêm nguồn chỉ sửa ở đây.
CloudHistoryClient buildCloudClient(AppSettings settings, CloudSource source) {
  final url = settings.cloudUrlFor(source);
  final headers = settings.cloudHeadersFor(source);
  switch (source) {
    case CloudSource.rapidErp:
      return RapidErpApi(url,
          headers: headers, deviceIds: settings.rapidErpDeviceIdList);
    case CloudSource.engineer:
      return FbtApi(url, headers: headers);
    case CloudSource.google:
      return CloudHistoryApi(url, headers: headers);
  }
}
