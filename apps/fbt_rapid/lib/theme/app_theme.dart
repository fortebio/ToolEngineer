import 'package:flutter/material.dart';

/// Design system tập trung (port từ `UI_UX_FLUTTER.md`) — MỌI màu/shadow/radius
/// khai báo ở đây, widget lấy qua `Theme.of(context).colorScheme` /
/// `Theme.of(context).extension<AppSemantic>()`. KHÔNG hardcode `Color(0x..)`
/// trong widget (trừ chữ trắng trên nút màu + palette đồ thị kSlotColors/kTempColors).

// --- Tokens (hex xấp xỉ oklch; navy primary là chính xác) ---
const kNavy = Color(0xFF0A1F47); // primary
const kAccent = Color(0xFFE0A63A); // tertiary — amber
const kAccentInk = Color(0xFF4A3410); // chữ trên nền amber
const kError = Color(0xFFD6483B);
const kSuccess = Color(0xFF2E9E6B);
const kWarning = kAccent;
const kInfo = Color(0xFF3E7BC7);

// Light
const kBg = Color(0xFFF1F2F5); // background (scaffold)
const kCard = Color(0xFFFFFFFF); // surface (thẻ)
const kFg = Color(0xFF2A2E37); // onSurface
const kBorder = Color(0xFFE7E8EB); // outlineVariant
const kMutedFg = Color(0xFF7C8088); // onSurfaceVariant
const kSunken = Color(0xFFEBECEF); // surfaceSunken (ô lõm)

// Dark (dịch cùng tông, nền xanh mực)
const kBgDark = Color(0xFF0C1220);
const kCardDark = Color(0xFF141C2E);
const kFgDark = Color(0xFFE7EAF0);
const kBorderDark = Color(0xFF263149);
const kMutedFgDark = Color(0xFF97A0B2);
const kSunkenDark = Color(0xFF0F1626);

/// Bo góc: gốc 12 (nút/input/list), thẻ 16.
class AppRadius {
  AppRadius._();
  static const double base = 12;
  static const double card = 16;
  static const double sm = 8;
}

/// Bóng thẻ xếp lớp — port của CARD_SHADOW (light; dark dùng viền là chính).
const kCardShadow = <BoxShadow>[
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 0, spreadRadius: 1),
  BoxShadow(color: Color(0x0A2A3345), blurRadius: 3, offset: Offset(0, 3)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 12, offset: Offset(0, 12)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 24, offset: Offset(0, 24)),
];

/// Màu ngữ nghĩa ngoài ColorScheme M3 — lấy qua
/// `Theme.of(context).extension<AppSemantic>()!`.
@immutable
class AppSemantic extends ThemeExtension<AppSemantic> {
  final Color success;
  final Color warning;
  final Color info;
  final Color surfaceSunken;
  const AppSemantic({
    required this.success,
    required this.warning,
    required this.info,
    required this.surfaceSunken,
  });

  static const light = AppSemantic(
      success: kSuccess, warning: kWarning, info: kInfo, surfaceSunken: kSunken);
  static const dark = AppSemantic(
      success: kSuccess,
      warning: kWarning,
      info: kInfo,
      surfaceSunken: kSunkenDark);

  /// Tiện đọc trong widget: `AppSemantic.of(context).success`.
  static AppSemantic of(BuildContext c) =>
      Theme.of(c).extension<AppSemantic>() ?? light;

  @override
  AppSemantic copyWith(
          {Color? success,
          Color? warning,
          Color? info,
          Color? surfaceSunken}) =>
      AppSemantic(
        success: success ?? this.success,
        warning: warning ?? this.warning,
        info: info ?? this.info,
        surfaceSunken: surfaceSunken ?? this.surfaceSunken,
      );

  @override
  AppSemantic lerp(ThemeExtension<AppSemantic>? other, double t) {
    if (other is! AppSemantic) return this;
    return AppSemantic(
      success: Color.lerp(success, other.success, t)!,
      warning: Color.lerp(warning, other.warning, t)!,
      info: Color.lerp(info, other.info, t)!,
      surfaceSunken: Color.lerp(surfaceSunken, other.surfaceSunken, t)!,
    );
  }
}

/// Thẻ chuẩn — recipe: nền `surface`, bo 16, viền `outlineVariant`, padding
/// 16–24, `kCardShadow`. [hover]=true cho thẻ bấm được (nhấc 2px + bóng sâu).
class AppCard extends StatefulWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;
  final VoidCallback? onTap;
  final bool hover;

  const AppCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(20),
    this.onTap,
    this.hover = false,
  });

  @override
  State<AppCard> createState() => _AppCardState();
}

