import 'dart:convert';
import 'dart:typed_data';

import 'package:archive/archive.dart';
import 'package:flutter/foundation.dart' show kIsWeb;

import '../models/test_result.dart';
import '../util/curve_processing.dart';
import '../util/platform_files.dart' as pf;
import 'storage_paths.dart';

/// Xuất kết quả 1 lần chạy: 4 ảnh đồ thị PNG + data.json, lưu **gom theo mã máy**:
/// `Documents\FBT_RAPID_ketqua\<MãMáy>\<Ngày_Giờ_Firmware>\`.
/// Trên WEB: không có filesystem → tải từng file xuống Downloads (tên file
/// ghép `<MãMáy>_<Ngày_Giờ>_...`), [saveRun] trả '' (caller ẩn nút "Mở").
class ResultExport {
  // Thư mục gốc đặt trong Cài đặt (mặc định Documents) + tên cố định.
  static String get baseDir => '${StoragePaths.parent}\\FBT_RAPID_ketqua';

  // Giữ chữ Việt + dấu cách; chỉ bỏ ký tự Windows cấm trong tên file/folder.
  static String _safe(String s) {
    final c = s.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F]'), '_').trim();
    return c.isEmpty ? 'NA' : c;
  }

  static String _date(DateTime d) {
    final l = d.toLocal();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${l.year}-${p2(l.month)}-${p2(l.day)}';
  }

  static String _time(DateTime d) {
    final l = d.toLocal();
    String p2(int x) => x.toString().padLeft(2, '0');
    return '${p2(l.hour)}${p2(l.minute)}${p2(l.second)}';
  }

  static const Map<CurveView, String> _pngName = {
    CurveView.rawDraw: 'raw.png',
    CurveView.calibratedDraw: 'calib.png',
    CurveView.baseline: 'baseline.png',
    CurveView.baselineSmoothed: 'baseline_sg.png',
  };

  /// Một lần chạy → map JSON. Dùng CHUNG cho `data.json` của một lần chạy và cho
  /// kho tải-tất-cả, để hai đường xuất không trôi khỏi nhau theo thời gian.
  static Map<String, dynamic> runToJson(TestResult run) => {
        'deviceId': run.deviceId,
        'version': run.version,
        'time': run.timestamp.toIso8601String(),
        'slopes': [for (final s in run.slots) s.slope],
        'slots': [
          for (final s in run.slots)
            {
              'index': s.index,
              'name': s.name, // tên bệnh (firmware v2.4.3+), '' nếu máy cũ
              'result': s.classification.label,
              'ct': s.ct,
              'slope': s.slope,
              'data': s.curve, // raw draw
            },
        ],
      };

  /// Tên thư mục kho: `<MãMáy>_toanbo_<ngày>`.
  ///
  /// MỘT thư mục, trong đó MỖI lần đo là MỘT file JSON riêng — cố ý không dồn
  /// hết vào một file: file gộp thì muốn xem/gửi một lần đo phải kéo theo cả mớ,
  /// còn thư mục thì tự nó là danh sách, sắp theo tên là sắp theo thời gian.
  static String archiveDirName(String deviceId, DateTime at) =>
      '${_safe(deviceId.isEmpty ? "May" : deviceId)}_toanbo_${_date(at)}';

  /// Tên thư mục của MỘT lần đo: `<Ngày>_<Giờ>_<Firmware>`.
  /// Dùng CHUNG cho thư mục con của [saveRun] lẫn cây file trong mẻ tải hàng
  /// loạt → xuất lẻ hay xuất cả mẻ đều ra cùng một cách đặt tên.
  static String runDirName(TestResult run) =>
      '${_date(run.timestamp)}_${_time(run.timestamp)}'
      '_${_safe(run.version.isEmpty ? "NA" : run.version)}';

  /// Tên file JSON của MỘT lần đo trong mẻ: `<Ngày>_<Giờ>_<Firmware>.json`.
  static String runFileName(TestResult run) => '${runDirName(run)}.json';

  /// Cây file ẢNH ĐỒ THỊ của MỘT lần đo trong mẻ: mọi file nằm trong THƯ MỤC
  /// RIÊNG của lần đo đó (`<Ngày_Giờ_Fw>/raw.png`, …, `/data.json`).
  ///
  /// Trả về đường-dẫn-tương-đối → bytes, KHÔNG tự ghi: cùng một cây này đem ghi
  /// ra thư mục (desktop) hay nhét vào .zip (web) đều được, nên hai nền tảng
  /// không thể trôi khỏi nhau.
  static Map<String, List<int>> chartEntries(
    TestResult run,
    Map<CurveView, Uint8List> pngs,
  ) {
    final sub = runDirName(run);
    return {
      for (final e in pngs.entries)
        if (_pngName[e.key] != null) '$sub/${_pngName[e.key]}': e.value,
      '$sub/data.json': utf8.encode(jsonEncode(runToJson(run))),
    };
  }

  /// Lưu các ảnh đồ thị (theo CurveView) + data.json. Trả về đường dẫn folder
  /// (web: tải xuống Downloads và trả '').
  static Future<String> saveRun(
    TestResult run,
    Map<CurveView, Uint8List> pngs,
  ) async {
    // Gom theo MÃ MÁY → mỗi lần chạy là 1 thư mục con "Ngày_Giờ_Firmware".
    final device = _safe(run.deviceId.isEmpty ? 'May' : run.deviceId);
    final sub = runDirName(run);

    final data = runToJson(run);

    if (kIsWeb) {
      // Không có thư mục trên web → mã máy + thời gian vào TÊN từng file.
      for (final e in pngs.entries) {
        final name = _pngName[e.key];
        if (name != null) pf.downloadBytes('${device}_${sub}_$name', e.value);
      }
      pf.downloadBytes(
          '${device}_${sub}_data.json', utf8.encode(jsonEncode(data)));
      return '';
    }

    final dir = '$baseDir\\$device\\$sub';
    pf.ensureDir(dir);
    for (final e in pngs.entries) {
      final name = _pngName[e.key];
      if (name != null) await pf.writeFileBytes('$dir\\$name', e.value);
    }
    await pf.writeFileText('$dir\\data.json', jsonEncode(data));
    return dir;
  }

  static void revealInExplorer(String path) => pf.openFolder(path);
}

