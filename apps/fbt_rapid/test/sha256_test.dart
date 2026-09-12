import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/util/sha256.dart';

// Test vector CHUẨN của SHA-256 (FIPS 180-4). Tự viết thuật toán thì đây là
// thứ duy nhất chứng minh nó đúng — hồ sơ ATE ghi mã băm này làm vân tay của
// firmware đã nạp, sai một bit là truy vết vô nghĩa.
void main() {
  test('vector chuẩn', () {
    expect(sha256HexOfString(''),
        'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
    expect(sha256HexOfString('abc'),
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    // 56 byte — đúng mốc phải đẩy phần đệm sang khối thứ hai (chỗ dễ sai nhất).
    expect(
        sha256HexOfString(
            'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
        '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
    // 1 triệu ký tự 'a' — vector nhiều khối, cũng là cỡ gần với file .bin thật.
    expect(sha256Hex(List<int>.filled(1000000, 0x61)),
        'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0');
  });

  test('dữ liệu nhị phân', () {
    // Byte > 0x7F phải là số nguyên KHÔNG DẤU, không phải ký tự.
    final h = sha256Hex(const [0x00, 0xFF, 0x80, 0x7F]);
    expect(h.length, 64);
    expect(RegExp(r'^[0-9a-f]{64}$').hasMatch(h), isTrue);
    // Hai nội dung khác nhau ở đúng biên khối vẫn phải ra hai mã khác nhau.
    expect(sha256Hex(List<int>.filled(63, 0x61)) ==
        sha256Hex(List<int>.filled(64, 0x61)), isFalse);
  });

  test('băm chuỗi = băm bytes UTF-8', () {
    const s = 'Forte Rapid+ RPL02013 — nạp firmware';
    expect(sha256HexOfString(s), sha256Hex(utf8.encode(s)));
  });
}
