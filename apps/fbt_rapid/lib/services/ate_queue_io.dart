/// **Hàng đợi hồ sơ ATE** trên máy trạm: ghi file TRƯỚC, đẩy server SAU.
///
/// Vì sao: xưởng mất mạng (hoặc server bảo trì) không được làm dừng dây chuyền.
/// Đây đúng mô hình "file trước, DB sau" mà `server/app` dùng cho dữ liệu đo —
/// và là quyết định nền số 2 của kế hoạch (§4): PC trạm ghi cục bộ rồi mới đẩy,
/// có hàng đợi gửi lại.
///
/// Bố cục thư mục (dưới thư mục gốc chọn trong Cài đặt):
/// ```
/// <gốc>\FBT_RAPID_ate\cho_gui\<sn>_<thời gian>.json   ← chưa lên server
/// <gốc>\FBT_RAPID_ate\da_gui\<sn>_<thời gian>.json    ← đã lên (giữ lại làm bản sao)
/// <gốc>\FBT_RAPID_ate\loi_gui\<sn>_<thời gian>.json   ← server TỪ CHỐI (400) / file hỏng
/// ```
/// Gửi thành công thì **chuyển thư mục**, KHÔNG xoá: hồ sơ là bằng chứng
/// nghiệm thu, và server có thể là nơi mất dữ liệu chứ không chỉ máy trạm.
///
/// `dart:io` → chỉ dùng từ màn desktop (bản web của tab Sản xuất không có mục
/// "Chạy trạm").
library;

import 'dart:convert';
import 'dart:io';

import '../models/ate_record.dart';
import 'ate_api.dart';
import 'storage_paths.dart';

class AteQueue {
  static String get baseDir => '${StoragePaths.parent}\\FBT_RAPID_ate';
  static String get pendingDir => '$baseDir\\cho_gui';
  static String get sentDir => '$baseDir\\da_gui';

  /// Hồ sơ server TỪ CHỐI vĩnh viễn (400) hoặc file JSON hỏng. Đẩy sang đây thay
  /// vì xoá (vẫn là bằng chứng) và thay vì để lại trong hàng đợi — một hồ sơ méo
  /// nằm lại thì mọi hồ sơ SAU nó không bao giờ được gửi.
  static String get rejectedDir => '$baseDir\\loi_gui';

  /// Tên file: `<sn>_<yyyyMMdd_HHmmss>.json` — xếp theo tên là xếp theo thời gian.
  static String fileNameFor(AteRecord r) {
    final t = r.startedAt.toUtc();
    String two(int v) => v.toString().padLeft(2, '0');
    final stamp = '${t.year}${two(t.month)}${two(t.day)}_'
        '${two(t.hour)}${two(t.minute)}${two(t.second)}';
    final sn = r.sn.replaceAll(RegExp(r'[^A-Za-z0-9_.-]'), '_');
    return '${sn.isEmpty ? 'unknown' : sn}_$stamp.json';
  }

  /// Ghi hồ sơ vào hàng đợi. Trả đường dẫn file.
  static Future<String> save(AteRecord r) async {
    Directory(pendingDir).createSync(recursive: true);
    final path = '$pendingDir\\${fileNameFor(r)}';
    await File(path)
        .writeAsString(const JsonEncoder.withIndent('  ').convert(r.toJson()));
    return path;
  }

  /// Số hồ sơ còn chờ gửi (API dùng chung với bản web — xem `ate_queue.dart`).
  static Future<int> pendingCount() async => pending().length;

  /// Các hồ sơ còn chờ gửi (cũ nhất trước — gửi theo đúng thứ tự chạy).
  static List<File> pending() {
    final d = Directory(pendingDir);
    if (!d.existsSync()) return const [];
    final files = d
        .listSync()
        .whereType<File>()
        .where((f) => f.path.toLowerCase().endsWith('.json'))
        .toList()
      ..sort((a, b) => a.path.compareTo(b.path));
    return files;
  }

  /// Gửi lại toàn bộ hàng đợi. Trả `(đã gửi, còn lại, bị từ chối, lỗi cuối)`.
  ///
  /// Hai loại lỗi, xử lý NGƯỢC nhau:
  /// - **Mạng / token** (`CloudApiException` thường): DỪNG ngay ở file đầu tiên —
  ///   thử tiếp 200 file nữa chỉ để nhận 200 lần cùng một lỗi.
  /// - **Server từ chối** ([AteRejectedException] = HTTP 400) hoặc file hỏng: đẩy
  ///   sang `loi_gui` rồi ĐI TIẾP. Gửi lại kiểu gì cũng hỏng y vậy, mà để nó nằm
  ///   trong hàng đợi thì mọi hồ sơ phía sau chết theo.
  static Future<({int sent, int left, int rejected, String? error})> flush(
      AteApi api) async {
    var sent = 0;
    var rejected = 0;
    final files = pending();
    for (var i = 0; i < files.length; i++) {
      final f = files[i];
      try {
        final body = jsonDecode(await f.readAsString());
        if (body is! Map<String, dynamic>) {
          _move(f, rejectedDir);
          rejected++;
          continue;
        }
        await api.putRecordJson(body);
        _move(f, sentDir);
        sent++;
      } on AteRejectedException {
        _move(f, rejectedDir);
        rejected++;
      } catch (e) {
        return (sent: sent, left: files.length - i, rejected: rejected, error: '$e');
      }
    }
    return (sent: sent, left: 0, rejected: rejected, error: null);
  }

  static void _move(File f, String toDir) {
    try {
      Directory(toDir).createSync(recursive: true);
      final name = f.uri.pathSegments.last;
      f.renameSync('$toDir\\$name');
    } catch (_) {
      // Đổi tên hỏng (file đang mở, ổ chỉ đọc) → để nguyên trong hàng đợi; lần
      // sau gửi lại là idempotent nên không sinh hồ sơ trùng trên server.
    }
  }
}
