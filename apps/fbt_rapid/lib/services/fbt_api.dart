import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/test_result.dart';
import 'app_settings.dart';
import 'cloud_history_api.dart';
import 'firmware_history.dart';

/// Client đọc lịch sử từ **FBT Home Server** (nguồn "Engineer Server") — server
/// FastAPI của kỹ sư (source: `server/app/`, expose qua Cloudflare Tunnel, mặc
/// định `https://hub.fortebio.tech`).
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
/// Một bản firmware `.bin` nằm trong kho OTA của server.
/// Một máy đang được ghim vào bản firmware riêng.
///
/// [by] là **do client tự khai** khi bấm — token admin không mang danh tính nào để server
/// đối chiếu. Ghi chú vận hành ("ai đặt cái này?"), KHÔNG phải nhật ký kiểm toán.
/// Rỗng = ghim từ trước khi có trường này, hoặc đặt bằng curl.
/// Một lỗi cảm biến máy tự phát hiện và gửi về (`errorCheck.cpp` phía firmware).
///
/// ⚠️ Firmware gửi lỗi bằng POST **RIÊNG**, không nhét vào payload kết quả — nên việc nó
/// thuộc lần đo nào là **suy theo thời gian** ở server (`db.session_errors`), không phải
/// khoá ngoại. Màn hiển thị phải nói rõ điều đó.
class SensorError {
  /// Nguyên văn máy gửi, vd `Slot 6`. Không parse ra số: chuỗi này do firmware dựng từ
  /// `errorSlotStr[module][slot]` và không phải lỗi nào cũng gắn với một slot.
  final String slot;

  /// Mã 4 chữ số `module*1000 + type*100 + step*10 + slot` — **in y hệt mã trên màn TFT**
  /// để kỹ sư đọc chéo hai nơi.
  final String code;
  final String message;
  final DateTime? at;

  const SensorError({
    required this.slot,
    required this.code,
    required this.message,
    this.at,
  });
}

class OtaPin {
  final String file;
  final String by;
  final DateTime? at;

  const OtaPin({required this.file, this.by = '', this.at});
}

class OtaFile {
  final String name; // đồng thời là "version" — đặt tên file có version
  final int size;
  final DateTime? modified;

  const OtaFile({required this.name, required this.size, this.modified});
}

/// Trạng thái kho OTA: các bản đã tải lên + bản đang được chọn để thiết bị nạp.
class OtaState {
  final String? target; // null = chưa chọn → thiết bị không cập nhật gì

  /// Ai đặt bản CHUNG + lúc nào — cùng loại "client tự khai" như [OtaPin.by].
  /// Rỗng = đặt trước khi có trường này, hoặc đặt bằng curl.
  final String targetBy;
  final DateTime? targetAt;

  final List<OtaFile> files;

  /// Máy được **ghim** một bản riêng: `{id_device: OtaPin}`. Ghim thắng [target].
  /// Chỉ có tác dụng với firmware **v2.4.4 trở lên** — bản cũ không gọi `/ota/check`
  /// nên ghim cho chúng là im lặng không xảy ra gì. UI phải gác, server không biết được.
  final Map<String, OtaPin> devices;

