@Tags(['shots'])
library;

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/ate_record.dart';
import 'package:RapidPlusApp/screens/ate_run_screen.dart';
import 'package:RapidPlusApp/services/app_settings.dart';
import 'package:RapidPlusApp/services/ate_api.dart';
import 'package:RapidPlusApp/services/ate_runner.dart';
import 'package:RapidPlusApp/services/session_store.dart';
import 'package:RapidPlusApp/services/storage_paths.dart';
import 'package:RapidPlusApp/models/user_session.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

/// Chụp màn **Chạy trạm** (desktop-only) ra PNG để đưa vào tài liệu vận hành —
/// máy build tài liệu không có Visual Studio nên không dựng được bản Windows,
/// mà đây lại là màn công nhân nhìn cả ca.
///
/// Chạy:  flutter test test/ate_run_shots_test.dart --update-goldens
/// Ảnh ra: docs/images/ate/*.png
///
/// KHÔNG chạy trong `flutter test` thường (`@Tags(['shots'])` + `--exclude-tags`
/// trong dart_test.yaml): golden so từng pixel, mà pixel phụ thuộc font hệ thống
/// của máy chạy — để nó trong bộ test chung là tự tạo một test đỏ ngẫu nhiên.

/// Font: `assets/fonts/DMSans.ttf` trong repo THIẾU vài glyph tiếng Việt tổ hợp
/// (vd `ạ` U+1EA1). Trên máy thật engine tự lùi về font hệ thống nên không ai
/// thấy; trong `flutter test` thì không có font nào để lùi → chữ ra ô vuông.
/// Vì vậy ảnh tài liệu dựng bằng font hệ thống: bố cục/màu y hệt bản chạy thật,
/// chỉ khác dáng chữ.
Future<void> _loadFonts() async {
  for (final f in const [
    ('DM Sans', r'C:\Windows\Fonts\segoeui.ttf'),
    ('JetBrains Mono', r'C:\Windows\Fonts\consola.ttf'),
  ]) {
    final file = File(f.$2);
    if (!file.existsSync()) continue;
    await (FontLoader(f.$1)
          ..addFont(Future.value(file.readAsBytesSync().buffer.asByteData())))
        .load();
  }
}

/// Máy giả: trả lời như một bo tốt. `hangAt` = lệnh esptool sẽ TREO (để chụp
/// đúng khoảnh khắc đang chạy), `failBoot` = log boot có lỗi (để chụp màn HỎNG).
class _ShotStation implements AteStation {
  final String? hangAt;
  final bool failBoot;
  _ShotStation({this.hangAt, this.failBoot = false});

  @override
  void cancel() {}

  @override
  Future<AteChipInfo> chipInfo({void Function(String)? onLog}) async {
    const out = 'esptool.py v4.7.0\nDetecting chip type... ESP32\n'
        'Chip is ESP32-D0WD-V3 (revision v3.0)\nMAC: 24:6f:28:aa:bb:cc\n'
        'Detected flash size: 8MB\n';
    onLog?.call(out);
    return const AteChipInfo(
      ok: true,
      chip: 'ESP32-D0WD-V3',
      mac: '24:6f:28:aa:bb:cc',
      flashSize: '8MB',
      raw: out,
    );
  }

  @override
  Future<AteFlashResult> flash(AteFlashRequest req,
      {void Function(String)? onLog}) async {
    onLog?.call('Writing at 0x00010000... (37 %)\n');
    if (hangAt == 'write_flash') {
      return Completer<AteFlashResult>().future; // treo: giữ "đang chạy"
    }
    onLog?.call('Hash of data verified.\nLeaving...\n');
    return AteFlashResult(
        ok: true, output: 'Hash of data verified.', verified: req.verify);
  }

  @override
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  }) async {
    String out;
    if (send == null || send == kAteCmdReset) {
      out = failBoot
          ? 'ets Jul 29 2019 12:21:46\nrst:0xf (RTCWDT_BROWN_OUT_RESET)\n'
              'Brownout detector was triggered\n'
          : 'ets Jul 29 2019 12:21:46\nrst:0x1 (POWERON_RESET)\n'
              'Forte Rapid+ device RPL02017 firmware version 2.4.4\n';
    } else if (send == kAteCmdParaRead) {
      out = 'device ID: RPL02017\npara version: 1\nPCB version: R3\n';
    } else if (RegExp(r'^\d$').hasMatch(send)) {
      out = '{Green: ${1000 + int.parse(send) * 3}}';
    } else if (send == kAteCmdTempOutput) {
      String row(String tag, List<double> v) =>
          '$tag\t1.0\t0\t0\t${v[0]}\t${v[1]}\t${v[2]}';
      out = '${row('TimeRT', [28.7, 28.3, 28.5])}\n'
          '${row('TimeRB', [28.4, 28.6, 28.5])}';
    } else {
      out = 'OK';
    }
    onLog?.call(out);
    return out;
  }

  @override
  Future<List<int>> readFile(String path) async => List<int>.filled(1024, 7);

  @override
  Future<String?> dutGet(String ip, String path,
          {Duration timeout = const Duration(seconds: 5)}) async =>
      null;
}

/// API giả: bộ ngưỡng của lô + nhận hồ sơ.
class _ShotApi extends AteApi {
  _ShotApi() : super('http://shots.local');

