import 'dart:convert';
import 'dart:typed_data';

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

  /// Gói TOÀN BỘ lần chạy của một máy thành một map JSON.
  ///
  /// Có `exportedAt` + `count` ở đầu vì file này rời khỏi app đi vào tay người
  /// khác: mở ra phải biết ngay xuất lúc nào và có đủ bao nhiêu bản ghi, khỏi
  /// phải đếm tay để đoán file có bị cắt giữa chừng không.
  static Map<String, dynamic> buildDeviceArchive(
    String deviceId,
    List<TestResult> runs, {
    DateTime? exportedAt,
  }) =>
      {
        'deviceId': deviceId,
        'exportedAt': (exportedAt ?? DateTime.now()).toIso8601String(),
        'count': runs.length,
        'runs': [for (final r in runs) runToJson(r)],
      };

  /// Tên file kho: `<MãMáy>_toanbo_<ngày>.json`.
  static String archiveFileName(String deviceId, DateTime at) =>
      '${_safe(deviceId.isEmpty ? "May" : deviceId)}_toanbo_${_date(at)}.json';

  /// Lưu kho toàn bộ dữ liệu của 1 máy. Desktop: vào `FBT_RAPID_ketqua\<MãMáy>\`
  /// (trả đường dẫn để caller mời "Mở"). Web: tải xuống Downloads, trả ''.
  static Future<String> saveDeviceArchive(
    String deviceId,
    List<TestResult> runs,
  ) async {
    final at = DateTime.now();
    final name = archiveFileName(deviceId, at);
    final text = jsonEncode(buildDeviceArchive(deviceId, runs, exportedAt: at));

    if (kIsWeb) {
      pf.downloadBytes(name, utf8.encode(text));
      return '';
    }
    final dir = '$baseDir\\${_safe(deviceId.isEmpty ? "May" : deviceId)}';
    pf.ensureDir(dir);
    await pf.writeFileText('$dir\\$name', text);
    return dir;
  }

  /// Lưu các ảnh đồ thị (theo CurveView) + data.json. Trả về đường dẫn folder
  /// (web: tải xuống Downloads và trả '').
  static Future<String> saveRun(
    TestResult run,
    Map<CurveView, Uint8List> pngs,
  ) async {
    // Gom theo MÃ MÁY → mỗi lần chạy là 1 thư mục con "Ngày_Giờ_Firmware".
    final device = _safe(run.deviceId.isEmpty ? 'May' : run.deviceId);
    final sub = '${_date(run.timestamp)}_${_time(run.timestamp)}'
        '_${_safe(run.version.isEmpty ? "NA" : run.version)}';

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
