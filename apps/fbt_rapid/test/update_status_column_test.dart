// Cột "Trạng thái update" của bảng *Trạng thái máy*: vừa BÁO trạng thái, vừa LÀ nút chọn
// firmware cho từng máy.
//
// Đo màu ĐÃ RENDER, không đọc cấu hình — bài học đắt nhất của repo này (CLAUDE.md): một test
// đọc `Theme.of(ctx).colorScheme` từng PASS trong khi lỗi màu sống nguyên, vì chữ không lấy
// màu từ đó.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/models/user_session.dart';
import 'package:RapidPlusApp/screens/manager_machine_screen.dart';
import 'package:RapidPlusApp/services/app_settings.dart';
import 'package:RapidPlusApp/services/session_store.dart';
import 'package:RapidPlusApp/theme/app_theme.dart';

// RPL03003: GHIM riêng fbt_v2.4.4.bin, đang chạy v2.4.5 -> Riêng (đỏ) · Chờ nạp
// RPL02013: không ghim, bản chung fbt_v2.4.5.bin, chạy v2.4.5 -> Chung (xanh) · Đúng bản
// RPL01001: không ghim, chạy v2.4.3 -> dưới v2.4.4 nên KHÔNG ghim riêng được
// RPL04004: không ghim, chạy v2.4.6 -> bản KHÁC, để phân biệt menu lấy version của máy nào
Future<HttpServer> _fake() async {
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  server.listen((req) async {
    Object body;
    switch (req.uri.path) {
      case '/ota':
        body = {
          'target': 'fbt_v2.4.5.bin',
          'devices': {
            'RPL03003': {
              'file': 'fbt_v2.4.4.bin',
              'by': 'dat',
              'at': '2026-08-18T02:00:00Z'
            }
          },
          'files': [
            {'name': 'fbt_v2.4.5.bin', 'size': 2193408, 'modified': '2026-08-19T03:15:00Z'},
            {'name': 'fbt_v2.4.4.bin', 'size': 2190112, 'modified': '2026-08-12T09:00:00Z'},
          ],
        };
        break;
      case '/devices':
        body = [
          {'id_device': 'RPL03003', 'sessions': 12, 'last_seen': '2026-08-19T07:12:00Z', 'version': 'v2.4.5'},
          {'id_device': 'RPL02013', 'sessions': 7, 'last_seen': '2026-08-19T06:00:00Z', 'version': 'v2.4.5'},
          {'id_device': 'RPL01001', 'sessions': 3, 'last_seen': '2026-06-01T01:02:00Z', 'version': 'v2.4.3'},
          // Chay ban KHAC hai may tren: khong co no thi gieo loi "lay version cua hang dau"
          // van xanh, vi ca hai deu v2.4.5.
          {'id_device': 'RPL04004', 'sessions': 9, 'last_seen': '2026-08-19T05:00:00Z', 'version': 'v2.4.6'},
        ];
        break;
      default:
        body = {'total': 0, 'page': 1, 'limit': 10, 'items': <dynamic>[]};
    }
    req.response
      ..headers.contentType = ContentType.json
      ..write(jsonEncode(body));
    await req.response.close();
  });
  return server;
}

/// WCAG 2.x. `computeLuminance()` của Flutter đã là relative luminance đúng chuẩn — dùng
/// đúng cách `test/theme_contrast_test.dart` đang làm, đừng tự viết lại sRGB→linear.
double _ratio(Color a, Color b) {
  final l1 = a.computeLuminance(), l2 = b.computeLuminance();
  final hi = l1 > l2 ? l1 : l2;
  final lo = l1 > l2 ? l2 : l1;
  return (hi + 0.05) / (lo + 0.05);
}

