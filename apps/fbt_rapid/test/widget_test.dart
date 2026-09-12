// Smoke test cơ bản: app khởi động và hiện đủ các tab điều hướng.

import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:RapidPlusApp/main.dart';

void main() {
  testWidgets('App khởi động và hiện các tab điều hướng', (tester) async {
    SharedPreferences.setMockInitialValues({});

    await tester.pumpWidget(const RapidPlusApp());
    // HomeShell load AppSettings (Future) rồi mới dựng NavigationRail.
    // Không dùng pumpAndSettle vì có CircularProgressIndicator (animation vô hạn).
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    expect(find.text('Cloud'), findsOneWidget);
    expect(find.text('Cài đặt'), findsWidgets);
  });
}
