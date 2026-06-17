// Các hàm format dùng chung (tránh phụ thuộc package intl để giảm rủi ro build).

String _pad2(int n) => n.toString().padLeft(2, '0');

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
