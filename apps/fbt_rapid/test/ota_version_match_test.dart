import 'package:RapidPlusApp/screens/manager_machine_screen.dart';
import 'package:flutter_test/flutter_test.dart';

/// Đối chiếu version để tính tiến độ triển khai OTA.
///
/// Bối cảnh: server KHÔNG hiểu ngữ nghĩa version — nó chỉ coi TÊN FILE là "bản mục
/// tiêu". Muốn biết máy nào đã lên bản mới thì app phải tự rút version từ tên file
/// rồi so với version máy gửi kèm lần đo gần nhất.
void main() {
  group('versionInFileName', () {
    test('rút được version từ các kiểu tên thường dùng', () {
      expect(versionInFileName('fbt_v2.4.4.bin'), '2.4.4');
      expect(versionInFileName('FBT_V2.4.4.BIN'), '2.4.4'); // không phân biệt hoa/thường
      expect(versionInFileName('rapid-2.4.4.bin'), '2.4.4'); // không có chữ 'v'
      expect(versionInFileName('fw_v2.4.bin'), '2.4'); // 2 nhóm số cũng nhận
      expect(versionInFileName('fbt_v10.0.12_beta.bin'), '10.0.12');
    });

    test('tên KHÔNG mang version -> null (UI phải cảnh báo, không đoán bừa)', () {
      expect(versionInFileName('firmware.bin'), isNull);
      expect(versionInFileName('ota.bin'), isNull);
      // '.bin' bị cắt trước khi dò nên không nhặt nhầm số trong đuôi file
      expect(versionInFileName('build.bin'), isNull);
    });
  });

  group('isValidOtaVersion + otaFileNameFor (app tự đặt tên file khi upload)', () {
    test('nhận version hợp lệ, có/không chữ v, có hậu tố', () {
      for (final v in ['2.4.4', 'v2.4.4', 'V2.4.4', '2.4', '10.0.12', '2.4.4AT', '2.4.4_rc1']) {
        expect(isValidOtaVersion(v), isTrue, reason: v);
      }
      expect(isValidOtaVersion('  2.4.4  '), isTrue); // trim
    });

    test('chặn thứ server sẽ từ chối hoặc app không đối chiếu được', () {
      for (final v in [
        '', '   ',
        '2', 'v2', // 1 nhóm số -> không đủ để so version
        'abc', 'v', 'v.2.4',
        '2.4.4 beta', // khoảng trắng -> safe_name phía server sẽ đổi -> 400
        '2.4.4/x', '2.4.4@1', '../2.4.4', // ký tự bẩn / traversal
      ]) {
        expect(isValidOtaVersion(v), isFalse, reason: '"$v" phải bị chặn');
      }
    });

    test('tên file sinh ra luôn mang version và đọc lại được', () {
      expect(otaFileNameFor('2.4.4'), 'fbt_v2.4.4.bin');
      expect(otaFileNameFor('v2.4.4'), 'fbt_v2.4.4.bin'); // không thành 'vv'
      expect(otaFileNameFor(' V2.4.4 '), 'fbt_v2.4.4.bin');
      // Vòng tròn khép kín: tên app sinh ra phải rút lại được đúng version
      expect(versionInFileName(otaFileNameFor('2.4.4')), '2.4.4');
      expect(versionInFileName(otaFileNameFor('10.0.12')), '10.0.12');
      // Và máy chạy đúng version đó phải được tính là ĐÃ cập nhật
      expect(isDeviceOnTarget('v2.4.4', otaFileNameFor('2.4.4')), isTrue);
    });
  });

  group('isDeviceOnTarget', () {
    test('khớp version -> đã cập nhật', () {
      expect(isDeviceOnTarget('v2.4.4', 'fbt_v2.4.4.bin'), isTrue);
      expect(isDeviceOnTarget('2.4.4', 'fbt_v2.4.4.bin'), isTrue);
      expect(isDeviceOnTarget('V2.4.4', 'fbt_v2.4.4.bin'), isTrue);
      expect(isDeviceOnTarget('  v2.4.4  ', 'fbt_v2.4.4.bin'), isTrue);
    });

    test('lệch version -> chưa cập nhật', () {
      expect(isDeviceOnTarget('v2.4.3', 'fbt_v2.4.4.bin'), isFalse);
      expect(isDeviceOnTarget('v2.4.40', 'fbt_v2.4.4.bin'), isFalse);
    });

    test('bản có HẬU TỐ không được coi là đã cập nhật', () {
      // Kho log thật có đủ kiểu 'v2.4.3AT', '_loop', 'vtest'... Đó là build KHÁC.
      // Thà báo THIẾU còn hơn báo "đã xong" sai rồi bỏ sót máy chưa nạp.
      expect(isDeviceOnTarget('v2.4.4AT', 'fbt_v2.4.4.bin'), isFalse);
      expect(isDeviceOnTarget('v2.4.4_loop', 'fbt_v2.4.4.bin'), isFalse);
    });

    test('không kết luận được -> null, KHÔNG phải false', () {
      // Tên file không mang version
      expect(isDeviceOnTarget('v2.4.4', 'firmware.bin'), isNull);
      // Máy chưa báo version lần nào
      expect(isDeviceOnTarget('', 'fbt_v2.4.4.bin'), isNull);
      expect(isDeviceOnTarget('   ', 'fbt_v2.4.4.bin'), isNull);
    });
  });
}
