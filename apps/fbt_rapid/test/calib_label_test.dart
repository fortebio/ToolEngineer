import 'package:RapidPlusApp/services/calib_api.dart';
import 'package:RapidPlusApp/services/calib_label.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qr/qr.dart';

/// Nhãn QR của bộ ống chuẩn.
///
/// Vì sao đáng test: nhãn đi RA GIẤY rồi dán lên túi nằm trong tủ lạnh hàng
/// tháng. Sai một lần là sai trên vật lý, không sửa bằng deploy được — nên ba
/// thứ phải khoá: (1) bộ FAIL/đã huỷ KHÔNG được ra nhãn, (2) định dạng payload
/// không trôi, (3) mã lô do người gõ không phá được cấu trúc payload/HTML.
CalibSet _set({
  String id = 'B2609A-S01',
  String batch = 'B2609A',
  String status = 'stored',
  String verdict = 'PASS',
  String expires = '2026-12-20',
  double slope = 12.3456,
  double r2 = 0.998712,
  double? lod = 8.42,
  String limitsVer = '2026-09-v1',
}) =>
    CalibSet(
      id: id,
      batch: batch,
      rank: 1,
      status: status,
      tubes: const {'0': '3', '300': '3', '100': '3', '200': '2'},
      raw: const {'0': 40.0, '100': 1270.0, '200': 2500.0, '300': 3740.0},
      slope: slope,
      intercept: 41.2,
      r2: r2,
      lod: lod,
      verdict: verdict,
      createdAt: '2026-09-21T10:00:00',
      createdBy: 'khai',
      expiresAt: expires,
      device: '',
      updatedAt: '2026-09-21T10:00:00',
      limitsVer: limitsVer,
      history: const [],
    );

void main() {
  group('chọn bộ được in', () {
    test('chỉ bộ ĐẠT còn dùng được', () {
      final all = [
        _set(id: 'A-S01'),
        _set(id: 'A-S02', status: 'issued'),
        _set(id: 'A-S03', verdict: 'FAIL'),
        _set(id: 'A-S04', status: 'discarded'),
        _set(id: 'A-S05', status: 'used'),
      ];
      expect(calibPrintableSets(all).map((s) => s.id).toList(),
          ['A-S01', 'A-S02']);
    });

    test('hồ sơ cũ không có verdict vẫn in được', () {
      expect(calibPrintableSets([_set(verdict: '')]), hasLength(1));
    });
  });

  group('payload QR', () {
    test('đúng thứ tự cột, nồng độ giảm dần', () {
      expect(
        calibQrPayload(_set()),
        'FBTCAL1|B2609A-S01|B2609A|2026-12-20|12.3456|41.2|0.99871|8.4|'
        '300/3,200/2,100/3,0/3|2026-09-v1',
      );
    });

    test('LOD rỗng khi chưa tính được (không in "null")', () {
      final p = calibQrPayload(_set(lod: null)).split('|');
      expect(p[7], '');
    });

    test('dấu | trong mã lô không phá cấu trúc', () {
      // Mã lô do người gõ tay. Lọt một dấu `|` là máy quét đọc lệch hết các cột
      // sau nó — payload phải luôn đúng 10 cột.
      final p = calibQrPayload(_set(batch: 'B26|09A'));
      expect(p.split('|'), hasLength(10));
      expect(p.split('|')[2], 'B26/09A');
    });

    test('mã QR dựng được và giải mã lại ra module hợp lệ', () {
      final payload = calibQrPayload(_set());
      final img = QrImage(QrCode(
        payload: QrPayload.fromString(payload),
        errorCorrectLevel: QrErrorCorrectLevel.quartile,
      ));
      // Ba ô định vị luôn tối ở góc trên-trái/trên-phải/dưới-trái.
      expect(img.isDark(0, 0), isTrue);
      expect(img.isDark(0, img.moduleCount - 1), isTrue);
      expect(img.isDark(img.moduleCount - 1, 0), isTrue);
    });
  });

  group('SVG mã QR', () {
    test('có khung, có ô tối, kích thước tính bằng mm', () {
      final svg = calibQrSvg(calibQrPayload(_set()), sizeMm: 24);
      expect(svg, startsWith('<svg'));
      expect(svg, contains('width="24.0mm"'));
      expect(svg, contains('fill="#000"'));
      expect(svg, endsWith('</svg>'));
    });

    test('gộp ô liền nhau: số rect ít hơn hẳn số module tối', () {
      final svg = calibQrSvg(calibQrPayload(_set()));
      final rects = RegExp('<rect').allMatches(svg).length;
      expect(rects, lessThan(700), reason: 'không gộp run-length thì ~1000+');
    });
  });

  group('tờ nhãn A4', () {
    test('mỗi bộ một nhãn, nhân theo số bản', () {
      final html = calibLabelSheetHtml([_set(id: 'A-S01'), _set(id: 'A-S02')],
          copies: 2, now: DateTime(2026, 9, 23, 8, 5));
      expect(RegExp('class="lb"').allMatches(html).length, 4);
      expect(RegExp('A-S01').allMatches(html).length, greaterThanOrEqualTo(2));
      expect(html, contains('2026-09-23 08:05'));
    });

    test('tự in khi mở + CSS khổ A4', () {
      final html = calibLabelSheetHtml([_set()]);
      expect(html, contains('window.print()'));
      expect(html, contains('@page { size: A4'));
      expect(html, contains('<meta charset="utf-8">'));
    });

    test('không nhúng gì từ mạng (in được ở kho không có internet)', () {
      final html = calibLabelSheetHtml([_set()]);
      // URL DUY NHẤT được phép là namespace SVG — nó là định danh, trình duyệt
      // không tải gì từ đó. Mọi URL khác (font, CSS, ảnh) là nhãn in ra trắng
      // ở máy kho không có mạng.
      final urls = RegExp(r'https?://[^"'
              r"'\s>]+")
          .allMatches(html)
          .map((m) => m.group(0))
          .toSet();
      expect(urls, {'http://www.w3.org/2000/svg'});
      expect(html, isNot(contains('<link')));
      expect(html, isNot(contains('<img')));
    });

    test('mã lô có HTML không chèn được thẻ', () {
      final html =
          calibLabelSheetHtml([_set(batch: '<script>alert(1)</script>')]);
      expect(html, isNot(contains('<script>alert(1)')));
      expect(html, contains('&lt;script&gt;alert(1)'));
    });
  });
}
