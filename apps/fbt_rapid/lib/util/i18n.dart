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
    case 'manager':
      return tr('role.manager');
    case 'operator':
      return tr('role.operator');
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
  // Nút THU/MỞ ở chân rail (`_RailFoot`) — nút DUY NHẤT đổi bề rộng rail.
  'nav.railCollapse': {
    'vi': 'Thu gọn thanh điều hướng',
    'en': 'Collapse navigation',
    'zh': '收起导航栏',
    'th': 'ย่อแถบนำทาง'
  },
  'nav.railExpand': {
    'vi': 'Mở rộng thanh điều hướng',
    'en': 'Expand navigation',
    'zh': '展开导航栏',
    'th': 'ขยายแถบนำทาง'
  },

  // --- Kỹ Thuật (nhân sự): Log nhiệt | Đọc serial | Nạp code ---
  'tech.comShared': {'vi': 'Ba công cụ dùng CHUNG một cổng COM — đóng cổng trước khi nạp.', 'en': 'All three tools share one COM port — close it before flashing.', 'zh': '三个工具共用一个 COM 端口——烧录前请先关闭。', 'th': 'ทั้งสามเครื่องมือใช้พอร์ต COM ร่วมกัน — ปิดพอร์ตก่อนแฟลช'},
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

  // --- Tải toàn bộ dữ liệu của 1 máy (thẻ máy ở tab Lịch sử) ---
  'dl.tooltip': {'vi': 'Tải toàn bộ dữ liệu của máy này', 'en': 'Download all data for this machine', 'zh': '下载此设备的全部数据', 'th': 'ดาวน์โหลดข้อมูลทั้งหมดของเครื่องนี้'},
  'dl.title': {'vi': 'Tải dữ liệu: {id}', 'en': 'Download data: {id}', 'zh': '下载数据：{id}', 'th': 'ดาวน์โหลดข้อมูล: {id}'},
  'dl.counting': {'vi': 'Đang đếm số lần chạy…', 'en': 'Counting runs…', 'zh': '正在统计运行次数…', 'th': 'กำลังนับจำนวนครั้ง…'},
  'dl.listing': {'vi': 'Đang liệt kê {done}/{total} lần chạy…', 'en': 'Listing {done}/{total} runs…', 'zh': '正在列出 {done}/{total} 次运行…', 'th': 'กำลังแสดง {done}/{total} ครั้ง…'},
  'dl.progress': {'vi': 'Đã tải {done}/{total} lần chạy', 'en': 'Fetched {done}/{total} runs', 'zh': '已获取 {done}/{total} 次运行', 'th': 'ดึงแล้ว {done}/{total} ครั้ง'},
  'dl.kindJson': {'vi': 'JSON', 'en': 'JSON', 'zh': 'JSON', 'th': 'JSON'},
  'dl.kindCharts': {'vi': 'Ảnh đồ thị', 'en': 'Chart images', 'zh': '图表图片', 'th': 'ภาพกราฟ'},
  'dl.colTime': {'vi': 'Thời gian đo', 'en': 'Measured at', 'zh': '测量时间', 'th': 'เวลาที่วัด'},
  'dl.colFw': {'vi': 'Firmware', 'en': 'Firmware', 'zh': '固件', 'th': 'เฟิร์มแวร์'},
  'dl.colResult': {'vi': 'Kết quả', 'en': 'Result', 'zh': '结果', 'th': 'ผลลัพธ์'},
  'dl.nPositive': {'vi': '{n} slot dương', 'en': '{n} positive', 'zh': '{n} 个阳性', 'th': '{n} ช่องบวก'},
  'dl.allNegative': {'vi': 'Âm tính', 'en': 'All negative', 'zh': '全部阴性', 'th': 'ลบทั้งหมด'},
  'dl.downloadN': {'vi': 'Tải {n} lần đo', 'en': 'Download {n} runs', 'zh': '下载 {n} 次运行', 'th': 'ดาวน์โหลด {n} ครั้ง'},
  'dl.slowCharts': {
    'vi': 'Mỗi lần đo phải render 4 ảnh đồ thị — chậm hơn JSON nhiều. Bấm Dừng vẫn giữ những ảnh đã lưu.',
    'en': 'Each run renders 4 chart images — much slower than JSON. Stopping keeps whatever was already saved.',
    'zh': '每次运行需渲染 4 张图表 — 比 JSON 慢得多。停止后已保存的图片仍保留。',
    'th': 'แต่ละครั้งต้องเรนเดอร์กราฟ 4 ภาพ — ช้ากว่า JSON มาก หยุดแล้วภาพที่บันทึกไว้ยังอยู่'
  },
  'dl.noCurve': {
    'vi': '⚠ {n} lần đo không lấy được đường cong — chỉ còn CT và kết quả.',
    'en': '⚠ {n} runs had no curves fetched — only CT and result were kept.',
    'zh': '⚠ {n} 次运行未能获取曲线 — 仅保留 CT 与结果。',
    'th': '⚠ {n} ครั้งดึงกราฟไม่ได้ — เหลือเพียง CT และผลลัพธ์'
  },
  'dl.slowHint': {
    'vi': 'Mỗi lần chạy là một request để lấy đường cong — máy chạy nhiều thì việc này mất vài phút. Bấm Dừng vẫn lưu được phần đã tải.',
    'en': 'Each run needs its own request to fetch the curves — this takes a few minutes for busy machines. Stopping still saves what was fetched.',
    'zh': '每次运行都需单独请求以获取曲线 — 数据多时需要几分钟。中途停止仍会保存已获取的部分。',
    'th': 'แต่ละครั้งต้องเรียกแยกเพื่อดึงกราฟ — เครื่องที่ใช้งานมากจะใช้เวลาหลายนาที หยุดกลางคันก็ยังบันทึกส่วนที่ดึงมาแล้ว'
  },
  'dl.stop': {'vi': 'Dừng', 'en': 'Stop', 'zh': '停止', 'th': 'หยุด'},
  'dl.stopping': {'vi': 'Đang dừng…', 'en': 'Stopping…', 'zh': '正在停止…', 'th': 'กำลังหยุด…'},
  'dl.done': {'vi': 'Đã lưu {n} lần chạy.', 'en': 'Saved {n} runs.', 'zh': '已保存 {n} 次运行。', 'th': 'บันทึก {n} ครั้งแล้ว'},
  'dl.partial': {
    'vi': '⚠ Mới lấy {n}/{total} lần chạy — file này CHƯA đủ dữ liệu của máy.',
    'en': '⚠ Only {n}/{total} runs fetched — this file is NOT the machine\'s complete data.',
    'zh': '⚠ 仅获取 {n}/{total} 次运行 — 此文件并非该设备的完整数据。',
    'th': '⚠ ดึงมาเพียง {n}/{total} ครั้ง — ไฟล์นี้ยังไม่ใช่ข้อมูลครบของเครื่อง'
  },
  'dl.empty': {'vi': 'Máy này chưa có lần chạy nào để tải.', 'en': 'This machine has no runs to download.', 'zh': '此设备没有可下载的运行记录。', 'th': 'เครื่องนี้ยังไม่มีข้อมูลให้ดาวน์โหลด'},
  'dl.webSaved': {'vi': 'File đã tải xuống thư mục Downloads của trình duyệt.', 'en': 'The file was downloaded to your browser\'s Downloads folder.', 'zh': '文件已下载到浏览器的下载文件夹。', 'th': 'ไฟล์ถูกดาวน์โหลดไปยังโฟลเดอร์ Downloads ของเบราว์เซอร์'},
  'dl.saveError': {'vi': 'Lỗi lưu file', 'en': 'Failed to save file', 'zh': '保存文件失败', 'th': 'บันทึกไฟล์ไม่สำเร็จ'},
  'dl.open': {'vi': 'Mở thư mục', 'en': 'Open folder', 'zh': '打开文件夹', 'th': 'เปิดโฟลเดอร์'},

  // --- Tab Quản lý máy (OTA + trạng thái thiết bị) ---
  'nav.manager': {'vi': 'Quản lý máy', 'en': 'Manage machines', 'zh': '设备管理', 'th': 'จัดการเครื่อง'},
  'mm.shellHint': {'vi': 'Chọn bản firmware cho cả fleet hoặc ghim riêng từng máy. Máy nhận ở lần hỏi server kế tiếp.', 'en': 'Pick firmware for the whole fleet, or pin one machine. Machines see it at their next check.', 'zh': '为整个设备群选择固件，或单独指定某台设备。设备会在下次查询时看到。', 'th': 'เลือกเฟิร์มแวร์สำหรับทั้งกลุ่ม หรือปักหมุดเฉพาะเครื่อง เครื่องจะเห็นในการตรวจครั้งถัดไป'},
  'mm.ota': {'vi': 'Cập nhật OTA', 'en': 'OTA update', 'zh': 'OTA 更新', 'th': 'อัปเดต OTA'},
  'mm.status': {'vi': 'Trạng thái máy', 'en': 'Machine status', 'zh': '设备状态', 'th': 'สถานะเครื่อง'},
  'mm.upload': {'vi': 'Tải firmware lên', 'en': 'Upload firmware', 'zh': '上传固件', 'th': 'อัปโหลดเฟิร์มแวร์'},
  'mm.uploaded': {'vi': 'Đã tải firmware lên server.', 'en': 'Firmware uploaded.', 'zh': '固件已上传。', 'th': 'อัปโหลดเฟิร์มแวร์แล้ว'},
  'mm.uploadedAs': {'vi': 'Đã tải lên: {name}', 'en': 'Uploaded as: {name}', 'zh': '已上传为：{name}', 'th': 'อัปโหลดเป็น: {name}'},

  // Pop-up đặt tên phiên bản khi tải firmware lên
  'mm.versionTitle': {'vi': 'Đặt tên phiên bản', 'en': 'Name this version', 'zh': '为此版本命名', 'th': 'ตั้งชื่อเวอร์ชัน'},
  'mm.versionPickedFile': {'vi': 'File đã chọn: {name}', 'en': 'Selected file: {name}', 'zh': '已选文件：{name}', 'th': 'ไฟล์ที่เลือก: {name}'},
  'mm.versionLabel': {'vi': 'Phiên bản', 'en': 'Version', 'zh': '版本', 'th': 'เวอร์ชัน'},
  'mm.versionHint': {'vi': 'Nhập version dạng 2.4.4 (ít nhất 2 nhóm số).', 'en': 'Enter a version like 2.4.4 (at least two number groups).', 'zh': '请输入形如 2.4.4 的版本号（至少两组数字）。', 'th': 'ใส่เวอร์ชันแบบ 2.4.4 (อย่างน้อยสองกลุ่มตัวเลข)'},
  'mm.versionInvalid': {'vi': 'Không hợp lệ. Ví dụ đúng: 2.4.4 · 2.4.4AT · 2.4.4_rc1', 'en': 'Invalid. Valid examples: 2.4.4 · 2.4.4AT · 2.4.4_rc1', 'zh': '无效。有效示例：2.4.4 · 2.4.4AT · 2.4.4_rc1', 'th': 'ไม่ถูกต้อง ตัวอย่างที่ใช้ได้: 2.4.4 · 2.4.4AT · 2.4.4_rc1'},
  'mm.versionSavedAs': {'vi': 'Lưu trên server thành: {name}', 'en': 'Saved on the server as: {name}', 'zh': '在服务器上保存为：{name}', 'th': 'บันทึกบนเซิร์ฟเวอร์เป็น: {name}'},
  // Server (2026-09-11) KHÔNG ghi đè: cùng tên + cùng nội dung → chấp nhận (không đổi gì),
  // cùng tên + nội dung KHÁC → 409 "một tên = một nội dung". Nói đúng thứ sẽ xảy ra.
  'mm.versionOverwrite': {
    'vi': '⚠ Server đã có bản cùng tên. Server KHÔNG ghi đè: nếu file khác nội dung sẽ bị từ chối — xoá bản cũ trước, hoặc đặt version khác.',
    'en': '⚠ A build with this name already exists. The server will NOT overwrite it: a different file is rejected — delete the old build first, or use a different version.',
    'zh': '⚠ 服务器上已存在同名版本。服务器不会覆盖：内容不同的文件会被拒绝——请先删除旧版本，或使用其他版本号。',
    'th': '⚠ มีรุ่นชื่อนี้บนเซิร์ฟเวอร์แล้ว เซิร์ฟเวอร์จะไม่เขียนทับ: ไฟล์ที่เนื้อหาต่างจะถูกปฏิเสธ — ลบรุ่นเก่าก่อน หรือใช้เวอร์ชันอื่น'
  },
  'mm.otaEmpty': {'vi': 'Chưa có bản firmware nào trên server.', 'en': 'No firmware on the server yet.', 'zh': '服务器上还没有固件。', 'th': 'ยังไม่มีเฟิร์มแวร์บนเซิร์ฟเวอร์'},
  'mm.select': {'vi': 'Chọn bản này', 'en': 'Select', 'zh': '选择此版本', 'th': 'เลือกรุ่นนี้'},
  'mm.selected': {'vi': 'Đang chọn', 'en': 'Selected', 'zh': '已选择', 'th': 'กำลังเลือก'},
  'mm.noTarget': {'vi': 'Chưa chọn bản nào — máy không cập nhật.', 'en': 'No firmware selected — machines will not update.', 'zh': '未选择固件 — 设备不会更新。', 'th': 'ยังไม่ได้เลือกรุ่น — เครื่องจะไม่อัปเดต'},
  'mm.targetIs': {'vi': 'Máy sẽ nạp: {name}', 'en': 'Machines will flash: {name}', 'zh': '设备将刷入：{name}', 'th': 'เครื่องจะแฟลช: {name}'},
  'mm.clearTarget': {'vi': 'Huỷ chọn', 'en': 'Clear', 'zh': '取消选择', 'th': 'ยกเลิก'},
  'mm.delete': {'vi': 'Xoá', 'en': 'Delete', 'zh': '删除', 'th': 'ลบ'},
  'mm.download': {'vi': 'Tải bản này về máy tính', 'en': 'Download to this computer', 'zh': '下载到本机', 'th': 'ดาวน์โหลดลงเครื่องนี้'},
  'mm.downloaded': {'vi': 'Đã lưu firmware: {path}', 'en': 'Firmware saved: {path}', 'zh': '固件已保存：{path}', 'th': 'บันทึกเฟิร์มแวร์แล้ว: {path}'},
  'mm.deleteTitle': {'vi': 'Xoá bản firmware?', 'en': 'Delete firmware?', 'zh': '删除固件？', 'th': 'ลบเฟิร์มแวร์?'},
  'mm.deleteBody': {'vi': 'Xoá "{name}" khỏi server? Không thể hoàn tác.', 'en': 'Delete "{name}" from the server? This cannot be undone.', 'zh': '从服务器删除“{name}”？此操作无法撤销。', 'th': 'ลบ "{name}" ออกจากเซิร์ฟเวอร์? ไม่สามารถย้อนกลับได้'},
  'mm.firmware': {'vi': 'Firmware', 'en': 'Firmware', 'zh': '固件', 'th': 'เฟิร์มแวร์'},
  // Pop-up xác nhận triển khai OTA
  'mm.deployTitle': {'vi': 'Triển khai cho TOÀN BỘ máy?', 'en': 'Deploy to ALL machines?', 'zh': '向所有设备发布？', 'th': 'ติดตั้งให้เครื่องทั้งหมด?'},
  'mm.deployFile': {'vi': 'Bản firmware', 'en': 'Firmware', 'zh': '固件', 'th': 'เฟิร์มแวร์'},
  'mm.deployAffected': {'vi': 'Ảnh hưởng', 'en': 'Affects', 'zh': '影响范围', 'th': 'ผลกระทบ'},
  'mm.deployNMachines': {'vi': '{n} máy', 'en': '{n} machines', 'zh': '{n} 台设备', 'th': '{n} เครื่อง'},
  'mm.deployWarning': {
    'vi': 'Server hiện CHƯA chọn được theo từng máy — mọi máy sẽ nhận bản này ở lần kiểm tra tới. Firmware lỗi = hỏng cả fleet ngoài hiện trường. Nên thử trên 1–2 máy trước khi có tính năng chọn theo máy.',
    'en': 'The server cannot target individual machines yet — every machine will take this build on its next check. A bad firmware breaks the whole fleet in the field. Test on 1–2 machines first.',
    'zh': '服务器暂不支持按设备选择 — 所有设备将在下次检查时安装此版本。固件有问题会导致整个机群故障。请先在 1–2 台设备上测试。',
    'th': 'เซิร์ฟเวอร์ยังเลือกทีละเครื่องไม่ได้ — ทุกเครื่องจะรับรุ่นนี้ในการตรวจสอบครั้งถัดไป เฟิร์มแวร์ที่ผิดพลาดจะทำให้เครื่องทั้งหมดเสีย ควรทดสอบ 1–2 เครื่องก่อน'
  },
  'mm.deployNoVersion': {
    'vi': '⚠ Tên file không chứa số version (vd fbt_v2.4.4.bin) → sau này KHÔNG tính được tiến độ triển khai.',
    'en': '⚠ The file name has no version number (e.g. fbt_v2.4.4.bin) → rollout progress cannot be computed later.',
    'zh': '⚠ 文件名不含版本号（如 fbt_v2.4.4.bin）→ 之后无法计算发布进度。',
    'th': '⚠ ชื่อไฟล์ไม่มีเลขเวอร์ชัน (เช่น fbt_v2.4.4.bin) → จะคำนวณความคืบหน้าไม่ได้'
  },
  'mm.deployGo': {'vi': 'Triển khai', 'en': 'Deploy', 'zh': '发布', 'th': 'ติดตั้ง'},

  // Pop-up tiến độ triển khai
  'mm.progress': {'vi': 'Tiến độ', 'en': 'Progress', 'zh': '进度', 'th': 'ความคืบหน้า'},
  'mm.progressTitle': {'vi': 'Triển khai: {name}', 'en': 'Rollout: {name}', 'zh': '发布：{name}', 'th': 'การติดตั้ง: {name}'},
  'mm.progressCount': {'vi': '{done}/{total} máy đã lên v{ver}', 'en': '{done}/{total} machines on v{ver}', 'zh': '{done}/{total} 台设备已升级到 v{ver}', 'th': '{done}/{total} เครื่องอยู่บน v{ver}'},
  'mm.progressNoVersion': {
    'vi': 'Không tính được tiến độ: tên file không chứa số version. Đặt tên kiểu fbt_v2.4.4.bin thì app mới đối chiếu được với version máy báo về.',
    'en': 'Cannot compute progress: the file name has no version number. Name it like fbt_v2.4.4.bin so it can be matched against what machines report.',
    'zh': '无法计算进度：文件名不含版本号。请命名为 fbt_v2.4.4.bin 之类，才能与设备上报的版本比对。',
    'th': 'คำนวณความคืบหน้าไม่ได้: ชื่อไฟล์ไม่มีเลขเวอร์ชัน ตั้งชื่อแบบ fbt_v2.4.4.bin จึงจะเทียบกับเวอร์ชันที่เครื่องรายงานได้'
  },
  'mm.progressCsv': {'vi': 'Tải CSV', 'en': 'Download CSV', 'zh': '下载 CSV', 'th': 'ดาวน์โหลด CSV'},
  'mm.progressCsvSaved': {'vi': 'Đã xuất {n} máy ra CSV.', 'en': 'Exported {n} machines to CSV.', 'zh': '已导出 {n} 台设备到 CSV。', 'th': 'ส่งออก {n} เครื่องเป็น CSV แล้ว'},
  'mm.progressCaveat': {
    'vi': 'Suy ra từ version máy gửi kèm lần đo gần nhất — server không ghi nhận máy nào đã tải. Máy đã nạp xong nhưng chưa đo lại thì vẫn hiện bản cũ.',
    'en': 'Inferred from the version each machine sent with its last run — the server does not record who downloaded. A machine that updated but has not measured since still shows the old build.',
    'zh': '根据每台设备最近一次上传携带的版本推断 — 服务器不记录谁下载过。已升级但尚未再次测量的设备仍显示旧版本。',
    'th': 'อนุมานจากเวอร์ชันที่เครื่องส่งมาพร้อมการวัดครั้งล่าสุด — เซิร์ฟเวอร์ไม่ได้บันทึกว่าใครดาวน์โหลด เครื่องที่อัปเดตแล้วแต่ยังไม่ได้วัดจะยังแสดงรุ่นเก่า'
  },

  // Tiêu đề cột bảng Trạng thái máy
  'mm.colDevice': {'vi': 'Máy', 'en': 'Machine', 'zh': '设备', 'th': 'เครื่อง'},
  'mm.colRuns': {'vi': 'Số phiên', 'en': 'Runs', 'zh': '运行次数', 'th': 'จำนวนครั้ง'},
  'mm.colLastSeen': {'vi': 'Lần gửi cuối', 'en': 'Last upload', 'zh': '最后上传', 'th': 'อัปโหลดล่าสุด'},
  'mm.fresh': {'vi': 'Có gửi dữ liệu trong 24 giờ qua', 'en': 'Sent data in the last 24h', 'zh': '过去 24 小时内有上传', 'th': 'ส่งข้อมูลใน 24 ชม. ที่ผ่านมา'},
  'mm.stale': {'vi': 'Không gửi dữ liệu trong 24 giờ qua', 'en': 'No data in the last 24h', 'zh': '过去 24 小时内无上传', 'th': 'ไม่ได้ส่งข้อมูลใน 24 ชม. ที่ผ่านมา'},
  'mm.statusEmpty': {'vi': 'Chưa có máy nào gửi dữ liệu.', 'en': 'No machine has sent data yet.', 'zh': '还没有设备上传数据。', 'th': 'ยังไม่มีเครื่องส่งข้อมูล'},
  'mm.statusHint': {'vi': 'Version firmware lấy từ lần gửi dữ liệu gần nhất của máy.', 'en': 'Firmware version comes from each machine\'s most recent upload.', 'zh': '固件版本取自设备最近一次上传。', 'th': 'เวอร์ชันเฟิร์มแวร์มาจากการอัปโหลดล่าสุดของเครื่อง'},
  'mm.searchOta': {'vi': 'Tìm bản firmware…', 'en': 'Search firmware…', 'zh': '搜索固件…', 'th': 'ค้นหาเฟิร์มแวร์…'},
  'mm.searchStatus': {'vi': 'Tìm theo mã máy hoặc version…', 'en': 'Search by machine ID or version…', 'zh': '按设备编号或版本搜索…', 'th': 'ค้นหาด้วยรหัสเครื่องหรือเวอร์ชัน…'},
  'mm.searchEmpty': {'vi': 'Không có kết quả khớp.', 'en': 'Nothing matches.', 'zh': '没有匹配项。', 'th': 'ไม่พบรายการที่ตรงกัน'},
  'mm.pinSaved': {'vi': 'Đã ghim {id} vào {name}', 'en': 'Pinned {id} to {name}', 'zh': '已将 {id} 指定为 {name}', 'th': 'ปักหมุด {id} ไว้ที่ {name} แล้ว'},
  'mm.pinCleared': {'vi': 'Đã gỡ ghim {id} — máy quay về theo bản chung', 'en': 'Unpinned {id} — it follows the fleet target again', 'zh': '已取消 {id} 的指定——恢复跟随统一版本', 'th': 'ยกเลิกการปักหมุด {id} — กลับไปตามเวอร์ชันรวม'},
  'mm.pinFailed': {'vi': 'KHÔNG ghim được {id}: {err}', 'en': 'Could not pin {id}: {err}', 'zh': '无法指定 {id}：{err}', 'th': 'ปักหมุด {id} ไม่สำเร็จ: {err}'},
  'mm.colPinnedBy': {'vi': 'Người thiết lập', 'en': 'Set by', 'zh': '设置人', 'th': 'ผู้ตั้งค่า'},
  'mm.pinNoneHint': {'vi': 'Không ghim — máy đi theo bản chung, tự cập nhật qua OTA.', 'en': 'Not pinned — this machine follows the fleet target and updates over OTA.', 'zh': '未指定——此设备跟随统一版本，通过 OTA 自动更新。', 'th': 'ไม่ได้ปักหมุด — เครื่องนี้ตามเวอร์ชันรวมและอัปเดตผ่าน OTA'},
  'mm.pinMenuHint': {'vi': 'Ghim thắng bản chung. Máy nhận ở lần hỏi kế tiếp, vẫn cần bấm nút ĐỎ.', 'en': 'A pin overrides the fleet target. The machine sees it at its next check and still needs the RED button.', 'zh': '指定版本优先于统一版本。设备下次查询时看到，仍需按红色按钮。', 'th': 'การปักหมุดแทนที่เวอร์ชันรวม เครื่องจะเห็นในการตรวจครั้งถัดไป และยังต้องกดปุ่มแดง'},
  'mm.pinRunning': {
    'vi': 'Máy đang chạy: {v}',
    'en': 'Machine is running: {v}',
    'zh': '设备当前运行：{v}',
    'th': 'เครื่องกำลังใช้: {v}'
  },
  'mm.pinIsRunning': {
    'vi': '· đang chạy',
    'en': '· running',
    'zh': '· 运行中',
    'th': '· กำลังใช้'
  },
  'mm.pinNoTarget': {'vi': 'chưa chọn', 'en': 'none selected', 'zh': '未选择', 'th': 'ยังไม่ได้เลือก'},
  'mm.pinFollowTarget': {'vi': 'Theo bản chung', 'en': 'Follow the fleet target', 'zh': '跟随统一版本', 'th': 'ตามเวอร์ชันรวม'},
  'mm.pinSet': {'vi': 'Chọn bản riêng cho máy này', 'en': 'Pin a version for this machine', 'zh': '为此设备指定版本', 'th': 'ปักหมุดเวอร์ชันสำหรับเครื่องนี้'},
  'mm.pinnedTo': {'vi': 'Đang ghim: {name}', 'en': 'Pinned to {name}', 'zh': '已指定：{name}', 'th': 'ปักหมุดไว้ที่ {name}'},
  'mm.pinNeeds244': {'vi': 'Cần firmware v2.4.4 trở lên — bản cũ không hỏi server nên ghim sẽ không có tác dụng.', 'en': 'Needs firmware v2.4.4 or newer — older builds never ask the server, so a pin would do nothing.', 'zh': '需要 v2.4.4 或更新的固件——旧版本不会查询服务器，指定不会生效。', 'th': 'ต้องใช้เฟิร์มแวร์ v2.4.4 ขึ้นไป — รุ่นเก่าไม่ถามเซิร์ฟเวอร์ การปักหมุดจะไม่มีผล'},


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
  'role.manager': {'vi': 'Quản lý SX', 'en': 'Production manager', 'zh': '生产经理', 'th': 'ผู้จัดการผลิต'},
  'role.operator': {'vi': 'Thao tác viên', 'en': 'Operator', 'zh': '操作员', 'th': 'ผู้ปฏิบัติงาน'},
  'role.user': {'vi': 'Khách hàng', 'en': 'Customer', 'zh': '客户', 'th': 'ลูกค้า'},
  'um.onlyOperators': {
    'vi': 'Quản lý sản xuất chỉ tạo/sửa được tài khoản Thao tác viên.',
    'en': 'A production manager can only create or edit operator accounts.'
  },
  'um.idsFactoryHint': {
    'vi': 'Vai trò xưởng không dùng danh sách mã máy — hồ sơ sản xuất không lọc theo mã máy được cấp.',
    'en': 'Factory roles do not use the machine list — production records are not filtered by granted machines.'
  },
  'us.devices': {'vi': 'Mã máy được cấp', 'en': 'Assigned machines'},
  'us.changePassword': {'vi': 'Đổi mật khẩu', 'en': 'Change password'},
  'us.changeEmail': {'vi': 'Đổi email', 'en': 'Change email'},
  'us.provider': {'vi': 'Nhà cung cấp', 'en': 'Provider'},

  // --- Phiên bản ứng dụng (Thiết lập › Phiên bản) ---
  'ver.title': {'vi': 'Phiên bản', 'en': 'Version', 'zh': '版本', 'th': 'เวอร์ชัน'},
  'ver.app': {'vi': 'Phiên bản ứng dụng', 'en': 'App version'},
  'ver.channel': {'vi': 'Kênh', 'en': 'Channel'},
  'ver.dev': {'vi': 'Đang phát triển', 'en': 'In development'},
  'ver.release': {'vi': 'Bản phát hành', 'en': 'Release'},
  'ver.buildDate': {'vi': 'Ngày dựng', 'en': 'Build date'},
  'ver.rev': {'vi': 'Commit', 'en': 'Commit'},
  'ver.lastRelease': {'vi': 'Phát hành gần nhất', 'en': 'Last release'},
  'ver.devNote': {
    'vi': 'Bản này chưa phát hành — đang phát triển, có thể còn lỗi.',
    'en': 'This build is not released yet — in development, may contain bugs.'
  },
  'ver.whatsNew': {'vi': 'Đang thêm trong bản này', 'en': 'New in this build'},
  'ver.copy': {'vi': 'Chép thông tin phiên bản', 'en': 'Copy version info'},
  'ver.copied': {'vi': 'Đã chép thông tin phiên bản.', 'en': 'Version info copied.'},
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
    'vi': 'URL gốc FBT Home Server (mặc định '
        'https://hub.fortebio.tech) + token (RECEIVER_TOKEN trên server; '
        'trống nếu server không đặt). Chỉ admin nhập.',
    'en': 'FBT Home Server base URL (default '
        'https://hub.fortebio.tech) + token (the server\'s RECEIVER_TOKEN; '
        'leave empty if the server has none). Admin only.'
  },
  'us.appearance': {'vi': 'Giao diện', 'en': 'Appearance'},
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

  // --- Đại tu giao diện 2026-08-19: tiêu đề/phụ đề màn + nút chung ---
  'history.hint': {'vi': 'Chọn một máy để xem các lần chạy và đồ thị CT.', 'en': 'Pick a machine to see its runs and CT curves.', 'zh': '选择一台设备查看其运行记录与 CT 曲线。', 'th': 'เลือกเครื่องเพื่อดูรอบการทำงานและกราฟ CT'},
  'folder.hint': {'vi': 'Duyệt dữ liệu thiết bị đã đẩy lên server dưới dạng file.', 'en': 'Browse device data on the server as files.', 'zh': '以文件形式浏览设备上传到服务器的数据。', 'th': 'เรียกดูข้อมูลอุปกรณ์บนเซิร์ฟเวอร์ในรูปแบบไฟล์'},
  'um.hint': {'vi': 'Tạo tài khoản, đặt vai trò và cấp mã máy được xem.', 'en': 'Create accounts, set roles, grant machine access.', 'zh': '创建账号、设置角色并授予设备访问权限。', 'th': 'สร้างบัญชี กำหนดบทบาท และให้สิทธิ์เข้าถึงเครื่อง'},
  'common.refresh': {'vi': 'Làm mới', 'en': 'Refresh', 'zh': '刷新', 'th': 'รีเฟรช'},
  'common.sort': {'vi': 'Sắp xếp', 'en': 'Sort', 'zh': '排序', 'th': 'เรียงลำดับ'},
  'common.retry': {'vi': 'Thử lại', 'en': 'Retry', 'zh': '重试', 'th': 'ลองใหม่'},
  'common.search': {'vi': 'Tìm…', 'en': 'Search…', 'zh': '搜索…', 'th': 'ค้นหา…'},

  // --- Lịch sử cập nhật firmware của một máy (Trạng thái máy) ---
  'mm.hist': {'vi': 'Lịch sử cập nhật', 'en': 'Update history', 'zh': '更新历史', 'th': 'ประวัติการอัปเดต'},
  'mm.histFor': {'vi': 'Lịch sử cập nhật — {id}', 'en': 'Update history — {id}', 'zh': '更新历史 — {id}', 'th': 'ประวัติการอัปเดต — {id}'},
  'mm.histVersion': {'vi': 'Phiên bản', 'en': 'Version', 'zh': '版本', 'th': 'เวอร์ชัน'},
  'mm.histHow': {'vi': 'Cách cập nhật', 'en': 'Method', 'zh': '更新方式', 'th': 'วิธีอัปเดต'},
  'mm.histWhen': {'vi': 'Ngày cập nhật', 'en': 'Updated on', 'zh': '更新日期', 'th': 'วันที่อัปเดต'},
  'mm.histOta': {'vi': 'OTA', 'en': 'OTA', 'zh': 'OTA', 'th': 'OTA'},
  'mm.histManual': {'vi': 'Nạp tay', 'en': 'Manual', 'zh': '手动烧录', 'th': 'แฟลชมือ'},
  'mm.histFirst': {'vi': 'bản đầu tiên ghi nhận', 'en': 'first version on record', 'zh': '最早记录版本', 'th': 'เวอร์ชันแรกที่บันทึก'},
  'mm.histRuns': {'vi': '{n} lần đo', 'en': '{n} runs', 'zh': '{n} 次测量', 'th': '{n} รอบ'},
  'mm.histEmpty': {'vi': 'Máy chưa gửi lần đo nào có kèm version.', 'en': 'No run has reported a firmware version yet.', 'zh': '尚无带版本号的测量记录。', 'th': 'ยังไม่มีรอบใดที่รายงานเวอร์ชัน'},
  // --- Bảng mã lỗi cảm biến (màn chi tiết kết quả) ---
  'rd.errTitle': {
    'vi': 'Máy báo {n} lỗi cảm biến',
    'en': '{n} sensor error(s) reported',
    'zh': '设备报告 {n} 个传感器错误',
    'th': 'เครื่องรายงานข้อผิดพลาดเซนเซอร์ {n} รายการ'
  },
  'rd.errSlot': {'vi': 'Giếng', 'en': 'Slot', 'zh': '孔位', 'th': 'ช่อง'},
  'rd.errCode': {'vi': 'Mã', 'en': 'Code', 'zh': '代码', 'th': 'รหัส'},
  'rd.errMsg': {'vi': 'Mô tả', 'en': 'Description', 'zh': '说明', 'th': 'รายละเอียด'},
  'rd.errNote': {
    'vi': 'Máy gửi lỗi bằng bản tin RIÊNG, không kèm mã lần đo — server ghép theo thời gian (lỗi phát sinh giữa lần đo này). Mã 4 chữ số trùng mã hiện trên màn máy.',
    'en': 'The machine reports errors in a SEPARATE message with no run id — the server matches them by time (errors raised during this run). The 4-digit code matches the one shown on the machine.'
  },
  'mm.delBlockedTarget': {
    'vi': 'Không xoá được: đây là bản ĐANG CHỌN cho cả fleet. Bỏ chọn trước rồi mới xoá.',
    'en': 'Cannot delete: this is the version currently selected for the whole fleet. Unselect it first.'
  },
  'mm.delBlockedPinned': {
    'vi': 'Không xoá được: đang ghim riêng cho {n} máy. Gỡ ghim ở cột "Trạng thái update" trước.',
    'en': 'Cannot delete: pinned to {n} machine(s). Unpin them in the "Update status" column first.'
  },
  'mm.fwFilter': {
    'vi': 'Lọc theo firmware',
    'en': 'Filter by firmware',
    'zh': '按固件筛选',
    'th': 'กรองตามเฟิร์มแวร์'
  },
  'mm.fwFilterAll': {
    'vi': 'Tất cả ({n} máy)',
    'en': 'All ({n} machines)',
    'zh': '全部（{n} 台）',
    'th': 'ทั้งหมด ({n} เครื่อง)'
  },
  // --- Cột "Trạng thái update" (Quản lý máy → Trạng thái máy) ---
  'mm.colUpdate': {
    'vi': 'Trạng thái update',
    'en': 'Update status',
    'zh': '更新状态',
    'th': 'สถานะอัปเดต'
  },
  'mm.updOnTarget': {'vi': 'Đúng bản', 'en': 'Up to date', 'zh': '已是目标版本', 'th': 'ตรงเวอร์ชัน'},
  'mm.updPending': {'vi': 'Chờ nạp', 'en': 'Pending', 'zh': '待安装', 'th': 'รอติดตั้ง'},
  'mm.updNoTarget': {'vi': 'Chưa chọn bản', 'en': 'No version selected', 'zh': '未选版本', 'th': 'ยังไม่เลือกเวอร์ชัน'},
  'mm.updUnknown': {'vi': 'Chưa rõ', 'en': 'Unknown', 'zh': '未知', 'th': 'ไม่ทราบ'},
  'mm.updModeCommon': {'vi': 'Chung', 'en': 'Common', 'zh': '通用', 'th': 'ร่วม'},
  'mm.updModePinned': {'vi': 'Riêng', 'en': 'Pinned', 'zh': '专用', 'th': 'เฉพาะ'},
  'mm.updTipCommon': {
    'vi': 'Máy đi theo BẢN CHUNG của cả fleet (OTA thường). Chọn bản khác ở đây để ghim riêng cho máy này.',
    'en': 'This machine follows the fleet-wide version (normal OTA). Pick another here to pin it to this machine only.'
  },
  'mm.updTipPinned': {
    'vi': 'Máy được GHIM RIÊNG, không đi theo bản chung. Đặt bởi {by}.',
    'en': 'This machine is PINNED and ignores the fleet-wide version. Set by {by}.'
  },
  'mm.updTipUnknown': {
    'vi': 'Không so được: tên file không mang version, hoặc máy chưa báo version lần nào.',
    'en': 'Cannot compare: the file name carries no version, or the machine never reported one.'
  },
  'mm.fwAsOf': {
    'vi': 'Bản của LẦN ĐO gần nhất ({when}) — không phải bản máy đang chạy lúc này. Nạp xong mà chưa chạy mẫu thì số này chưa đổi.',
    'en': 'Version as of the LAST RUN ({when}) — not what the machine is running right now. Flash it and this stays put until someone runs a sample.'
  },
  'mm.fwNoRun': {
    'vi': 'Máy chưa gửi lần đo nào — chưa biết bản đang chạy.',
    'en': 'No run reported yet — the running version is unknown.'
  },
  'mm.histGuess': {
    'vi': '≈ suy từ lần đo đầu',
    'en': '≈ inferred from first run',
    'zh': '≈ 由首次测量推算',
    'th': '≈ ประมาณจากรอบแรก'
  },
  'mm.histNote': {
    'vi': 'Từ v2.4.5 máy TỰ BÁO về server ngay khi nạp xong → dòng KHÔNG có dấu ≈ là ngày cập nhật THẬT của khách. Dòng có ≈ là suy ra từ version kèm mỗi lần đo: ngày đó là lần đo ĐẦU TIÊN báo bản ấy, lần nạp xảy ra trước nó. "Cách cập nhật" thì LUÔN là ước đoán, theo bản ĐANG CHẠY TRƯỚC ĐÓ: từ v2.4.0 firmware mới tự cập nhật được, nên máy đang chạy bản cũ hơn thì chỉ có thể nạp tay.',
    'en': 'From v2.4.5 the machine REPORTS IN as soon as it reboots after flashing → a row without ≈ is the customer’s real update date. A row with ≈ is derived from the version each run carries: that date is the FIRST run reporting the version, so the flash happened before it. "Method" is ALWAYS inferred, from the PREVIOUS version: self-update exists only from v2.4.0, so a machine on an older build can only have been flashed by hand.'
  },
  'mm.histMore': {
    'vi': 'Chỉ đọc {n} lần đo gần nhất — máy có {total}. Phần cũ hơn chưa hiện.',
    'en': 'Read only the latest {n} runs of {total}. Older history not shown.'
  },
  // --- Tab Chăm sóc KH (nhân sự): Thông tin máy | Xử lý sự cố ---
  'nav.support': {'vi': 'Chăm sóc KH', 'en': 'Customer care', 'zh': '客户服务', 'th': 'ดูแลลูกค้า'},
  'sp.info': {'vi': 'Thông tin máy', 'en': 'Machine info', 'zh': '设备信息', 'th': 'ข้อมูลเครื่อง'},
  'sp.trouble': {'vi': 'Xử lý sự cố', 'en': 'Troubleshooting', 'zh': '故障处理', 'th': 'แก้ไขปัญหา'},
  'sp.infoHint': {'vi': 'Hiểu sản phẩm và cách vận hành để trả lời khách — không cần kiến thức kỹ thuật. Gõ từ khoá để lọc.', 'en': 'Understand the product and how it is used so you can answer customers — no technical background needed. Type to filter.'},
  'sp.troubleHint': {'vi': 'Cắm cáp USB máy → Kết nối → nhật ký máy tự hiện → app tóm tắt máy đang gặp gì → điền mã máy + mô tả → Gửi về kỹ thuật.', 'en': 'Plug the USB cable → Connect → the machine log appears → the app sums up what is wrong → enter machine ID + note → Send to engineering.'},
  'sp.search': {'vi': 'Tìm trong thông tin máy…', 'en': 'Search machine info…', 'zh': '搜索设备信息…', 'th': 'ค้นหาข้อมูลเครื่อง…'},
  'sp.noMatch': {'vi': 'Không có mục nào khớp.', 'en': 'Nothing matches.', 'zh': '没有匹配项。', 'th': 'ไม่พบรายการที่ตรงกัน'},
  'sp.errCodeLabel': {'vi': 'Mã lỗi trên màn', 'en': 'On-screen code'},
  'sp.errCodeHint': {'vi': 'Nhập 4 chữ số hiện trên màn TFT để tách thành module · loại · bước · slot.', 'en': 'Type the 4 digits shown on the TFT to split into module · type · step · slot.'},
  'sp.errCodeInvalid': {'vi': 'Mã lỗi gồm đúng 4 chữ số.', 'en': 'The code is exactly 4 digits.'},
  'sp.errModule': {'vi': 'Module', 'en': 'Module'},
  'sp.errType': {'vi': 'Loại', 'en': 'Type'},
  'sp.errStep': {'vi': 'Bước', 'en': 'Step'},
  'sp.errSlot': {'vi': 'Slot', 'en': 'Slot'},
  'sp.step1': {'vi': 'Kết nối máy', 'en': 'Connect the machine'},
  'sp.step2': {'vi': 'Nhật ký máy (log)', 'en': 'Machine log'},
  'sp.step3': {'vi': 'Gửi về kỹ thuật', 'en': 'Send to engineering'},
  'sp.port': {'vi': 'Cổng COM', 'en': 'COM port', 'zh': 'COM 端口', 'th': 'พอร์ต COM'},
  'sp.refreshPorts': {'vi': 'Làm mới cổng', 'en': 'Refresh ports'},
  'sp.connect': {'vi': 'Kết nối', 'en': 'Connect', 'zh': '连接', 'th': 'เชื่อมต่อ'},
  'sp.connecting': {'vi': 'Đang kết nối…', 'en': 'Connecting…'},
  'sp.disconnect': {'vi': 'Ngắt kết nối', 'en': 'Disconnect', 'zh': '断开', 'th': 'ตัดการเชื่อมต่อ'},
  'sp.connected': {'vi': 'Đã kết nối', 'en': 'Connected', 'zh': '已连接', 'th': 'เชื่อมต่อแล้ว'},
  'sp.notConnected': {'vi': 'Chưa kết nối', 'en': 'Not connected', 'zh': '未连接', 'th': 'ยังไม่เชื่อมต่อ'},
  'sp.noPorts': {'vi': 'Không thấy cổng USB nào. Cắm cáp USB máy (cáp có dây data) rồi bấm làm mới; chưa có driver CP210x/CH340 thì cài.', 'en': 'No USB port found. Plug the machine USB cable (a data cable) then refresh; install the CP210x/CH340 driver if missing.'},
  'sp.webPick': {'vi': 'Bấm "Kết nối" rồi chọn cổng của máy trong hộp thoại trình duyệt.', 'en': 'Press "Connect" and pick the machine port in the browser dialog.'},
  'sp.openFail': {'vi': 'Không mở được cổng: {err}', 'en': 'Could not open the port: {err}'},
  'sp.lost': {'vi': 'Mất kết nối với máy — cáp rút hoặc máy tắt. Cắm lại rồi bấm Kết nối; log đã đọc vẫn còn.', 'en': 'Connection lost — cable unplugged or machine off. Reconnect; the log read so far is kept.'},
  'sp.quick': {'vi': 'Yêu cầu máy:', 'en': 'Ask the machine to:'},
  'sp.cmdTip': {'vi': 'Lệnh gửi cho máy: {cmd}', 'en': 'Command sent to the machine: {cmd}'},
  'sp.cmdPara': {'vi': 'Đọc tham số', 'en': 'Read parameters'},
  'sp.cmdCal': {'vi': 'Đọc hiệu chuẩn', 'en': 'Read calibration'},
  'sp.cmdTemp': {'vi': 'Bật/tắt nhiệt', 'en': 'Toggle temperatures'},
  'sp.cmdReset': {'vi': 'Khởi động lại máy', 'en': 'Restart machine'},
  'sp.cmdSent': {'vi': 'Đã gửi lệnh {cmd} — xem phản hồi trong log.', 'en': 'Sent {cmd} — watch the log for the reply.'},
  'sp.resetTitle': {'vi': 'Khởi động lại máy?', 'en': 'Restart the machine?'},
  'sp.resetBody': {'vi': 'Máy đang chạy mẫu sẽ MẤT lần chạy đó. Chỉ khởi động lại khi máy rảnh — dùng để lấy log lúc máy khởi động (lý do reset, version).', 'en': 'A run in progress will be LOST. Restart only when idle — useful to capture the boot log (reset reason, version).'},
  'sp.lines': {'vi': '{n} dòng', 'en': '{n} lines', 'zh': '{n} 行', 'th': '{n} บรรทัด'},
  'sp.onlyIssues': {'vi': 'Chỉ dòng đáng chú ý', 'en': 'Notable lines only'},
  'sp.autoscroll': {'vi': 'Tự cuộn', 'en': 'Auto-scroll'},
  'sp.clear': {'vi': 'Xóa log', 'en': 'Clear log'},
  'sp.saveLocal': {'vi': 'Lưu file', 'en': 'Save file', 'zh': '保存文件', 'th': 'บันทึกไฟล์'},
  'sp.saved': {'vi': 'Đã lưu: {path}', 'en': 'Saved: {path}'},
  'sp.openFolder': {'vi': 'Mở thư mục', 'en': 'Open folder'},
  'sp.noData': {'vi': 'Chưa có nhật ký. Cắm cáp USB máy vào laptop và bấm Kết nối — chữ máy in ra sẽ hiện ở đây.', 'en': 'No log yet. Plug the machine into the laptop by USB and press Connect — what the machine prints shows up here.'},
  'sp.waiting': {'vi': 'Đã kết nối, đang chờ máy in… Máy im lặng thì bấm "Đọc tham số" hoặc thao tác trên máy.', 'en': 'Connected, waiting for the machine to print… If it stays quiet, press "Read parameters" or use the machine.'},
  'sp.triageTitle': {'vi': 'Máy đang nói gì', 'en': 'What the machine is saying'},
  'sp.triageNoLog': {'vi': 'Có nhật ký rồi app sẽ tự tóm tắt ở đây bằng lời dễ hiểu: máy gặp gì, khách tự thử được gì, khi nào chuyển kỹ thuật.', 'en': 'Once there is a log, the app sums it up here in plain words: what is wrong, what the customer can try, when to escalate.'},
  'sp.statusError': {'vi': 'Có lỗi máy không tự hết — chuyển kỹ thuật kèm nhật ký này.', 'en': 'There is a machine fault that will not clear itself — escalate with this log.'},
  'sp.statusWarn': {'vi': 'Có việc khách tự thử được — làm theo gợi ý bên dưới; vẫn lặp thì gửi nhật ký.', 'en': 'The customer can try something first — follow the tips below; if it keeps happening, send the log.'},
  'sp.statusOk': {'vi': 'Không thấy dấu hiệu lỗi quen thuộc. Khách vẫn gặp sự cố thì mô tả rõ hiện tượng rồi gửi nhật ký.', 'en': 'No known error pattern. If the customer still has a problem, describe it and send the log.'},
  'sp.detectedId': {'vi': 'Mã máy trong log: {id}', 'en': 'Machine ID in log: {id}'},
  'sp.detectedVersion': {'vi': 'Firmware: {v}', 'en': 'Firmware: {v}'},
  'sp.deviceId': {'vi': 'Mã máy', 'en': 'Machine ID', 'zh': '设备编号', 'th': 'รหัสเครื่อง'},
  'sp.deviceIdHint': {'vi': 'In trên tem máy (RPL…). App tự điền nếu thấy trong log.', 'en': 'Printed on the label (RPL…). Filled automatically when found in the log.'},
  'sp.note': {'vi': 'Mô tả sự cố', 'en': 'Issue description', 'zh': '故障描述', 'th': 'รายละเอียดปัญหา'},
  'sp.noteHint': {'vi': 'Khách thấy gì, xảy ra lúc nào, có lặp không…', 'en': 'What the customer sees, when, does it repeat…'},
  'sp.by': {'vi': 'Người gửi', 'en': 'Sent by'},
  'sp.send': {'vi': 'Gửi log về kỹ thuật', 'en': 'Send log to engineering', 'zh': '发送日志给技术', 'th': 'ส่งล็อกให้ฝ่ายเทคนิค'},
  'sp.sending': {'vi': 'Đang gửi…', 'en': 'Sending…'},
  'sp.sent': {'vi': 'Đã gửi lên server: {file}', 'en': 'Sent to the server: {file}'},
  'sp.sendNeedText': {'vi': 'Chưa có log để gửi — kết nối máy và chờ log hiện ra.', 'en': 'Nothing to send yet — connect the machine and wait for the log.'},
  'sp.sendNeedId': {'vi': 'Nhập mã máy trước (in trên tem máy).', 'en': 'Enter the machine ID first (on the label).'},
  'sp.sendFail': {'vi': 'Gửi thất bại: {err}', 'en': 'Send failed: {err}'},
  'sp.sentList': {'vi': 'Log đã gửi', 'en': 'Sent logs'},
  'sp.sentEmpty': {'vi': 'Máy này chưa có log nào trên server.', 'en': 'No log on the server for this machine yet.'},
  'sp.viewSent': {'vi': 'Log đã gửi', 'en': 'Sent logs'},
  'sp.sentLoadFail': {'vi': 'Không tải được log: {err}', 'en': 'Could not load the log: {err}'},
  'sp.guideTitle': {'vi': 'Làm theo 4 bước', 'en': 'Four steps'},
  'sp.guide1': {'vi': 'Cắm cáp USB từ máy vào laptop, chọn cổng vừa xuất hiện, bấm Kết nối. Máy vẫn chạy bình thường, không bị tắt hay khởi động lại.', 'en': 'Plug the machine into the laptop by USB, pick the new port, press Connect. The machine keeps running normally.'},
  'sp.guide2': {'vi': 'Nhờ khách làm lại thao tác gây lỗi trên máy. Muốn xem máy khởi động thế nào thì bấm "Khởi động lại máy" (chỉ khi máy đang rảnh).', 'en': 'Have the customer reproduce the problem. To see how the machine boots, press "Restart machine" (only when idle).'},
  'sp.guide3': {'vi': 'Đọc khung "Máy đang nói gì": app tóm tắt bằng lời dễ hiểu. Nhiều ca (nguồn yếu, mất WiFi) xử lý được ngay với khách.', 'en': 'Read "What the machine is saying": the app sums it up in plain words. Many cases (weak power, WiFi) can be fixed with the customer right away.'},
  'sp.guide4': {'vi': 'Điền mã máy + mô tả sự cố rồi bấm Gửi về kỹ thuật. Không có mạng thì Lưu file và gửi email.', 'en': 'Fill in machine ID + description and press Send to engineering. Without network, Save file and email it.'},
  'sp.introInfo': {'vi': 'Tài liệu này viết cho nhân viên chăm sóc khách hàng: đọc từ trên xuống lần đầu, sau đó dùng chip bên trên hoặc ô tìm để tra nhanh khi khách gọi. Mục "Dành cho kỹ thuật" ở cuối chỉ mở khi kỹ thuật yêu cầu.', 'en': 'Written for customer-care staff: read top to bottom once, then use the chips above or the search box while on a call. The "For engineering" section at the end is only needed when engineering asks.'},
  'sp.jumpTo': {'vi': 'Đi tới:', 'en': 'Jump to:'},
  'sp.advancedTag': {'vi': 'Kỹ thuật', 'en': 'Technical'},
  'sp.advancedShow': {'vi': 'Mở', 'en': 'Show'},
  'sp.advancedHide': {'vi': 'Thu gọn', 'en': 'Hide'},

  // --- Log đã nhận (support_inbox_screen.dart) — hộp thư log CSKH cho kỹ thuật ---
  'lg.tab': {'vi': 'Log đã nhận', 'en': 'Received logs'},
  'lg.hint': {'vi': 'Log CSKH gửi về từ mọi máy. Mở một bản → Nhận xử lý → Đã xử lý, kèm ghi chú trả lời (CSKH thấy ở "Log đã gửi").', 'en': 'Logs sent by customer care from every machine. Open one → Take → Done, with a reply note (customer care sees it under "Sent logs").'},
  'lg.status.new': {'vi': 'Mới', 'en': 'New'},
  'lg.status.working': {'vi': 'Đang xử lý', 'en': 'In progress'},
  'lg.status.done': {'vi': 'Đã xử lý', 'en': 'Done'},
  'lg.all': {'vi': 'Tất cả', 'en': 'All'},
  'lg.filterDevice': {'vi': 'Lọc mã máy', 'en': 'Filter machine ID'},
  'lg.empty': {'vi': 'Không có bản log nào khớp bộ lọc.', 'en': 'No log matches the filter.'},
  'lg.emptyNew': {'vi': 'Không còn log mới — mọi bản đã có người nhận. 🎉', 'en': 'No new logs — everything has been taken. 🎉'},
  'lg.more': {'vi': 'Tải thêm ({n} bản)', 'en': 'Load more ({n} left)'},
  'lg.detail': {'vi': 'Log', 'en': 'Log'},
  'lg.receivedAt': {'vi': 'Nhận lúc', 'en': 'Received'},
  'lg.size': {'vi': 'Dung lượng', 'en': 'Size'},
  'lg.signs': {'vi': 'Dấu hiệu bộ quét thấy lúc gửi', 'en': 'Signs found by the scanner when sent'},
  'lg.noSigns': {'vi': 'Không thấy lỗi quen thuộc.', 'en': 'No familiar error found.'},
  'lg.handledBy': {'vi': 'Người xử lý', 'en': 'Handled by'},
  'lg.replyLabel': {'vi': 'Ghi chú / trả lời CSKH', 'en': 'Note / reply to customer care'},
  'lg.replyHelp': {'vi': 'Nguyên nhân, đã làm gì, dặn khách gì — lưu cùng trạng thái.', 'en': 'Cause, what was done, what to tell the customer — saved with the status.'},
  'lg.reply': {'vi': 'Kỹ thuật trả lời', 'en': 'Engineering reply'},
  'lg.openRaw': {'vi': 'Xem log thô', 'en': 'Open raw log'},
  'lg.reopen': {'vi': 'Mở lại', 'en': 'Reopen'},
  'lg.take': {'vi': 'Nhận xử lý', 'en': 'Take'},
  'lg.markDone': {'vi': 'Đã xử lý', 'en': 'Mark done'},
  'lg.saveNote': {'vi': 'Lưu ghi chú', 'en': 'Save note'},
  'lg.statusSaved': {'vi': 'Đã chuyển sang "{s}".', 'en': 'Moved to "{s}".'},
  'lg.deleteTitle': {'vi': 'Xoá bản log?', 'en': 'Delete this log?'},
  'lg.deleteBody': {'vi': 'Xoá hẳn {file} khỏi server — không khôi phục được. Chỉ dùng cho bản gửi nhầm/thử.', 'en': 'Permanently delete {file} from the server — cannot be undone. Only for mistaken/test uploads.'},

  // --- Thống kê lỗi (support_log_stats_screen.dart) ---
  'ls.tab': {'vi': 'Thống kê lỗi', 'en': 'Error stats'},
  'ls.hint': {'vi': 'Lỗi nào lặp lại trên cả đội máy — gom từ dấu hiệu bộ quét trong các log CSKH đã gửi.', 'en': 'Which errors repeat across the fleet — built from scanner signs in logs sent by customer care.'},
  'ls.days': {'vi': '{n} ngày', 'en': '{n} days'},
  'ls.all': {'vi': 'Mọi lúc', 'en': 'All time'},
  'ls.empty': {'vi': 'Chưa có log nào trong khoảng này.', 'en': 'No logs in this period.'},
  'ls.total': {'vi': 'Số log', 'en': 'Logs'},
  'ls.pending': {'vi': 'Chưa xong', 'en': 'Open'},
  'ls.withErrors': {'vi': 'Có lỗi', 'en': 'With errors'},
  'ls.devices': {'vi': 'Số máy', 'en': 'Machines'},
  'ls.clean': {'vi': 'Không dấu hiệu', 'en': 'No signs'},
  'ls.signs': {'vi': 'Dấu hiệu hay gặp', 'en': 'Most common signs'},
  'ls.signsHint': {'vi': 'Đỏ = lỗi, vàng = cảnh báo. Số máy cao = vấn đề diện rộng, không phải một máy hỏng.', 'en': 'Red = error, yellow = warning. Many machines = widespread issue, not one bad unit.'},
  'ls.noSigns': {'vi': 'Không có dấu hiệu lỗi/cảnh báo nào.', 'en': 'No error/warning signs.'},
  'ls.signTrail': {'vi': '{logs} log · {dev} máy', 'en': '{logs} logs · {dev} machines'},
  'ls.byDevice': {'vi': 'Máy gửi nhiều log nhất', 'en': 'Machines with most logs'},
  'ls.byDeviceHint': {'vi': 'Bấm một máy để mở hộp thư lọc theo máy đó.', 'en': 'Tap a machine to open the inbox filtered by it.'},
  'ls.deviceSub': {'vi': '{logs} log · {err} có lỗi · {new} mới · gần nhất {last}', 'en': '{logs} logs · {err} with errors · {new} new · last {last}'},
  'ls.byFw': {'vi': 'Theo phiên bản firmware', 'en': 'By firmware version'},
  'ls.byFwHint': {'vi': 'Thanh = tỉ lệ log có lỗi của bản đó. Bản mới mà tỉ lệ vọt lên là dấu hiệu lỗi hồi quy.', 'en': 'Bar = share of logs with errors for that version. A jump on a new version hints at a regression.'},
  'ls.fwUnknown': {'vi': 'Không rõ bản', 'en': 'Unknown version'},
  'ls.fwTrail': {'vi': '{err}/{logs} có lỗi · {dev} máy', 'en': '{err}/{logs} with errors · {dev} machines'},
  'ls.daily': {'vi': 'Số log theo ngày', 'en': 'Logs per day'},
  'ls.dailyHint': {'vi': 'Tối đa 60 ngày có log gần nhất; phần đỏ = log có lỗi.', 'en': 'Up to the last 60 days with logs; red = logs with errors.'},
  'ls.dayTip': {'vi': '{day}: {logs} log, {err} có lỗi', 'en': '{day}: {logs} logs, {err} with errors'},

  // --- Bộ quét log (util/log_triage.dart): tiêu đề + gợi ý theo khoá luật ---
  'triage.brownout': {'vi': 'Nguồn yếu — máy tự reset (brownout)', 'en': 'Weak power — machine self-resets (brownout)'},
  'triage.brownoutHint': {'vi': 'Điện áp tụt. Đổi adapter/ổ cắm, không dùng chung ổ với thiết bị công suất lớn. Vẫn lặp → chuyển kỹ thuật.', 'en': 'Voltage dropped. Change the adapter/outlet, avoid sharing with high-power devices. Still repeating → escalate.'},
  'triage.crash': {'vi': 'Firmware bị lỗi nghiêm trọng (crash)', 'en': 'Firmware crashed'},
  'triage.crashHint': {'vi': 'Máy tự khởi động lại do lỗi phần mềm. Ghi lại thao tác ngay trước đó và gửi log — kỹ thuật cần dòng Backtrace.', 'en': 'The machine rebooted from a software fault. Note what happened right before and send the log — engineering needs the Backtrace line.'},
  'triage.noFirmware': {'vi': 'Không có firmware hợp lệ trong máy', 'en': 'No valid firmware on the machine'},
  'triage.noFirmwareHint': {'vi': 'Thường sau lần nạp code hỏng/thiếu file. Kỹ thuật cần nạp lại đủ bootloader + partition + app đúng offset.', 'en': 'Usually after a bad/incomplete flash. Engineering must reflash bootloader + partition + app at the right offsets.'},
  'triage.flashRead': {'vi': 'Lỗi đọc bộ nhớ flash', 'en': 'Flash read error'},
  'triage.flashReadHint': {'vi': 'Sai chế độ flash (DIO/QIO) khi nạp hoặc chip flash hỏng. Chuyển kỹ thuật kèm log.', 'en': 'Wrong flash mode (DIO/QIO) when flashing, or a bad flash chip. Escalate with the log.'},
  'triage.watchdog': {'vi': 'Máy bị treo rồi tự reset (watchdog)', 'en': 'Machine hung then self-reset (watchdog)'},
  'triage.watchdogHint': {'vi': 'Một tác vụ trong firmware bị kẹt. Nếu lặp ở cùng một bước thì ghi rõ bước đó và gửi log.', 'en': 'A firmware task got stuck. If it repeats at the same step, note the step and send the log.'},
  'triage.tempSensor': {'vi': 'Cảm biến nhiệt không đọc được (-127 / NaN)', 'en': 'Temperature sensor unreadable (-127 / NaN)'},
  'triage.tempSensorHint': {'vi': 'Đứt dây hoặc lỏng đầu nối cảm biến. Máy không chạy đúng được — chuyển kỹ thuật.', 'en': 'Broken wire or loose sensor connector. The machine cannot run correctly — escalate.'},
  'triage.wifi': {'vi': 'WiFi không ổn định / không nối được', 'en': 'WiFi unstable / cannot connect'},
  'triage.wifiHint': {'vi': 'Hỏi khách: WiFi có đổi mật khẩu, đổi router, hay máy ở xa sóng không. Cấu hình lại WiFi trên máy.', 'en': 'Ask: did the WiFi password/router change, is the machine far from the router? Reconfigure WiFi on the machine.'},
  'triage.upload': {'vi': 'Gửi kết quả lên server thất bại', 'en': 'Result upload to the server failed'},
  'triage.uploadHint': {'vi': 'Máy có mạng nhưng không tới được server (chặn cổng, mất Internet). Kiểm "Lần gửi cuối" ở Trạng thái máy; báo kỹ thuật nếu nhiều máy cùng lúc.', 'en': 'Networked but cannot reach the server (blocked port, no Internet). Check "Last upload" in Machine status; escalate if several machines at once.'},
  'triage.ota': {'vi': 'Cập nhật firmware (OTA) lỗi', 'en': 'Firmware update (OTA) failed'},
  'triage.otaHint': {'vi': 'Tải bản mới không xong (mạng yếu) hoặc bản không hợp lệ. Thử lại khi mạng ổn; vẫn lỗi → kỹ thuật.', 'en': 'Download incomplete (weak network) or invalid image. Retry on a stable network; still failing → engineering.'},
  'triage.idfError': {'vi': 'Có dòng lỗi hệ thống (E)', 'en': 'System error lines (E)'},
  'triage.idfErrorHint': {'vi': 'Thông điệp lỗi mức thấp của ESP32. Không tự xử được — gửi kèm log để kỹ thuật đọc.', 'en': 'Low-level ESP32 error messages. Not user-serviceable — include in the log for engineering.'},
  'triage.resetPower': {'vi': 'Máy vừa bật nguồn', 'en': 'Machine was powered on'},
  'triage.resetPowerHint': {'vi': 'Khởi động bình thường sau khi cắm điện — không phải lỗi.', 'en': 'Normal boot after power-on — not an error.'},
  'triage.resetSoft': {'vi': 'Máy tự khởi động lại (reset mềm)', 'en': 'Machine restarted itself (soft reset)'},
  'triage.resetSoftHint': {'vi': 'Reset do phần mềm (sau cập nhật, lệnh Res…). Một lần = bình thường; lặp liên tục = xem các dấu hiệu khác.', 'en': 'Software-initiated reset (after update, Res command…). Once = normal; repeating = check the other findings.'},

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

  // --- Tab Hiệu chuẩn (ống chuẩn quang): Lô pha | Bộ ống | Ngưỡng ---
  'nav.calib': {'vi': 'Hiệu chuẩn', 'en': 'Calibration', 'zh': '校准', 'th': 'การสอบเทียบ'},
  'calib.hint': {
    'vi': 'Pha ống chuẩn để hiệu chuẩn máy: pha dung dịch → đo từng ống → chọn bộ đạt → cấp cho máy.',
    'en': 'Reference tubes for calibrating devices: dilute → measure each tube → pick passing sets → issue to devices.'
  },
  'calib.batches': {'vi': 'Lô pha', 'en': 'Batches'},
  'calib.sets': {'vi': 'Bộ ống', 'en': 'Tube sets'},
  'calib.limits': {'vi': 'Ngưỡng', 'en': 'Limits'},

  // --- Tab Sản xuất (ATE): Chạy trạm | Hồ sơ máy | Thống kê ---
  'nav.ate': {'vi': 'Sản xuất', 'en': 'Production', 'zh': '生产', 'th': 'การผลิต'},
  'ate.shellHint': {
    'vi': 'Trạm nghiệm thu máy tại xưởng: nạp firmware → ghi số máy → hồ sơ có truy vết.',
    'en': 'Factory acceptance station: flash firmware → write serial number → traceable record.'
  },
  'ate.shellHintWeb': {
    'vi': 'Bản web chỉ tra cứu hồ sơ và thống kê — chạy trạm phải dùng app trên máy tính của trạm.',
    'en': 'The web build only looks up records and stats — running the station needs the desktop app.'
  },
  'ate.run': {'vi': 'Chạy trạm', 'en': 'Run station'},
  'ate.profile': {'vi': 'Hồ sơ máy', 'en': 'Machine record'},
  'ate.stats': {'vi': 'Thống kê', 'en': 'Statistics'},

  // Tên bước (mã bước giữ nguyên, không dịch — nó là khoá vẽ Pareto)
  'ate.step.FW-02': {'vi': 'Nhận chip · flash · MAC', 'en': 'Detect chip · flash · MAC'},
  'ate.step.FW-01': {'vi': 'Nạp firmware', 'en': 'Flash firmware'},
  'ate.step.BOOT-01': {'vi': 'Khởi động sạch', 'en': 'Clean boot'},
  'ate.step.ID-01': {'vi': 'Ghi số máy', 'en': 'Write serial number'},
  'ate.step.ID-02': {'vi': 'Tham số mặc định của lô', 'en': 'Batch default parameters'},
  'ate.step.OPT-01': {'vi': '10 cảm biến quang', 'en': '10 optical sensors'},
  'ate.step.OPT-03': {'vi': 'Tín hiệu sáng từng slot', 'en': 'Slot bright signal'},
  'ate.step.TMP-01': {'vi': '6 kênh nhiệt', 'en': '6 temperature channels'},
  'ate.step.FAN-01': {'vi': 'Quạt', 'en': 'Fan'},
  'ate.step.BUZ-01': {'vi': 'Còi', 'en': 'Buzzer'},
  'ate.step.HMI-01': {'vi': 'Màn hình + 3 nút', 'en': 'Display + 3 buttons'},
  'ate.batch': {'vi': 'Lô sản xuất', 'en': 'Production batch'},
  'ate.batchHint': {
    'vi': 'Quyết định bộ TIÊU CHUẨN máy này bị chấm theo. Đổi lô thì bấm Enter để nạp lại ngưỡng.',
    'en': 'Chooses which limits this machine is judged by. Change it then press Enter to reload.'
  },
  'ate.limitsBatchWarn': {
    'vi': 'Lô "{b}" chưa có tiêu chuẩn riêng — đang chấm theo bộ {s}',
    'en': 'Batch "{b}" has no limits of its own — judging by the {s} set'
  },
  'ate.limits2': {'vi': 'Tiêu chuẩn', 'en': 'Limits'},
  'ate.limitsCommon': {'vi': 'Bộ chung', 'en': 'Common set'},
  'ate.limitsForBatch': {'vi': 'Tiêu chuẩn của lô {b}', 'en': 'Limits for batch {b}'},
  'ate.limitsInherited': {
    'vi': 'Lô này CHƯA có bộ riêng — trạm đang chấm theo bộ {s}. Lưu ở đây là tạo bộ riêng cho lô.',
    'en': 'This batch has no set yet — the station uses the {s} set. Saving here creates one for the batch.'
  },
  'ate.limitsNewBatch': {'vi': 'Thêm lô…', 'en': 'Add batch…'},
  'ate.limitsVersion': {'vi': 'Version bộ ngưỡng', 'en': 'Limits version'},
  'ate.limitsVersionHint': {
    'vi': 'Hồ sơ chỉ ghi chuỗi này. Sửa ngưỡng thì ĐỔI version — server từ chối dùng lại version cũ cho nội dung khác.',
    'en': 'Records only store this string. Change it whenever you change a threshold — the server rejects reusing a version for different content.'
  },
  'ate.limitsNeedVersion': {'vi': 'Phải đặt version cho bộ ngưỡng.', 'en': 'The limits set needs a version.'},
  'ate.limitsBadNumber': {'vi': 'Giá trị của "{f}" không phải số.', 'en': '"{f}" is not a number.'},
  'ate.limitsSaved': {'vi': 'Đã lưu tiêu chuẩn version {v}.', 'en': 'Saved limits version {v}.'},
  'ate.limitsSave': {'vi': 'Lưu tiêu chuẩn', 'en': 'Save limits'},
  'ate.limitsReadOnly': {
    'vi': 'Chỉ nhân sự kỹ thuật đặt được tiêu chuẩn. Bạn xem để biết lô đang chấm theo bộ nào.',
    'en': 'Only engineering staff can set limits. You can view which set the batch is judged by.'
  },
  'ate.limitsImport': {'vi': 'Nhập JSON', 'en': 'Import JSON'},
  'ate.limitsExport': {'vi': 'Xuất JSON', 'en': 'Export JSON'},
  'ate.limitsImported': {
    'vi': 'Đã nạp "{n}" vào biểu mẫu của {b} — KIỂM lại rồi bấm Lưu tiêu chuẩn.',
    'en': 'Loaded "{n}" into the form for {b} — review it, then press Save.'
  },
  'ate.limitsImportErr': {'vi': 'Không đọc được file: {e}', 'en': 'Could not read the file: {e}'},
  'ate.limitsImportMany': {
    'vi': 'File có {n} bộ tiêu chuẩn — ghi thẳng lên server?',
    'en': 'The file holds {n} limit sets — write them to the server?'
  },
  'ate.limitsImportManyDo': {'vi': 'Ghi tất cả', 'en': 'Write all'},
  'ate.limitsImportedMany': {'vi': 'Đã ghi {n} bộ: {b}.', 'en': 'Wrote {n} sets: {b}.'},
  'ate.limitsUnknownKeys': {
    'vi': 'Khoá app chưa biết (vẫn giữ nguyên khi lưu): {k}',
    'en': 'Keys this app does not know (kept as-is on save): {k}'
  },
  'ate.limitsExtraKept': {'vi': 'giữ {n} khoá lạ', 'en': '{n} unknown keys kept'},
  'ate.limitsExported': {'vi': 'Đã xuất: {p}', 'en': 'Exported: {p}'},
  'ate.limitsUnset': {'vi': 'chưa chốt', 'en': 'not set'},
  'ate.limitsOpto': {'vi': 'Quang (OPT-03)', 'en': 'Optics (OPT-03)'},
  'ate.limitsOptoHint': {
    'vi': 'Để TRỐNG = chưa chốt: trạm vẫn đo và ghi số vào hồ sơ, nhưng không chấm đạt/hỏng. Chạy 10–20 máy tốt rồi điền từ chính các số đó.',
    'en': 'Leave EMPTY = not set: the station still measures and records the numbers but does not judge. Run 10–20 known-good machines, then fill these in from those numbers.'
  },
  'ate.limitsTemp': {'vi': 'Nhiệt (TMP-01)', 'en': 'Temperature (TMP-01)'},
  'ate.limitsTempHint': {
    'vi': 'Nhiệt phòng là số của TỪNG xưởng — đo rồi điền; để trống thì chỉ chấm phần lệch giữa các kênh.',
    'en': 'Room temperature differs per workshop — measure it; leave empty and only channel spread is judged.'
  },
  'ate.limitsFlash': {'vi': 'Nạp & khai sinh (P0)', 'en': 'Flash & birth (P0)'},
  'ate.limitsVerify': {'vi': 'Bắt buộc verify sau khi nạp', 'en': 'Require verify after flashing'},
  'ate.confirmTitle': {'vi': 'Xác nhận tại chỗ', 'en': 'Confirm on the spot'},
  'ate.confirmYes': {'vi': 'ĐẠT', 'en': 'PASS'},
  'ate.confirmNo': {'vi': 'KHÔNG ĐẠT', 'en': 'FAIL'},
  'ate.confirmHint': {
    'vi': 'Nhìn/nghe máy rồi trả lời. Trả lời sai làm hỏng giá trị của cả cột dữ liệu này.',
    'en': 'Look at (or listen to) the machine, then answer. A careless answer ruins this whole column of data.'
  },

  // Chạy trạm
  'ate.port': {'vi': 'Cổng COM', 'en': 'COM port'},
  'ate.setup': {'vi': 'Cấu hình trạm', 'en': 'Station setup'},
  'ate.setupHint': {
    'vi': 'Khai một lần cho cả ca: mã trạm, người chạy, bộ 3 file .bin đang chốt cho lô và tham số nạp.',
    'en': 'Set once per shift: station code, operator, the three .bin files for this batch and flash options.'
  },
  'ate.limits': {'vi': 'Bộ ngưỡng', 'en': 'Limits'},
  'ate.limitsOffline': {
    'vi': 'Không lấy được bộ ngưỡng từ server — đang dùng bộ dự phòng',
    'en': 'Could not fetch limits from the server — using the local fallback'
  },
  'ate.queued': {'vi': 'Gửi lại {n} hồ sơ đang chờ', 'en': 'Resend {n} pending records'},
  'ate.offset': {'vi': 'Offset', 'en': 'Offset'},
  'ate.grantPort': {'vi': 'Chọn cổng', 'en': 'Pick port'},
  'ate.fromServer': {
    'vi': 'Lấy từ kho firmware trên server',
    'en': 'From the firmware store on the server'
  },
  'ate.webNote': {
    'vi': 'Bản web: nạp bằng esptool-js qua Web Serial (Chrome/Edge máy tính), firmware lấy từ kho trên server. Bước nạp CHƯA đối chiếu lại được nội dung flash.',
    'en': 'Web build: flashing via esptool-js over Web Serial (desktop Chrome/Edge), firmware from the server store. The flash step cannot verify flash contents yet.'
  },
  'ate.noBin': {'vi': '(chưa chọn file)', 'en': '(no file selected)'},
  'ate.station': {'vi': 'Mã trạm', 'en': 'Station'},
  'ate.operator': {'vi': 'Người chạy', 'en': 'Operator'},
  'ate.pcbVersion': {'vi': 'PCB version', 'en': 'PCB version'},
  'ate.expectFw': {'vi': 'FW kỳ vọng', 'en': 'Expected FW'},
  'ate.paraVersion': {'vi': 'para version', 'en': 'para version'},
  'ate.pickBootloader': {'vi': 'Chọn bootloader', 'en': 'Pick bootloader'},
  'ate.pickPartition': {'vi': 'Chọn partition', 'en': 'Pick partition'},
  'ate.pickApp': {'vi': 'Chọn firmware', 'en': 'Pick firmware'},
  'ate.chip': {'vi': 'Chip', 'en': 'Chip'},
  'ate.flashMode': {'vi': 'Flash mode', 'en': 'Flash mode'},
  'ate.flashSize': {'vi': 'Flash size', 'en': 'Flash size'},
  'ate.flashBaud': {'vi': 'Baud nạp', 'en': 'Flash baud'},
  'ate.eraseFirst': {'vi': 'Xoá flash trước khi nạp', 'en': 'Erase flash first'},
  'ate.scanHint': {
    'vi': 'Quét mã vạch số máy (hoặc gõ rồi Enter) — trạm chạy ngay sau khi quét.',
    'en': 'Scan the machine barcode (or type then press Enter) — the station starts right away.'
  },
  'ate.sn': {'vi': 'Số máy', 'en': 'Serial number'},
  'ate.snHelp': {
    'vi': 'Tối đa {n} ký tự — firmware chỉ nhận bấy nhiêu, dài hơn là máy không nhận mã.',
    'en': 'At most {n} characters — the firmware rejects longer ids.'
  },
  'ate.start': {'vi': 'BẮT ĐẦU', 'en': 'START'},
  'ate.stop': {'vi': 'DỪNG', 'en': 'STOP'},
  'ate.note': {'vi': 'Ghi chú (tuỳ chọn)', 'en': 'Note (optional)'},
  'ate.pass': {'vi': 'ĐẠT', 'en': 'PASS'},
  'ate.fail': {'vi': 'HỎNG', 'en': 'FAIL'},
  'ate.aborted': {'vi': 'DỪNG GIỮA CHỪNG', 'en': 'ABORTED'},
  'ate.failedAt': {'vi': 'hỏng ở', 'en': 'failed at'},
  'ate.rerun': {'vi': 'Chạy lại bước', 'en': 'Rerun step'},
  'ate.stepOf': {'vi': 'Bước {i}/{n}', 'en': 'Step {i}/{n}'},
  'ate.nextMachine': {
    'vi': 'Gỡ máy ra, quét số máy tiếp theo.',
    'en': 'Remove the machine and scan the next one.'
  },
  'ate.binGone': {
    'vi': 'Bản "{f}" KHÔNG còn trong kho — chọn lại trước khi chạy.',
    'en': 'Version "{f}" is NO LONGER in the store — pick another before running.'
  },
  'ate.log': {'vi': 'Nhật ký trạm', 'en': 'Station log'},
  'ate.logEmpty': {'vi': 'Chưa chạy máy nào.', 'en': 'No run yet.'},
  'ate.logLines': {'vi': '{n} dòng', 'en': '{n} lines'},
  'ate.logSave': {'vi': 'Tải nhật ký (.txt)', 'en': 'Download log (.txt)'},
  'ate.logSaved': {'vi': 'Đã lưu nhật ký: {p}', 'en': 'Log saved: {p}'},
  'ate.saveFailed': {
    'vi': 'KHÔNG lưu được hồ sơ xuống máy trạm: {err}. Dừng dây chuyền và báo kỹ thuật — chạy tiếp là mất dữ liệu nghiệm thu.',
    'en': 'Could NOT write the record on this PC: {err}. Stop and call engineering — continuing loses acceptance data.'
  },
  'ate.sentOk': {'vi': 'Đã lưu và gửi hồ sơ ({id}).', 'en': 'Record saved and uploaded ({id}).'},
  'ate.savedOffline': {
    'vi': 'Đã lưu hồ sơ trên máy trạm, CHƯA gửi được lên server ({err}) — sẽ tự gửi lại.',
    'en': 'Record saved locally but not uploaded ({err}) — it will be resent automatically.'
  },
  'ate.flushed': {'vi': 'Đã gửi thêm {n} hồ sơ chờ.', 'en': 'Sent {n} pending records.'},
  'ate.rejected': {
    'vi': '{n} hồ sơ bị server từ chối — đã chuyển sang thư mục loi_gui để không kẹt hàng đợi.',
    'en': '{n} records were rejected by the server — moved to loi_gui so the queue keeps moving.'
  },
  'ate.nothingRan': {
    'vi': 'Chưa bước nào chạy xong — không có gì để nghiệm thu, chưa lưu hồ sơ nào.',
    'en': 'No step finished — nothing to accept, no record was saved.'
  },

  // Hồ sơ máy
  'ate.recent': {'vi': 'Hồ sơ gần đây', 'en': 'Recent records'},
  'ate.recentEmpty': {
    'vi': 'Chưa có hồ sơ nghiệm thu nào.\nMáy chạy qua trạm xong sẽ hiện ở đây.',
    'en': 'No acceptance records yet.\nMachines show up here once they pass through the station.'
  },
  'ate.clearSearch': {'vi': 'Xoá tìm kiếm', 'en': 'Clear search'},
  'ate.snSearchHint': {'vi': 'Gõ hoặc quét số máy rồi Enter', 'en': 'Type or scan a serial number then press Enter'},
  'ate.search': {'vi': 'Tra cứu', 'en': 'Look up'},
  'ate.noPermission': {
    'vi': 'Tài khoản của bạn không được cấp quyền xem máy {sn}.',
    'en': 'Your account is not allowed to view machine {sn}.'
  },
  'ate.noRecords': {'vi': 'Chưa có hồ sơ ATE nào của máy {sn}.', 'en': 'No ATE record for machine {sn} yet.'},
  'ate.attempts': {'vi': 'Các lần qua trạm ({n})', 'en': 'Station runs ({n})'},
  'ate.noBirth': {
    'vi': 'Máy này CHƯA có lần nào ĐẠT — chưa được nghiệm thu.',
    'en': 'This machine has never passed — not accepted yet.'
  },
  'ate.birthAt': {'vi': 'Ngày khai sinh', 'en': 'Birth date'},
  'ate.fw': {'vi': 'Firmware', 'en': 'Firmware'},

  // Thống kê
  'ate.from': {'vi': 'Từ ngày', 'en': 'From'},
  'ate.to': {'vi': 'Đến ngày', 'en': 'To'},
  'ate.fpy': {'vi': 'FPY (đạt ngay lần đầu)', 'en': 'FPY (first pass yield)'},
  'ate.machines': {'vi': 'máy', 'en': 'machines'},
  'ate.recordsTotal': {'vi': 'Hồ sơ', 'en': 'Records'},
  'ate.failHint': {'vi': 'hồ sơ hỏng', 'en': 'failed records'},
  'ate.abortedHint': {'vi': 'dừng giữa chừng', 'en': 'aborted runs'},
  'ate.pareto': {'vi': 'Pareto mã bước hỏng', 'en': 'Failure Pareto'},
  'ate.paretoHint': {
    'vi': 'Bước hỏng ĐẦU TIÊN của mỗi hồ sơ — sửa từ trên xuống là bớt lỗi nhanh nhất.',
    'en': 'The FIRST failing step of each record — fixing from the top removes the most failures.'
  },
  'ate.noFail': {'vi': 'Không có hồ sơ hỏng trong khoảng này.', 'en': 'No failures in this range.'},
  'ate.byDay': {'vi': 'Sản lượng theo ngày', 'en': 'Output by day'},
  'ate.noData': {'vi': 'Chưa có dữ liệu.', 'en': 'No data yet.'},
  'ate.day': {'vi': 'Ngày', 'en': 'Day'},

  // --- Tab Giám sát (root) ---
  'nav.monitor': {'vi': 'Giám sát', 'en': 'Monitoring', 'zh': '监控', 'th': 'เฝ้าระวัง'},
  'mon.subtitle': {'vi': 'Tình trạng Engineer Server và lưu lượng dữ liệu nhận được', 'en': 'Engineer Server health and incoming data flow', 'zh': '服务器状态与数据流量', 'th': 'สถานะเซิร์ฟเวอร์และปริมาณข้อมูล'},
  'mon.refresh': {'vi': 'Làm mới', 'en': 'Refresh', 'zh': '刷新', 'th': 'รีเฟรช'},
  'mon.updatedAt': {'vi': 'Cập nhật lúc {t}', 'en': 'Updated at {t}', 'zh': '更新于 {t}', 'th': 'อัปเดตเมื่อ {t}'},
  'mon.service': {'vi': 'Dịch vụ', 'en': 'Service', 'zh': '服务', 'th': 'บริการ'},
  'mon.uptime': {'vi': 'Chạy liên tục', 'en': 'Uptime', 'zh': '运行时长', 'th': 'เวลาทำงาน'},
  'mon.db': {'vi': 'Cơ sở dữ liệu', 'en': 'Database', 'zh': '数据库', 'th': 'ฐานข้อมูล'},
  'mon.dbOk': {'vi': 'Kết nối được', 'en': 'Connected', 'zh': '已连接', 'th': 'เชื่อมต่อแล้ว'},
  'mon.dbFail': {'vi': 'KHÔNG kết nối được', 'en': 'NOT reachable', 'zh': '无法连接', 'th': 'เชื่อมต่อไม่ได้'},
  'mon.cpu': {'vi': 'CPU', 'en': 'CPU', 'zh': 'CPU', 'th': 'CPU'},
  'mon.cpuLoad': {'vi': 'Tải 1 phút · {n} nhân', 'en': '1-min load · {n} cores', 'zh': '1 分钟负载 · {n} 核', 'th': 'โหลด 1 นาที · {n} คอร์'},
  'mon.mem': {'vi': 'RAM', 'en': 'Memory', 'zh': '内存', 'th': 'หน่วยความจำ'},
  'mon.disk': {'vi': 'Ổ đĩa', 'en': 'Disk', 'zh': '磁盘', 'th': 'ดิสก์'},
  'mon.freeOf': {'vi': 'còn {free} / {total}', 'en': '{free} free of {total}', 'zh': '剩余 {free} / {total}', 'th': 'เหลือ {free} / {total}'},
  'mon.flow': {'vi': 'Luồng dữ liệu', 'en': 'Data flow', 'zh': '数据流量', 'th': 'การไหลของข้อมูล'},
  'mon.last24h': {'vi': '24 giờ qua', 'en': 'Last 24h', 'zh': '过去 24 小时', 'th': '24 ชม. ล่าสุด'},
  'mon.last7d': {'vi': '7 ngày qua', 'en': 'Last 7 days', 'zh': '过去 7 天', 'th': '7 วันล่าสุด'},
  'mon.total': {'vi': 'Tổng phiên đo', 'en': 'Total runs', 'zh': '总测量数', 'th': 'ทั้งหมด'},
  'mon.devices': {'vi': 'Số máy đã gửi', 'en': 'Machines seen', 'zh': '设备数', 'th': 'จำนวนเครื่อง'},
  'mon.byDay': {'vi': 'Phiên đo nhận theo ngày (7 ngày)', 'en': 'Runs received per day (7 days)', 'zh': '每日接收量（7 天）', 'th': 'จำนวนต่อวัน (7 วัน)'},
  'mon.noRoute': {'vi': 'Server chưa có endpoint /monitor. Cần deploy bản server mới rồi khởi động lại dịch vụ fbt-receiver.', 'en': 'Server has no /monitor endpoint yet. Deploy the new server build and restart fbt-receiver.', 'zh': '服务器尚无 /monitor 接口，需部署新版本并重启 fbt-receiver。', 'th': 'เซิร์ฟเวอร์ยังไม่มี /monitor ต้องดีพลอยใหม่แล้วรีสตาร์ต fbt-receiver'},
  'mon.error': {'vi': 'Không đọc được số liệu giám sát', 'en': 'Could not read monitoring data', 'zh': '无法读取监控数据', 'th': 'อ่านข้อมูลเฝ้าระวังไม่ได้'},
  'unit.day': {'vi': 'ngày', 'en': 'd', 'zh': '天', 'th': 'วัน'},
  'unit.hour': {'vi': 'giờ', 'en': 'h', 'zh': '小时', 'th': 'ชม.'},
  'unit.min': {'vi': 'phút', 'en': 'min', 'zh': '分', 'th': 'นาที'},
  'unit.sec': {'vi': 'giây', 'en': 's', 'zh': '秒', 'th': 'วิ'},
  'mon.live': {'vi': 'CPU / RAM theo thời gian thực', 'en': 'CPU / RAM live', 'zh': 'CPU / 内存实时', 'th': 'CPU / RAM เรียลไทม์'},
  'mon.liveOn': {'vi': 'Đang lấy mẫu 5 giây/lần · cửa sổ 5 phút', 'en': 'Sampling every 5s · 5-minute window', 'zh': '每 5 秒采样 · 5 分钟窗口', 'th': 'เก็บทุก 5 วิ · หน้าต่าง 5 นาที'},
  'mon.liveOff': {'vi': 'Tạm dừng (chỉ lấy mẫu khi đang mở tab này)', 'en': 'Paused (samples only while this tab is open)', 'zh': '已暂停（仅在打开此标签时采样）', 'th': 'หยุดชั่วคราว (เก็บเฉพาะตอนเปิดแท็บนี้)'},
  'mon.liveWait': {'vi': 'Đang thu thập mẫu đầu tiên…', 'en': 'Collecting first samples…', 'zh': '正在采集首批样本…', 'th': 'กำลังเก็บตัวอย่างแรก…'},
  'mon.healthOk': {'vi': 'Server bình thường', 'en': 'Server healthy', 'zh': '服务器正常', 'th': 'เซิร์ฟเวอร์ปกติ'},
  'mon.healthWarn': {'vi': 'Cần chú ý: {what} đang ở {n}%', 'en': 'Attention: {what} at {n}%', 'zh': '需注意：{what} 已达 {n}%', 'th': 'ควรระวัง: {what} อยู่ที่ {n}%'},
  'mon.healthBad': {'vi': 'Nguy hiểm: {what} đã {n}%', 'en': 'Critical: {what} at {n}%', 'zh': '危险：{what} 已达 {n}%', 'th': 'วิกฤต: {what} ถึง {n}%'},
  'mon.healthDb': {'vi': 'Không kết nối được cơ sở dữ liệu — kết quả đo không vào được DB', 'en': 'Database unreachable — measurements are not being stored', 'zh': '数据库无法连接 — 测量结果无法入库', 'th': 'เชื่อมต่อฐานข้อมูลไม่ได้ — ผลวัดไม่ถูกบันทึก'},
  'mon.resources': {'vi': 'Tài nguyên', 'en': 'Resources', 'zh': '资源', 'th': 'ทรัพยากร'},
  'mon.status': {'vi': 'Trạng thái', 'en': 'Status', 'zh': '状态', 'th': 'สถานะ'},
  'mon.temp': {'vi': 'Nhiệt độ', 'en': 'Temperature', 'zh': '温度', 'th': 'อุณหภูมิ'},
  'mon.tempSensor': {'vi': 'Cảm biến nóng nhất: {s}', 'en': 'Hottest sensor: {s}', 'zh': '最热传感器：{s}', 'th': 'เซ็นเซอร์ร้อนสุด: {s}'},
  'mon.dbSize': {'vi': 'Cỡ dữ liệu', 'en': 'Data size', 'zh': '数据大小', 'th': 'ขนาดข้อมูล'},
  'mon.fullIn': {'vi': 'đầy sau {t}', 'en': 'full in {t}', 'zh': '{t} 后写满', 'th': 'เต็มใน {t}'},
  'mon.overYear': {'vi': 'hơn 1 năm', 'en': 'over a year', 'zh': '一年以上', 'th': 'มากกว่า 1 ปี'},
  'mon.months': {'vi': 'khoảng {n} tháng', 'en': 'about {n} months', 'zh': '约 {n} 个月', 'th': 'ประมาณ {n} เดือน'},
  'mon.days': {'vi': 'khoảng {n} ngày', 'en': 'about {n} days', 'zh': '约 {n} 天', 'th': 'ประมาณ {n} วัน'},
};