  @override
  Future<AteLimits> limits({String batch = ''}) async => AteLimits.fromJson({
        'version': 'L2609A-1',
        'batch': batch,
        'source': batch.isEmpty ? 'chung' : 'batch',
        'boot_watch_sec': 2,
        'ack_timeout_sec': 1,
        'temp_window_sec': 1,
        'fan_wait_sec': 0,
        'bright_min': 800,
        'bright_spread_pct': 10,
        'ambient_c': 28,
        'temp_tol_c': 3,
        'temp_spread_c': 2,
      });

  @override
  Future<String> putRecord(AteRecord record) async =>
      'RPL02017_20260908_081500_9f31c0d5e100.json';
}

Future<void> _prefs() async {
  SharedPreferences.setMockInitialValues({
    'ate_station': 'TRAM-01',
    'ate_batch': 'L2609A',
    'ate_operator': 'Nguyen Thi Hoa',
    'ate_port': 'COM7',
    'ate_bin_bootloader': r'D:\fw\v2.4.4\bootloader.bin',
    'ate_bin_partition': r'D:\fw\v2.4.4\partitions.bin',
    'ate_bin_app': r'D:\fw\v2.4.4\fbt_v2.4.4.bin',
    'ate_pcb_version': 'R3',
    'ate_expect_fw': '2.4.4',
  });
}

Widget _app(Widget child) => MaterialApp(
      theme: appTheme(),
      home: Scaffold(
        backgroundColor: appTheme().colorScheme.surfaceContainerLowest,
        body: Padding(padding: const EdgeInsets.all(16), child: child),
      ),
      debugShowCheckedModeBanner: false,
    );

Future<void> _shot(WidgetTester t, String name) =>
    expectLater(find.byType(MaterialApp), matchesGoldenFile('../docs/images/ate/$name.png'));

void main() {
  setUpAll(() async {
    await _loadFonts();
    StoragePaths.setParent(Directory.systemTemp.createTempSync('ate_shots').path);
    SessionStore.current = const UserSession(
      username: 'hoa',
      name: 'Nguyen Thi Hoa',
      role: UserRole.operator,
      ids: [],
      allowAll: false,
    );
  });

  Future<void> open(WidgetTester t, {AteStation? station}) async {
    await _prefs();
    t.view.physicalSize = const Size(1280, 940);
    t.view.devicePixelRatio = 1.0;
    addTearDown(t.view.reset);
    await t.pumpWidget(_app(AteRunScreen(
      settings: AppSettings(),
      api: _ShotApi(),
      portLister: () => const ['COM7', 'COM8'],
      stationFactory: (port, baud) => station ?? _ShotStation(),
    )));
    await t.pump(); // AtePrefs.load + limits
    await t.pump(const Duration(milliseconds: 50));
  }

  Future<void> scanAndStart(WidgetTester t, String sn) async {
    await t.enterText(find.byType(TextField).first, sn);
    await t.pump();
    await t.tap(find.text('BẮT ĐẦU'));
  }

  testWidgets('01 — sẵn sàng quét máy', (t) async {
    await open(t);
    await _shot(t, '01-san-sang');
  });

  testWidgets('02 — đang chạy (kẹt ở bước nạp)', (t) async {
    await open(t, station: _ShotStation(hangAt: 'write_flash'));
    await scanAndStart(t, 'RPL02017');
    await t.pump(const Duration(milliseconds: 300));
    await _shot(t, '02-dang-chay');
  });

  /// Ba bước bán tự động (quạt · còi · màn hình) hỏi người vận hành — bấm ĐẠT
  /// cho từng cái, đúng như công nhân làm ở trạm.
  Future<void> answerAll(WidgetTester t) async {
    for (var i = 0; i < 60; i++) {
      await t.pump(const Duration(milliseconds: 100));
      final yes = find.text('ĐẠT');
      if (yes.evaluate().isNotEmpty) {
        await t.tap(yes.last);
        await t.pump();
      }
    }
  }

  testWidgets('03 — máy ĐẠT', (t) async {
    await open(t);
    await scanAndStart(t, 'RPL02017');
    await answerAll(t);
    await _shot(t, '03-dat');
  });

  testWidgets('04 — máy HỎNG ở BOOT-01', (t) async {
    await open(t, station: _ShotStation(failBoot: true));
    await scanAndStart(t, 'RPL02018');
    await answerAll(t);
    await _shot(t, '04-hong');
  });

  /// Nhật ký bung ra — ảnh này để soi đúng lỗi đã sửa 2026-09-09: khung log phải
  /// chiếm HẾT bề ngang thẻ (trước đó co lại bằng dòng dài nhất rồi nằm giữa).
  testWidgets('05 — nhật ký trạm bung ra', (t) async {
    await open(t);
    await scanAndStart(t, 'RPL02017');
    await answerAll(t);
    // ListView dựng LAZY: thẻ nhật ký nằm cuối, chưa cuộn tới thì chưa có
    // trong cây widget (ensureVisible/tap đều ném "No element").
    final tile = find.text('Nhật ký trạm');
    for (var i = 0; i < 8 && tile.evaluate().isEmpty; i++) {
      await t.drag(find.byType(ListView), const Offset(0, -400));
      await t.pumpAndSettle();
    }
    await t.tap(tile);
    await t.pumpAndSettle();
    await t.drag(find.byType(ListView), const Offset(0, -400));
    await t.pumpAndSettle();
    await _shot(t, '05-nhat-ky');
  });
}
