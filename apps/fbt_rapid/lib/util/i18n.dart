import '../services/app_prefs.dart';

/// Hệ dịch tối giản: `tr('key')` trả chuỗi theo ngôn ngữ hiện tại
/// (`AppPrefs.instance.localeCode`). Thiếu bản dịch → lùi về **tiếng Việt**.
///
/// Hạ tầng + Việt/Anh trước; Trung (zh) / Thái (th) bổ sung dần. Màn cũ chưa
/// migrate vẫn hiển thị tiếng Việt cứng cho tới khi thay bằng `tr(...)`.
String tr(String key) {
  final m = _t[key];
  if (m == null) return key; // key chưa khai báo → trả nguyên (dễ thấy để bổ sung)
  return m[AppPrefs.instance.localeCode] ?? m['vi'] ?? key;
}

/// Tên hiển thị của vai trò: root→Root, admin→Nhân viên, user→Khách hàng.
String roleLabel(String roleCode) {
  switch (roleCode.toLowerCase()) {
    case 'root':
      return tr('role.root');
    case 'admin':
      return tr('role.admin');
    default:
      return tr('role.user');
  }
}

const Map<String, Map<String, String>> _t = {
  // --- Điều hướng ---
  'nav.history': {'vi': 'Lịch sử', 'en': 'History', 'zh': '历史', 'th': 'ประวัติ'},
  'nav.temperature': {'vi': 'Log nhiệt', 'en': 'Temperature', 'zh': '温度', 'th': 'อุณหภูมิ'},
  'nav.tech': {'vi': 'Kỹ Thuật', 'en': 'Engineering', 'zh': '技术', 'th': 'ช่างเทคนิค'},
  'nav.settings': {'vi': 'Cài đặt', 'en': 'Settings', 'zh': '设置', 'th': 'ตั้งค่า'},

  // --- Kỹ Thuật (nhân sự): Log nhiệt | Đọc serial | Nạp code ---
  'tech.templog': {'vi': 'Log nhiệt', 'en': 'Temp log', 'zh': '温度记录', 'th': 'บันทึกอุณหภูมิ'},
  'tech.serial': {'vi': 'Đọc serial', 'en': 'Serial monitor', 'zh': '串口监视', 'th': 'มอนิเตอร์ซีเรียล'},
  'tech.flash': {'vi': 'Nạp code', 'en': 'Flash firmware', 'zh': '烧录固件', 'th': 'แฟลชเฟิร์มแวร์'},

  // --- Kỹ Thuật bản WEB (Web Serial API) ---
  'techweb.unsupported': {
    'vi': 'Trình duyệt này không hỗ trợ Web Serial.\nDùng Chrome hoặc Edge trên máy tính để truy cập cổng COM.',
    'en': 'This browser does not support Web Serial.\nUse Chrome or Edge on desktop to access COM ports.',
    'zh': '此浏览器不支持 Web Serial。\n请在电脑上使用 Chrome 或 Edge 访问串口。',
    'th': 'เบราว์เซอร์นี้ไม่รองรับ Web Serial\nใช้ Chrome หรือ Edge บนคอมพิวเตอร์'
  },
  'techweb.openPort': {'vi': 'Mở cổng', 'en': 'Open port', 'zh': '打开串口', 'th': 'เปิดพอร์ต'},
  'techweb.pickPort': {'vi': 'Chọn cổng', 'en': 'Pick port', 'zh': '选择串口', 'th': 'เลือกพอร์ต'},
  'techweb.saveLog': {'vi': 'Lưu log', 'en': 'Save log', 'zh': '保存日志', 'th': 'บันทึกล็อก'},
  'techweb.downloaded': {'vi': 'Đã tải xuống Downloads.', 'en': 'Downloaded.', 'zh': '已下载。', 'th': 'ดาวน์โหลดแล้ว'},
  'techweb.noData': {'vi': 'Chưa có dữ liệu để lưu.', 'en': 'No data to save yet.', 'zh': '还没有可保存的数据。', 'th': 'ยังไม่มีข้อมูลให้บันทึก'},
  'techweb.serialHint': {
    'vi': 'Chọn baud rồi bấm "Mở cổng" — trình duyệt sẽ hỏi bạn chọn cổng COM.\nMở được nhiều cổng, mỗi cổng một tab.',
    'en': 'Pick a baud rate then press "Open port" — the browser will ask you to select a COM port.\nMultiple ports supported, one tab each.',
    'zh': '选择波特率后点击"打开串口"——浏览器会让你选择串口。\n支持多个串口，每个一个标签页。',
    'th': 'เลือก baud แล้วกด "เปิดพอร์ต" — เบราว์เซอร์จะให้เลือกพอร์ต COM'
  },
  'techweb.tempHint': {
    'vi': 'Bấm "Mở cổng" và chọn cổng COM của máy — bắt đầu ghi nhiệt ngay khi mở.\n(App tự gửi lệnh TemperatureOutput @115200.)',
    'en': 'Press "Open port" and select the device COM port — logging starts immediately.\n(The TemperatureOutput command is sent automatically @115200.)',
    'zh': '点击"打开串口"并选择设备串口——打开后立即开始记录。',
    'th': 'กด "เปิดพอร์ต" แล้วเลือกพอร์ต COM ของเครื่อง — เริ่มบันทึกทันที'
  },

  // --- Gộp Lịch sử (Cục bộ | Cloud) ---
  'history.local': {'vi': 'Cục bộ', 'en': 'Local', 'zh': '本地', 'th': 'ในเครื่อง'},
  'history.cloud': {'vi': 'Cloud', 'en': 'Cloud', 'zh': '云端', 'th': 'คลาวด์'},
  'history.cloudGoogle': {'vi': 'Cloud Google', 'en': 'Google Cloud', 'zh': '谷歌云', 'th': 'กูเกิลคลาวด์'},
  'history.cloudRapid': {'vi': 'RAPID ERP', 'en': 'RAPID ERP', 'zh': 'RAPID ERP', 'th': 'RAPID ERP'},
  'history.cloudEngineer': {'vi': 'Engineer Server', 'en': 'Engineer Server', 'zh': 'Engineer 服务器', 'th': 'Engineer Server'},
  'history.openJson': {'vi': 'Mở file JSON', 'en': 'Open JSON file', 'zh': '打开 JSON 文件', 'th': 'เปิดไฟล์ JSON'},
  'history.openJsonError': {'vi': 'File JSON không đúng định dạng dữ liệu kết quả.', 'en': 'JSON file is not a valid result data file.', 'zh': 'JSON 文件不是有效的结果数据。', 'th': 'ไฟล์ JSON ไม่ใช่ข้อมูลผลลัพธ์ที่ถูกต้อง'},
  'history.jsonRefresh': {'vi': 'Tải lại', 'en': 'Reload', 'zh': '重新加载', 'th': 'โหลดใหม่'},
  'history.jsonEmpty': {'vi': 'Chưa có file JSON nào trên server.', 'en': 'No JSON files on the server yet.', 'zh': '服务器上还没有 JSON 文件。', 'th': 'ยังไม่มีไฟล์ JSON บนเซิร์ฟเวอร์'},
  'history.jsonLoadMore': {'vi': 'Tải thêm', 'en': 'Load more', 'zh': '加载更多', 'th': 'โหลดเพิ่ม'},

  // --- Tab Thư Mục (duyệt dữ liệu dạng file) ---
  'nav.folder': {'vi': 'Thư Mục', 'en': 'Files', 'zh': '文件夹', 'th': 'โฟลเดอร์'},
  'folder.jsonData': {'vi': 'JSON data', 'en': 'JSON data', 'zh': 'JSON 数据', 'th': 'ข้อมูล JSON'},
  'common.copy': {'vi': 'Sao chép', 'en': 'Copy', 'zh': '复制', 'th': 'คัดลอก'},
  'common.copied': {'vi': 'Đã sao chép.', 'en': 'Copied.', 'zh': '已复制。', 'th': 'คัดลอกแล้ว'},

  // --- Đăng nhập ---
  'login.subtitle': {
    'vi': 'Đăng nhập để tiếp tục',
    'en': 'Sign in to continue',
    'zh': '登录以继续',
    'th': 'เข้าสู่ระบบเพื่อดำเนินการต่อ'
  },
  'login.account': {'vi': 'Tài khoản', 'en': 'Account', 'zh': '账户', 'th': 'บัญชี'},
  'login.password': {'vi': 'Mật khẩu', 'en': 'Password', 'zh': '密码', 'th': 'รหัสผ่าน'},
  'login.signIn': {'vi': 'Đăng nhập', 'en': 'Sign in', 'zh': '登录', 'th': 'เข้าสู่ระบบ'},
  'login.signingIn': {'vi': 'Đang đăng nhập…', 'en': 'Signing in…'},
  'login.fillBoth': {
    'vi': 'Nhập đầy đủ tài khoản và mật khẩu.',
    'en': 'Enter both account and password.'
  },

  // --- Chung ---
  'common.cancel': {'vi': 'Hủy', 'en': 'Cancel', 'zh': '取消', 'th': 'ยกเลิก'},
  'common.save': {'vi': 'Lưu', 'en': 'Save', 'zh': '保存', 'th': 'บันทึก'},
  'common.close': {'vi': 'Đóng', 'en': 'Close', 'zh': '关闭', 'th': 'ปิด'},
  'common.show': {'vi': 'Hiện', 'en': 'Show'},
  'common.hide': {'vi': 'Ẩn', 'en': 'Hide'},
  'common.logout': {'vi': 'Đăng xuất', 'en': 'Log out', 'zh': '退出', 'th': 'ออกจากระบบ'},
  'common.logoutConfirm': {
    'vi': 'Bạn sẽ cần đăng nhập lại để dùng app.',
    'en': 'You will need to sign in again to use the app.'
  },
  'common.logoutTitle': {'vi': 'Đăng xuất?', 'en': 'Log out?'},

  // --- Thiết lập người dùng ---
  'us.title': {'vi': 'Thiết lập', 'en': 'Settings', 'zh': '设置', 'th': 'ตั้งค่า'},
  'us.accountInfo': {'vi': 'Thông tin người dùng', 'en': 'Account info'},
  'us.email': {'vi': 'Email', 'en': 'Email'},
  'us.username': {'vi': 'Tài khoản', 'en': 'Username'},
  'us.name': {'vi': 'Tên', 'en': 'Name'},
  'us.role': {'vi': 'Vai trò', 'en': 'Role'},
  'role.root': {'vi': 'Root', 'en': 'Root', 'zh': 'Root', 'th': 'Root'},
  'role.admin': {'vi': 'Nhân viên', 'en': 'Staff', 'zh': '员工', 'th': 'พนักงาน'},
  'role.user': {'vi': 'Khách hàng', 'en': 'Customer', 'zh': '客户', 'th': 'ลูกค้า'},
  'us.devices': {'vi': 'Mã máy được cấp', 'en': 'Assigned machines'},
  'us.changePassword': {'vi': 'Đổi mật khẩu', 'en': 'Change password'},
  'us.changeEmail': {'vi': 'Đổi email', 'en': 'Change email'},
  'us.provider': {'vi': 'Nhà cung cấp', 'en': 'Provider'},
  'us.rapidErp': {
    'vi': 'RAPID ERP (admin)',
    'en': 'RAPID ERP (admin)',
    'zh': 'RAPID ERP（管理员）',
    'th': 'RAPID ERP (แอดมิน)'
  },
  'us.rapidErpUrl': {
    'vi': 'URL RAPID ERP',
    'en': 'RAPID ERP URL',
    'zh': 'RAPID ERP 地址',
    'th': 'URL RAPID ERP'
  },
  'us.rapidErpKey': {
    'vi': 'API key',
    'en': 'API key',
    'zh': 'API 密钥',
    'th': 'API key'
  },
  'us.rapidErpHint': {
    'vi': 'URL gốc REST (vd https://api.fortebio.tech/api/v1/results) + X-API-Key. '
        'Chỉ admin nhập.',
    'en': 'REST base URL (e.g. https://api.fortebio.tech/api/v1/results) + X-API-Key. '
        'Admin only.'
  },
  'us.rapidErpIds': {
    'vi': 'Danh sách mã máy',
    'en': 'Device IDs',
    'zh': '设备编号列表',
    'th': 'รายการรหัสเครื่อง'
  },
  'us.rapidErpIdsHint': {
    'vi': 'Dán các mã máy (vd RPL03010), cách nhau bằng dấu phẩy hoặc xuống dòng. '
        'App hiển thị thành danh sách; bấm để xem xét nghiệm. Để trống = dùng "Mã máy được cấp".',
    'en': 'Paste device IDs (e.g. RPL03010), separated by comma or newline. The app shows them '
        'as a list; tap to view tests. Empty = use "Assigned machines".'
  },
  'us.engineer': {
    'vi': 'Engineer Server (admin)',
    'en': 'Engineer Server (admin)',
    'zh': 'Engineer 服务器（管理员）',
    'th': 'Engineer Server (แอดมิน)'
  },
  'us.engineerUrl': {
    'vi': 'URL Engineer Server',
    'en': 'Engineer Server URL',
    'zh': 'Engineer 服务器地址',
    'th': 'URL Engineer Server'
  },
  'us.engineerToken': {
    'vi': 'Admin token',
    'en': 'Admin token',
    'zh': '管理员令牌',
    'th': 'Admin token'
  },
  'us.engineerHint': {
    'vi': 'URL gốc FBT Home Server qua Tailscale (mặc định '
        'https://fbt.basa-luma.ts.net) + token (RECEIVER_TOKEN trên server; '
        'trống nếu server không đặt). Chỉ admin nhập.',
    'en': 'FBT Home Server base URL over Tailscale (default '
        'https://fbt.basa-luma.ts.net) + token (the server\'s RECEIVER_TOKEN; '
        'leave empty if the server has none). Admin only.'
  },
  'us.appearance': {'vi': 'Giao diện', 'en': 'Appearance'},
  'us.darkMode': {'vi': 'Chế độ tối', 'en': 'Dark mode'},
  'us.language': {'vi': 'Ngôn ngữ', 'en': 'Language', 'zh': '语言', 'th': 'ภาษา'},
  'us.saveLocation': {'vi': 'Nơi lưu file', 'en': 'Save location'},
  'us.chooseFolder': {'vi': 'Chọn thư mục…', 'en': 'Choose folder…'},
  'us.openFolder': {'vi': 'Mở thư mục', 'en': 'Open folder'},
  'us.defaultFolder': {'vi': 'Về mặc định', 'en': 'Reset to default'},
  'us.interval': {'vi': 'Khoảng đọc (giây)', 'en': 'Reading interval (s)'},
  'us.intervalHint': {
    'vi': 'Quy đổi trục thời gian đồ thị CT (mặc định 20).',
    'en': 'Time axis scale for CT charts (default 20).'
  },
  'us.display': {'vi': 'Thông tin hiển thị', 'en': 'Display info'},
  'us.userName': {'vi': 'Tên người dùng', 'en': 'User name'},
  'us.userOrg': {'vi': 'Đơn vị / phòng khám', 'en': 'Organization / clinic'},
  'us.backup': {'vi': 'Sao lưu & Khôi phục', 'en': 'Backup & Restore'},
  'us.backupBtn': {'vi': 'Sao lưu lịch sử', 'en': 'Back up history'},
  'us.restoreBtn': {'vi': 'Khôi phục từ file', 'en': 'Restore from file'},
  'common.save2': {'vi': 'Lưu', 'en': 'Save'},
  'common.saved': {'vi': 'Đã lưu.', 'en': 'Saved.'},
  'common.delete': {'vi': 'Xóa', 'en': 'Delete'},

  // --- Quản lý User (admin) ---
  'um.title': {'vi': 'Quản lý User', 'en': 'Manage users'},
  'um.adminPass': {
    'vi': 'Nhập mật khẩu admin để quản lý',
    'en': 'Enter admin password to manage'
  },
  'um.continue': {'vi': 'Tiếp tục', 'en': 'Continue'},
  'um.create': {'vi': 'Tạo user', 'en': 'New user'},
  'um.edit': {'vi': 'Sửa user', 'en': 'Edit user'},
  'um.active': {'vi': 'Hoạt động', 'en': 'Active'},
  'um.ids': {
    'vi': 'Mã máy được cấp (cách nhau dấu phẩy, * = tất cả)',
    'en': 'Assigned machines (comma-separated, * = all)'
  },
  'um.pwKeep': {
    'vi': 'Mật khẩu (để trống = giữ nguyên)',
    'en': 'Password (blank = keep)'
  },
  'um.pwNew': {'vi': 'Mật khẩu', 'en': 'Password'},
  'um.deleteConfirm': {
    'vi': 'Xóa user này? Không thể hoàn tác.',
    'en': 'Delete this user? Cannot be undone.'
  },
  'um.empty': {'vi': 'Chưa có user nào.', 'en': 'No users yet.'},
  'um.needUsername': {'vi': 'Nhập username.', 'en': 'Enter a username.'},
  'um.needPassword': {
    'vi': 'Tạo user mới phải nhập mật khẩu.',
    'en': 'A new user requires a password.'
  },

  // --- Đổi mật khẩu / email ---
  'cp.title': {'vi': 'Đổi mật khẩu', 'en': 'Change password'},
  'cp.current': {'vi': 'Mật khẩu hiện tại', 'en': 'Current password'},
  'cp.new': {'vi': 'Mật khẩu mới', 'en': 'New password'},
  'cp.confirm': {'vi': 'Nhập lại mật khẩu mới', 'en': 'Confirm new password'},
  'cp.mismatch': {'vi': 'Mật khẩu mới không khớp.', 'en': 'New passwords do not match.'},
  'cp.ok': {'vi': 'Đã đổi mật khẩu.', 'en': 'Password changed.'},
  'ce.title': {'vi': 'Đổi email', 'en': 'Change email'},
  'ce.new': {'vi': 'Email mới', 'en': 'New email'},
  'ce.ok': {'vi': 'Đã đổi email.', 'en': 'Email updated.'},
};
