/// SHA-256 thuần Dart (không phụ thuộc gói ngoài, không dart:io) — dùng cho
/// trạm ATE: **vân tay của file .bin đã nạp** ghi vào hồ sơ từng máy.
///
/// Vì sao tự viết thay vì thêm `package:crypto`: repo hiện chỉ có 5 dependency,
/// `pubspec.lock` KHÔNG có `crypto`, mà thêm gói mới thì máy nào build cũng phải
/// tải được pub.dev. Thuật toán này là chuẩn cố định, ~80 dòng, có test vector
/// (`test/sha256_test.dart`) — rẻ hơn một phụ thuộc mạng.
///
/// Vì sao cần: chuỗi version KHÔNG đủ để truy vết. Bài học ghi trong
/// `docs/plan/ate-san-xuat.md` §7.1: `fbt_v2.4.5.bin` trên server từng là một
/// **image khác** với source v2.4.5, và không ai phân biệt được. Hồ sơ ATE vì
/// vậy ghi cả sha256 của đúng bytes đã nạp vào máy.
library;

import 'dart:convert';
import 'dart:typed_data';

const List<int> _k = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, //
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

int _rotr(int x, int n) => ((x >> n) | (x << (32 - n))) & 0xFFFFFFFF;

/// Mã băm SHA-256 của [input], dạng hex thường (64 ký tự).
String sha256Hex(List<int> input) {
  final h = <int>[
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, //
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ];

  // Đệm: 0x80, các byte 0, rồi độ dài BIT dạng 64-bit big-endian.
  final len = input.length;
  final padded = Uint8List(((len + 9 + 63) ~/ 64) * 64);
  padded.setRange(0, len, input);
  padded[len] = 0x80;
  final bits = len * 8;
  final view = ByteData.view(padded.buffer);
  view.setUint32(padded.length - 8, bits ~/ 0x100000000);
  view.setUint32(padded.length - 4, bits & 0xFFFFFFFF);

  final w = Int32List(64); // Int32List: giữ đúng 32 bit trên CẢ VM lẫn web
  for (var block = 0; block < padded.length; block += 64) {
    for (var i = 0; i < 16; i++) {
      w[i] = view.getUint32(block + i * 4);
    }
    for (var i = 16; i < 64; i++) {
      final s0 = _rotr(w[i - 15] & 0xFFFFFFFF, 7) ^
          _rotr(w[i - 15] & 0xFFFFFFFF, 18) ^
          ((w[i - 15] & 0xFFFFFFFF) >> 3);
      final s1 = _rotr(w[i - 2] & 0xFFFFFFFF, 17) ^
          _rotr(w[i - 2] & 0xFFFFFFFF, 19) ^
          ((w[i - 2] & 0xFFFFFFFF) >> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xFFFFFFFF;
    }

    var a = h[0], b = h[1], c = h[2], d = h[3];
    var e = h[4], f = h[5], g = h[6], hh = h[7];
    for (var i = 0; i < 64; i++) {
      final s1 = _rotr(e, 6) ^ _rotr(e, 11) ^ _rotr(e, 25);
      final ch = (e & f) ^ ((~e & 0xFFFFFFFF) & g);
      final t1 = (hh + s1 + ch + _k[i] + (w[i] & 0xFFFFFFFF)) & 0xFFFFFFFF;
      final s0 = _rotr(a, 2) ^ _rotr(a, 13) ^ _rotr(a, 22);
      final maj = (a & b) ^ (a & c) ^ (b & c);
      final t2 = (s0 + maj) & 0xFFFFFFFF;
      hh = g;
      g = f;
      f = e;
      e = (d + t1) & 0xFFFFFFFF;
      d = c;
      c = b;
      b = a;
      a = (t1 + t2) & 0xFFFFFFFF;
    }
    h[0] = (h[0] + a) & 0xFFFFFFFF;
    h[1] = (h[1] + b) & 0xFFFFFFFF;
    h[2] = (h[2] + c) & 0xFFFFFFFF;
    h[3] = (h[3] + d) & 0xFFFFFFFF;
    h[4] = (h[4] + e) & 0xFFFFFFFF;
    h[5] = (h[5] + f) & 0xFFFFFFFF;
    h[6] = (h[6] + g) & 0xFFFFFFFF;
    h[7] = (h[7] + hh) & 0xFFFFFFFF;
  }

  final sb = StringBuffer();
  for (final v in h) {
    sb.write(v.toRadixString(16).padLeft(8, '0'));
  }
  return sb.toString();
}

/// Tiện ích: băm một chuỗi UTF-8.
String sha256HexOfString(String s) => sha256Hex(utf8.encode(s));
