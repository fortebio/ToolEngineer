// Trục của đồ thị CT: mốc phải TRÒN và số lượng mốc phải theo bề rộng khung.
//
// Lỗi đã sửa: bước chia trục X ghim cứng 5 phút, còn biên là số lẻ (120 điểm × 20s
// = 39,67 phút). fl_chart LUÔN vẽ thêm nhãn tại đúng min/max → cạnh "35" có thêm
// "39.7" dí sát, và lưới dọc thì fl_chart tự chọn bước riêng nên không trùng nhãn nào.

import 'package:RapidPlusApp/models/test_result.dart';
import 'package:RapidPlusApp/widgets/ct_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

List<SlotResult> _slots(int points, {double from = 150, double step = 15}) => [
      for (var i = 0; i < 3; i++)
        SlotResult(
          index: i + 1,
          classification: Classification.positive,
          ct: 20 + i.toDouble(),
          curve: [for (var j = 0; j < points; j++) from + j * step],
        ),
    ];

Future<void> _pump(WidgetTester t, Size size,
    {int points = 120, double from = 150, double step = 15}) async {
  await t.pumpWidget(MaterialApp(
    home: Scaffold(
      body: Center(
        child: SizedBox(
          width: size.width,
          height: size.height,
          child: CtChart(
            slots: _slots(points, from: from, step: step),
            visibleIndexes: const {1, 2, 3},
            readingIntervalSec: 20, // 120 điểm → 39,67 phút
          ),
        ),
      ),
    ),
  ));
  await t.pump(const Duration(milliseconds: 300));
}

void main() {
  testWidgets('khung rộng: mốc 5 phút, KHÔNG còn nhãn lẻ ở biên', (t) async {
    await _pump(t, const Size(760, 420));

    expect(find.text('39.7'), findsNothing,
        reason: 'biên lẻ phải được làm tròn lên, không in ra thành một nhãn');
    expect(find.text('5'), findsOneWidget);
    expect(find.text('35'), findsOneWidget);
    expect(find.text('40'), findsOneWidget, reason: 'biên phải nằm trên mốc');

    // Và nhãn cuối phải CÁCH ĐỀU như mọi mốc khác. Chỉ kiểm "có chữ 40" là chưa đủ:
    // biên lẻ 39,67 cũng in ra "40" (làm tròn khi hiển thị) nhưng đứng dí vào "35".
    double x(String s) => t.getCenter(find.text(s)).dx;
    expect(x('40') - x('35'), closeTo(x('10') - x('5'), 1));
  });

  testWidgets('khung hẹp: thưa mốc lại cho khỏi dính chữ', (t) async {
    await _pump(t, const Size(390, 300));

    expect(find.text('10'), findsOneWidget);
    expect(find.text('40'), findsOneWidget);
    expect(find.text('5'), findsNothing,
        reason: 'bề rộng điện thoại chỉ đủ ~5 mốc → bước 10 phút, không phải 5');
  });

  testWidgets('trục tung: nấc chia hết 50, biên bám nấc', (t) async {
    // 150 → 150 + 119×15 = 1935.
    await _pump(t, const Size(760, 420));

    // ('0' có ở CẢ hai trục nên không kiểm được bằng find.text)
    for (final v in ['500', '1000', '1500', '2000']) {
      expect(find.text(v), findsOneWidget, reason: 'mốc $v');
    }
    expect(find.text('1935'), findsNothing, reason: 'biên phải bám nấc, không lấy số lẻ');
  });

  testWidgets('đường cong THẤP: khung vẫn mở tới 200, không phóng to', (t) async {
    // Không giá trị nào quá 100 (20 → 20+19×4 = 96).
    await _pump(t, const Size(760, 420), points: 20, from: 20, step: 4);

    expect(find.text('200'), findsOneWidget,
        reason: 'dưới 100 thì đỉnh khung vẫn là 200');
    expect(find.text('50'), findsOneWidget, reason: 'nấc nhỏ nhất là 50');
    expect(find.text('100'), findsOneWidget);
    expect(find.text('150'), findsOneWidget);
    // 20 hay 25 là nấc nhỏ hơn 50 -> không được phép.
    expect(find.text('25'), findsNothing);
    expect(find.text('20'), findsNothing);
  });

  testWidgets('lần chạy ngắn: vẫn đủ mốc, không phải 1 nhãn duy nhất',
      (t) async {
    // 20 điểm × 20s = 6,33 phút — bước 5 phút cũ chỉ cho ra "0" và "5".
    await _pump(t, const Size(760, 420), points: 20);

    expect(find.text('1'), findsOneWidget);
    expect(find.text('7'), findsOneWidget);
  });
}
