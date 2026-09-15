import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/test_result.dart';

class DeviceApiException implements Exception {
  final String message;
  DeviceApiException(this.message);
  @override
  String toString() => message;
}

/// Client HTTP nói chuyện với web server của thiết bị FBT_RAPID.
///
/// Thiết bị chỉ có endpoint: GET /  và  GET /getdata  (xem docs/APP_SPEC.md).
class DeviceApi {
  /// Base URL, ví dụ "http://192.168.1.50" (có thể kèm cổng).
  final String baseUrl;
  final Duration timeout;

  DeviceApi(String host, {this.timeout = const Duration(seconds: 10)})
      : baseUrl = _normalize(host);

  static String _normalize(String host) {
    var h = host.trim();
    if (h.isEmpty) return h;
    if (!h.startsWith('http://') && !h.startsWith('https://')) {
      h = 'http://$h';
    }
    // bỏ dấu '/' cuối
    while (h.endsWith('/')) {
      h = h.substring(0, h.length - 1);
    }
    return h;
  }

  Uri _uri(String path) => Uri.parse('$baseUrl$path');

  /// Lấy kết quả lần chạy gần nhất từ máy.
  Future<TestResult> fetchLatest() async {
    if (baseUrl.isEmpty) {
      throw DeviceApiException('Chưa cấu hình địa chỉ IP của máy (vào Cài đặt).');
    }
    http.Response resp;
    try {
      resp = await http.get(_uri('/getdata')).timeout(timeout);
    } catch (e) {
      throw DeviceApiException('Không kết nối được tới máy ($baseUrl): $e');
    }
    if (resp.statusCode != 200) {
      throw DeviceApiException('Máy trả về HTTP ${resp.statusCode}.');
    }
    Map<String, dynamic> json;
    try {
      json = jsonDecode(resp.body) as Map<String, dynamic>;
    } catch (e) {
      throw DeviceApiException('Dữ liệu trả về không phải JSON hợp lệ.');
    }
    return TestResult.fromDeviceJson(json, fetchedAt: DateTime.now());
  }

  /// Kiểm tra kết nối: trả về id_device nếu OK.
  Future<String> ping() async {
    final r = await fetchLatest();
    return r.deviceId.isEmpty ? '(không có id_device)' : r.deviceId;
  }

  /// (Roadmap) Gửi cấu hình WiFi cho máy nếu firmware có endpoint /setwifi.
  /// Hiện firmware CHƯA có endpoint này — sẽ ném lỗi 404; UI bắt lỗi để hướng
  /// dẫn dùng captive portal của WiFiManager.
  Future<void> setWifi(String ssid, String password) async {
    final uri = _uri('/setwifi').replace(queryParameters: {
      'ssid': ssid,
      'pass': password,
    });
    http.Response resp;
    try {
      resp = await http.get(uri).timeout(timeout);
    } catch (e) {
      throw DeviceApiException('Không gửi được tới máy: $e');
    }
    if (resp.statusCode != 200) {
      throw DeviceApiException(
          'Máy chưa hỗ trợ /setwifi (HTTP ${resp.statusCode}).');
    }
  }
}
