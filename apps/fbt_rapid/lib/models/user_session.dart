/// Vai trò người dùng (3 cấp):
/// - `root`  : Root — quản lý tài khoản (thêm/xóa nhân viên + khách hàng).
/// - `admin` : Nhân viên — full app, KHÔNG quản lý tài khoản.
/// - `user`  : Khách hàng — chỉ xem đồ thị các mã máy được cấp (read-only).
enum UserRole { root, admin, user }

/// Mã vai trò gửi/nhận với backend.
String roleCodeOf(UserRole r) => r == UserRole.root
    ? 'root'
    : r == UserRole.admin
        ? 'admin'
        : 'user';

UserRole roleFromCode(Object? v) {
  switch (v.toString().trim().toLowerCase()) {
    case 'root':
      return UserRole.root;
    case 'admin':
      return UserRole.admin;
    default:
      return UserRole.user;
  }
}

/// Phiên đăng nhập: ai đang dùng app + được phép thấy những mã máy nào.
///
/// Trả về từ Apps Script accounts (action `login`):
///   { ok, username, name, role, ids:[...], allowAll }
class UserSession {
  final String username;
  final String name;
  final String email;
  final UserRole role;

  /// Mã máy được cấp (rỗng nếu [allowAll]). So khớp KHÔNG phân biệt hoa thường.
  final List<String> ids;

  /// Được xem TẤT CẢ máy: **root** (super-admin) HOẶC `ids` chứa `"*"` (full).
  /// Admin/khách hàng KHÔNG có `"*"` → chỉ thấy đúng các mã máy được cấp.
  final bool allowAll;

  const UserSession({
    required this.username,
    required this.name,
    this.email = '',
    required this.role,
    required this.ids,
    required this.allowAll,
  });

  /// Bản sao với email mới (sau khi user đổi email).
  UserSession copyWith({String? email}) => UserSession(
        username: username,
        name: name,
        email: email ?? this.email,
        role: role,
        ids: ids,
        allowAll: allowAll,
      );

  bool get isRoot => role == UserRole.root;
  bool get isAdmin => role == UserRole.admin; // "Nhân viên" cụ thể
  /// Nhân sự (root + admin): full app + được GHI (Lưu/Đồng bộ/Xóa). LƯU Ý: KHÔNG
  /// còn tự thấy mọi máy — phạm vi xem theo [allowAll]/[ids] như mọi vai trò.
  bool get isStaff => role == UserRole.root || role == UserRole.admin;
  String get roleCode => roleCodeOf(role);

  /// Xem máy: [allowAll] (root hoặc ids chứa "*") thấy hết; còn lại — kể cả
  /// **admin** — chỉ thấy đúng mã máy được cấp.
  bool canSee(String deviceId) {
    if (allowAll) return true;
    final id = deviceId.trim().toLowerCase();
    if (id.isEmpty) return false;
    return ids.any((e) => e.trim().toLowerCase() == id);
  }

  factory UserSession.fromJson(Map<String, dynamic> j) {
    final role = roleFromCode(j['role']);
    final ids = ((j['ids'] as List?) ?? const [])
        .map((e) => e.toString().trim())
        .where((e) => e.isNotEmpty)
        .toList();
    return UserSession(
      username: (j['username'] ?? '').toString(),
      name: (j['name'] ?? '').toString(),
      email: (j['email'] ?? '').toString(),
      role: role,
      ids: ids,
      // Full view CHỈ khi root (super-admin) hoặc ids chứa "*". Admin KHÔNG còn
      // auto thấy mọi máy → lọc theo ids như khách hàng. (Tự tính, không tin
      // `allowAll` từ backend cũ — phòng backend chưa deploy lại vẫn trả true.)
      allowAll: role == UserRole.root || ids.contains('*'),
    );
  }

  Map<String, dynamic> toJson() => {
        'username': username,
        'name': name,
        'email': email,
        'role': roleCode,
        'ids': ids,
        'allowAll': allowAll,
      };
}
