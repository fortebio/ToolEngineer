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
  'mm.versionOverwrite': {
    'vi': '⚠ Server đã có bản cùng tên — tải lên sẽ GHI ĐÈ. Nếu muốn giữ bản cũ, đặt version khác.',
    'en': '⚠ A build with this name already exists on the server — uploading will OVERWRITE it. Use a different version to keep the old one.',
    'zh': '⚠ 服务器上已存在同名版本 — 上传将覆盖它。若要保留旧版本，请使用其他版本号。',
    'th': '⚠ มีรุ่นชื่อนี้บนเซิร์ฟเวอร์แล้ว — การอัปโหลดจะเขียนทับ หากต้องการเก็บรุ่นเก่า ให้ใช้เวอร์ชันอื่น'
  },
  'mm.otaEmpty': {'vi': 'Chưa có bản firmware nào trên server.', 'en': 'No firmware on the server yet.', 'zh': '服务器上还没有固件。', 'th': 'ยังไม่มีเฟิร์มแวร์บนเซิร์ฟเวอร์'},
  'mm.select': {'vi': 'Chọn bản này', 'en': 'Select', 'zh': '选择此版本', 'th': 'เลือกรุ่นนี้'},
  'mm.selected': {'vi': 'Đang chọn', 'en': 'Selected', 'zh': '已选择', 'th': 'กำลังเลือก'},
  'mm.noTarget': {'vi': 'Chưa chọn bản nào — máy không cập nhật.', 'en': 'No firmware selected — machines will not update.', 'zh': '未选择固件 — 设备不会更新。', 'th': 'ยังไม่ได้เลือกรุ่น — เครื่องจะไม่อัปเดต'},
  'mm.targetIs': {'vi': 'Máy sẽ nạp: {name}', 'en': 'Machines will flash: {name}', 'zh': '设备将刷入：{name}', 'th': 'เครื่องจะแฟลช: {name}'},
  'mm.clearTarget': {'vi': 'Huỷ chọn', 'en': 'Clear', 'zh': '取消选择', 'th': 'ยกเลิก'},
  'mm.delete': {'vi': 'Xoá', 'en': 'Delete', 'zh': '删除', 'th': 'ลบ'},
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
