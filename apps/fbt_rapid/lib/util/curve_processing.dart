import 'dart:math' as math;

/// Các loại đồ thị đường cong (tính từ raw draw + slope của 1 slot).
///
/// Pipeline (theo firmware): raw → ÷slope (calibrate) → baseline → Savitzky–Golay.
enum CurveView { rawDraw, calibratedDraw, baseline, baselineSmoothed }

extension CurveViewLabel on CurveView {
  /// Nhãn đầy đủ.
  String get label {
    switch (this) {
      case CurveView.rawDraw:
        return 'Raw (Giá trị gốc)';
      case CurveView.calibratedDraw:
        return 'Calibrate (raw/slope)';
      case CurveView.baseline:
        return 'Baseline (chưa làm mượt)';
      case CurveView.baselineSmoothed:
        return 'Baseline + làm mượt (Savitzky–Golay)';
    }
  }

  /// Nhãn ngắn cho nút chọn.
  String get short {
    switch (this) {
      case CurveView.rawDraw:
        return 'Raw';
      case CurveView.calibratedDraw:
        return 'Calib';
      case CurveView.baseline:
        return 'Baseline';
      case CurveView.baselineSmoothed:
        return 'SG';
    }
  }
}

/// Tham số xử lý đường cong — **mặc định theo firmware** (`define.h` parastructure),
/// vì log cloud không lưu các tham số này.
class CurveParams {
  final int baselineStart; // phút
  final int baselineRange; // phút
  final int sgWidth; // sg_window (cửa sổ = 2*width+1)
  final int sgOrder; // sg_order

  const CurveParams({
    this.baselineStart = 3,
    this.baselineRange = 4,
    this.sgWidth = 4,
    this.sgOrder = 2,
  });
}

const kDefaultCurveParams = CurveParams();

/// Tính chuỗi giá trị cho 1 loại đồ thị, từ đường cong **raw** + slope của 1 slot.
/// [readingIntervalSec] để quy đổi index → phút (cho mốc baseline).
List<double> computeCurveSeries(
  CurveView view,
  List<double> raw,
  double? slope,
  int readingIntervalSec, {
  CurveParams params = kDefaultCurveParams,
}) {
  if (raw.isEmpty) return const [];
  switch (view) {
    case CurveView.rawDraw:
      return raw;
    case CurveView.calibratedDraw:
      return _calibrate(raw, slope);
    case CurveView.baseline:
      return _baseline(_calibrate(raw, slope), readingIntervalSec, params);
    case CurveView.baselineSmoothed:
      final b = _baseline(_calibrate(raw, slope), readingIntervalSec, params);
      return sgSmooth(b, params.sgWidth, params.sgOrder);
  }
}

List<double> _calibrate(List<double> raw, double? slope) {
  if (slope == null || slope == 0) return raw;
  return [for (final v in raw) v / slope];
}

/// Trừ baseline = giá trị trung bình trong cửa sổ [baselineStart, +baselineRange] phút.
/// Port của `baseline()` trong src/Alg/Algo.cpp.
List<double> _baseline(List<double> data, int readingIntervalSec, CurveParams p) {
  final n = data.length;
  double tMin(int i) => i * readingIntervalSec / 60.0;
  int start = 0;
  while (start < n && tMin(start) < p.baselineStart) {
    start++;
  }
  int stop = start;
  while (stop < n && tMin(stop) < p.baselineStart + p.baselineRange) {
    stop++;
  }
  stop = math.min(stop + 1, n); // firmware cộng +1
  if (stop <= start) {
    start = 0;
    stop = math.min(n, 1);
  }
  double sum = 0;
  for (int i = start; i < stop; i++) {
    sum += data[i];
  }
  final base = (stop > start) ? sum / (stop - start) : 0.0;
  return [for (final v in data) v - base];
}

