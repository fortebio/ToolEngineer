// Tên BỆNH trong chuỗi `result` (firmware v2.4.3+ cho chọn PCV, EHP, WSSV… cho
// từng slot) phải đi được tới đồ thị kết quả, và KHÔNG được làm hỏng cách đọc
// CT của các bản cũ.

import 'package:flutter_test/flutter_test.dart';
import 'package:RapidPlusApp/models/test_result.dart';

void main() {
  test('parseResultCell: 3 phần (v2.4.3+) = tên | CT | chữ', () {
    final r = parseResultCell('PCV | 22.3 | P');
    expect(r.name, 'PCV');
    expect(r.ct, 22.3);
    expect(r.letter, 'P');

    // Tên có khoảng trắng (ASF I177L) vẫn nguyên vẹn.
    expect(parseResultCell('ASF I177L | 30.0 | N').name, 'ASF I177L');

    // Slot chưa đặt tên -> firmware ghi "N/A", KHÔNG hiện như một cái tên.
    expect(parseResultCell('N/A | 25.0 | P').name, isEmpty);
  });

  test('parseResultCell: dạng lỗi của firmware "| /E" là Error', () {
    // Trước đây lấy ký tự đầu của "/E" ra "/" -> phân loại "Không rõ" thay vì "Lỗi".
    expect(parseResultCell('PCV | 22 | /E').letter, 'E');
    expect(parseResultCell('PCV | !  | E').letter, 'E');
    expect(parseResultCell('PCV | - | /E').ct, isNull);
  });

  test('parseResultCell: bản CŨ 2 phần vẫn ra CT, không có tên', () {
    final r = parseResultCell('22.3 | N');
    expect(r.name, isEmpty);
    expect(r.ct, 22.3, reason: 'phần đầu của dạng 2 phần là CT chứ không phải tên');
    expect(r.letter, 'N');

    // Một phần: chữ cái hoặc nguyên chữ của /getdata.
    expect(parseResultCell('Positive').letter, 'P');
    expect(parseResultCell('Slide Positive').letter, 'S');
    expect(parseResultCell('N').letter, 'N');
    expect(parseResultCell('').letter, '?');
    expect(parseResultCell('N/A').letter, '?', reason: '"không có dữ liệu" ≠ Negative');
  });

  test('tên bệnh chạy tới SlotResult + nhãn đồ thị', () {
    final r = TestResult.fromDeviceJson({
      'id_device': 'RPL02013',
      'CT_value': ['22.3', '0'],
      'result': ['PCV | 22.3 | P', '0.0 | N'],
    }, fetchedAt: DateTime(2026, 8, 19));

    expect(r.slots[0].name, 'PCV');
    expect(r.slots[0].classification, Classification.positive);
    expect(r.slots[0].label, 'Slot 1 - PCV');
    expect(r.slots[1].name, isEmpty);
    expect(r.slots[1].label, 'Slot 2', reason: 'máy cũ: nhãn giữ nguyên như trước');
  });

  test('tên bệnh sống sót qua lưu/đọc lại (lịch sử + data.json)', () {
    const s = SlotResult(
        index: 3,
        name: 'EHP',
        classification: Classification.positive,
        ct: 21.0,
        curve: []);
    expect(SlotResult.fromJson(s.toJson()).name, 'EHP');

    // data.json app xuất (ResultExport ghi field `name`).
    final r = TestResult.fromLooseJson({
      'slots': [
        {'index': 1, 'name': 'WSSV', 'result': 'Dương tính', 'ct': 20.0, 'data': []},
      ],
    });
    expect(r.slots.single.name, 'WSSV');
  });
}
