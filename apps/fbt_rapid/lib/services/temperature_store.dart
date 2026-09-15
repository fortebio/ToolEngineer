import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'storage_paths.dart';
import 'temperature_serial.dart';

/// Metadata 1 bản ghi log đã lưu (mỗi lần lưu = 1 folder).
class SavedLog {
  final File file; // log.json trong folder
  final String port;
  final DateTime savedAt;
  final int count;
  final bool hasRaw; // có uart.txt
  final bool hasChart; // có chart.png
  const SavedLog(this.file, this.port, this.savedAt, this.count, this.hasRaw,
      this.hasChart);

  Directory get folder => file.parent;
}

/// 1 phiên log đã nạp đầy đủ (để vẽ lại đồ thị).
class LoadedLog {
  final String port;
  final DateTime savedAt;
  final List<TempSample> samples;
  const LoadedLog(this.port, this.savedAt, this.samples);
}

/// Lưu/đọc log nhiệt trên đĩa (Windows). **Mỗi lần lưu = 1 folder** chứa tất cả:
///
/// `<gốc>\FBT_RAPID_templog\<tên>_<dấu thời gian>\`
///   - `log.json`  (mẫu — nạp lại/vẽ lại được)
///   - `log.csv`   (Excel)
///   - `uart.txt`  (UART thô, nếu có)
///   - `chart.png` (ảnh đồ thị lúc lưu, nếu có)
class TemperatureStore {
  // Thư mục gốc đặt trong Cài đặt (mặc định Documents) + tên cố định.
  static String get baseDir => '${StoragePaths.parent}\\FBT_RAPID_templog';
  static Directory get rootDir => Directory(baseDir);

  static String _stamp(DateTime n) {
    String p2(int x) => x.toString().padLeft(2, '0');
    String p3(int x) => x.toString().padLeft(3, '0');
    // kèm mili-giây để tên folder luôn duy nhất (không ghi đè lần lưu trước)
    return '${n.year}${p2(n.month)}${p2(n.day)}_'
        '${p2(n.hour)}${p2(n.minute)}${p2(n.second)}${p3(n.millisecond)}';
  }

  // Chỉ thay ký tự Windows CẤM; giữ chữ Việt + dấu cách để đọc theo tên gợi nhớ.
  static String _safe(String s) {
    final c = s.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1F]'), '_').trim();
    return c.isEmpty ? 'COM' : c;
  }

  // ---- Lưu (gộp log + đồ thị vào 1 folder) ----

  /// Lưu cả phiên vào **1 folder**: `log.json` + `log.csv` (+ `uart.txt` &
  /// `chart.png` nếu có). Trả về folder đã tạo.
  static Future<Directory> saveBundle(
    String port,
    List<TempSample> samples, {
    List<String>? rawLines,
    Uint8List? chartPng,
    DateTime? when,
  }) async {
    final now = when ?? DateTime.now();
    final dir = Directory('$baseDir\\${_safe(port)}_${_stamp(now)}');
    dir.createSync(recursive: true);

    final obj = {
      'port': port,
      'savedAt': now.toIso8601String(),
      'channels': kTempChannels,
      'samples': samples.map((s) => s.toJson()).toList(),
    };
    await File('${dir.path}\\log.json').writeAsString(jsonEncode(obj));
    await File('${dir.path}\\log.csv').writeAsString(toCsv(samples));
    if (rawLines != null && rawLines.isNotEmpty) {
      await File('${dir.path}\\uart.txt').writeAsString(rawLines.join('\n'));
    }
    if (chartPng != null) {
      await File('${dir.path}\\chart.png').writeAsBytes(chartPng);
    }
    return dir;
  }

  static String toCsv(List<TempSample> samples) {
    final sb = StringBuffer('time_s,${kTempChannels.join(',')}\n');
    for (final s in samples) {
      sb.write(s.t.toStringAsFixed(2));
      for (final v in s.v) {
        sb.write(',');
        if (v != null) sb.write(v.toStringAsFixed(2));
      }
      sb.write('\n');
    }
    return sb.toString();
  }

  // ---- Đọc / liệt kê ----

  static List<SavedLog> listSessions() {
    final root = rootDir;
    if (!root.existsSync()) return [];
    final out = <SavedLog>[];
    for (final e in root.listSync()) {
      if (e is! Directory) continue;
      final jf = File('${e.path}\\log.json');
      if (!jf.existsSync()) continue;
      try {
        final m = jsonDecode(jf.readAsStringSync()) as Map<String, dynamic>;
        out.add(SavedLog(
          jf,
          (m['port'] ?? '').toString(),
          DateTime.tryParse((m['savedAt'] ?? '').toString()) ??
              jf.lastModifiedSync(),
          (m['samples'] as List?)?.length ?? 0,
          File('${e.path}\\uart.txt').existsSync(),
          File('${e.path}\\chart.png').existsSync(),
        ));
      } catch (_) {
        // bỏ qua folder hỏng
      }
    }
    out.sort((a, b) => b.savedAt.compareTo(a.savedAt));
    return out;
  }

  static LoadedLog loadSession(File f) {
    final m = jsonDecode(f.readAsStringSync()) as Map<String, dynamic>;
    final samples = ((m['samples'] as List?) ?? const [])
        .map((e) => TempSample.fromJson(e as List))
        .toList();
    return LoadedLog(
      (m['port'] ?? '').toString(),
      DateTime.tryParse((m['savedAt'] ?? '').toString()) ??
          DateTime.fromMillisecondsSinceEpoch(0),
      samples,
    );
  }

  /// UART thô đã lưu của 1 bản ghi (null nếu không có).
  static String? loadRawText(File logJson) {
    final f = File('${logJson.parent.path}\\uart.txt');
    return f.existsSync() ? f.readAsStringSync() : null;
  }

  /// Đường dẫn ảnh đồ thị (chart.png) trong folder của 1 bản ghi.
  static File chartFileFor(File logJson) =>
      File('${logJson.parent.path}\\chart.png');

  /// Xoá cả folder của bản ghi (log + csv + uart + chart).
  static void deleteSession(File logJson) {
    try {
      final dir = logJson.parent;
      if (dir.existsSync()) dir.deleteSync(recursive: true);
    } catch (_) {}
  }

  // ---- Gallery ảnh đồ thị ----

  /// Tất cả `chart.png` trong các folder bản ghi (mới nhất trước).
  static List<File> listCharts() {
    final root = rootDir;
    if (!root.existsSync()) return [];
    final files = <File>[];
    for (final e in root.listSync()) {
      if (e is! Directory) continue;
      final png = File('${e.path}\\chart.png');
      if (png.existsSync()) files.add(png);
    }
    files.sort((a, b) => b.lastModifiedSync().compareTo(a.lastModifiedSync()));
    return files;
  }

  static void deleteFile(File f) => _tryDelete(f);

  /// Mở thư mục bằng Windows Explorer.
  static void revealInExplorer(Directory dir) {
    try {
      if (!dir.existsSync()) dir.createSync(recursive: true);
      Process.run('explorer.exe', [dir.path]);
    } catch (_) {}
  }

  static void _tryDelete(File f) {
    try {
      if (f.existsSync()) f.deleteSync();
    } catch (_) {}
  }
}