/// Savitzky–Golay smoothing — port từ `sg_smooth()` trong src/Alg/sgsmooth.cpp.
/// Cửa sổ = 2*width+1, bậc đa thức = deg.
List<double> sgSmooth(List<double> v, int width, int deg) {
  final n = v.length;
  final res = List<double>.filled(n, 0.0);
  if (width < 1 || deg < 0 || n < 2 * width + 2) {
    return List<double>.from(v); // không đủ dữ liệu -> trả nguyên
  }
  final window = 2 * width + 1;
  final endidx = n - 1;

  if (deg == 0) {
    // trung bình trượt + xử lý biên
    for (int i = 0; i < width; i++) {
      final scale = 1.0 / (i + 1);
      for (int j = 0; j <= i; j++) {
        res[i] += scale * v[j];
        res[endidx - i] += scale * v[endidx - j];
      }
    }
    final scale = 1.0 / window;
    for (int i = 0; i <= n - window; i++) {
      for (int j = 0; j < window; j++) {
        res[i + width] += scale * v[i + j];
      }
    }
    return res;
  }

  // biên (hệ số bất đối xứng)
  for (int i = 0; i < width; i++) {
    final b1 = List<double>.filled(window, 0.0)..[i] = 1.0;
    final c1 = _sgCoeff(b1, deg);
    for (int j = 0; j < window; j++) {
      res[i] += c1[j] * v[j];
      res[endidx - i] += c1[j] * v[endidx - j];
    }
  }
  // phần giữa (hệ số đối xứng, tái dùng)
  final b2 = List<double>.filled(window, 0.0)..[width] = 1.0;
  final c2 = _sgCoeff(b2, deg);
  for (int i = 0; i <= n - window; i++) {
    for (int j = 0; j < window; j++) {
      res[i + width] += c2[j] * v[i + j];
    }
  }
  return res;
}

/// Hệ số SG: fit đa thức bậc `deg` vào `b` (least squares) rồi trả giá trị fit.
/// Port của `sg_coeff` (giải hệ chuẩn (AᵀA)·c = Aᵀ·b với A[i][j] = i^j).
List<double> _sgCoeff(List<double> b, int deg) {
  final n = b.length;
  final cols = deg + 1;
  final ata = List.generate(cols, (_) => List<double>.filled(cols, 0.0));
  final atb = List<double>.filled(cols, 0.0);
  for (int i = 0; i < n; i++) {
    final pows = List<double>.filled(cols, 1.0);
    for (int j = 1; j < cols; j++) {
      pows[j] = pows[j - 1] * i;
    }
    for (int r = 0; r < cols; r++) {
      atb[r] += pows[r] * b[i];
      for (int c = 0; c < cols; c++) {
        ata[r][c] += pows[r] * pows[c];
      }
    }
  }
  final c = _solveLinear(ata, atb);
  final res = List<double>.filled(n, 0.0);
  for (int i = 0; i < n; i++) {
    double val = 0, p = 1;
    for (int j = 0; j < cols; j++) {
      val += c[j] * p;
      p *= i;
    }
    res[i] = val;
  }
  return res;
}

/// Giải hệ tuyến tính nhỏ M·x = y bằng khử Gauss (partial pivoting).
List<double> _solveLinear(List<List<double>> m, List<double> y) {
  final n = y.length;
  final a = [for (final r in m) List<double>.from(r)];
  final x = List<double>.from(y);
  for (int col = 0; col < n; col++) {
    int piv = col;
    for (int r = col + 1; r < n; r++) {
      if (a[r][col].abs() > a[piv][col].abs()) piv = r;
    }
    if (piv != col) {
      final tr = a[piv];
      a[piv] = a[col];
      a[col] = tr;
      final ty = x[piv];
      x[piv] = x[col];
      x[col] = ty;
    }
    final d = a[col][col];
    if (d.abs() < 1e-12) continue;
    for (int r = 0; r < n; r++) {
      if (r == col) continue;
      final f = a[r][col] / d;
      for (int c = col; c < n; c++) {
        a[r][c] -= f * a[col][c];
      }
      x[r] -= f * x[col];
    }
  }
  for (int i = 0; i < n; i++) {
    final d = a[i][i];
    x[i] = (d.abs() < 1e-12) ? 0.0 : x[i] / d;
  }
  return x;
}
