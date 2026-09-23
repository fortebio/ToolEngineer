import 'package:flutter/material.dart';
import 'package:qr/qr.dart';

/// Vẽ mã QR ngay trong app (xem trước trước khi in).
///
/// Tự vẽ bằng `CustomPainter` trên gói `qr` (thuần Dart) thay vì thêm
/// `qr_flutter`: chỗ duy nhất cần là ô xem trước nhãn, mà bản in thật thì đi
/// đường SVG trong `services/calib_label.dart` — hai chỗ dùng CHUNG một bộ mã
/// hoá nên cái hiện trên màn đúng bằng cái ra giấy.
///
/// Nền TRẮNG + viền yên tĩnh vẽ luôn trong widget: QR đặt trên nền tối hoặc sát
/// mép nội dung là máy quét không bắt được, và đây là app có chế độ tối.
class QrView extends StatelessWidget {
  final String data;
  final double size;

  /// Số module trắng quanh mã (chuẩn QR: 4; 2 đủ cho màn hình, bản in dùng 2).
  final int quiet;

  const QrView({super.key, required this.data, this.size = 148, this.quiet = 2});

  @override
  Widget build(BuildContext context) {
    final img = QrImage(QrCode(
      payload: QrPayload.fromString(data),
      errorCorrectLevel: QrErrorCorrectLevel.quartile,
    ));
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: const Color(0xFFC0D6DA)),
      ),
      child: CustomPaint(painter: _QrPainter(img, quiet)),
    );
  }
}

class _QrPainter extends CustomPainter {
  final QrImage img;
  final int quiet;
  const _QrPainter(this.img, this.quiet);

  @override
  void paint(Canvas canvas, Size size) {
    final n = img.moduleCount;
    final total = n + quiet * 2;
    // Làm tròn XUỐNG cạnh module rồi căn giữa: cạnh lẻ (vd 148/37 = 4,0) làm
    // các ô rộng khác nhau vài phần pixel, quét bằng điện thoại kém hẳn.
    final px = (size.width / total).floorToDouble().clamp(1.0, size.width);
    final dx = (size.width - px * total) / 2;
    final dy = (size.height - px * total) / 2;
    final paint = Paint()..color = Colors.black;
    for (var r = 0; r < n; r++) {
      var c = 0;
      while (c < n) {
        if (!img.isDark(r, c)) {
          c++;
          continue;
        }
        final start = c;
        while (c < n && img.isDark(r, c)) {
          c++;
        }
        canvas.drawRect(
          Rect.fromLTWH(
            dx + (start + quiet) * px,
            dy + (r + quiet) * px,
            (c - start) * px,
            px,
          ),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(_QrPainter old) => old.img != img || old.quiet != quiet;
}
