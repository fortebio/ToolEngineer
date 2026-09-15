// Kiểm tra port baseline + Savitzky–Golay (curve_processing.dart).

import 'package:flutter_test/flutter_test.dart';
import 'package:RapidPlusApp/util/curve_processing.dart';

void main() {
  test('sgSmooth của hằng số ≈ hằng số (kernel bảo toàn hằng số)', () {
    final v = List<double>.filled(120, 5.0);
    final s = sgSmooth(v, 4, 2);
    expect(s.length, 120);
    for (final x in s) {
      expect((x - 5.0).abs() < 1e-6, true, reason: 'giá trị: $x');
    }
  });

  test('sgSmooth của đường thẳng ≈ đường thẳng (bậc 2 fit tuyến tính)', () {
    final v = [for (var i = 0; i < 60; i++) 2.0 * i + 1];
    final s = sgSmooth(v, 4, 2);
    for (var i = 0; i < v.length; i++) {
      expect((s[i] - v[i]).abs() < 1e-6, true, reason: 'i=$i: ${s[i]} vs ${v[i]}');
    }
  });

  test('baseline của hằng số → 0', () {
    final raw = List<double>.filled(120, 7.0);
    final base = computeCurveSeries(CurveView.baseline, raw, null, 20);
    for (final x in base) {
      expect(x.abs() < 1e-6, true);
    }
  });

  test('calibrate = raw / slope', () {
    final calib =
        computeCurveSeries(CurveView.calibratedDraw, [2.0, 4.0, 6.0], 2.0, 20);
    expect(calib, [1.0, 2.0, 3.0]);
  });

  test('raw trả nguyên đường cong', () {
    final raw = [1.0, 2.0, 3.0];
    expect(computeCurveSeries(CurveView.rawDraw, raw, 9.0, 20), raw);
  });
}
