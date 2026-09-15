import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/models/ate_record.dart';
import 'package:RapidPlusApp/services/ate_runner.dart';

/// Test kịch bản trạm ATE bằng **máy giả**: không cắm bo, không esptool.
///
/// Đây là lý do `ate_runner.dart` giữ thuần Dart — phần chấm PASS/FAIL sai thì
/// hỏng cả lô máy, mà cắm bo thật để thử từng nhánh hỏng thì không ai làm nổi
/// (làm sao ép một bo "boot ra Guru Meditation" hay "mất cảm biến nhiệt slot 3"
/// theo yêu cầu?).

const _bootOk = '''
ets Jul 29 2019 12:21:46
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
Forte Rapid+ device RPL02013 firmware version 2.4.4
''';

const _bootOkOnline = '''
ets Jul 29 2019 12:21:46
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
Forte Rapid+ device RPL02013 firmware version 2.4.4
WiFi connected, IP 192.168.1.23
''';

const _bootCrash = '''
ets Jul 29 2019 12:21:46
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
''';

/// Hai mẫu nhiệt (máy còn nguội, 6 kênh xấp xỉ nhau) — định dạng tab y hệt
/// firmware: `TimeRT/TimeRB <t> .. .. a b c`.
String tempLog({List<double> bottom = const [28.4, 28.6, 28.5], List<double> top = const [28.7, 28.3, 28.5]}) {
  String row(String tag, double t, List<double> v) =>
      '$tag\t$t\t0\t0\t${v[0]}\t${v[1]}\t${v[2]}';
  return [
    row('TimeRT', 1.0, top),
    row('TimeRB', 1.0, bottom),
    row('TimeRT', 2.0, top),
    row('TimeRB', 2.0, bottom),
  ].join('\n');
}

/// Máy giả: trả lời esptool theo lệnh con và trả lời UART **theo nội dung lệnh**
/// (không theo hàng đợi cứng) — nhờ vậy thêm bước mới vào kịch bản không làm vỡ
/// mọi test cũ.
class FakeStation implements AteStation {
  /// Các lượt nạp đã yêu cầu — test soi ở đây thay vì soi chuỗi tham số dòng
  /// lệnh: interface giờ nói Ý ĐỊNH ("nạp mấy phần này, có verify không"), còn
  /// cú pháp esptool là chuyện riêng của bản desktop.
  final List<AteFlashRequest> flashCalls = [];
  final List<String?> sentCommands = [];

  /// Log lượt nghe UART gần nhất THẬT SỰ trả về (đã cắt theo `until`).
  String lastCaptured = '';

  /// Bo mạch có trả lời không (cáp/nguồn/cổng).
  bool chipOk;

  /// Đọc được MAC không (bo trả lời nửa vời).
  bool chipMac;
  String chipFlashSize;

  /// Khâu nạp bị hỏng: '' = ổn, hoặc 'erase' | 'write' | 'verify'.
  String failStage;

  /// Nền tảng có đối chiếu lại nội dung flash không (web: không).
  bool canVerify;

  final List<int> fileBytes;
  bool readFileThrows = false;

  String bootLog;
  String paraRead;
  List<double?> greens; // 10 số testShot; null = máy không trả lời slot đó
  String temps;
  String optoReconfig;
  Map<String, String?> dutBodies; // path → body ('null' = không gọi được)
  final List<String> dutCalls = [];

  int _listenCount = 0;

  FakeStation({
    this.chipOk = true,
    this.chipMac = true,
    this.chipFlashSize = '8MB',
    this.failStage = '',
    this.canVerify = true,
    List<int>? fileBytes,
    this.bootLog = _bootOk,
    this.paraRead = 'device ID: RPL02013\npara version: 1',
    List<double?>? greens,
    String? temps,
    this.optoReconfig = 'Opto sensors configured: 10 channels ready',
    Map<String, String?>? dutBodies,
  })  : fileBytes = fileBytes ?? utf8.encode('firmware bytes'),
        greens = greens ?? List<double?>.filled(10, 1000),
        temps = temps ?? tempLog(),
        dutBodies = dutBodies ?? const {};

