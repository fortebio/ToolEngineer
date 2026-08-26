// Vùng chụp PNG: đo màu chữ ĐÃ RENDER, không phải màu trong ColorScheme.
//
// Vì sao file riêng và vì sao phép đo này: bản vá đầu (2026-08-19) bọc vùng chụp
// bằng `Theme(data: appTheme(Brightness.light))` rồi tuyên bố đã xong — và test
// lúc đó PASS. Nhưng nó chỉ đọc `Theme.of(ctx).colorScheme`, trong khi widget
// `Text` KHÔNG lấy màu từ colorScheme: nó lấy từ `DefaultTextStyle`, mà
// `DefaultTextStyle` do `Material` dựng — và `Material` gần nhất là của
// `Scaffold` NẰM TRÊN vùng chụp, nên vẫn mang màu chữ của theme đang chạy.
// Kết quả: test xanh, lỗi vẫn sống, ảnh xuất ra vẫn trắng-trên-trắng.
//
// Bài học ghim vào đây: đo thứ NGƯỜI DÙNG THẤY (RenderParagraph), không đo thứ
// mình vừa cấu hình.

import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/theme/app_theme.dart';

double _lum(Color c) {
  double f(double v) =>
      v <= 0.03928 ? v / 12.92 : math.pow((v + 0.055) / 1.055, 2.4).toDouble();
  return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
}

double ratio(Color a, Color b) {
  final la = _lum(a), lb = _lum(b);
  return (math.max(la, lb) + 0.05) / (math.min(la, lb) + 0.05);
}

/// Dựng ĐÚNG hình dạng cây widget thật: MaterialApp có theme chữ SÁNG > Scaffold
/// (Material của nó đặt DefaultTextStyle theo theme đó) > vùng chụp nền trắng >
/// Text không đặt màu.
///
/// Dùng `ThemeData.dark()` CÓ SẴN của Flutter chứ không phải theme của app: app
/// đã bỏ dark mode, nhưng bất biến cần ghim là "vùng chụp luôn sáng bất kể theme
/// bao quanh", nên phép thử phải dựng được một theme bao quanh có chữ sáng.
Future<Color> mauChuTrongVungChup(WidgetTester tester,
    {required bool boc}) async {
  const nhan = 'Lysis';
  Widget noiDung = Container(
    color: Colors.white,
    child: const Text(nhan),
  );
  if (boc) noiDung = AppExportTheme(child: noiDung);

  await tester.pumpWidget(MaterialApp(
    theme: ThemeData.dark(),
    home: Scaffold(body: RepaintBoundary(child: noiDung)),
  ));

  final p = tester.renderObject<RenderParagraph>(find.text(nhan));
  return p.text.style!.color!;
}

void main() {
  const trang = Color(0xFFFFFFFF);

  testWidgets('KHÔNG bọc → đúng là trắng trên trắng (lỗi có thật)',
      (tester) async {
    // Canh gác cho ca dưới. Nếu ngày nào ca này hết đỏ nghĩa là theme tối đã đổi
    // và ca dưới không còn chứng minh được điều gì.
    final c = await mauChuTrongVungChup(tester, boc: false);
    expect(ratio(c, trang), lessThan(2.0),
        reason: 'gần-trắng trên trắng — đây chính là lỗi cần chặn');
  });

  testWidgets('CÓ bọc AppExportTheme → chữ tối, đọc được trên nền trắng',
      (tester) async {
    final c = await mauChuTrongVungChup(tester, boc: true);
    expect(ratio(c, trang), greaterThan(4.5),
        reason: 'ảnh PNG đi vào hồ sơ kết quả — phải đọc được');
  });
}
