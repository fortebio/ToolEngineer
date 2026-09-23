/// Vai trò người dùng (5 cấp — 2 vai trò xưởng thêm 2026-09-07, xem
/// `docs/plan/tai-khoan-nha-may.md`):
/// - `root`     : Root — quản lý MỌI tài khoản, full app.
/// - `admin`    : Nhân viên kỹ thuật/CSKH — full app trừ quản lý tài khoản.
/// - `manager`  : Quản lý sản xuất — tab Sản xuất đầy đủ + xem trạng thái máy;
///   quản lý tài khoản **thao tác viên**; KHÔNG ghi OTA, KHÔNG xem dữ liệu lâm sàng.
/// - `operator` : Thao tác viên xưởng — CHỈ chạy trạm + tra hồ sơ máy.
/// - `user`     : Khách hàng — chỉ xem đồ thị các mã máy được cấp (read-only).
///
/// ⚠️ Vai trò lạ (app cũ gặp vai trò mới, hoặc backend trả rác) → **`user`**:
/// fail-closed, mất quyền chứ không thừa quyền. Hệ quả thực tế: phải cập nhật
/// app trên máy trạm TRƯỚC khi tạo tài khoản `manager`/`operator`, không thì họ
/// đăng nhập vào chỉ thấy tab Lịch sử và tưởng app hỏng.
enum UserRole { root, admin, manager, operator, user }

/// Mã vai trò gửi/nhận với backend (khớp `_ROLES` trong `server/app/auth.py`).
String roleCodeOf(UserRole r) {
  switch (r) {
    case UserRole.root:
      return 'root';
    case UserRole.admin:
      return 'admin';
    case UserRole.manager:
      return 'manager';
    case UserRole.operator:
      return 'operator';
    case UserRole.user:
      return 'user';
  }
}

UserRole roleFromCode(Object? v) {
  switch (v.toString().trim().toLowerCase()) {
    case 'root':
      return UserRole.root;
    case 'admin':
      return UserRole.admin;
    case 'manager':
      return UserRole.manager;
    case 'operator':
      return UserRole.operator;
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
  bool get isManager => role == UserRole.manager;
  bool get isOperator => role == UserRole.operator;

  /// Nhân sự kỹ thuật/CSKH (root + admin). LƯU Ý: KHÔNG còn tự thấy mọi máy —
  /// phạm vi xem theo [allowAll]/[ids] như mọi vai trò.
  bool get isStaff => role == UserRole.root || role == UserRole.admin;

  /// Người của XƯỞNG (quản lý sản xuất + thao tác viên).
  bool get isFactory => role == UserRole.manager || role == UserRole.operator;

  String get roleCode => roleCodeOf(role);

  // --- Quyền theo VIỆC, không theo chức danh ------------------------------
  //
  // Vì sao tách nhỏ thay vì tiếp tục dùng một `canWrite`: cờ đó đang mang BỐN
  // nghĩa khác nhau (đồng bộ lịch sử · ghi OTA · chạy trạm · hiện tab Kỹ Thuật)
  // ở 21 chỗ gọi. Thêm vai trò xưởng vào đó thì mỗi lần đọc code phải đoán nó
  // đang nói nghĩa nào — và đoán sai một lần là trao quyền nạp firmware cho cả
  // fleet vào tay người đứng máy. Bảng quyền: docs/plan/tai-khoan-nha-may.md §3.1.

  /// Xem dữ liệu **lâm sàng** (Lịch sử, Chăm sóc KH) — vẫn lọc tiếp theo [canSee].
  /// Người của xưởng KHÔNG xem (quyết định của chủ dự án 2026-09-07).
  bool get canSeeClinical =>
      role == UserRole.root || role == UserRole.admin || role == UserRole.user;

  /// Ghi dữ liệu lâm sàng: Đồng bộ / Xoá / Lấy-từ-máy.
  bool get canWriteClinical => isStaff;

  /// Chăm sóc KH (tra thông tin máy, đọc log qua USB, gửi log về kỹ thuật).
  bool get canSupport => isStaff;

  /// Tab Kỹ Thuật (nạp firmware tuỳ ý, đọc serial, log nhiệt).
  bool get canUseTech => isStaff;

  /// **Ghi** kho OTA: tải .bin lên, chọn/huỷ bản cho fleet, ghim máy, xoá.
  bool get canWriteOta => isStaff;

  /// **Xem** tab Quản lý máy (trạng thái fleet + kho firmware). Quản lý sản xuất
  /// xem được để biết bản nào đang chốt, nhưng không bấm được nút ghi nào.
  bool get canSeeOta => isStaff || isManager;

  /// Xem dữ liệu **sản xuất** (hồ sơ ATE). KHÁC phạm vi lâm sàng: hồ sơ ATE
  /// KHÔNG lọc theo [ids] — máy vừa ra khỏi chuyền chưa thuộc về khách hàng nào,
  /// chặn theo `ids` là chặn đúng người vừa làm ra nó.
  bool get canSeeProduction => isStaff || isFactory;

  /// Xem **Thống kê** toàn xưởng (FPY, Pareto). Thao tác viên KHÔNG thấy —
  /// đây là số liệu quản lý, không phải công cụ của người đứng máy.
  bool get canSeeProductionStats => isStaff || isManager;

  /// Chạy trạm ATE (nạp, ghi số máy, chốt hồ sơ).
  bool get canRunStation => isStaff || isFactory;

  /// **Đặt tiêu chuẩn (bộ ngưỡng) cho từng lô sản xuất** — nhân sự kỹ thuật.
  /// Quản lý sản xuất và thao tác viên XEM được để biết lô đang chấm theo bộ
  /// nào, nhưng không sửa: đổi ngưỡng là đổi định nghĩa "máy thế nào là đạt",
  /// phải là quyết định của kỹ thuật chứ không của người đang chạy sản lượng.
  bool get canEditLimits => isStaff;

  /// Tab **Hiệu chuẩn** (ống chuẩn quang, 2026-09-21): pha dung dịch, ghi số đo, đóng
  /// bộ ống, cấp phát cho máy. Kỹ sư lẫn người của xưởng đều làm (chủ dự án chốt);
  /// khách hàng (`user`) KHÔNG thấy — đây là công cụ nội bộ. Ngưỡng PASS đi theo
  /// [canEditLimits] (chỉ nhân sự kỹ thuật) như tiêu chuẩn ATE.
  bool get canSeeCalib => isStaff || isFactory;
  bool get canWriteCalib => isStaff || isFactory;

  /// Vào được màn Quản lý User. Phạm vi cụ thể xem [canManageRole].
  bool get canManageUsers => isRoot || isManager;

  /// Được tạo/sửa/xoá tài khoản có vai trò [target] không.
  ///
  /// Root: mọi vai trò. Quản lý sản xuất: **chỉ thao tác viên** — xưởng tự chủ
  /// việc thêm/khoá người theo ca mà không với tới được tài khoản kỹ thuật hay
  /// tài khoản khách hàng. (Server gác lại lần nữa trong `auth.py`.)
  bool canManageRole(UserRole target) {
    if (isRoot) return true;
    if (isManager) return target == UserRole.operator;
    return false;
  }

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