  @override
  void cancel() {}

  @override
  Future<AteChipInfo> chipInfo({void Function(String)? onLog}) async {
    onLog?.call('Chip is ESP32-D0WD-V3\n');
    if (!chipOk) return const AteChipInfo(ok: false, raw: 'không kết nối được');
    return AteChipInfo(
      ok: true,
      chip: 'ESP32-D0WD-V3',
      mac: chipMac ? '24:6f:28:aa:bb:cc' : '',
      flashSize: chipFlashSize,
      raw: 'Chip is ESP32-D0WD-V3\nMAC: 24:6f:28:aa:bb:cc',
    );
  }

  @override
  Future<AteFlashResult> flash(AteFlashRequest req,
      {void Function(String)? onLog}) async {
    flashCalls.add(req);
    onLog?.call('Writing...\n');
    if (failStage.isNotEmpty) {
      return AteFlashResult(
          ok: false, output: 'lỗi ở $failStage', stage: failStage);
    }
    return AteFlashResult(
        ok: true,
        output: 'Hash of data verified.',
        verified: req.verify && canVerify);
  }

  @override
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  }) async {
    sentCommands.add(send);
    String out;
    if (send == null) {
      out = _listenCount++ == 0 ? bootLog : ''; // lượt nghe đầu = log boot
    } else if (send == kAteCmdReset) {
      out = bootLog;
    } else if (send == kAteCmdParaRead) {
      out = paraRead;
    } else if (send.endsWith('@')) {
      out = 'OK';
    } else if (RegExp(r'^\d$').hasMatch(send)) {
      final v = greens[int.parse(send)];
      out = v == null ? '' : '{Green: ${v.toStringAsFixed(0)}}';
    } else if (send == kAteCmdTempOutput) {
      out = temps;
    } else if (send == kAteCmdOptoReconfig) {
      out = optoReconfig;
    } else {
      out = 'ok';
    }
    // Trả theo TỪNG MẨU như UART thật và tôn trọng `until` — không phải cả
    // khối một lần. Bắt được lỗi "dừng nghe quá sớm": trên bo thật bộ nghe boot
    // từng cắt ngay ở chữ `ets` đầu banner, chưa tới dòng version (2026-09-09).
    final buf = StringBuffer();
    for (var i = 0; i < out.length; i += 8) {
      final piece = out.substring(i, math.min(i + 8, out.length));
      buf.write(piece);
      onLog?.call(piece);
      if (until != null && until(buf.toString())) break;
    }
    lastCaptured = buf.toString();
    return lastCaptured;
  }

  @override
  Future<List<int>> readFile(String path) async {
    if (readFileThrows) throw StateError('không đọc được $path');
    return fileBytes;
  }

  @override
  Future<String?> dutGet(String ip, String path,
      {Duration timeout = const Duration(seconds: 5)}) async {
    dutCalls.add('$ip$path');
    return dutBodies[path];
  }
}

/// Ngưỡng test: KHÔNG khai ngưỡng quang/nhiệt → các bước đo chỉ ghi số (`info`).
const _limits = AteLimits(
  version: 'test-1',
  bootWatchSec: 2,
  ackTimeoutSec: 1,
  tempWindowSec: 1,
  fanWaitSec: 0,
);

AteJob _job({
  String sn = 'RPL02013',
  Map<String, dynamic> extra = const {},
  String expectFw = '',
  String chip = 'auto',
}) =>
    AteJob(
      sn: sn,
      station: 'TRAM-01',
      operator: 'cskh',
      chip: chip,
      expectFwVersion: expectFw,
      extraParams: extra,
      parts: const [
        AteBinPart('Bootloader', '0x1000', 'boot.bin'),
        AteBinPart('Partition', '0x8000', 'part.bin'),
        AteBinPart('App', '0x10000', 'app.bin'),
      ],
      appBinPath: 'app.bin',
    );

