import 'dart:ui';

import 'package:RapidPlusApp/theme/app_theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// Khoá tương phản của bảng màu.
///
/// Vì sao đáng có test riêng: đây là app đọc **kết quả xét nghiệm**, và bảng màu
/// port từ dashboard firmware — nơi tỉ lệ tương phản được đo và ghi thẳng vào CSS
/// vì "readability wins over hitting the swatch exactly". Một lần chỉnh màu cho
/// hợp mắt có thể tụt xuống dưới ngưỡng đọc được mà **không ai thấy trên máy
/// mình** (màn tốt, phòng sáng) — chỉ người dùng ngoài hiện trường thấy.
///
/// Test tính lại từ CHÍNH các hằng số đang chạy, không chép số vào đây.
void main() {
  // WCAG 2.x: (L_sáng + .05) / (L_tối + .05). `computeLuminance()` của Flutter
  // đã là relative luminance đúng chuẩn nên không phải tự viết lại sRGB→linear.
  double ratio(Color a, Color b) {
    final l1 = a.computeLuminance(), l2 = b.computeLuminance();
    final hi = l1 > l2 ? l1 : l2, lo = l1 > l2 ? l2 : l1;
    return (hi + 0.05) / (lo + 0.05);
  }

  void aa(String what, Color fg, Color bg, {double need = 4.5}) {
    final v = ratio(fg, bg);
    expect(v, greaterThanOrEqualTo(need),
        reason: '$what = ${v.toStringAsFixed(2)}:1, cần ≥ $need');
  }

  group('sáng', () {
    test('chữ trên nền đạt AA', () {
      aa('ink/scaffold', kFg, kBg);
      aa('ink/card', kFg, kCard);
      aa('muted/card', kMutedFg, kCard);
      aa('muted/scaffold', kMutedFg, kBg);
    });

    test('nút và nhãn thương hiệu đạt AA', () {
      aa('trắng/CTA', const Color(0xFFFFFFFF), kBrandInk);
      aa('trắng/CTA hover', const Color(0xFFFFFFFF), kBrandInkHi);
      aa('nhãn brand/card', kBrandInk, kCard);
      aa('onPrimaryContainer/primaryContainer', kOnPrimaryContainer,
          kPrimaryContainer);
    });

    test('màu trạng thái đạt AA', () {
      aa('success/card', kSuccess, kCard);
      aa('error/card', kError, kCard);
      // Cam cảnh báo: 3:1 là ngưỡng cho chữ LỚN / icon. Nó không bao giờ được
      // dùng làm chữ thường — hạ nó xuống cho đạt 4.5 sẽ thành nâu, mất nghĩa
      // "cảnh báo".
      aa('warning/card', kWarning, kCard, need: 3.0);
    });

    test('mảng tối mang thương hiệu', () {
      aa('trắng/brand-deep', const Color(0xFFEAF1F8), kBrandDeep);
    });
  });

  test('app CHỈ còn theme sáng', () {
    // Dark mode bỏ 2026-08-19. Ghim lại để không ai vô tình dựng lại một nhánh
    // tối nửa vời: bảng màu hiện tại được cân theo nền SÁNG đậm (#DCE9EB), một
    // bộ tối mới sẽ phải cân lại từ đầu chứ không phải lật `Brightness`.
    expect(appTheme().brightness, Brightness.light);
    expect(appTheme().colorScheme.brightness, Brightness.light);
    expect(appTheme().scaffoldBackgroundColor, kBg);
  });

  group('cấu trúc nhìn thấy được', () {
    // Bảng màu bản đầu quá nhạt: thẻ trắng trên nền #F2F8F9 chỉ chênh 1.073 và
    // viền #DFEAEC trên nền đó chỉ 1.14 — người dùng đọc ra là "một mảng trắng
    // mờ", không ra được đâu là thẻ đâu là trang. Làm đậm nền 2026-08-19.
    test('mép thẻ đọc được — bằng bậc sáng HOẶC bằng viền', () {
      // Hai vế, ghim cả hai. Nền `#F1F2F5` (chủ dự án chọn) chỉ cho bậc sáng
      // 1.119 — mảnh hơn hai bản thử trước — nên phần còn lại của mép là VIỀN.
      // Bỏ vế viền đi là quay lại đúng bản `#F2F8F9`: một mảng trắng mờ không
      // ra được đâu là thẻ đâu là trang.
      expect(ratio(kCard, kBg), greaterThan(1.10),
          reason: 'vẫn phải có bậc sáng, dù mảnh');
      expect(ratio(kBorder, kBg), greaterThan(1.30),
          reason: 'viền thẻ là thứ vẽ ra mép khi bậc sáng mảnh');
    });

    test('outline đạt 3:1 — nó nói ra "gõ/bấm được vào đây"', () {
      // WCAG 1.4.11: thành phần giao diện phi-chữ cần 3:1. Viền ô nhập thuộc
      // nhóm này; đường kẻ trang trí (`outlineVariant`) thì không.
      aa('kOutline/kCard', kOutline, kCard, need: 3.0);
      aa('kOutline/kBg', kOutline, kBg, need: 3.0);
    });

    test('kSunken tách khỏi CẢ thẻ trắng LẪN nền trang', () {
      // Nó là nền chip, nền tiêu đề bảng, và đĩa icon ở màn rỗng — cả ba đều
      // xuất hiện trên hai loại nền khác nhau. Bản #E3EFF1 chỉ hơn nền trang
      // 1.06 nên đĩa icon màn rỗng gần như tan vào nền; lộ ra ngay khi nền trang
      // được làm đậm, và trước đó thì không ai thấy vì nền vốn gần trắng.
      expect(ratio(kSunken, kBg), greaterThan(1.10));
      expect(ratio(kSunken, kCard), greaterThan(1.30));
      // Tiêu đề bảng là chữ mờ ĐẶT TRÊN nó → vẫn phải đạt AA.
      aa('kMutedFg/kSunken', kMutedFg, kSunken);
    });

    test('hairline và outline KHÔNG được là một màu', () {
      // Bản trước gán cả `outline` lẫn `outlineVariant` = `kBorder`, nên viền ô
      // nhập nhạt y hệt đường kẻ chia khối.
      expect(kOutline, isNot(kBorder));
      expect(ratio(kOutline, kCard), greaterThan(ratio(kBorder, kCard)));
    });
  });

  group('chữ mờ nằm trên nền LÕM, không phải trên trắng', () {
    // Bảng tỉ lệ bản đầu chỉ đo `kMutedFg` trên `kCard` TRẮNG (4.83:1) rồi coi
    // là xong. Nhưng chữ mờ hiếm khi nằm trên trắng: hint của ô nhập nằm trên
    // `kMuted` (inputDecorationTheme.fillColor) và nhãn chip chưa chọn nằm trên
    // `kSunken`. Ở đó bản cũ chỉ 4.45 và 4.25 — TRƯỢT, mà không test nào thấy.
    test('kMutedFg đạt AA trên CẢ BỐN nền sáng', () {
      aa('kMutedFg/kCard', kMutedFg, kCard);
      aa('kMutedFg/kBg', kMutedFg, kBg);
      aa('kMutedFg/kMuted (nền ô nhập)', kMutedFg, kMuted);
      aa('kMutedFg/kSunken (nền chip)', kMutedFg, kSunken);
    });

  });

  group('SnackBar lỗi', () {
    // `snackBarTheme.contentTextStyle` ghim cứng #EAF1F8 — chữ gần-trắng, tính
    // cho nền TỐI. Vì vậy nền SnackBar lỗi KHÔNG được lấy `colorScheme.error`.
    const chuSnack = Color(0xFFEAF1F8);

    test('kErrorSnackBg đọc rõ hơn hẳn kError', () {
      aa('chữ/kErrorSnackBg', chuSnack, kErrorSnackBg, need: 7.0);
      // Bản đầu ca này ghim `kError` phải TRƯỢT 4.5 — sau khi làm đậm bảng màu
      // (2026-08-19) `kError` lên 5.30:1 và tiền đề đó hết đúng. Không nới ngưỡng
      // cho qua: đổi thành phép so tương đối, vì lý do giữ hằng số riêng giờ là
      // "đọc rõ hơn hẳn" chứ không còn là "cái kia trượt chuẩn".
      expect(ratio(chuSnack, kErrorSnackBg),
          greaterThan(ratio(chuSnack, kError) * 1.4),
          reason: 'hết hơn hẳn thì dùng thẳng colorScheme.error cho gọn');
    });

    test('nền SnackBar tối hẳn, kể cả khi app chỉ còn theme sáng', () {
      // Chữ SnackBar ghim cứng gần-trắng, nên nền của nó phải tối — bỏ dark mode
      // không được kéo theo việc làm sáng nền SnackBar.
      aa('chữ/nền SnackBar', chuSnack, kBrandDeep, need: 7.0);
    });
  });

  // Vùng chụp PNG: xem `test/export_theme_test.dart`.
  //
  // Ở đây từng có hai ca đọc `Theme.of(ctx).colorScheme` rồi kết luận
  // AppExportTheme đã hoạt động. Chúng PASS trong khi lỗi vẫn sống nguyên —
  // vì `Text` không lấy màu từ `colorScheme` mà từ `DefaultTextStyle`. Đã thay
  // bằng ca đo màu ĐÃ RENDER (RenderParagraph). Đừng thêm lại kiểu đo cũ.

  group('SnackBar: đo màu ĐÃ RENDER, không đo cấu hình', () {
    // Cùng bài học: `closeIconColor`/`actionTextColor` không đặt thì rơi về mặc
    // định M3 tính theo `inverseSurface` — ở theme tối là màu SÁNG, mà nền
    // SnackBar lại bị ép TỐI ⇒ nút X gần như biến mất (~1:1) và người dùng phải
    // ngồi chờ nó tự tắt. Đo bằng cách dựng SnackBar thật rồi đọc icon ra.
    {
      const dark = false;
      testWidgets('nút X đọc được trên nền SnackBar (${dark ? "tối" : "sáng"})',
          (tester) async {
        final theme = appTheme();
        await tester.pumpWidget(MaterialApp(
          theme: theme,
          home: Scaffold(
            body: Builder(builder: (c) {
              return TextButton(
                onPressed: () => ScaffoldMessenger.of(c)
                    .showSnackBar(const SnackBar(content: Text('x'))),
                child: const Text('mở'),
              );
            }),
          ),
        ));
        await tester.tap(find.text('mở'));
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 800));

        final nen = theme.snackBarTheme.backgroundColor!;
        final icon = tester.widget<Icon>(find.descendant(
            of: find.byType(SnackBar), matching: find.byType(Icon)));
        final mauIcon = icon.color ?? theme.snackBarTheme.closeIconColor!;
        expect(ratio(mauIcon, nen), greaterThan(3.0),
            reason: 'nút X phải nhìn thấy được, ngưỡng 3:1 cho biểu tượng');
      });
    }
  });

  test('cyan logo là MÀU DẤU, không phải màu chữ', () {
    // Ghim theo cả hai chiều, cố ý:
    //  1. Nó PHẢI trượt 4.5:1 trên trắng — đó là bằng chứng nó vẫn đúng cyan
    //     #20C6D0 của logo. Ai "sửa cho đạt chuẩn" là đã đổi màu thương hiệu.
    //  2. Vì trượt, nó chỉ được dùng làm icon/mark/focus. `AppSemantic.mark`
    //     tồn tại để chỗ dùng nói rõ ý định đó ngay tại chỗ gọi.
    expect(ratio(kBrand, kCard), lessThan(4.5),
        reason: 'kBrand phải giữ đúng cyan logo; nó không dùng làm chữ');
    expect(AppSemantic.light.mark, kBrand);
      });
}
