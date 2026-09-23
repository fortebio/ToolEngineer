/// **Nhãn QR cho bộ ống chuẩn ĐẠT** — dán lên túi zip, in từ tab Hiệu chuẩn.
///
/// Vì sao cần: bộ ống rời tủ lạnh là mất dấu. Túi chỉ có chữ viết tay thì không
/// ai tra được nó thuộc lô nào, hệ số hiệu chuẩn bao nhiêu, hết hạn khi nào —
/// và khi máy đọc lệch thì không truy ngược được ống nào đã dùng. Nhãn có QR
/// giải quyết đúng chỗ đó: quét là ra toàn bộ danh tính + đường chuẩn của bộ.
///
/// **Chỉ bộ ĐẠT mới được in** ([calibPrintableSets]): bộ FAIL/đã huỷ mà có nhãn
/// đẹp dán trên túi là mời người ta dùng nhầm một vật chuẩn không đạt — đây là
/// vật chuẩn cho cả fleet máy xét nghiệm, nhầm một lần là sai cả loạt kết quả.
///
/// **Nội dung QR là TEXT rời, không phải URL** ([calibQrPayload]): kho lạnh
/// không có mạng, và `/calib/sets/{id}` đòi Bearer token nên điện thoại quét ra
/// URL cũng chỉ nhận 401. Payload mang sẵn mọi thứ cần để dùng ống offline; mã
/// bộ trong đó gõ thẳng vào ô lọc của mục "Bộ ống" là ra hồ sơ đầy đủ.
///
/// Định dạng (phiên bản hoá ở đầu chuỗi để máy quét sau này còn biết đường đọc):
///
/// ```
/// FBTCAL1|<mã bộ>|<lô>|<hạn>|<slope>|<intercept>|<R2>|<LOD>|<ống>|<ngưỡng>
/// FBTCAL1|B2609A-S01|B2609A|2026-12-20|12.3456|41.2|0.99871|8.4|300/3,200/2,100/3,0/3|2026-09-v1
/// ```
///
/// In bằng HTML ([calibLabelSheetHtml]) chứ không phải plugin in: app chạy cả
/// web lẫn desktop, và mọi máy đều có trình duyệt biết in A4. Phần mở file nằm
/// ở `util/printable.dart`.
library;

import 'package:qr/qr.dart';

import 'calib_api.dart';

/// Tiền tố + phiên bản payload. ĐỪNG đổi ý nghĩa cột đã phát hành — nhãn đã in
/// nằm trên túi trong tủ lạnh hàng tháng trời; muốn đổi thì lên `FBTCAL2`.
const String kCalibQrPrefix = 'FBTCAL1';

/// Bộ được phép in nhãn: **ĐẠT** và còn trong vòng đời (chưa huỷ, chưa dùng hết).
///
/// `verdict` rỗng = hồ sơ cũ trước khi server ghi verdict → coi như đạt (server
/// chỉ đóng gói tổ hợp PASS), nhưng FAIL thì chặn hẳn.
List<CalibSet> calibPrintableSets(Iterable<CalibSet> all) => [
      for (final s in all)
        if (s.verdict.toUpperCase() != 'FAIL' &&
            (s.status == 'stored' || s.status == 'issued'))
          s
    ];

/// Chuỗi trong mã QR của một bộ ống. Xem định dạng ở doc đầu file.
String calibQrPayload(CalibSet s) {
  final ks = s.tubes.keys.toList()
    ..sort((a, b) => (double.tryParse(b) ?? 0).compareTo(double.tryParse(a) ?? 0));
  final tubes = ks.map((k) => '$k/${s.tubes[k]}').join(',');
  // Dấu `|` là dấu tách cột nên KHÔNG được lọt vào giá trị (mã lô do người gõ).
  String cell(String v) => v.replaceAll('|', '/').trim();
  return [
    kCalibQrPrefix,
    cell(s.id),
    cell(s.batch),
    cell(s.expiresAt),
    s.slope.toStringAsFixed(4),
    s.intercept.toStringAsFixed(1),
    s.r2.toStringAsFixed(5),
    s.lod == null ? '' : s.lod!.toStringAsFixed(1),
    cell(tubes),
    cell(s.limitsVer),
  ].join('|');
}

/// Mã QR của [payload] thành SVG vuông cạnh [sizeMm] milimét.
///
/// Gộp module tối liền nhau trên CÙNG một hàng thành một `<rect>` (run-length):
/// một QR ~33×33 vẽ từng ô là cả nghìn thẻ rect, mà tờ nhãn 10 bộ thì nhân mười
/// — Chrome vẫn in được nhưng file phình vô cớ.
///
/// Mức sửa lỗi **Q (25%)**: nhãn dán túi zip đi tủ lạnh, bị ướt/xước/che một góc
/// là chuyện thường.
String calibQrSvg(String payload, {double sizeMm = 26, int quiet = 2}) {
  final img = QrImage(QrCode(
    payload: QrPayload.fromString(payload),
    errorCorrectLevel: QrErrorCorrectLevel.quartile,
  ));
  final n = img.moduleCount;
  final total = n + quiet * 2;
  final b = StringBuffer()
    ..write('<svg xmlns="http://www.w3.org/2000/svg" width="${sizeMm}mm" '
        'height="${sizeMm}mm" viewBox="0 0 $total $total" '
        'shape-rendering="crispEdges" role="img">')
    ..write('<rect width="$total" height="$total" fill="#fff"/>');
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
      b.write('<rect x="${start + quiet}" y="${r + quiet}" '
          'width="${c - start}" height="1" fill="#000"/>');
    }
  }
  b.write('</svg>');
  return b.toString();
}

