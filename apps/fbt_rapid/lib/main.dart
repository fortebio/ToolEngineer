import 'package:flutter/material.dart';

import 'screens/home_shell.dart';
import 'screens/login_screen.dart';
import 'services/app_prefs.dart';
import 'services/session_store.dart';
import 'theme/app_theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppPrefs.instance.load(); // theme + ngôn ngữ đã lưu
  runApp(const RapidPlusApp());
}

class RapidPlusApp extends StatelessWidget {
  const RapidPlusApp({super.key});


  @override
  Widget build(BuildContext context) {
    // Rebuild khi đổi theme/ngôn ngữ (AppPrefs là ChangeNotifier toàn cục).
    return AnimatedBuilder(
      animation: AppPrefs.instance,
      builder: (context, _) => MaterialApp(
        // Đổi ngôn ngữ → key đổi → rebuild toàn bộ để dịch lại mọi `tr(...)`.
        // (Theme đổi qua prop bên dưới nên KHÔNG cần reset.)
        key: ValueKey('locale_${AppPrefs.instance.localeCode}'),
        title: 'FBT_RAPID',
        debugShowCheckedModeBanner: false,
        theme: appTheme(Brightness.light),
        darkTheme: appTheme(Brightness.dark),
        themeMode: AppPrefs.instance.themeMode,
        home: const _AuthGate(),
      ),
    );
  }
}

/// Quyết định màn khởi động: khôi phục phiên đã lưu → nếu còn thì vào thẳng
/// [HomeShell], chưa đăng nhập thì hiện [LoginScreen].
class _AuthGate extends StatefulWidget {
  const _AuthGate();

  @override
  State<_AuthGate> createState() => _AuthGateState();
}

class _AuthGateState extends State<_AuthGate> {
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    SessionStore.load().then((_) {
      if (mounted) setState(() => _loading = false);
    });
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }
    return SessionStore.current == null
        ? const LoginScreen()
        : const HomeShell();
  }
}
