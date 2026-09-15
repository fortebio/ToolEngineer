// Chạy bộ quét log của tab Chăm sóc KH (util/log_triage.dart) trên MỘT file log
// ngoài app — để kỹ thuật soi nhanh log khách gửi qua email, hoặc kiểm báo động
// giả khi thêm/sửa luật mà không cần mở app.
//
//   dart run tool/triage_cli.dart <file.txt>
//
// In: version + mã máy dò được, từng dấu hiệu (mức · khoá · số dòng · dòng dẫn
// chứng) và các dòng "nghi lỗi" (thứ màn Xử lý sự cố tô đỏ). Chỉ in KHOÁ i18n
// (triage.<key>), không dịch — file này thuần Dart, không kéo Flutter.
import 'dart:io';

import 'package:RapidPlusApp/util/log_triage.dart';

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('Dùng: dart run tool/triage_cli.dart <file log>');
    exit(2);
  }
  final text = File(args.first).readAsStringSync();
  final r = triageLog(text);
  print('lines     : ${r.lineCount}');
  print('deviceId  : ${r.deviceId ?? '-'}');
  print('version   : ${r.version ?? '-'}');
  print('hasError  : ${r.hasError}');
  if (r.findings.isEmpty) {
    print('findings  : (không có)');
  } else {
    print('findings  :');
    for (final f in r.findings) {
      print('  [${f.level.name.padRight(7)}] ${f.key.padRight(11)} ×${f.count}'
          '  "${f.sample}"');
    }
  }
  final bad = [
    for (final l in text.split('\n'))
      if (isSuspiciousLine(l)) l.trim()
  ];
  print('suspicious: ${bad.length} dòng');
  for (final l in bad.take(30)) {
    print('  ! ${l.length > 140 ? '${l.substring(0, 137)}…' : l}');
  }
}