  const OtaState({
    this.target,
    this.targetBy = '',
    this.targetAt,
    this.files = const [],
    this.devices = const {},
  });
}

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
    return _decode(resp);
  }

  /// PUT/DELETE (thao tác ghi của mục OTA). Dùng `http.Request` thay vì
  /// `http.put` để gửi được body bytes thô của file .bin.
  Future<dynamic> _send(String method, String path, {List<int>? body}) async {
    if (baseUrl.isEmpty) {
      throw CloudApiException(
          'Chưa cấu hình URL Engineer Server (vào Cài đặt).');
    }
    final req = http.Request(method, _uri(path))..headers.addAll(headers);
    if (body != null) {
      req.bodyBytes = body;
      req.headers['Content-Type'] = 'application/octet-stream';
    }
    http.Response resp;
    try {
      resp = await http.Response.fromStream(await req.send().timeout(timeout));
    } catch (e) {
      throw CloudApiException('Không kết nối được Engineer Server: $e');
    }
    return _decode(resp);
  }

  /// Map HTTP → lỗi tiếng Việt rồi parse JSON (dùng chung cho GET và PUT/DELETE).
  dynamic _decode(http.Response resp) {
    switch (resp.statusCode) {
      case 200:
        break;
      case 400:
        // Server kèm lý do trong `detail` (vd tên file OTA không hợp lệ).
        throw CloudApiException(_detailOf(resp) ?? 'Yêu cầu không hợp lệ (400).');
      case 401:
        // Nói thẳng CÁCH CHỮA, vì nguyên nhân gần như luôn là một cái: token API chỉ được
        // cấp lúc ĐĂNG NHẬP (`AuthApi.login` → `api_token_for` theo vai trò), mà `_AuthGate`
        // khôi phục phiên cũ bằng `SessionStore.load()` KHÔNG gọi `/auth`. Máy nào đăng nhập
        // từ trước đợt cấp-token-theo-vai-trò (2026-08-19) sẽ giữ token THIẾT BỊ vĩnh viễn:
        // đọc chạy bình thường, ghi OTA 401 — và người dùng không có manh mối nào.
        // Gặp thật trên bản web điện thoại 2026-08-20 với tài khoản root.
        throw CloudApiException(
            'Sai hoặc thiếu token (401). Token cấp lúc ĐĂNG NHẬP, mà phiên khôi phục '
            'từ lần trước vẫn giữ token cũ — hãy Đăng xuất rồi đăng nhập lại.');
      case 404:
        throw CloudApiException('Không tìm thấy dữ liệu (404).');
      case 405:
        // Server có route `POST /{path}` catch-all → MỌI path chưa khai báo trả
        // 405 (không phải 404). Hai nguyên nhân thật: URL trong Cài đặt thừa
        // path (vd .../api), HOẶC server chưa deploy route đó (mục OTA mới).
        throw CloudApiException(
            'Server không có endpoint này (405). Kiểm tra URL trong Cài đặt chỉ '
            'là URL GỐC (vd $kDefaultEngineerUrl); nếu URL đúng thì server chưa '
            'được cập nhật cho tính năng này.');
      case 413:
        throw CloudApiException('File quá lớn, server từ chối (413).');
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

  String? _detailOf(http.Response resp) {
    try {
      final j = jsonDecode(utf8.decode(resp.bodyBytes));
      final d = (j is Map) ? j['detail'] : null;
      return (d == null || '$d'.isEmpty) ? null : '$d';
    } catch (_) {
      return null;
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
          // version firmware của phiên gần nhất — màn "Trạng thái máy" hiện cột
          // này. Server cũ chưa trả field → rỗng, UI hiện "—".
          version: (e['version'] ?? '').toString(),
        )
    ];
  }

  /// Nhật ký **mốc thật** máy tự khai đổi version — `GET /devices/{id}/fw-log`.
  /// Mới nhất trước.
  ///
  /// **Nuốt MỌI lỗi, trả rỗng.** Đây là lớp làm giàu cho lịch sử firmware, không
  /// phải nguồn chính: server chưa deploy route này trả **405** (route
  /// `POST /{path}` catch-all khớp path, sai method — xem ghi chú OTA bên dưới),
  /// và máy chạy firmware < v2.4.5 thì chẳng bao giờ có mốc nào. Ném lỗi ở đây
  /// là làm hỏng cả hộp thoại vì thiếu phần trang trí. Mạng hỏng thật thì
  /// `listRuns` gọi ngay cạnh sẽ báo.
  Future<List<FwLogEntry>> fwLog(String deviceId) async {
    try {
      final json = await _get(_uri('/devices/$deviceId/fw-log'));
      if (json is! List) return const [];
      return [
        for (final e in json.whereType<Map>())
          if (DateTime.tryParse((e['at'] ?? '').toString()) != null &&
              (e['version'] ?? '').toString().isNotEmpty)
            FwLogEntry(
              version: (e['version']).toString(),
              at: DateTime.parse((e['at']).toString()).toLocal(),
              // `how: "update"` = máy tự khai vừa nạp xong. Server cũ không có field
              // này → false, tức rơi về hành vi cũ, không phải lỗi.
              confirmed: (e['how'] ?? '').toString() == 'update',
            )
      ];
    } catch (_) {
      return const [];
    }
  }

  /// Lỗi cảm biến máy báo về TRONG lần đo `sessionId` — `GET /sessions/{id}/errors`.
  ///
  /// **Nuốt MỌI lỗi, trả rỗng** — cùng lý do `fwLog`: server chưa deploy route này trả 405
  /// (catch-all `POST /{path}` khớp path, sai method), và rỗng vốn là kết quả BÌNH THƯỜNG
  /// (máy chạy sạch). Ném lỗi ở đây là làm hỏng cả màn chi tiết vì thiếu một bảng phụ.
  Future<List<SensorError>> sessionErrors(String sessionId) async {
    try {
      final json = await _get(_uri('/sessions/$sessionId/errors'));
      final list = (json is Map ? json['errors'] : null) as List?;
      if (list == null) return const [];
      return [
        for (final e in list.whereType<Map>())
          SensorError(
            slot: (e['slot'] ?? '').toString(),
            code: (e['code'] ?? '').toString(),
            message: (e['message'] ?? '').toString(),
            at: DateTime.tryParse((e['at'] ?? '').toString())?.toLocal(),
          )
      ];
    } catch (_) {
      return const [];
    }
  }

  // --- OTA firmware (tab Quản lý máy) ---------------------------------------
  // Thao tác GHI dùng PUT/DELETE vì server có route `POST /{path}` catch-all
  // (ingest) nuốt mọi POST — xem ghi chú trong server/app/main.py.

  /// Kho firmware trên server + bản đang chọn (`GET /ota`).
  Future<OtaState> listOta() async {
    final json = await _get(_uri('/ota'));
    if (json is! Map<String, dynamic>) {
      throw CloudApiException('Định dạng /ota không đúng.');
    }
    final files = (json['files'] as List?) ?? const [];
    final devs = (json['devices'] as Map?) ?? const {};
    // Server cũ trả chuỗi tên file trần; server hiện tại trả {file, by, at}. Đọc cả hai.
    OtaPin? pin(dynamic v) {
      if (v is String) return v.isEmpty ? null : OtaPin(file: v);
      if (v is Map) {
        final f = (v['file'] ?? '').toString();
        return f.isEmpty
            ? null
            : OtaPin(
                file: f,
                by: (v['by'] ?? '').toString(),
                at: DateTime.tryParse((v['at'] ?? '').toString())?.toLocal(),
              );
      }
      return null;
    }
    return OtaState(
      target: (json['target'] as String?)?.trim().isEmpty ?? true
          ? null
          : json['target'] as String,
      targetBy: (json['target_by'] ?? '').toString(),
      targetAt:
          DateTime.tryParse((json['target_at'] ?? '').toString())?.toLocal(),
      devices: {
        for (final e in devs.entries)
          if (pin(e.value) case final p?) '${e.key}': p
      },
      files: [
        for (final e in files.whereType<Map>())
          OtaFile(
            name: (e['name'] ?? '').toString(),
            size: (e['size'] as num?)?.toInt() ?? 0,
            modified:
                DateTime.tryParse((e['modified'] ?? '').toString())?.toLocal(),
          )
      ],
    );
  }

  /// Tải 1 file .bin lên (body = bytes thô, KHÔNG multipart).
  Future<void> uploadOta(String name, List<int> bytes) =>
      _send('PUT', '/ota/${Uri.encodeComponent(name)}', body: bytes);

  /// Chọn bản firmware sẽ nạp ở lần `/ota/check` tới.
  ///
  /// [device] rỗng → bản CHUNG cho cả fleet. Có [device] → **ghim riêng máy đó**, thắng
  /// bản chung. Ghim chỉ ăn với firmware **v2.4.4+** (bản cũ không gọi `/ota/check`).
  /// [by] = tài khoản đang thao tác, chỉ để hiển thị "Người thiết lập" (cột của ghim,
  /// dải trạng thái của bản chung). Gửi cho CẢ HAI: đẩy cho 109 máy mà không ghi lại ai
  /// đẩy thì câu hỏi "ai set firmware?" không có chỗ nào trả lời.
  Future<void> setOtaTarget(String name,
      {String device = '', String by = ''}) {
    // KHÔNG gửi `?device=` rỗng: server sẽ ghim vào khoá "" thay vì đổi bản chung.
    final q = {
      if (device.isNotEmpty) 'device': device,
      if (by.isNotEmpty) 'by': by,
    };
    return _send(
      'PUT',
      '/ota/target/${Uri.encodeComponent(name)}'
      '${q.isEmpty ? '' : '?${Uri(queryParameters: q).query}'}',
    );
  }

  /// Huỷ chọn. [device] rỗng → bỏ bản CHUNG nhưng **giữ nguyên các ghim riêng**
  /// (bỏ chọn cho fleet không được âm thầm thả máy đang bị giữ lại).
  /// Có [device] → chỉ gỡ ghim của máy đó, nó quay về theo bản chung.
  Future<void> clearOtaTarget({String device = ''}) => _send(
        'DELETE',
        '/ota/target${device.isEmpty ? '' : '?device=${Uri.encodeComponent(device)}'}',
      );

  /// Xoá 1 bản firmware khỏi server.
  Future<void> deleteOta(String name) =>
      _send('DELETE', '/ota/${Uri.encodeComponent(name)}');

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
      final res =
          parseResultCell(a['result'] ?? (i < resList.length ? resList[i] : ''));
      final ct = _numOf(a['ct_value']) ??
          _numOf(i < ctList.length ? ctList[i] : null) ??
          res.ct;
      final curve = [
        for (final p in (a['points'] as List? ?? const []))
          p is num ? p.toDouble() : (double.tryParse('$p') ?? 0.0)
      ];
      if (curve.isNotEmpty) anyCurve = true;
      slots.add(_slot(i + 1, res.letter, ct, curve,
          _numOf(i < slopesList.length ? slopesList[i] : null),
          name: res.name));
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
      final res = parseResultCell(i < resList.length ? resList[i] : '');
      final ct = _numOf(i < ctList.length ? ctList[i] : null) ?? res.ct;
      slots.add(_slot(i + 1, res.letter, ct, const [], null, name: res.name));
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

  // --- Helper parse (chuỗi `result` tách bằng [parseResultCell]) ------------

  SlotResult _slot(int index, String letter, double? ct, List<double> curve,
      double? slope,
      {String name = ''}) {
    final cls = Classification.fromLetter(letter);
    // CT chỉ có nghĩa với dương tính (đồng bộ quy ước fromCloudRun).
    final shownCt = (cls == Classification.negative ||
            cls == Classification.error ||
            cls == Classification.unknown)
        ? null
        : ct;
    return SlotResult(
        index: index,
        name: name,
        classification: cls,
        ct: shownCt,
        curve: curve,
        slope: slope);
  }

  /// `received_at` ISO (có timezone) → giờ địa phương; hỏng/thiếu → epoch 0.
  DateTime _timeOf(Map<String, dynamic> j) =>
      DateTime.tryParse((j['received_at'] ?? '').toString())?.toLocal() ??
      DateTime.fromMillisecondsSinceEpoch(0);

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
