import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/user_session.dart';
import 'app_settings.dart';

class AuthException implements Exception {
  final String message;
  AuthException(this.message);
  @override
  String toString() => message;
}

/// Một tài khoản trong sheet Accounts (cho màn Quản lý User). KHÔNG chứa mật khẩu.
class AccountInfo {
  final String username;
  final String role;
  final List<String> ids;
  final String name;
  final String email;
  final bool active;

  const AccountInfo({
    required this.username,
    required this.role,
    required this.ids,
    required this.name,
    required this.email,
    required this.active,
  });

  bool get isRoot => role == 'root';
  bool get isAdmin => role == 'admin';
  bool get isStaff => role == 'root' || role == 'admin';

  factory AccountInfo.fromJson(Map<String, dynamic> j) => AccountInfo(
        username: (j['username'] ?? '').toString(),
        role: (j['role'] ?? 'user').toString(),
        ids: ((j['ids'] as List?) ?? const [])
            .map((e) => e.toString())
            .toList(),
        name: (j['name'] ?? '').toString(),
        email: (j['email'] ?? '').toString(),
        active: j['active'] != false, // mặc định hoạt động
      );
}

/// Client tài khoản/đăng nhập. Backend là **Engineer Server** `POST /auth`
/// (Postgres, `../Server/app/auth.py`) — đã CHUYỂN từ Apps Script userAuth.js
/// (2026-07) nhưng nói CÙNG hợp đồng JSON nên client dùng được cho cả hai:
///   POST {action:"login", username, password}
///     → {ok:true, username, name, role, ids:[...], allowAll}  (thành công)
///     → {ok:false, error}                                     (sai/khoá)
/// Khác biệt duy nhất: Engineer Server cần header Bearer (cửa chung) → [headers].
class AuthApi {
  final String baseUrl;
  final Duration timeout;
  final Map<String, String> headers;

  AuthApi(String url,
      {this.timeout = const Duration(seconds: 20), this.headers = const {}})
      : baseUrl = url.trim();

  /// AuthApi trỏ Engineer Server (URL + token lấy từ Cài đặt, mặc định
  /// kDefaultEngineerUrl / FBT_TOKEN nạp lúc build).
  static Future<AuthApi> engineer() async {
    final s = await AppSettings.load();
    return AuthApi(
      '${s.cloudUrlFor(CloudSource.engineer).replaceAll(RegExp(r'/+$'), '')}/auth',
      headers: s.cloudHeadersFor(CloudSource.engineer),
    );
  }

  Future<UserSession> login(String username, String password) async {
    final decoded = await _post({
      'action': 'login',
      'username': username,
      'password': password,
    });
    if (decoded['ok'] != true) {
      throw AuthException(
          (decoded['error'] ?? 'Đăng nhập thất bại').toString());
    }
    // Server cấp token API sau đăng nhập thành công → LƯU ĐÈ, mỗi lần đăng nhập.
    //
    // Trước 2026-08-19 chỉ lưu KHI Ô ĐANG TRỐNG, và đó là nguồn của lỗi "chọn
    // bản OTA trên điện thoại thì 401": máy mới đăng nhập nhận token THIẾT BỊ
    // (server phát chung cho mọi vai trò), đọc chạy bình thường nhưng ghi OTA
    // thì hỏng — mà cách chữa duy nhất là dán tay token admin vào từng máy.
    //
    // Nay server phát token THEO VAI TRÒ (`auth.api_token_for`): nhân sự nhận
    // token ghi OTA, khách hàng nhận token đọc. Ghi đè mỗi lần đăng nhập nên
    // đổi vai trò hay xoay token đều tự áp, không phải đụng tay ở đâu.
    //
    // ⚠️ THỨ TỰ TRIỂN KHAI: **server trước, app sau**. Chạy app này với server
    // CHƯA vá thì token admin đã dán tay bị ghi đè bằng token thiết bị → OTA
    // 401. Kiểm server đã vá chưa: đăng nhập bằng tài khoản root rồi so
    // `apiToken` trả về với `OTA_ADMIN_TOKEN` trong `/etc/fbt-receiver.env`.
    final tok = (decoded['apiToken'] ?? '').toString().trim();
    if (tok.isNotEmpty) {
      final s = await AppSettings.load();
      if (s.engineerToken.trim() != tok) {
        s.engineerToken = tok;
        await s.save();
      }
    }
    return UserSession.fromJson(decoded);
  }

  /// User tự đổi mật khẩu (xác thực bằng mật khẩu hiện tại).
  Future<void> changePassword(
      String username, String oldPassword, String newPassword) async {
    final decoded = await _post({
      'action': 'changePassword',
      'username': username,
      'oldPassword': oldPassword,
      'newPassword': newPassword,
    });
    if (decoded['ok'] != true) {
      throw AuthException(
          (decoded['error'] ?? 'Đổi mật khẩu thất bại').toString());
    }
  }

