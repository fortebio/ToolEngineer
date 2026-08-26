// Hàng tiêu đề của AppTabScaffold không được tràn khi cửa sổ hẹp.
//
// Vì sao cần test riêng: bản trước là `Row` với cụm hành động bọc trong `Wrap`,
// kèm comment khẳng định "nó xuống dòng thay vì gây RenderFlex overflow".
// Comment đó SAI và không ai kiểm — `RenderFlex` cấp cho con không-flex ràng
// buộc `maxWidth: infinity`, nên `Wrap` bên trong luôn thấy bề rộng vô hạn và
// xếp hết vào một dòng. Bài học: một lời khẳng định trong comment không phải
// bằng chứng; hãy pump nó ở bề rộng thật.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/theme/app_theme.dart';
import 'package:RapidPlusApp/widgets/app_tab_scaffold.dart';

Widget _man({required double rong, required List<Widget> actions}) => MaterialApp(
      theme: appTheme(),
      home: Center(
        child: SizedBox(
          width: rong,
          height: 600,
          child: AppTabScaffold(
            title: 'Quản lý tài khoản',
            subtitle: 'Tạo tài khoản, đặt vai trò và cấp mã máy được xem.',
            index: 0,
            onChanged: (_) {},
            actions: actions,
            tabs: [
              AppTab(
                icon: Icons.manage_accounts_outlined,
                label: 'Quản lý tài khoản',
                page: const SizedBox.expand(),
              ),
            ],
          ),
        ),
      ),
    );

void main() {
  // Bề rộng nội dung thật = bề rộng cửa sổ trừ 96px thanh nav. 360 ứng với cửa
  // sổ ~456px — hẹp nhưng người ta kéo tới đó thật khi xếp hai cửa sổ cạnh nhau.
  const heps = [360.0, 420.0, 560.0, 900.0];

  testWidgets('không tràn ở mọi bề rộng, kể cả khi có hành động',
      (tester) async {
    for (final w in heps) {
      await tester.pumpWidget(_man(rong: w, actions: [
        IconButton(onPressed: () {}, icon: const Icon(Icons.refresh)),
        FilledButton.icon(
          onPressed: () {},
          icon: const Icon(Icons.person_add),
          label: const Text('Tạo tài khoản'),
        ),
      ]));
      await tester.pump();
      expect(tester.takeException(), isNull,
          reason: 'hàng tiêu đề tràn ở bề rộng $w');
    }
  });

  testWidgets('không có hành động thì vẫn không tràn', (tester) async {
    for (final w in heps) {
      await tester.pumpWidget(_man(rong: w, actions: const []));
      await tester.pump();
      expect(tester.takeException(), isNull, reason: 'tràn ở bề rộng $w');
    }
  });

  testWidgets('một mục thì KHÔNG hiện dải chọn', (tester) async {
    // Một nút đơn độc trông như điều khiển hỏng.
    await tester.pumpWidget(_man(rong: 900, actions: const []));
    await tester.pump();
    expect(find.byType(SegmentedButton<int>), findsNothing);
  });
}
