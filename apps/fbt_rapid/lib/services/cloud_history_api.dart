import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/test_result.dart';

class CloudApiException implements Exception {
  final String message;
  CloudApiException(this.message);
  @override
  String toString() => message;
}

/// Một máy (device id) tổng hợp từ folder Drive (doGet action=ids).
class CloudDevice {
  final String id;
  final int runCount;
  final DateTime? latest;
  final String version; // version firmware của lần chạy mới nhất

  const CloudDevice({
    required this.id,
    required this.runCount,
    this.latest,
    this.version = '',
  });

  factory CloudDevice.fromJson(Map<String, dynamic> j) => CloudDevice(
        id: (j['id'] ?? '').toString(),
        runCount: (j['runCount'] as num?)?.toInt() ?? 0,
        latest: DateTime.tryParse((j['latest'] ?? '').toString()),
        version: (j['version'] ?? '').toString(),
      );

  Map<String, dynamic> toJson() => {
        'id': id,
        'runCount': runCount,
        'latest': latest?.toIso8601String(),
        'version': version,
      };
}

/// Một trang kết quả của [CloudHistoryApi.listRuns] — kèm `total` để phân trang.
class CloudRunsPage {
  final List<TestResult> runs;
  final int total; // tổng số lần chạy của máy (mọi trang)
  final int offset;
  final int limit;

  const CloudRunsPage({
    required this.runs,
    required this.total,
    required this.offset,
    required this.limit,
  });
}

/// Giao diện CHUNG cho mọi nguồn cloud (Google/self-hosted/RAPID ERP). Các màn
/// Cloud (`cloud_devices`/`cloud_runs`) chỉ phụ thuộc giao diện này; nguồn cụ
/// thể do [buildCloudClient] (xem rapid_erp_api.dart) quyết định theo
/// [CloudSource]. Nhờ vậy thêm nguồn mới KHÔNG phải sửa UI.
abstract class CloudHistoryClient {
  /// Danh sách máy (id, số lần chạy, lần mới nhất).
  Future<List<CloudDevice>> listDevices({bool fresh = false});

  /// Một trang tóm tắt các lần chạy của 1 máy (KHÔNG kèm đường cong).
  Future<CloudRunsPage> listRuns(String deviceId, {int limit = 10, int offset = 0});

  /// Chi tiết 1 lần chạy (kèm `curves` để vẽ đồ thị CT khi nguồn có).
  Future<TestResult> fetchRun(String fileId);
}

/// Client đọc lịch sử nhiều máy từ cloud qua Apps Script `doGet` (nguồn Google,
/// hợp đồng `action=ids|runs|run`).
///
/// Backend: [sheet/getData.js] — xem hợp đồng API ở docs/APP_SPEC.md §10.
/// Khác với [DeviceApi] (đọc trực tiếp 1 máy qua LAN), client này đọc dữ liệu
/// đã đẩy lên Google Drive, gom theo từng `id_device`.
class CloudHistoryApi implements CloudHistoryClient {
  /// URL web app Apps Script, dạng:
  /// `https://script.google.com/macros/s/<DEPLOY_ID>/exec`
  final String baseUrl;
  final Duration timeout;

  /// Header gửi kèm MỌI request — vd `{'Authorization': 'Bearer <token>'}` cho
  /// server tự host. Rỗng (mặc định) cho Apps Script Google (không cần auth).
  final Map<String, String> headers;

  CloudHistoryApi(
    String url, {
    this.timeout = const Duration(seconds: 20),
    this.headers = const {},
  }) : baseUrl = url.trim();

  Uri _uri(Map<String, String> params) {
    final u = Uri.parse(baseUrl);
    return u.replace(queryParameters: {...u.queryParameters, ...params});
  }

  Future<Map<String, dynamic>> _getJson(
    Map<String, String> params, {
    Duration? timeout,
  }) async {
    if (baseUrl.isEmpty) {
      throw CloudApiException('Chưa cấu hình URL cloud (vào Cài đặt).');
    }
    http.Response resp;
    try {
      resp = await http
          .get(_uri(params), headers: headers.isEmpty ? null : headers)
          .timeout(timeout ?? this.timeout);
    } catch (e) {
      throw CloudApiException('Không kết nối được cloud: $e');
    }
    if (resp.statusCode != 200) {
      throw CloudApiException('Cloud trả về HTTP ${resp.statusCode}.');
    }
    Object? decoded;
    try {
      decoded = jsonDecode(resp.body);
    } catch (e) {
      throw CloudApiException(
          'Dữ liệu cloud không phải JSON hợp lệ (URL /exec đúng chưa?).');
    }
    if (decoded is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng phản hồi cloud không đúng.');
    }
    if (decoded['ok'] != true) {
      throw CloudApiException('Cloud lỗi: ${decoded['error'] ?? 'không rõ'}');
    }
    return decoded;
  }

  /// action=ids → danh sách máy (id, số lần chạy, lần mới nhất).
  ///
  /// Quét toàn bộ folder (hàng nghìn file) nên có thể mất 20–60s — dùng timeout
  /// dài hơn mặc định để không bị cắt giữa chừng.
  @override
  Future<List<CloudDevice>> listDevices({bool fresh = false}) async {
    final json = await _getJson(
      {'action': 'ids', if (fresh) 'nocache': '1'},
      timeout: const Duration(seconds: 120),
    );
    final list = (json['devices'] as List?) ?? const [];
    return list
        .map((e) => CloudDevice.fromJson(e as Map<String, dynamic>))
        .toList();
  }

  /// action=runs → một trang tóm tắt các lần chạy của 1 máy (mới nhất trước,
  /// KHÔNG kèm đường cong — gọi [fetchRun] khi cần vẽ đồ thị).
  @override
  Future<CloudRunsPage> listRuns(
    String deviceId, {
    int limit = 10,
    int offset = 0,
  }) async {
    final json = await _getJson({
      'action': 'runs',
      'id': deviceId,
      'limit': '$limit',
      'offset': '$offset',
    });
    final list = (json['runs'] as List?) ?? const [];
    return CloudRunsPage(
      runs: list
          .map((e) => TestResult.fromCloudRun(e as Map<String, dynamic>))
          .toList(),
      total: (json['total'] as num?)?.toInt() ?? list.length,
      offset: (json['offset'] as num?)?.toInt() ?? offset,
      limit: (json['limit'] as num?)?.toInt() ?? limit,
    );
  }

  /// action=run → chi tiết 1 lần chạy (kèm `curves` để vẽ đồ thị CT).
  @override
  Future<TestResult> fetchRun(String fileId) async {
    final json = await _getJson({'action': 'run', 'fileId': fileId});
    final run = json['run'];
    if (run is! Map<String, dynamic>) {
      throw CloudApiException('Thiếu dữ liệu chi tiết lần chạy.');
    }
    return TestResult.fromCloudRun(run);
  }
}
