// Kiểm tra TestResult.fromLooseJson đoán đúng 3 shape file JSON mở tay.

import 'package:flutter_test/flutter_test.dart';
import 'package:RapidPlusApp/models/test_result.dart';

void main() {
  test('shape data.json app xuất (ResultExport)', () {
    final r = TestResult.fromLooseJson({
      'deviceId': 'RPL02013',
      'version': '2.1',
      'time': '2026-07-01T10:00:00',
      'slots': [
        {'index': 1, 'result': 'Dương tính', 'ct': 22.3, 'slope': 1.1, 'data': [1, 2, 3]},
        {'index': 2, 'result': 'Âm tính', 'ct': null, 'slope': null, 'data': []},
      ],
    });
    expect(r.deviceId, 'RPL02013');
    expect(r.slots[0].classification, Classification.positive);
    expect(r.slots[0].ct, 22.3);
    expect(r.slots[0].curve, [1.0, 2.0, 3.0]);
    expect(r.slots[1].classification, Classification.negative);
    expect(r.curvesAreRaw, true);
    expect(r.timestamp.year, 2026);
  });

  test('shape payload firmware (/getdata: CT_value/result/amplification)', () {
    final r = TestResult.fromLooseJson({
      'id_device': 'RPL01',
      'time': '01-07-2026 09:30:00',
      'CT_value': ['22.3', 'N/A'],
      'result': ['Positive', 'Negative'],
      'amplification': ['1,2,3,', '4,5,6,'],
      'slopes': [1.0, 1.0],
    });
    expect(r.deviceId, 'RPL01');
    expect(r.slots[0].classification, Classification.positive);
    expect(r.slots[0].curve, [1.0, 2.0, 3.0]);
    expect(r.curvesAreRaw, true);
  });

  test('shape run cloud (ct/curves chữ cái)', () {
    final r = TestResult.fromLooseJson({
      'id_device': 'RPL02',
      'time': '2026-07-01T08:00:00',
      'ct': ['20.1', ''],
      'result': ['P', 'N'],
      'curves': [
        [1, 2],
        [3, 4],
      ],
    });
    expect(r.slots[0].classification, Classification.positive);
    expect(r.slots[0].ct, 20.1);
    expect(r.slots[1].classification, Classification.negative);
    expect(r.curvesAreRaw, true);
  });
}