/// MỘT mẻ "tải hàng loạt" của một máy — gom mọi file vào MỘT nơi duy nhất.
///
///  * **Desktop**: ghi thẳng ra `FBT_RAPID_ketqua\<MãMáy>\<MãMáy>_toanbo_<ngày>\`.
///    Ghi tới đâu chắc tới đó → bấm Dừng giữa chừng vẫn giữ phần đã tải.
///  * **Web**: không có thư mục nên tích vào bộ nhớ rồi cuối mẻ nén thành **MỘT
///    file `<MãMáy>_toanbo_<ngày>.zip`** cùng cây thư mục. Trước đây mỗi file là
///    một lượt tải — chọn 50 lần đo là trình duyệt hỏi tải 50 lần.
///
/// Đường dẫn truyền vào [addText]/[addBytes] là **tương đối, ngăn bằng `/`**
/// (vd `2026-08-26_135300_v2.4.4/raw.png`); bản desktop tự đổi sang `\` và tạo
/// thư mục con.
class BulkExport {
  BulkExport(String deviceId, {DateTime? at})
      : dirName = ResultExport.archiveDirName(deviceId, at ?? DateTime.now()),
        _device = ResultExport._safe(deviceId.isEmpty ? 'May' : deviceId);

  /// Tên thư mục (desktop) / tên file .zip không đuôi (web).
  final String dirName;
  final String _device;
  final Archive _zip = Archive();
  final Set<String> _seen = <String>{};

  int get count => _seen.length;

  String get _root => '${ResultExport.baseDir}\\$_device\\$dirName';

  /// Trùng đường dẫn thì thêm `_2`, `_3`… — trùng giây + trùng firmware gần như
  /// không xảy ra (một lần đo mất hàng chục phút) nhưng nếu xảy ra mà đè lên
  /// nhau thì MẤT bản ghi, không báo gì.
  String _uniq(String path) {
    if (_seen.add(path)) return path;
    final dot = path.lastIndexOf('.');
    final stem = dot < 0 ? path : path.substring(0, dot);
    final ext = dot < 0 ? '' : path.substring(dot);
    for (var i = 2;; i++) {
      final alt = '${stem}_$i$ext';
      if (_seen.add(alt)) return alt;
    }
  }

  Future<void> addText(String path, String text) =>
      addBytes(path, utf8.encode(text));

  Future<void> addBytes(String path, List<int> bytes) async {
    final p = _uniq(path);
    if (kIsWeb) {
      _zip.add(ArchiveFile.bytes(p, bytes));
      return;
    }
    final rel = p.replaceAll('/', '\\');
    final cut = rel.lastIndexOf('\\');
    pf.ensureDir(cut < 0 ? _root : '$_root\\${rel.substring(0, cut)}');
    await pf.writeFileBytes('$_root\\$rel', bytes);
  }

  /// Kết mẻ. Desktop: trả đường dẫn thư mục (caller mời "Mở"). Web: tải file
  /// .zip rồi trả '' (không có gì để mở). Mẻ rỗng → trả '' ở cả hai.
  Future<String> finish() async {
    if (_seen.isEmpty) return '';
    if (kIsWeb) {
      pf.downloadBytes('$dirName.zip', ZipEncoder().encodeBytes(_zip));
      return '';
    }
    return _root;
  }
}