AteRunner _runner(
  AteStation st, {
  AteJob? job,
  AteLimits? limits,
  Future<bool> Function(String)? confirm,
}) =>
    AteRunner(
      station: st,
      job: job ?? _job(),
      limits: limits ?? _limits,
      confirm: confirm,
      now: () => DateTime.utc(2026, 9, 7, 1),
    );

AteStepResult _stepOf(AteRecord r, String code) =>
    r.steps.firstWhere((s) => s.code == code);

void main() {
  group('chạy trọn kịch bản', () {
    test('máy tốt → PASS, đủ 11 bước, có sha256 + MAC', () async {
      final st = FakeStation();
      final rec = await _runner(st, confirm: (_) async => true).runAll();

      expect(rec.verdict, AteVerdict.pass);
      expect(rec.failCode, '');
      expect(rec.steps.map((s) => s.code).toList(), AteRunner.stepCodes);
      expect(rec.mac, '24:6F:28:AA:BB:CC');
      expect(rec.fwVersion, '2.4.4');
      expect(rec.fwSha256.length, 64);
      expect(rec.limitsVer, 'test-1');
      // ID-02 không khai tham số → SKIP; quang/nhiệt chưa có ngưỡng → INFO.
      expect(_stepOf(rec, 'ID-02').verdict, AteVerdict.skip);
      expect(_stepOf(rec, 'OPT-03').verdict, AteVerdict.info);
      expect(_stepOf(rec, 'TMP-01').verdict, AteVerdict.info);
      // Ba bước bán tự động có người xác nhận → PASS.
      for (final c in ['FAN-01', 'BUZ-01', 'HMI-01']) {
        expect(_stepOf(rec, c).verdict, AteVerdict.pass, reason: c);
      }
    });

    test('yêu cầu nạp mang đúng ý định: 3 phần, verify, không xoá', () async {
      final st = FakeStation();
      await _runner(st).runAll();
      expect(st.flashCalls.length, 1);
      final req = st.flashCalls.single;
      expect(req.parts.map((p) => p.path).toList(),
          ['boot.bin', 'part.bin', 'app.bin']);
      expect(req.appOffset, '0x10000');
      expect(req.appBin, 'app.bin');
      expect(req.verify, isTrue); // bộ ngưỡng mặc định bắt verify
      expect(req.erase, isFalse); // không bật thì không xoá flash
      expect(req.chip, 'auto');
    });

    test('chip cụ thể đi thẳng vào yêu cầu nạp', () async {
      final st = FakeStation();
      await _runner(st, job: _job(chip: 'esp32')).runAll();
      expect(st.flashCalls.single.chip, 'esp32');
    });

    test('nền tảng không verify được → FW-01 là INFO, không phải PASS', () async {
      // Đúng tình huống bản web (esptool-js chưa có verify_flash): máy vẫn nạp
      // được, nhưng hồ sơ KHÔNG được ghi "verify khớp".
      final st = FakeStation(canVerify: false);
      final rec = await _runner(st, confirm: (_) async => true).runAll();
      final fw = _stepOf(rec, 'FW-01');
      expect(fw.verdict, AteVerdict.info);
      expect(fw.detail, contains('CHƯA đối chiếu'));
      expect(rec.verdict, AteVerdict.pass); // info không làm hỏng hồ sơ
    });
  });

  group('dừng đúng bước hỏng', () {
    test('FW-02 hỏng → không nạp gì cả', () async {
      final st = FakeStation(chipOk: false);
      final rec = await _runner(st).runAll();
      expect(rec.verdict, AteVerdict.fail);
      expect(rec.failCode, 'FW-02');
      expect(rec.steps.length, 1); // dừng ngay
      expect(st.flashCalls, isEmpty); // KHÔNG nạp
    });

    test('không đọc được MAC → FAIL (không im lặng bỏ qua)', () async {
      final st = FakeStation(chipMac: false);
      expect((await _runner(st).runAll()).failCode, 'FW-02');
      expect(st.flashCalls, isEmpty);
    });

    test('verify lệch → FW-01 FAIL, nói rõ là verify', () async {
      final st = FakeStation(failStage: 'verify');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'FW-01');
      expect(rec.steps.last.detail, contains('verify'));
    });

    test('xoá flash hỏng → FW-01 FAIL, phân biệt được khâu xoá', () async {
      final st = FakeStation(failStage: 'erase');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'FW-01');
      expect(rec.steps.last.detail, contains('Xoá flash'));
    });

    test('không đọc được file .bin → FW-01 FAIL trước khi nạp', () async {
      final st = FakeStation()..readFileThrows = true;
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'FW-01');
      expect(st.flashCalls, isEmpty); // chưa kịp nạp
    });

    test('log boot có Guru Meditation → BOOT-01 FAIL', () async {
      final st = FakeStation(bootLog: _bootCrash);
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'BOOT-01');
      expect(rec.steps.last.raw, contains('Guru Meditation'));
    });

    test('máy im lặng → thử lệnh Res rồi mới FAIL', () async {
      final st = FakeStation(bootLog: '');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'BOOT-01');
      expect(st.sentCommands, [null, kAteCmdReset]); // lượt 1 nghe, lượt 2 Res
    });

    test('version boot khác bản vừa nạp → BOOT-01 FAIL', () async {
      final rec = await _runner(FakeStation(), job: _job(expectFw: 'v2.4.5')).runAll();
      expect(rec.failCode, 'BOOT-01');
      expect(rec.steps.last.detail, contains('2.4.4'));
    });

    test('UART về từng mẩu → vẫn nghe tới dòng version, không cắt ở "ets"',
        () async {
      // Lỗi thật 2026-09-09: 14 hồ sơ đầu của trạm web FAIL "Không thấy version"
      // vì bộ nghe ngắt ngay khi thấy banner ROM, log thô chỉ còn
      // `ets Jul 29 2019 12:21:46 / rst:`. Máy giả giờ trả theo mẩu 8 ký tự.
      final st = FakeStation();
      final rec = await _runner(st, job: _job(expectFw: 'v2.4.4')).runAll();
      final b = rec.steps.firstWhere((s) => s.code == 'BOOT-01');
      expect(b.verdict, AteVerdict.pass);
      expect(b.raw, contains('firmware version 2.4.4'));
      expect(b.detail, contains('v2.4.4'));
    });

    test('log boot lộ lỗi thì ngắt nghe sớm — không chờ hết cửa sổ', () async {
      final st = FakeStation(bootLog: _bootCrash);
      await _runner(st).runAll();
      // Máy giả trả theo mẩu và dừng đúng lúc `until` đúng: log thô ngắn hơn
      // toàn bộ fixture là bằng chứng bộ nghe đã ngắt sớm khi thấy lỗi.
      expect(st.lastCaptured.length, lessThan(_bootCrash.length));
      expect(st.lastCaptured, contains('Guru Meditation'));
    });
  });

  group('ID-01 ghi số máy', () {
    test('số máy quá dài → FAIL mà KHÔNG gửi lệnh ghi nào', () async {
      final st = FakeStation();
      final rec = await _runner(st, job: _job(sn: 'RPL020131234')).runAll();
      expect(rec.failCode, 'ID-01');
      final step = rec.steps.last;
      expect(step.value, 12);
      expect(step.max, 9);
      expect(st.sentCommands.where((c) => (c ?? '').contains('@')), isEmpty);
    });

    test('đọc lại không thấy số máy → FAIL (không tin ACK)', () async {
      final st = FakeStation(paraRead: 'device ID: UNSET\npara version: 1');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'ID-01');
      expect(rec.steps.last.detail, contains('không ăn'));
    });

    test('JSON gửi đi LUÔN kèm para version', () async {
      final st = FakeStation();
      await _runner(st).runAll();
      final cfg = st.sentCommands.firstWhere((c) => (c ?? '').contains('@'));
      final body = jsonDecode(cfg!.substring(0, cfg.length - 1)) as Map;
      expect(body[kAteKeyParaVersion], 1); // thiếu khoá này firmware bỏ qua im lặng
      expect(body[kAteKeyDeviceId], 'RPL02013');
    });
  });

  group('ID-02 tham số lô', () {
    test('đọc lại đủ giá trị → PASS', () async {
      final st = FakeStation(paraRead: 'device ID: RPL02013\nlysis temp: 95\nhotlid: 105');
      final rec = await _runner(st,
              job: _job(extra: {'lysis temp': 95, 'hotlid': 105}))
          .runAll();
      expect(_stepOf(rec, 'ID-02').verdict, AteVerdict.pass);
    });

    test('thiếu một giá trị khi đọc lại → FAIL và nói thiếu khoá nào', () async {
      final st = FakeStation(paraRead: 'device ID: RPL02013\nlysis temp: 95');
      final rec = await _runner(st,
              job: _job(extra: {'lysis temp': 95, 'hotlid': 105}))
          .runAll();
      expect(rec.failCode, 'ID-02');
      expect(rec.steps.last.detail, contains('hotlid'));
    });
  });

  // ------------------------------------------------------------------ P1

  group('OPT-01 — 10 cảm biến quang', () {
    test('máy có mạng, /errors sạch → PASS', () async {
      final st = FakeStation(
        bootLog: _bootOkOnline,
        dutBodies: {'/errors': jsonEncode({'errors': []})},
      );
      final rec = await _runner(st).runAll();
      expect(_stepOf(rec, 'OPT-01').verdict, AteVerdict.pass);
      expect(st.dutCalls, contains('192.168.1.23/errors'));
    });

    test('/errors có mã lỗi → FAIL, nói rõ slot nào', () async {
      final st = FakeStation(
        bootLog: _bootOkOnline,
        dutBodies: {
          '/errors': jsonEncode({
            'errors': [
              {'slot': 3, 'code': '1203', 'text': 'Light sensor'},
              {'slot': 7, 'code': '0000'}, // slot sạch → bỏ qua
            ]
          })
        },
      );
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'OPT-01');
      final s = rec.steps.last;
      expect(s.value, 1);
      expect(s.detail, contains('slot 3'));
      expect(s.detail, contains('1203'));
    });

    test('chưa có mạng → lùi về lệnh R, không thấy lỗi thì chỉ INFO', () async {
      final st = FakeStation(); // bootLog không có IP
      final rec = await _runner(st).runAll();
      final s = _stepOf(rec, 'OPT-01');
      expect(s.verdict, AteVerdict.info); // KHÔNG được tự nhận là PASS
      expect(s.detail, contains('chưa vào mạng'));
      expect(st.dutCalls, isEmpty);
    });

    test('log lệnh R có dấu hiệu lỗi → FAIL', () async {
      final st = FakeStation(optoReconfig: 'Opto ch3: I2C error, not found');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'OPT-01');
    });

    test('máy không trả lời lệnh R → FAIL', () async {
      final st = FakeStation(optoReconfig: '');
      expect((await _runner(st).runAll()).failCode, 'OPT-01');
    });
  });

  group('OPT-03 — tín hiệu sáng từng slot', () {
    test('gửi ĐÚNG từng slot một và đọc đủ 10 số', () async {
      final st = FakeStation();
      final rec = await _runner(st).runAll();
      final shots = st.sentCommands.where((c) => RegExp(r'^\d$').hasMatch(c ?? ''));
      expect(shots.toList(), ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']);
      expect(_stepOf(rec, 'OPT-03').verdict, AteVerdict.info);
    });

    test('chưa có ngưỡng → chỉ GHI SỐ (info), kèm min/max/lệch', () async {
      final st = FakeStation(
          greens: [1000, 1010, 990, 1005, 1000, 995, 1000, 1002, 998, 1001]);
      final rec = await _runner(st).runAll();
      final s = _stepOf(rec, 'OPT-03');
      expect(s.verdict, AteVerdict.info);
      expect(s.detail, contains('min 990'));
      expect(s.detail, contains('max 1010'));
      expect(s.detail, contains('CHƯA có ngưỡng'));
      expect(s.value, closeTo(2.0, 0.1)); // (1010-990)/1010 ≈ 2%
    });

    test('một slot không trả lời → FAIL, chỉ đúng slot đó', () async {
      final st = FakeStation(greens: [1000, 1000, null, 1000, 1000, 1000, 1000, 1000, 1000, 1000]);
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'OPT-03');
      expect(rec.steps.last.detail, contains('2'));
      expect(rec.steps.last.value, 9);
    });

    test('có ngưỡng: slot yếu → FAIL', () async {
      final st = FakeStation(
          greens: [1000, 1000, 400, 1000, 1000, 1000, 1000, 1000, 1000, 1000]);
      final rec = await _runner(st,
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  brightMin: 800))
          .runAll();
      expect(rec.failCode, 'OPT-03');
      expect(rec.steps.last.detail, contains('400'));
    });

    test('có ngưỡng: lệch giữa 10 kênh quá lớn → FAIL', () async {
      final st = FakeStation(
          greens: [1000, 1000, 700, 1000, 1000, 1000, 1000, 1000, 1000, 1000]);
      final rec = await _runner(st,
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  brightSpreadPct: 10))
          .runAll();
      expect(rec.failCode, 'OPT-03');
      expect(rec.steps.last.detail, contains('lệch giữa 10 kênh'));
    });

    test('có ngưỡng và đạt → PASS', () async {
      final rec = await _runner(FakeStation(),
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  brightMin: 800,
                  brightSpreadPct: 10))
          .runAll();
      expect(_stepOf(rec, 'OPT-03').verdict, AteVerdict.pass);
    });
  });

  group('TMP-01 — 6 kênh nhiệt', () {
    test('đủ 6 kênh, chưa có ngưỡng → INFO kèm bảng số', () async {
      final rec = await _runner(FakeStation()).runAll();
      final s = _stepOf(rec, 'TMP-01');
      expect(s.verdict, AteVerdict.info);
      expect(s.detail, contains('Lysis'));
      expect(s.detail, contains('Ambient'));
    });

    test('cảm biến mất kết nối (-127) → FAIL, gọi đúng tên kênh', () async {
      final st = FakeStation(temps: tempLog(bottom: [28.4, -127, 28.5]));
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'TMP-01');
      expect(rec.steps.last.detail, contains('Amp1'));
      expect(rec.steps.last.detail, contains('-127'));
    });

    test('máy không xuất dòng nhiệt nào → FAIL', () async {
      final st = FakeStation(temps: 'Booting...\nno temperature here');
      final rec = await _runner(st).runAll();
      expect(rec.failCode, 'TMP-01');
      expect(rec.steps.last.detail, contains('không xuất dòng nhiệt'));
    });

    test('lệch giữa các kênh quá ngưỡng → FAIL', () async {
      final st = FakeStation(temps: tempLog(bottom: [28.0, 28.2, 45.0]));
      final rec = await _runner(st,
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  tempSpreadC: 2))
          .runAll();
      expect(rec.failCode, 'TMP-01');
      expect(rec.steps.last.detail, contains('lệch giữa các kênh'));
    });

    test('lệch nhiệt phòng quá ngưỡng → FAIL', () async {
      final st = FakeStation(temps: tempLog(bottom: [40.0, 40.1, 40.2], top: [40.0, 40.1, 40.2]));
      final rec = await _runner(st,
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  ambientC: 28,
                  tempTolC: 3))
          .runAll();
      expect(rec.failCode, 'TMP-01');
      expect(rec.steps.last.detail, contains('nhiệt phòng'));
    });

    test('đủ ngưỡng và đạt → PASS', () async {
      final rec = await _runner(FakeStation(),
              limits: const AteLimits(
                  version: 'p1',
                  ackTimeoutSec: 1,
                  bootWatchSec: 2,
                  tempWindowSec: 1,
                  ambientC: 28,
                  tempTolC: 3,
                  tempSpreadC: 2))
          .runAll();
      expect(_stepOf(rec, 'TMP-01').verdict, AteVerdict.pass);
    });
  });

  group('bước bán tự động (quạt · còi · màn hình)', () {
    test('không có người xác nhận → SKIP, KHÔNG tự cho là đạt', () async {
      final rec = await _runner(FakeStation()).runAll(); // confirm = null
      for (final c in ['FAN-01', 'BUZ-01', 'HMI-01']) {
        expect(_stepOf(rec, c).verdict, AteVerdict.skip, reason: c);
      }
      expect(rec.verdict, AteVerdict.pass); // skip không làm hỏng hồ sơ
    });

    test('người vận hành trả lời KHÔNG ĐẠT → FAIL đúng bước đó', () async {
      final asked = <String>[];
      final rec = await _runner(FakeStation(), confirm: (p) async {
        asked.add(p);
        return !p.toLowerCase().contains('còi'); // còi hỏng
      }).runAll();
      expect(rec.verdict, AteVerdict.fail);
      expect(rec.failCode, 'BUZ-01');
      expect(asked.length, 2); // hỏi quạt, hỏi còi rồi dừng
    });

    test('có gửi lệnh quạt/còi trước khi hỏi', () async {
      final st = FakeStation();
      await _runner(st, confirm: (_) async => true).runAll();
      expect(st.sentCommands, contains(kAteCmdFanOn));
      expect(st.sentCommands, contains(kAteCmdBuzzer));
    });
  });

  group('kết luận hồ sơ', () {
    test('chạy nửa chừng rồi huỷ → ABORTED, không phải PASS', () async {
      final r = _runner(FakeStation());
      await r.runStep('FW-02');
      r.cancel();
      final rec = r.buildRecord();
      expect(rec.verdict, AteVerdict.aborted);
      expect(rec.steps.length, 1);
    });

    test('chạy lại bước hỏng và đạt → hồ sơ mới thành PASS', () async {
      final st = FakeStation(chipOk: false);
      final r = _runner(st, confirm: (_) async => true);
      final first = await r.runAll();
      expect(first.verdict, AteVerdict.fail);

      st.chipOk = true; // cắm lại cáp
      for (final code in AteRunner.stepCodes) {
        await r.runStep(code);
      }
      final second = r.buildRecord(note: 'cắm lại cáp');
      expect(second.verdict, AteVerdict.pass);
      expect(second.note, 'cắm lại cáp');
      expect(first.verdict, AteVerdict.fail); // hồ sơ cũ KHÔNG bị sửa
    });

    test('lỗi ném từ tầng phần cứng → FAIL bước đó, không sập trạm', () async {
      final rec = await _runner(_ThrowingStation()).runAll();
      expect(rec.verdict, AteVerdict.fail);
      expect(rec.failCode, 'FW-02');
      expect(rec.steps.first.detail, contains('cáp rút'));
    });
  });

  group('JSON hồ sơ', () {
    test('khớp hợp đồng server + đi vòng lại được', () async {
      final rec = await _runner(FakeStation()).runAll();
      final j = rec.toJson();
      expect(j['sn'], 'RPL02013');
      expect(j['verdict'], 'pass');
      expect(j['limits_ver'], 'test-1');
      expect(j['app'], 'FBT_RAPID');
      expect((j['steps'] as List).length, AteRunner.stepCodes.length);
      expect(j['started_at'].toString(), endsWith('Z')); // UTC ISO cho server

      final back = AteRecord.fromJson(Map<String, dynamic>.from(j));
      expect(back.sn, rec.sn);
      expect(back.steps.first.code, 'FW-02');
      expect(back.steps.length, rec.steps.length);
    });

    test('log thô quá dài bị cắt GIỮA, giữ đầu và đuôi', () {
      final s = AteStepResult(
        code: 'FW-01',
        name: 'Nạp',
        verdict: AteVerdict.fail,
        startedAt: DateTime.utc(2026, 9, 7),
        raw: '${'HEAD' * 10}${'x' * 50000}${'TAIL' * 10}',
      );
      final raw = s.toJson()['raw'] as String;
      expect(raw.length, lessThan(30000));
      expect(raw, startsWith('HEAD'));
      expect(raw, endsWith('TAIL'));
      expect(raw, contains('cắt bớt'));
    });
  });

  group('lô sản xuất', () {
    test('mã lô đi thẳng vào hồ sơ (khoá tra "chấm theo tiêu chuẩn nào")', () async {
      final rec = await _runner(FakeStation(),
              job: const AteJob(
                sn: 'RPL02013',
                batch: 'L2609A',
                parts: [AteBinPart('App', '0x10000', 'app.bin')],
                appBinPath: 'app.bin',
              ))
          .runAll();
      expect(rec.batch, 'L2609A');
      expect(rec.toJson()['batch'], 'L2609A');
      expect(AteRecord.fromJson(Map<String, dynamic>.from(rec.toJson())).batch,
          'L2609A');
    });

    test('AteLimits giữ lô + NGUỒN của bộ ngưỡng đang áp dụng', () {
      final l = AteLimits.fromJson({
        'version': 'L2609A-1',
        'batch': 'L2609A',
        'source': 'batch',
        'bright_min': 800,
        'bright_spread_pct': 8,
        'ambient_c': 28,
      });
      expect(l.batch, 'L2609A');
      expect(l.source, 'batch');
      expect(l.brightMin, 800);
      expect(l.brightSpreadPct, 8);
      expect(l.ambientC, 28);
      // Lô chưa có bộ riêng: server trả source='chung'/'mặc định' và app phải
      // giữ nguyên để màn trạm cảnh báo được.
      final f = AteLimits.fromJson(
          {'version': 'p1', 'batch': 'L9999Z', 'source': 'mặc định'});
      expect(f.source, 'mặc định');
      expect(f.brightMin, isNull); // chưa chốt → bước đo chỉ ghi số
    });
  });

  group('parser thuần', () {
    test('parseGreenMean đọc được cả hai kiểu in', () {
      expect(parseGreenMean('{Green: 1234}'), 1234);
      expect(parseGreenMean('green = 12.5'), 12.5);
      expect(parseGreenMean('Slot ready'), isNull);
    });

    test('parseTempSamples ghép đúng cặp TimeRT + TimeRB', () {
      final s = parseTempSamples(tempLog(bottom: [1, 2, 3], top: [4, 5, 6]));
      expect(s.length, 2);
      expect(s.last, [1, 2, 3, 4, 5, 6]); // thứ tự kTempChannels
      expect(parseTempSamples('rác\nkhông phải nhiệt'), isEmpty);
    });

    test('parseDutIp lấy IP trong log boot, bỏ 0.0.0.0', () {
      expect(parseDutIp(_bootOkOnline), '192.168.1.23');
      expect(parseDutIp(_bootOk), isNull);
      expect(parseDutIp('WiFi connected, IP 0.0.0.0'), isNull);
    });

    test('parseDutErrors bỏ mã rỗng/0000, chịu được body rác', () {
      expect(parseDutErrors('không phải json'), isEmpty);
      final e = parseDutErrors(jsonEncode({
        'errors': [
          {'slot': 0, 'code': '0000'},
          {'slot': 5, 'code': '2101', 'text': 'Heater'},
        ]
      }));
      expect(e.length, 1);
      expect(e.first.slot, 5);
      expect(e.first.text, 'Heater');
    });
  });
}

/// Máy giả luôn ném — mô phỏng cáp rút giữa chừng.
class _ThrowingStation implements AteStation {
  @override
  void cancel() {}

  @override
  Future<AteChipInfo> chipInfo({void Function(String)? onLog}) async =>
      throw StateError('cáp rút');

  @override
  Future<AteFlashResult> flash(AteFlashRequest req,
          {void Function(String)? onLog}) async =>
      throw StateError('cáp rút');

  @override
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  }) async =>
      throw StateError('cáp rút');

  @override
  Future<List<int>> readFile(String path) async => throw StateError('cáp rút');

  @override
  Future<String?> dutGet(String ip, String path,
          {Duration timeout = const Duration(seconds: 5)}) async =>
      null;
}