  /// User tự đổi email (xác thực bằng mật khẩu hiện tại).
  Future<void> changeEmail(
      String username, String password, String newEmail) async {
    final decoded = await _post({
      'action': 'changeEmail',
      'username': username,
      'password': password,
      'email': newEmail,
    });
    if (decoded['ok'] != true) {
      throw AuthException(
          (decoded['error'] ?? 'Đổi email thất bại').toString());
    }
  }

  // ===== Admin: quản lý user (cần adminUser + adminPassword mỗi lần) =====

  /// Liệt kê tài khoản (admin). KHÔNG trả mật khẩu.
  Future<List<AccountInfo>> listUsers(
      String adminUser, String adminPassword) async {
    final d = await _post({
      'action': 'listUsers',
      'adminUser': adminUser,
      'adminPassword': adminPassword,
    });
    if (d['ok'] != true) {
      throw AuthException((d['error'] ?? 'Không tải được danh sách').toString());
    }
    final list = (d['users'] as List?) ?? const [];
    return list
        .map((e) => AccountInfo.fromJson(e as Map<String, dynamic>))
        .toList();
  }

  /// Tạo mới / cập nhật 1 user (admin). [password]/[email] rỗng = giữ nguyên.
  Future<void> saveUser(
    String adminUser,
    String adminPassword, {
    required String username,
    required String role,
    required List<String> ids,
    String name = '',
    String? password,
    String? email,
    bool active = true,
  }) async {
    final user = <String, dynamic>{
      'username': username,
      'role': role,
      'ids': ids,
      'name': name,
      'active': active,
    };
    if (password != null && password.isNotEmpty) user['password'] = password;
    if (email != null) user['email'] = email;
    final d = await _post({
      'action': 'saveUser',
      'adminUser': adminUser,
      'adminPassword': adminPassword,
      'user': user,
    });
    if (d['ok'] != true) {
      throw AuthException((d['error'] ?? 'Lưu user thất bại').toString());
    }
  }

  /// Xóa 1 user (admin).
  Future<void> deleteUser(
      String adminUser, String adminPassword, String username) async {
    final d = await _post({
      'action': 'deleteUser',
      'adminUser': adminUser,
      'adminPassword': adminPassword,
      'username': username,
    });
    if (d['ok'] != true) {
      throw AuthException((d['error'] ?? 'Xóa user thất bại').toString());
    }
  }

  Future<Map<String, dynamic>> _post(Map<String, dynamic> payload) async {
    if (baseUrl.isEmpty) {
      throw AuthException('Chưa cấu hình URL đăng nhập.');
    }
    final client = http.Client();
    try {
      http.StreamedResponse streamed;
      try {
        // POST tới /exec. Apps Script xử lý body rồi trả 302 tới URL "echo"
        // (script.googleusercontent.com) chứa kết quả → tự đi theo bằng GET
        // (http.post mặc định KHÔNG đi theo redirect này một cách đáng tin).
        var url = Uri.parse(baseUrl);
        final req = http.Request('POST', url)
          ..followRedirects = false
          // text/plain → Apps Script vẫn đọc e.postData.contents; tránh preflight.
          ..headers['Content-Type'] = 'text/plain;charset=utf-8'
          // Bearer cho Engineer Server; KHÔNG gửi theo hop redirect (tránh rò token).
          ..headers.addAll(headers)
          ..body = jsonEncode(payload);
        streamed = await client.send(req).timeout(timeout);
        var hops = 0;
        while (streamed.statusCode >= 300 &&
            streamed.statusCode < 400 &&
            hops < 5) {
          final loc = streamed.headers['location'];
          if (loc == null || loc.isEmpty) break;
          // Phải xả hết body của response redirect, nếu không kết nối tái dùng
          // bị "bẩn" → hop GET sau nhận 401. (Bug đã gặp khi chưa drain.)
          await streamed.stream.drain();
          url = url.resolve(loc);
          final getReq = http.Request('GET', url)..followRedirects = false;
          streamed = await client.send(getReq).timeout(timeout);
          hops++;
        }
      } catch (e) {
        throw AuthException('Không kết nối được máy chủ: $e');
      }
      final resp = await http.Response.fromStream(streamed);
      if (resp.statusCode != 200) {
        throw AuthException('Máy chủ trả về HTTP ${resp.statusCode}.');
      }
      Object? decoded;
      try {
        // Giải mã UTF-8 tường minh để giữ dấu tiếng Việt (tên người dùng).
        decoded = jsonDecode(utf8.decode(resp.bodyBytes));
      } catch (_) {
        throw AuthException('Phản hồi không hợp lệ (URL /exec đúng chưa?).');
      }
      if (decoded is! Map<String, dynamic>) {
        throw AuthException('Định dạng phản hồi không đúng.');
      }
      return decoded;
    } finally {
      client.close();
    }
  }
}
