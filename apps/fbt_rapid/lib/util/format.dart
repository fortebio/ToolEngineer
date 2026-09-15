// Các hàm format dùng chung (tránh phụ thuộc package intl để giảm rủi ro build).

import 'i18n.dart';

String _pad2(int n) => n.toString().padLeft(2, '0');

/// Dung lượng theo bội số **1024** (đúng cách `df -h`/`ls -lh` của box đếm):
/// `1536` → `1.5 KB`, `1610612736` → `1.50 GB`.
///
/// Gom về đây từ bản `_size()` riêng của màn Quản lý máy — bản đó dừng ở MB, mà
/// tab Giám sát phải nói dung lượng đĩa (GB/TB). Hai bản sao của cùng một phép
/// chia là hai chỗ phải sửa mỗi lần đổi cách hiển thị.
String formatBytes(int bytes) {
  if (bytes < 1024) return '$bytes B';
  const units = ['KB', 'MB', 'GB', 'TB'];
  var v = bytes / 1024;
  var i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  // KB lẻ 1 số là đủ; từ MB trở lên giữ 2 số để 1.05 GB không hiện thành 1.0 GB.
  return '${v.toStringAsFixed(i == 0 ? 1 : 2)} ${units[i]}';
}

/// Khoảng thời gian → `3 ngày 4 giờ` / `5 giờ 12 phút` / `47 giây`.
///
/// Chỉ hai đơn vị lớn nhất: uptime để trả lời "server có vừa khởi động lại
/// không", đọc tới từng giây là nhiễu.
String formatUptime(Duration d) {
  if (d.inMinutes < 1) return '${d.inSeconds} ${tr('unit.sec')}';
  if (d.inHours < 1) return '${d.inMinutes} ${tr('unit.min')}';
  if (d.inDays < 1) {
    return '${d.inHours} ${tr('unit.hour')} ${d.inMinutes % 60} ${tr('unit.min')}';
  }
  return '${d.inDays} ${tr('unit.day')} ${d.inHours % 24} ${tr('unit.hour')}';
}

/// "dd/MM/yyyy HH:mm"
String formatDateTime(DateTime dt) {
  final d = dt.toLocal();
  return '${_pad2(d.day)}/${_pad2(d.month)}/${d.year} ${_pad2(d.hour)}:${_pad2(d.minute)}';
}

/// "HH:mm"
String formatTime(DateTime dt) {
  final d = dt.toLocal();
  return '${_pad2(d.hour)}:${_pad2(d.minute)}';
}

/// Hiển thị CT: số 1 chữ số thập phân, hoặc "—" nếu null.
String formatCt(double? ct) {
  if (ct == null) return '—';
  return ct.toStringAsFixed(1);
}

/// Ước tính "đầy sau bao lâu" → câu ngắn đọc được.
///
/// Cắt ngọn ở 1 năm: box thật đang cho ra **973.752 ngày** (~2.667 năm) vì DB chỉ
/// phình ~107 KB/ngày. In con số đó ra là tỏ vẻ chính xác về một phép ước lượng
/// thô, và người đọc sẽ tin nó. Cái người vận hành cần biết chỉ là "sắp đầy" hay
/// "còn chán".
String formatDaysLeft(double days) {
  if (days >= 365) return tr('mon.overYear');
  if (days >= 60) return tr('mon.months').replaceFirst('{n}', '${(days / 30).round()}');
  return tr('mon.days').replaceFirst('{n}', '${days.round()}');
}