void main() {
  late HttpServer server;
  late AppSettings settings;

  // flutter_test cắm HttpOverrides trả 400 cho MỌI request -> màn chỉ hiện lỗi đỏ.
  setUpAll(() => HttpOverrides.global = null);

  setUp(() async {
    SharedPreferences.setMockInitialValues({});
    server = await _fake();
    settings = AppSettings(engineerUrl: 'http://127.0.0.1:${server.port}');
    SessionStore.current = const UserSession(
        username: 'root',
        name: 'Root',
        role: UserRole.root,
        ids: ['*'],
        allowAll: true);
  });

  tearDown(() async {
    await server.close(force: true);
    SessionStore.current = null;
  });

  Future<void> moBang(WidgetTester tester,
      {Size size = const Size(1280, 900)}) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    // HTTP thật cần THỜI GIAN THẬT: đồng hồ của pumpAndSettle là giả, nó tua thẳng qua
    // timeout 20 s của FbtApi trước khi request kịp về.
    await tester.runAsync(() async {
      await tester.pumpWidget(MaterialApp(
        theme: appTheme(),
        home: Scaffold(body: ManagerMachineScreen(settings: settings)),
      ));
      await Future<void>.delayed(const Duration(milliseconds: 400));
      await tester.pump();
    });
    await tester.pumpAndSettle();
    await tester.tap(find.text('Trạng thái máy'));
    await tester.pumpAndSettle();
  }

  /// Màu THẬT SỰ được vẽ cho một đoạn chữ.
  Color mauChu(WidgetTester t, Finder f) =>
      t.renderObject<RenderParagraph>(f).text.style!.color!;

  testWidgets('tone XANH cho máy theo bản chung, ĐỎ cho máy ghim riêng',
      (tester) async {
    await moBang(tester);

    expect(find.text('Chung'), findsNWidgets(3)); // RPL02013 + RPL01001 + RPL04004
    expect(find.text('Riêng'), findsOneWidget); // RPL03003

    final xanh = mauChu(tester, find.text('Chung').first);
    final do_ = mauChu(tester, find.text('Riêng'));
    expect(xanh, isNot(do_), reason: 'hai chế độ phải khác tone');
    // Ghim theo THÀNH PHẦN màu, không theo hằng số: đổi bảng màu thương hiệu không được làm
    // test đỏ, nhưng đảo ngược xanh/đỏ thì phải.
    expect(xanh.g, greaterThan(xanh.r), reason: 'chế độ Chung phải là tone xanh');
    expect(do_.r, greaterThan(do_.g), reason: 'chế độ Riêng phải là tone đỏ');
  });

  testWidgets('chữ trên chip vẫn đọc được — chip là tone pha 12% trên thẻ trắng',
      (tester) async {
    await moBang(tester);
    for (final nhan in const ['Chung', 'Riêng']) {
      final chu = mauChu(tester, find.text(nhan).first);
      final nenChip =
          Color.alphaBlend(chu.withValues(alpha: 0.12), const Color(0xFFFFFFFF));
      expect(_ratio(chu, nenChip), greaterThan(4.5),
          reason: '$nhan: chữ trên chip trượt chuẩn AA');
    }
  });

  testWidgets('CANH GÁC: phép đo trên thật sự bắt được cặp màu xấu', (tester) async {
    // Không có ca này thì ca trên có thể xanh vì một lý do vô nghĩa (vd `_ratio` luôn trả
    // số lớn). Cặp dưới đây trượt chắc chắn: tone nhạt trên nền pha của chính nó.
    const nhat = Color(0xFF9AD3B0);
    final nen = Color.alphaBlend(nhat.withValues(alpha: 0.12), const Color(0xFFFFFFFF));
    expect(_ratio(nhat, nen), lessThan(4.5));
  });

  testWidgets('trạng thái: đúng bản vs chờ nạp, và tên bản SẼ nạp hiện ra',
      (tester) async {
    await moBang(tester);
    // RPL02013 chạy v2.4.5, bản chung fbt_v2.4.5.bin -> đúng bản.
    expect(find.textContaining('Đúng bản · fbt_v2.4.5.bin'), findsWidgets);
    // RPL03003 chạy v2.4.5 nhưng bị ghim fbt_v2.4.4.bin -> vẫn chờ nạp bản GHIM.
    // Tức ghim THẮNG bản chung, đúng ngữ nghĩa `/ota/check` phía server.
    expect(find.textContaining('Chờ nạp · fbt_v2.4.4.bin'), findsOneWidget);
  });

  testWidgets('máy < v2.4.4 KHÔNG bấm chọn được, nhưng vẫn hiện trạng thái',
      (tester) async {
    await moBang(tester);
    // 4 máy, chỉ 3 đủ điều kiện ghim riêng -> đúng 3 mũi tên.
    expect(find.byIcon(Icons.keyboard_arrow_down), findsNWidgets(3));
    // Máy cũ vẫn phải có chip: ẩn đi thì người vận hành tưởng thiếu dữ liệu.
    expect(find.text('Chung'), findsNWidgets(3));
  });

  testWidgets('ô chọn có VIỀN thật, và máy không bấm được thì viền nhạt hẳn',
      (tester) async {
    // Đây là TOÀN BỘ phương án D: cả ô mượn hình dạng ô nhập của app nên đọc ra là control.
    // Đọc `decoration` là hợp lệ ở đây (khác bài học màu chữ): viền không thừa kế qua cây
    // widget nào cả — thứ Container khai chính là thứ được vẽ.
    await moBang(tester);

    BorderSide vien(String id) {
      final c = tester.widget<Container>(find.byKey(ValueKey('updBox-$id')));
      return ((c.decoration! as BoxDecoration).border! as Border).top;
    }

    final batDuoc = vien('RPL02013'); // v2.4.5 -> ghim riêng được
    final tro = vien('RPL01001'); // v2.4.3 -> không ghim được

    expect(batDuoc.style, BorderStyle.solid);
    expect(batDuoc.color, isNot(tro.color),
        reason: 'ô bấm được và ô trơ phải khác viền');
    // Viền của ô BẤM ĐƯỢC phải ĐẬM hơn — đậm = tối hơn trên nền sáng. Đây chính là tín hiệu
    // "bấm được"; nhạt bằng nhau là quay lại đúng lỗi người dùng báo.
    expect(batDuoc.color.computeLuminance(),
        lessThan(tro.color.computeLuminance()),
        reason: 'viền ô bấm được phải đậm hơn viền ô trơ');
    // Và nó phải tách được khỏi nền thẻ, không chỉ tách khỏi ô trơ.
    expect(_ratio(batDuoc.color, const Color(0xFFFFFFFF)), greaterThan(3.0),
        reason: 'viền control cần ≥3:1 trên thẻ (WCAG 1.4.11)');

    // CHEVRON KHÔNG ĐƯỢC MỜ. Đây đúng là lỗi người dùng báo ("mũi tên hơi khó nhìn"): bản
    // đầu tô nó bằng màu chữ MỜ, cùng cỡ cùng màu với chữ bên cạnh nên nó tan vào nền. Nó
    // là phần nói "đây là control" nên phải mang màu chữ CHÍNH.
    final chev = tester
        .widget<Icon>(find.byIcon(Icons.keyboard_arrow_down).first)
        .color!;
    expect(_ratio(chev, const Color(0xFFFFFFFF)), greaterThan(7.0),
        reason: 'chevron mờ là quay lại đúng lỗi đã sửa');
    // Ghim chặt hơn: nó phải ĐẬM HƠN chữ phụ trong cùng ô.
    final chuPhu = mauChu(tester, find.textContaining('Đúng bản').first);
    expect(chev.computeLuminance(), lessThan(chuPhu.computeLuminance()),
        reason: 'chevron phải đậm hơn dòng trạng thái, không bằng');
  });

  testWidgets('mọi ô chọn CÙNG kích thước, kể cả ô trơ', (tester) async {
    await moBang(tester);
    final r = [
      for (final id in const ['RPL03003', 'RPL02013', 'RPL01001'])
        tester.getRect(find.byKey(ValueKey('updBox-$id')))
    ];
    // Bề ngang: co theo độ dài tên file thì mép phải 95 hàng răng cưa mỗi hàng một kiểu.
    expect(r[1].width, closeTo(r[0].width, 0.5));
    expect(r[2].width, closeTo(r[0].width, 0.5),
        reason: 'ô TRƠ cũng phải cùng bề ngang, không co lại');
    // Chiều cao: mỗi ô luôn là chip + đúng MỘT dòng trạng thái (ellipsis, không vỡ dòng).
    expect(r[1].height, closeTo(r[0].height, 0.5));
    expect(r[2].height, closeTo(r[0].height, 0.5));
    // Và mép trái phải thẳng cột.
    expect(r[1].left, closeTo(r[0].left, 0.5));
    expect(r[2].left, closeTo(r[0].left, 0.5));

    // Chevron phải BÁM MÉP PHẢI của ô, không trôi theo độ dài tên file — nếu không thì ô
    // rộng bằng nhau mà chỗ bấm vẫn nhảy chỗ từng hàng, tức chưa giải quyết được gì.
    for (final id in const ['RPL03003', 'RPL02013']) {
      final hop = tester.getRect(find.byKey(ValueKey('updBox-$id')));
      final chev = tester.getRect(find.descendant(
        of: find.byKey(ValueKey('updBox-$id')),
        matching: find.byIcon(Icons.keyboard_arrow_down),
      ));
      expect(hop.right - chev.right, lessThan(8.0),
          reason: '$id: chevron phải sát mép phải ô');
    }
  });

  testWidgets('bấm vào cột mở menu chọn bản', (tester) async {
    await moBang(tester);
    await tester.tap(find.byIcon(Icons.keyboard_arrow_down).first);
    await tester.pumpAndSettle();
    // Menu phải có cả "theo bản chung" lẫn từng file .bin.
    expect(find.textContaining('fbt_v2.4.4.bin'), findsWidgets);
    expect(find.textContaining('fbt_v2.4.5.bin'), findsWidgets);
  });

  testWidgets('menu NÓI RÕ máy đang chạy bản nào', (tester) async {
    // Đứng ở menu này là để chọn bản KHÁC — không biết máy đang chạy gì thì không biết
    // mình đang nâng hay đang HẠ. Menu cũ không nhận `CloudDevice` nên không nói được.
    await moBang(tester);
    await tester.tap(find.byIcon(Icons.keyboard_arrow_down).first); // RPL03003, v2.4.5
    await tester.pumpAndSettle();

    expect(find.text('Máy đang chạy: v2.4.5'), findsOneWidget);
    // Và đánh dấu ĐÚNG dòng file khớp bản đang chạy — dấu tích đã mang nghĩa "đang GHIM"
    // (ở đây là fbt_v2.4.4.bin), hai thứ khác nhau nên không dùng chung ký hiệu.
    expect(find.text('· đang chạy'), findsOneWidget);
    final chay = tester.getRect(find.text('· đang chạy'));
    final f245 = tester.getRect(find.text('fbt_v2.4.5.bin').last);
    expect((chay.center.dy - f245.center.dy).abs(), lessThan(4),
        reason: 'nhãn "đang chạy" phải nằm ở dòng fbt_v2.4.5.bin');
  });

  testWidgets('máy nào mở menu thì hiện version CỦA MÁY ĐÓ', (tester) async {
    await moBang(tester);
    // Mũi tên CUỐI = RPL04004 (v2.4.6) — bản khác hẳn hai máy trên. Ca này chặn kiểu lỗi
    // "menu lấy version của hàng đầu bảng"; fixture mà để mọi máy cùng version thì gieo
    // đúng lỗi đó test vẫn xanh (đã dính thật).
    await tester.tap(find.byIcon(Icons.keyboard_arrow_down).last);
    await tester.pumpAndSettle();
    expect(find.text('Máy đang chạy: v2.4.6'), findsOneWidget);
    expect(find.text('Máy đang chạy: v2.4.5'), findsNothing);
  });

  testWidgets('khổ hẹp 820px: không tràn, chữ không vỡ dòng', (tester) async {
    await moBang(tester, size: const Size(820, 900));
    expect(tester.takeException(), isNull);
    expect(tester.getRect(find.text('Riêng')).height, lessThan(26));
  });
}