class _AppCardState extends State<AppCard> {
  bool _hovering = false;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final dark = Theme.of(context).brightness == Brightness.dark;
    final lifted = widget.hover && _hovering;
    final card = AnimatedContainer(
      duration: const Duration(milliseconds: 160),
      curve: Curves.easeOutCubic,
      transform: lifted
          ? (Matrix4.identity()..translateByDouble(0, -2, 0, 1))
          : Matrix4.identity(),
      padding: widget.padding,
      decoration: BoxDecoration(
        color: cs.surface,
        borderRadius: BorderRadius.circular(AppRadius.card),
        border: Border.all(color: cs.outlineVariant),
        boxShadow: dark
            ? null
            : (lifted
                ? const [
                    BoxShadow(
                        color: Color(0x140E3F7E),
                        blurRadius: 20,
                        offset: Offset(0, 12)),
                  ]
                : kCardShadow),
      ),
      child: widget.child,
    );
    if (widget.onTap == null && !widget.hover) return card;
    return MouseRegion(
      onEnter: (_) => setState(() => _hovering = true),
      onExit: (_) => setState(() => _hovering = false),
      cursor:
          widget.onTap != null ? SystemMouseCursors.click : MouseCursor.defer,
      child: GestureDetector(onTap: widget.onTap, child: card),
    );
  }
}

/// Theme app (Material 3) — light/dark. Gọi từ `main.dart`. ColorScheme.fromSeed
/// (navy) rồi override role về token thương hiệu; đăng ký [AppSemantic].
ThemeData appTheme(Brightness b) {
  final dark = b == Brightness.dark;
  final seed = ColorScheme.fromSeed(seedColor: kNavy, brightness: b);
  final cs = dark
      ? seed.copyWith(
          tertiary: kAccent,
          onTertiary: kAccentInk,
          secondary: kAccent,
          onSecondary: kAccentInk,
          surface: kCardDark,
          onSurface: kFgDark,
          onSurfaceVariant: kMutedFgDark,
          outline: kBorderDark,
          outlineVariant: kBorderDark,
          error: kError,
        )
      : seed.copyWith(
          primary: kNavy,
          onPrimary: Colors.white,
          tertiary: kAccent,
          onTertiary: kAccentInk,
          secondary: kAccent,
          onSecondary: kAccentInk,
          primaryContainer: const Color(0xFFDCE3F1),
          onPrimaryContainer: kNavy,
          surface: kCard,
          onSurface: kFg,
          onSurfaceVariant: kMutedFg,
          outline: kBorder,
          outlineVariant: kBorder,
          error: kError,
        );

  final base =
      ThemeData(useMaterial3: true, colorScheme: cs, fontFamily: 'DM Sans');
  final rBase = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(AppRadius.base));
  OutlineInputBorder bd(Color c, [double w = 1]) => OutlineInputBorder(
        borderRadius: BorderRadius.circular(AppRadius.base),
        borderSide: BorderSide(color: c, width: w),
      );

  return base.copyWith(
    extensions: [dark ? AppSemantic.dark : AppSemantic.light],
    visualDensity: VisualDensity.standard,
    scaffoldBackgroundColor: dark ? kBgDark : kBg,
    appBarTheme: AppBarTheme(
      backgroundColor: cs.surface,
      foregroundColor: cs.onSurface,
      elevation: 0,
      scrolledUnderElevation: 1,
      centerTitle: false,
      titleTextStyle: TextStyle(
          fontFamily: 'DM Sans',
          fontSize: 20,
          fontWeight: FontWeight.w600,
          color: cs.onSurface),
    ),
    cardTheme: CardThemeData(
      elevation: 0,
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      color: cs.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(AppRadius.card),
        side: BorderSide(color: cs.outlineVariant),
      ),
    ),
    listTileTheme: ListTileThemeData(shape: rBase),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: dark ? kSunkenDark : Colors.white,
      isDense: true,
      border: bd(cs.outlineVariant),
      enabledBorder: bd(cs.outlineVariant),
      focusedBorder: bd(cs.primary, 1.5),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
          shape: rBase,
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12)),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
          shape: rBase,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12)),
    ),
    segmentedButtonTheme: SegmentedButtonThemeData(
      style: SegmentedButton.styleFrom(shape: rBase),
    ),
    dividerTheme:
        DividerThemeData(color: cs.outlineVariant, space: 1, thickness: 1),
    snackBarTheme: const SnackBarThemeData(
      behavior: SnackBarBehavior.fixed,
      showCloseIcon: true,
    ),
  );
}
