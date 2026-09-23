import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/services/calib_reader.dart';
import 'package:RapidPlusApp/util/serial_link_types.dart';

/// Cổng giả: ghi lệnh vào [written], máy "trả lời" bằng [reply].
class _FakeLink implements SerialLink {
  final _ctrl = StreamController<Uint8List>();
  final written = <List<int>>[];
  bool _open = true;
  String Function(List<int> cmd)? reply;

  @override
  String get label => 'FAKE';
  @override
  int get baud => 115200;
  @override
  bool get isOpen => _open;
  @override
  Stream<Uint8List> get stream => _ctrl.stream;

  void emit(String s) => _ctrl.add(Uint8List.fromList(latin1.encode(s)));

  @override
  Future<void> write(List<int> bytes) async {
    written.add(bytes);
    final r = reply?.call(bytes);
    if (r != null) {
      // Trả lời SAU khi write xong, như máy thật.
      scheduleMicrotask(() => emit(r));
    }
  }

  @override
  Future<void> close() async {
    _open = false;
    await _ctrl.close();
  }
}

void main() {
  group('parseCalibRaw', () {
    test('fleet Rapid+ {Green: N}', () {
      expect(parseCalibRaw('{Green: 1521}'), 1521);
      expect(parseCalibRaw('  {green:1193}\r'), 1193);
    });
    test('Beta prototype raw,calibrated (WI)', () {
      expect(parseCalibRaw('1521,792.19'), 1521);
      expect(parseCalibRaw('905, -3.5'), 905);
    });
    test('số trần và dòng chữ', () {
      expect(parseCalibRaw(' 607 '), 607);
      expect(parseCalibRaw('data received from Serial port'), isNull);
      expect(parseCalibRaw('Enter LED slot (0-9): '), isNull);
      expect(parseCalibRaw(''), isNull);
    });
  });

  test('calibSlotCommand: 1 byte, khe 1 = "0"', () {
    expect(calibSlotCommand(1), [0x30]);
    expect(calibSlotCommand(10), [0x39]);
    expect(() => calibSlotCommand(0), throwsArgumentError);
    expect(() => calibSlotCommand(11), throwsArgumentError);
  });

  group('CalibReader', () {
    test('gửi đúng byte khe, bỏ dòng chữ, lấy số', () async {
      final link = _FakeLink()
        ..reply = (cmd) => 'data received from Serial port\r\n{Green: ${1000 + cmd[0]}}\r\n';
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      expect(await r.readSlot(3), 1000 + 0x32);
      expect(link.written, [
        [0x32]
      ]);
      expect(r.recentLines.last, '{Green: 1050}');
      await r.close();
      expect(link.isOpen, isFalse);
    });

    test('byte về từng mẩu vẫn ghép được dòng', () async {
      final link = _FakeLink();
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      final f = r.readSlot(1);
      await Future<void>.delayed(Duration.zero);
      link.emit('{Gre');
      link.emit('en: 12');
      link.emit('34}\r\n');
      expect(await f, 1234);
      await r.close();
    });

    test('lỗi cảm biến → CalibReadException', () async {
      final link = _FakeLink()..reply = (_) => 'Opto sensor error\n Please power off/on\n';
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      await expectLater(r.readSlot(1), throwsA(isA<CalibReadException>()));
      await r.close();
    });

    test('hết giờ → CalibReadException kèm dòng máy in gần nhất', () async {
      final link = _FakeLink()..reply = (_) => 'Enter LED slot (0-9): \n';
      final r = CalibReader(link, timeout: const Duration(milliseconds: 150));
      try {
        await r.readSlot(2);
        fail('phải timeout');
      } on CalibReadException catch (e) {
        expect(e.message, contains('Enter LED slot'));
      }
      expect(r.isBusy, isFalse);
      await r.close();
    });

    test('đang bận thì lần gọi thứ hai bị từ chối, không gửi thêm byte', () async {
      final link = _FakeLink();
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      final f1 = r.readSlot(1);
      await Future<void>.delayed(Duration.zero);
      await expectLater(r.readSlot(1), throwsA(isA<CalibReadException>()));
      link.emit('{Green: 5}\n');
      expect(await f1, 5);
      expect(link.written.length, 1);
      await r.close();
    });

    test('mất cổng giữa chừng → lỗi, không treo', () async {
      final link = _FakeLink();
      final r = CalibReader(link, timeout: const Duration(seconds: 5));
      final f = r.readSlot(1);
      await Future<void>.delayed(Duration.zero);
      await link.close();
      await expectLater(f, throwsA(isA<CalibReadException>()));
      expect(r.isOpen, isFalse);
    });
  });

  group('chế độ đọc ống chuẩn (firmware v2.4.6)', () {
    test('sendCommand ghi lệnh kèm newline, bỏ newline thừa trong lệnh', () async {
      final link = _FakeLink();
      final r = CalibReader(link);
      await r.startMode(3);
      await r.setLabel('300 nM – ống 3\n');
      await r.endMode();
      expect(utf8.decode(link.written[0]), 'CalibStart,3\n');
      // nhãn: chỉ ASCII, dấu/ký tự lạ bị bỏ, newline không lọt vào giữa
      expect(utf8.decode(link.written[1]), 'CalibLabel,300 nM  ng 3\n');
      expect(utf8.decode(link.written[2]), 'CalibEnd\n');
      await r.close();
    });

    test('nhãn ASCII cắt 31 ký tự', () {
      expect(calibLabelFor((conc: '300', tube: 3)), '300 nM - ong 3');
      expect(calibLabelFor(null), 'Da doc het');
      expect(calibAsciiLabel('a' * 40).length, 31);
    });

    test('{Green} máy tự in (nút ĐỎ) → readings; số trần thì không', () async {
      final link = _FakeLink();
      final r = CalibReader(link);
      final got = <double>[];
      r.readings.listen(got.add);
      link.emit('{Green: 1521}\r\n');
      link.emit('1521\n');
      link.emit('data received from Serial port\n');
      await Future<void>.delayed(Duration.zero);
      expect(got, [1521]);
      await r.close();
    });

    test('đang readSlot thì {Green} thuộc về nó, không phát readings', () async {
      final link = _FakeLink()..reply = (_) => '{Green: 900}\n';
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      final got = <double>[];
      r.readings.listen(got.add);
      expect(await r.readSlot(3), 900);
      await Future<void>.delayed(Duration.zero);
      expect(got, isEmpty);
      await r.close();
    });

    test('{CalibSlot}/{CalibMode}/{CalibError} → slotChanges / modeOn / calibErrors', () async {
      final link = _FakeLink();
      final r = CalibReader(link);
      final slots = <int>[];
      final errs = <String>[];
      r.slotChanges.listen(slots.add);
      r.calibErrors.listen(errs.add);
      link.emit('{CalibMode: on}\n{CalibSlot: 4}\n{CalibError: busy}\n{CalibSlot: 11}\n');
      await Future<void>.delayed(Duration.zero);
      expect(slots, [4]);
      expect(errs, ['busy']);
      expect(r.modeOn, isTrue);
      link.emit('{CalibMode: off}\n');
      await Future<void>.delayed(Duration.zero);
      expect(r.modeOn, isFalse);
      await r.close();
    });

    test('modeOn: readSlot gửi CalibShot và KHÔNG nhận số trần máy echo (lô 40 ống ghi "3")', () async {
      final link = _FakeLink();
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      link.emit('{CalibMode: on}\n');
      await Future<void>.delayed(Duration.zero);
      expect(r.modeOn, isTrue);
      // Máy echo buffer "CalibLabel,...\n3" (Enter rơi vào cùng 1 s readBytes) rồi mới đọc thật.
      link.reply = (cmd) => 'CalibLabel,100 nM - ong 6\n3\n{Green: 208}\n';
      final v = await r.readSlot(4);
      expect(v, 208);
      expect(latin1.decode(link.written.last), 'CalibShot\n');
    });

    test('chưa modeOn (firmware cũ/Beta): readSlot vẫn 1 byte và nhận số trần', () async {
      final link = _FakeLink()..reply = (_) => '3\n';
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      expect(await r.readSlot(4), 3);
      expect(link.written.last, [0x33]);
      await r.close();
    });

    test('nút TRẮNG trên máy ({CalibMode: off}): không gửi nhãn, readSlot tự vào lại một gói', () async {
      final link = _FakeLink();
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      final modes = <bool>[];
      r.modeChanges.listen(modes.add);
      link.emit('{CalibMode: on}\n');
      await Future<void>.delayed(Duration.zero);
      await r.setLabel('300 nM - ong 1');
      expect(latin1.decode(link.written.last), 'CalibLabel,300 nM - ong 1\n');
      // Kỹ sư bấm TRẮNG trên máy.
      link.emit('{CalibMode: off}\n');
      await Future<void>.delayed(Duration.zero);
      expect(modes, [true, false]);
      final before = link.written.length;
      await r.setLabel('300 nM - ong 2'); // chỉ ghi nhớ, không gửi (máy sẽ trả notInMode)
      expect(link.written.length, before);
      // ĐỌC kế: một gói vào lại + nhãn cuối + đọc; máy trả lời theo thứ tự.
      link.reply = (_) => '{CalibMode: on}\n{CalibSlot: 4}\n{CalibLabel: ok}\n{Green: 207}\n';
      final v = await r.readSlot(4);
      expect(v, 207);
      expect(latin1.decode(link.written.last), 'CalibStart,4\nCalibLabel,300 nM - ong 2\nCalibShot\n');
      expect(r.modeOn, isTrue);
      // Lần đọc sau lại chỉ CalibShot.
      link.reply = (_) => '{Green: 208}\n';
      expect(await r.readSlot(4), 208);
      expect(latin1.decode(link.written.last), 'CalibShot\n');
      await r.close();
    });

    test('readSlot nhận {CalibError} là lỗi có nội dung', () async {
      final link = _FakeLink()..reply = (_) => '{CalibError: notInMode}\n';
      final r = CalibReader(link, timeout: const Duration(seconds: 2));
      try {
        await r.readSlot(1);
        fail('phải ném');
      } on CalibReadException catch (e) {
        expect(e.message, contains('notInMode'));
      }
      await r.close();
    });
  });

  test('nextCalibCell đi hết ống rồi sang nồng độ kế', () {
    const concs = ['300', '200', '100', '0'];
    expect(nextCalibCell(concs, 3, '300', 1), (conc: '300', tube: 2));
    expect(nextCalibCell(concs, 3, '300', 3), (conc: '200', tube: 1));
    expect(nextCalibCell(concs, 3, '0', 3), isNull);
    expect(nextCalibCell(concs, 3, 'xx', 9), (conc: '300', tube: 1));
  });
}
