import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/data/machine_info_content.dart';
import 'package:RapidPlusApp/util/log_triage.dart';

// Test bộ quét log (tab Chăm sóc KH › Xử lý sự cố). Thuần Dart — chạy
// `flutter test test/log_triage_test.dart`. Các dòng mẫu là thông điệp CHUẨN
// của ROM/ESP-IDF, cố ý giữ nguyên chính tả để luật regex bám đúng thực tế.

const _bootLog = '''
ets Jul 29 2019 12:21:46

rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
load:0x3fff0030,len:1184
entry 0x400805e4
Forte Rapid+ device RPL02013 firmware version 2.4.5
WiFi connecting to Clinic-5G ...
WiFi connected, IP 192.168.1.23
''';

const _brownoutLog = '''
Brownout detector was triggered

ets Jul 29 2019 12:21:46
rst:0xf (RTCWDT_BROWN_OUT_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
''';

const _crashLog = '''
Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
Core  1 register dump:
Backtrace: 0x400d1234:0x3ffb1f00 0x400d5678:0x3ffb1f20
ELF file SHA256: 0000000000000000
Rebooting...
rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
''';

void main() {
  group('triageLog', () {
    test('log rỗng → báo cáo rỗng', () {
      final r = triageLog('');
      expect(r.findings, isEmpty);
      expect(r.version, isNull);
      expect(r.deviceId, isNull);
    });

    test('boot bình thường: chỉ thông tin, có version + mã máy', () {
      final r = triageLog(_bootLog);
      expect(r.hasError, isFalse);
      expect(r.findings.map((f) => f.key), contains('resetPower'));
      expect(r.findings.every((f) => f.level == TriageLevel.info), isTrue);
      expect(r.version, '2.4.5');
      expect(r.deviceId, 'RPL02013');
    });

    test('brownout → lỗi nguồn, đếm đúng số dòng, có dòng dẫn chứng', () {
      final r = triageLog(_brownoutLog);
      expect(r.hasError, isTrue);
      final f = r.findings.firstWhere((f) => f.key == 'brownout');
      expect(f.level, TriageLevel.error);
      expect(f.count, 2); // "Brownout detector…" + "RTCWDT_BROWN_OUT_RESET"
      expect(f.sample, startsWith('Brownout detector'));
      expect(f.titleKey, 'triage.brownout');
      expect(f.hintKey, 'triage.brownoutHint');
    });

    test('crash → lỗi firmware xếp TRƯỚC thông tin reset mềm', () {
      final r = triageLog(_crashLog);
      final keys = r.findings.map((f) => f.key).toList();
      expect(keys.first, 'crash');
      expect(keys, contains('resetSoft'));
      expect(keys.indexOf('crash'), lessThan(keys.indexOf('resetSoft')));
    });

    test('cảm biến nhiệt -127 / nan → cảnh báo; -1270 hay 12.7 thì không', () {
      expect(triageLog('TimeRB 12.5,-127.00,45.1').findings.map((f) => f.key),
          contains('tempSensor'));
      expect(triageLog('Ambient: nan').findings.map((f) => f.key),
          contains('tempSensor'));
      // -1270 (số khác), 12.7, 0-127 (dấu trừ là phép trừ) — không phải cảm biến.
      expect(triageLog('value -1270 and 12.7 and 0-127').findings, isEmpty);
    });

    test('WiFi / HTTP lỗi → cảnh báo', () {
      final r = triageLog('WiFi disconnected, reason 201\nHTTP POST failed: -1');
      final keys = r.findings.map((f) => f.key).toSet();
      expect(keys, containsAll(['wifi', 'upload']));
      expect(r.hasError, isFalse);
    });

    test('dòng E (ms) chuẩn ESP-IDF được gom đếm', () {
      final r = triageLog('E (1234) wifi: err\nI (1240) ok\nE (1300) x: y');
      final f = r.findings.firstWhere((f) => f.key == 'idfError');
      expect(f.count, 2);
    });

    test('version nhận cả "FirmwareVer" lẫn "v2.4.4AT" trần', () {
      expect(triageLog('FirmwareVer: 2.4.4').version, '2.4.4');
      expect(triageLog('boot v2.4.4AT ok').version, '2.4.4AT');
    });
  });

  group('isSuspiciousLine', () {
    test('lỗi/cảnh báo → true; thông tin và dòng thường → false', () {
      expect(isSuspiciousLine('Brownout detector was triggered'), isTrue);
      expect(isSuspiciousLine('E (12) tag: boom'), isTrue);
      expect(isSuspiciousLine('rst:0x1 (POWERON_RESET)'), isFalse);
      expect(isSuspiciousLine('TimeRT 25.3,25.1'), isFalse);
      expect(isSuspiciousLine('   '), isFalse);
    });
  });

  group('decodeErrorCode', () {
    test('tách 4 chữ số theo module*1000 + type*100 + step*10 + slot', () {
      final d = decodeErrorCode('2318')!;
      expect(d.module, 2);
      expect(d.type, 3);
      expect(d.step, 1);
      expect(d.slot, 8);
    });

    test('không phải đúng 4 chữ số → null', () {
      expect(decodeErrorCode(''), isNull);
      expect(decodeErrorCode('123'), isNull);
      expect(decodeErrorCode('12345'), isNull);
      expect(decodeErrorCode('12a4'), isNull);
    });
  });
}
