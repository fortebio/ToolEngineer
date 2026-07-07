import 'package:flutter/material.dart';

import 'screens/home_shell.dart';
import 'screens/login_screen.dart';
import 'services/app_prefs.dart';
import 'services/session_store.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppPrefs.instance.load(); // theme + ngôn ngữ đã lưu
  runApp(const RapidPlusApp());
}

class RapidPlusApp extends StatelessWidget {
  const RapidPlusApp({super.key});

  // Màu thương hiệu (xanh y tế) — giữ nguyên để không lạ mắt.
  static const _seed = Color(0xFF1565C0);

  /// Theme tinh chỉnh cho app desktop dữ liệu: nền hơi xanh-xám để tách bạch
  /// với thẻ trắng (tăng phân cấp thị giác), thẻ "viền mảnh" phẳng (clinical),
  /// input/nút bo góc đồng bộ, tiêu đề đậm vừa. Token-driven (không hardcode màu).
  static ThemeData _theme(Brightness b) {
    final cs = ColorScheme.fromSeed(seedColor: _seed, brightness: b);
    final dark = b == Brightness.dark;
    final base = ThemeData(useMaterial3: true, colorScheme: cs);
    final r10 = RoundedRectangleBorder(borderRadius: BorderRadius.circular(10));
    final r12 = RoundedRectangleBorder(borderRadius: BorderRadius.circular(12));
    OutlineInputBorder bd(Color c, [double w = 1]) => OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: BorderSide(color: c, width: w),
        );
    return base.copyWith(
      // Desktop: mật độ thoáng hơn mặc định compact, dễ đọc bảng/danh sách.
      visualDensity: VisualDensity.standard,
      scaffoldBackgroundColor: dark ? cs.surface : const Color(0xFFF5F7FA),
      appBarTheme: AppBarTheme(
        backgroundColor: dark ? cs.surface : Colors.white,
        foregroundColor: cs.onSurface,
        elevation: 0,
        scrolledUnderElevation: 1,
        centerTitle: false,
        titleTextStyle: TextStyle(
            fontSize: 20, fontWeight: FontWeight.w600, color: cs.onSurface),
      ),
      // Thẻ phẳng + viền mảnh: tách khối rõ trên nền sáng, hợp app y tế/dữ liệu.
      cardTheme: CardThemeData(
        elevation: 0,
        margin: EdgeInsets.zero,
        clipBehavior: Clip.antiAlias,
        color: dark ? cs.surfaceContainerLow : Colors.white,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: cs.outlineVariant),
        ),
      ),
      listTileTheme: ListTileThemeData(shape: r12),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: dark ? cs.surfaceContainerHighest : Colors.white,
        isDense: true,
        border: bd(cs.outlineVariant),
        enabledBorder: bd(cs.outlineVariant),
        focusedBorder: bd(cs.primary, 1.5),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
            shape: r10, padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12)),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
            shape: r10, padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12)),
      ),
      dividerTheme: DividerThemeData(color: cs.outlineVariant, space: 1, thickness: 1),
      // Mọi thông báo (SnackBar) có nút X để tắt, ghim sát đáy màn hình.
      snackBarTheme: const SnackBarThemeData(
        behavior: SnackBarBehavior.fixed,
        showCloseIcon: true,
      ),
    );
  }

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
        theme: _theme(Brightness.light),
        darkTheme: _theme(Brightness.dark),
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