/// Thoát ký tự cho HTML (mã lô/ghi chú do người dùng gõ).
String calibHtmlEscape(String s) => s
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');

/// Tờ nhãn A4 (HTML tự chứa, không tải gì từ mạng) cho [sets].
///
/// Mỗi bộ MỘT nhãn, 2 cột × 5 hàng = 10 nhãn/tờ A4 — cỡ vừa túi zip và vừa khổ
/// decal A4 cắt sẵn phổ biến. Tự gọi `window.print()` khi mở: người dùng bấm nút
/// "In nhãn QR" là muốn in, không phải muốn đọc HTML.
///
/// [copies] nhân bản mỗi nhãn (dán một cái lên túi, một cái vào sổ giao nhận).
String calibLabelSheetHtml(
  List<CalibSet> sets, {
  String printedBy = '',
  DateTime? now,
  int copies = 1,
}) {
  final at = now ?? DateTime.now();
  String two(int v) => v.toString().padLeft(2, '0');
  final stamp = '${at.year}-${two(at.month)}-${two(at.day)} '
      '${two(at.hour)}:${two(at.minute)}';
  final who = printedBy.trim();

  final cards = StringBuffer();
  for (final s in sets) {
    final ks = s.tubes.keys.toList()
      ..sort((a, b) => (double.tryParse(b) ?? 0).compareTo(double.tryParse(a) ?? 0));
    final tubes = ks.map((k) => '$k nM/ống ${s.tubes[k]}').join(' · ');
    final lod = s.lod == null ? '—' : '${s.lod!.toStringAsFixed(1)} nM';
    final qr = calibQrSvg(calibQrPayload(s), sizeMm: 24);
    for (var i = 0; i < copies; i++) {
      cards
        ..write('<div class="lb"><div class="qr">')
        ..write(qr)
        ..write('</div><div class="tx">')
        ..write('<div class="id">${calibHtmlEscape(s.id)}</div>')
        ..write('<div class="tubes">${calibHtmlEscape(tubes)}</div>')
        ..write('<div class="fit">slope ${s.slope.toStringAsFixed(3)}'
            ' · R² ${s.r2.toStringAsFixed(5)} · LOD $lod</div>')
        ..write('<div class="meta">Lô ${calibHtmlEscape(s.batch)}'
            ' · ngưỡng ${calibHtmlEscape(s.limitsVer)}</div>')
        ..write('<div class="exp">HSD '
            '${calibHtmlEscape(s.expiresAt.isEmpty ? '—' : s.expiresAt)}</div>')
        ..write('<div class="brand">Forte Biotech · ống chuẩn quang Fluorescein'
            '</div></div></div>');
    }
  }

  final title = 'Nhãn ống chuẩn — ${sets.length} bộ'
      '${copies > 1 ? ' × $copies bản' : ''}';
  return '<!DOCTYPE html>\n'
      '<html lang="vi">\n<head>\n<meta charset="utf-8">\n'
      '<title>$title</title>\n<style>\n$_sheetCss</style>\n</head>\n<body>\n'
      '<div class="bar"><b>$title</b>'
      '<span>In ${calibHtmlEscape(stamp)}'
      '${who.isEmpty ? '' : ' · ${calibHtmlEscape(who)}'}</span>'
      '<button onclick="window.print()">In</button></div>\n'
      '<div class="sheet">\n${cards.toString()}\n</div>\n'
      '<script>window.addEventListener("load", function () '
      '{ window.print(); });</script>\n</body>\n</html>\n';
}

/// CSS của tờ nhãn. Tách hằng cho dễ đọc — trong chuỗi Dart thì `@media` và
/// `${...}` của CSS lẫn với nội suy Dart rất dễ sai.
const String _sheetCss = r'''
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body { margin: 0; background: #f1f2f5; color: #16202b;
         font-family: "Segoe UI", Arial, sans-serif; }
  .bar { padding: 12px 16px; background: #0f5f63; color: #fff;
         display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
  .bar b { font-size: 15px; }
  .bar span { opacity: .85; font-size: 13px; }
  .bar button { margin-left: auto; padding: 8px 16px; border: 0; border-radius: 8px;
                background: #fff; color: #0f5f63; font-weight: 700; cursor: pointer; }
  .sheet { padding: 8mm; display: grid; grid-template-columns: repeat(2, 95mm);
           gap: 4mm; justify-content: center; }
  .lb { width: 95mm; height: 55mm; background: #fff; border: 1px dashed #9bb;
        border-radius: 3mm; padding: 4mm; display: flex; gap: 4mm;
        align-items: flex-start; page-break-inside: avoid; break-inside: avoid; }
  .qr { flex: 0 0 auto; }
  .tx { min-width: 0; display: flex; flex-direction: column; gap: 1mm; }
  .id { font: 700 17px Consolas, monospace; letter-spacing: .4px; }
  .tubes { font: 600 11px Consolas, monospace; }
  .fit { font-size: 10.5px; color: #333; }
  .meta { font-size: 10.5px; color: #555; }
  .exp { font-size: 12px; font-weight: 700; color: #b45309; }
  .brand { margin-top: auto; font-size: 9px; color: #777; }
  @media print {
    body { background: #fff; }
    .bar { display: none; }
    .sheet { padding: 0; gap: 0; grid-template-columns: repeat(2, 99mm); }
    .lb { width: 99mm; height: 57mm; border: 0; border-radius: 0; }
    @page { size: A4; margin: 5mm; }
  }
''';
