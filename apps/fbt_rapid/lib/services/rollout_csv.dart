import '../services/cloud_history_api.dart' show CloudDevice;

/// Xuất **bảng tiến độ triển khai OTA** ra CSV.
///
/// Hàm thuần, không đụng file/UI — để test được và để chỗ dựng nội dung không
/// trộn với chỗ lưu.

/// Một ô CSV: bọc nháy kép khi có dấu phẩy / nháy / xuống dòng, và nhân đôi nháy
/// bên trong (chuẩn RFC 4180).
///
/// Không bỏ qua được: mã máy thật có cả dấu cách (`proto 1`), còn cột trạng thái
/// là tiếng Việt có dấu phẩy — không escape thì file lệch cột mà không báo gì.
String csvCell(Object? v) {
  final s = (v ?? '').toString();
  if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r')) {
    return '"${s.replaceAll('"', '""')}"';
  }
  return s;
}

String _iso(DateTime? t) => t == null ? '' : t.toLocal().toIso8601String();

/// Trạng thái một máy so với bản mục tiêu, dạng chữ cho người đọc.
String rolloutStatusLabel(bool? onTarget) => switch (onTarget) {
      true => 'Da cap nhat',
      false => 'Chua cap nhat',
      null => 'Khong xac dinh',
    };

/// Dựng nội dung CSV cho bảng tiến độ.
///
/// [onTarget] trả `true/false/null` cho từng máy (null = không kết luận được:
/// tên file mục tiêu không mang version, hoặc máy chưa báo version lần nào).
/// Truyền hàm vào thay vì tự tính ở đây để giữ đúng MỘT nguồn chân lý cho phép
/// so version (`isDeviceOnTarget` ở màn Quản lý máy).
///
/// ⚠️ Có **BOM UTF-8** và dòng `sep=,` ở đầu — cả hai đều vì Excel:
///  * thiếu BOM → Excel đọc UTF-8 thành ký tự rác, tiếng Việt hỏng hết;
///  * Windows tiếng Việt lấy dấu phẩy làm dấu thập phân nên **list separator là
///    `;`** → mở file phẩy ra là dồn hết vào MỘT cột. Dòng `sep=,` bảo Excel
///    tách bằng phẩy bất kể locale.
/// Đổi lại, công cụ đọc CSV nghiêm ngặt phải bỏ qua dòng đầu (`skiprows=1`).
String buildRolloutCsv({
  required String target,
  required List<CloudDevice> devices,
  required bool? Function(CloudDevice) onTarget,
  DateTime? exportedAt,
}) {
  final rows = <String>[
    'sep=,',
    // Ghi bản mục tiêu + thời điểm xuất vào chính file: nó rời khỏi app đi vào
    // báo cáo, không có hai dòng này thì không biết đang so với bản nào, lúc nào.
    '# Ban muc tieu,${csvCell(target)}',
    '# Xuat luc,${csvCell(_iso(exportedAt ?? DateTime.now()))}',
    '',
    [
      'Ma may',
      'Firmware hien tai',
      'Trang thai',
      'So phien',
      'Lan gui cuoi',
    ].join(','),
  ];

  for (final d in devices) {
    rows.add([
      csvCell(d.id),
      csvCell(d.version.trim()),
      csvCell(rolloutStatusLabel(onTarget(d))),
      csvCell(d.runCount),
      csvCell(_iso(d.latest)),
    ].join(','));
  }

  // \r\n theo RFC 4180; BOM ở đầu để Excel nhận UTF-8.
  return '﻿${rows.join('\r\n')}\r\n';
}

/// Tên file gợi ý: `tiendo_<ban muc tieu>_<ngay>.csv`.
String rolloutCsvFileName(String target, DateTime at) {
  final stem = target.replaceFirst(RegExp(r'\.bin$', caseSensitive: false), '');
  final safe = stem.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F]'), '_');
  String p2(int x) => x.toString().padLeft(2, '0');
  return 'tiendo_${safe}_${at.year}-${p2(at.month)}-${p2(at.day)}.csv';
}
