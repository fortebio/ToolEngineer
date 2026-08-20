import 'package:flutter/material.dart';

/// Design system tập trung — MỌI màu/shadow/radius khai báo ở đây, widget lấy qua
/// `Theme.of(context).colorScheme` / `AppSemantic.of(context)`. KHÔNG hardcode
/// `Color(0x..)` trong widget (trừ chữ trắng trên nút màu + palette đồ thị
/// `kSlotColors`/`kTempColors`).
///
/// ## Nguồn màu: dashboard web của FIRMWARE, không phải bảng màu mới
///
/// Toàn bộ hệ màu port thẳng từ `FBT-DXD243/data/style.css` — giao diện web mà
/// **chính máy phục vụ** khi kỹ thuật viên quét QR. Lý do không tự chọn màu:
/// người dùng thấy hai giao diện này **cạnh nhau** (điện thoại cầm tay + app trên
/// máy tính) trong cùng một buổi làm việc, nên chúng phải là một hệ.
///
/// Bảng màu đó đã được soát tương phản và ghi tỉ lệ đo được ngay trong CSS. Đã
/// kiểm lại toàn bộ khi port, mọi cặp đạt WCAG AA:
///
/// | cặp | tỉ lệ |
/// |---|---|
/// | `kFg` trên `kCard` | 16.46:1 |
/// | `kMutedFg` trên `kCard` | 7.01:1 |
/// | `kMutedFg` trên `kBg` (nền trang) | 5.64:1 |
/// | `kOutline` trên `kCard` (viền ô nhập) | 4.08:1 |
/// | `kErrorSnackBg` + chữ SnackBar | 8.80:1 |
/// | trắng trên `kBrandInk` (CTA) | 7.40:1 |
/// | `kSuccess` trên `kCard` | 6.37:1 |
/// | `kError` trên `kCard` | 6.04:1 |
///
/// ⚠️ **`kBrand` (cyan logo) chỉ 2.09:1 trên trắng — TUYỆT ĐỐI không dùng làm
/// chữ.** Nó là màu icon/mark/focus. Cần chữ mang màu thương hiệu thì dùng
/// `kBrandInk`. Đây là thiết bị y tế: đọc được thắng khớp swatch.
///
/// ## Tông: soft — mang bằng HÌNH, không bằng màu
///
/// Bo góc lớn, bóng khuếch tán, mật độ thoáng, không serif. Màu thì khoá theo
/// thương hiệu ở trên. Tách hai thứ đó ra là cách giữ được tông mà không phải
/// bịa một bảng màu thứ hai cho cùng một sản phẩm.

// ─── Thương hiệu Forte Biotech (đo từ logo, hue 183°) ───
const kBrand = Color(0xFF20C6D0); // cyan logo — CHỈ icon/mark/focus, KHÔNG chữ
const kBrandMint = Color(0xFF21DDBC); // mint — nhấn phụ
const kBrandTeal = Color(0xFF13A2BF); // teal đậm — mảng UI lớn
const kBrandInk = Color(0xFF0F5F63); // primary — chữ/CTA trên trắng, 7.40:1
const kBrandInkHi = Color(0xFF0A4448); // CTA hover — trắng 10.86:1
const kBrandDeep = Color(0xFF083336); // nền tối (header) — chữ trắng 11.99:1

// ─── Sáng ───
// --- Console / log ---
// Nền TỐI ở CẢ HAI theme, cố ý: log esptool và luồng UART thô là chữ đơn sắc
// canh cột, đọc trên nền tối đỡ mỏi hơn — và đó cũng là thứ mọi terminal khác
// trên máy người dùng đang làm. Trước 2026-08-19 ba màn (Nạp code desktop + web,
// UART thô) mỗi màn tự chép `#1E1E1E`/`#D4D4D4` của VS Code.
// Đổi sang tông ám thương hiệu hue 183° cho cùng hệ với phần còn lại; đo lại
// thì còn SÁNG HƠN bản cũ: 13.15:1 (cũ 11.25:1).
const kConsoleBg = Color(0xFF0C2126);
const kConsoleFg = Color(0xFFD6E8EA); // 13.15:1 trên kConsoleBg
const kConsoleDim = Color(0xFF8FB4B9); // chữ phụ/nhãn trong log — 7.45:1

// Nền trang — **chủ dự án chọn trực tiếp** (2026-08-19). Xám lạnh trung tính:
// hue 225°, bão hoà 17%, sáng 95%.
//
// Hai bản trước và vì sao bỏ: `#F2F8F9` quá nhạt (thẻ/nền chỉ 1.073, giao diện
// đọc ra một mảng trắng mờ); `#DCE9EB` đủ đậm nhưng bão hoà 27% nên mảng teal
// trải kín màn gây bí mắt. Bản này tách thẻ/nền **1.119** — nhạt hơn hai bản kia
// nên **mép thẻ dựa vào VIỀN + BÓNG chứ không chỉ vào bậc sáng**; `kBorder` trên
// nền này được 1.35, đủ vẽ ra mép. Test ghim CẢ HAI vế, đừng bỏ vế nào.
const kBg = Color(0xFFF1F2F5);
const kCard = Color(0xFFFFFFFF);
const kFg = Color(0xFF16202B); // 16.46:1 trên thẻ, 13.25:1 trên nền
const kMutedFg = Color(0xFF4F5A68); // thẻ 7.01 · nền 5.64 · chip 4.85 · ô phụ 6.22

// HAI đường kẻ khác nhau, đừng gộp lại làm một (bản trước dùng CHUNG `kBorder`
// cho cả hai, nên viền điều khiển nhạt y như đường kẻ trang trí):
//   `kBorder`  = outlineVariant — hairline chia khối, chỉ cần thấy thoáng.
//   `kOutline` = outline        — viền ô nhập/nút viền, là thứ NÓI RA "gõ/bấm
//                                 được vào đây", nên phải đạt 3:1 (WCAG 1.4.11).
const kBorder = Color(0xFFC0D6DA);
const kOutline = Color(0xFF57858E); // thẻ 4.08 · nền 3.28

// Nền chip + tiêu đề bảng + đĩa icon ở màn rỗng. Phải tách khỏi CẢ hai phía:
// thẻ trắng (1.45) và nền trang (1.16). Bản #E3EFF1 chỉ hơn nền trang 1.06 nên
// đĩa icon màn rỗng gần như tan vào nền — thấy ngay khi làm nền đậm lên.
const kSunken = Color(0xFFC7DADE);
const kMuted = Color(0xFFEAF3F5); // nền phụ
const kMutedInk = Color(0xFF23383D);
const kPrimaryContainer = Color(0xFFC3E4E7);
const kOnPrimaryContainer = kBrandInkHi; // 9.64:1 trên container

// ─── Trạng thái ───
const kSuccess = Color(0xFF0E6E32); // 6.37:1 trên thẻ
const kWarning = Color(0xFFB45309); // 5.02:1 trên thẻ
const kError = Color(0xFFC21C1C); // 6.04:1 trên thẻ
// Nền SnackBar LỖI. Không dùng `colorScheme.error` cho việc này: nền SnackBar
// bị ép TỐI (xem `snackBarTheme`) và `contentTextStyle` ghim cứng #EAF1F8, nên
// đặt chữ gần-trắng đó lên `kError` chỉ được 4.24:1. Màu này tối hẳn → 8.80:1.
const kErrorSnackBg = Color(0xFF7F1D1D);
const kInfo = kBrandInk;

/// Bo góc — tông **soft**: to hơn bản trước (12/16) và khớp `--radius:14px` của
/// dashboard firmware, nên hai giao diện đặt cạnh nhau đọc ra cùng một họ.
/// Dưới bề rộng này thì coi là **điện thoại**: thanh điều hướng chuyển xuống
/// dưới, bảng nhiều cột đổi sang thẻ xếp dọc, lề co lại.
///
/// 640 chứ không phải 400: máy tính bảng dựng dọc (768) và điện thoại xoay ngang
/// (~740–930) vẫn đủ chỗ cho bố cục rộng; còn 600–640 là chỗ bảng 5 cột của
/// *Trạng thái máy* bắt đầu dính chữ vào nhau (đo được ở 785px cửa sổ thì cột
/// "Runs" và "Last upload" đã chạm nhau).
///
/// **Một hằng số DUY NHẤT** cho mọi màn — hai breakpoint lệch nhau là kiểu lỗi
/// chỉ lộ ra ở đúng một dải bề rộng mà không ai nghĩ tới việc thử.
const double kMobileMaxWidth = 640;

/// Máy đang xem có phải khổ điện thoại không. Dùng `MediaQuery` chứ không phải
/// `LayoutBuilder` khi chỗ gọi nằm sâu trong cây widget.
bool isMobileWidth(BuildContext c) =>
    MediaQuery.sizeOf(c).width < kMobileMaxWidth;

class AppRadius {
  AppRadius._();
  static const double sm = 10; // chip, badge
  static const double base = 14; // nút, input, list — khớp webapp
  static const double card = 20; // thẻ
  static const double pill = 999; // nút tròn, chip trạng thái
}

/// Bóng thẻ — port `--shadow` của webapp rồi nới thêm một lớp khuếch tán.
/// Soft là bóng **rộng và nhạt**, không phải bóng đậm: một lớp tối rõ nét sẽ kéo
/// thẻ về phía material-elevation, đúng thứ tông này tránh.
const kCardShadow = <BoxShadow>[
  BoxShadow(color: Color(0x0A101828), blurRadius: 2, offset: Offset(0, 1)),
  BoxShadow(color: Color(0x0F101828), blurRadius: 14, offset: Offset(0, 4)),
  BoxShadow(color: Color(0x0A0B3E42), blurRadius: 28, offset: Offset(0, 14)),
];

/// Màu ngữ nghĩa ngoài ColorScheme M3 — `AppSemantic.of(context).success`.
@immutable
class AppSemantic extends ThemeExtension<AppSemantic> {
  final Color success;
  final Color warning;
  final Color info;
  final Color surfaceSunken;

  /// Cyan logo. Dành cho **icon/mark/chấm/viền focus** — không phải chữ.
  final Color mark;

  /// Nền/chữ vùng console-log. GIỐNG NHAU ở cả theme sáng lẫn tối — xem
  /// [kConsoleBg].
  final Color consoleBg;
  final Color consoleFg;
  final Color consoleDim;

  const AppSemantic({
    required this.success,
    required this.warning,
    required this.info,
    required this.surfaceSunken,
    required this.mark,
    this.consoleBg = kConsoleBg,
    this.consoleFg = kConsoleFg,
    this.consoleDim = kConsoleDim,
  });

  static const light = AppSemantic(
    success: kSuccess,
    warning: kWarning,
    info: kInfo,
    surfaceSunken: kSunken,
    mark: kBrand,
  );

  static AppSemantic of(BuildContext c) =>
      Theme.of(c).extension<AppSemantic>() ?? light;

  @override
  AppSemantic copyWith({
    Color? success,
    Color? warning,
    Color? info,
    Color? surfaceSunken,
    Color? mark,
    Color? consoleBg,
    Color? consoleFg,
    Color? consoleDim,
  }) =>
      AppSemantic(
        success: success ?? this.success,
        warning: warning ?? this.warning,
        info: info ?? this.info,
        surfaceSunken: surfaceSunken ?? this.surfaceSunken,
        mark: mark ?? this.mark,
        consoleBg: consoleBg ?? this.consoleBg,
        consoleFg: consoleFg ?? this.consoleFg,
        consoleDim: consoleDim ?? this.consoleDim,
      );

  @override
  AppSemantic lerp(ThemeExtension<AppSemantic>? other, double t) {
    if (other is! AppSemantic) return this;
    return AppSemantic(
      success: Color.lerp(success, other.success, t)!,
      warning: Color.lerp(warning, other.warning, t)!,
      info: Color.lerp(info, other.info, t)!,
      surfaceSunken: Color.lerp(surfaceSunken, other.surfaceSunken, t)!,
      mark: Color.lerp(mark, other.mark, t)!,
      consoleBg: Color.lerp(consoleBg, other.consoleBg, t)!,
      consoleFg: Color.lerp(consoleFg, other.consoleFg, t)!,
      consoleDim: Color.lerp(consoleDim, other.consoleDim, t)!,
    );
  }
}

/// Thẻ chuẩn — nền `surface`, bo [AppRadius.card], viền `outlineVariant`,
/// [kCardShadow]. [hover]=true cho thẻ bấm được.
class AppCard extends StatefulWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;
  final VoidCallback? onTap;
  final bool hover;

  const AppCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(22),
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
    final lifted = widget.hover && _hovering;
    final card = AnimatedContainer(
      // Soft: nhấc chậm hơn và mềm hơn bản trước (160ms) — chuyển động dứt khoát
      // đọc ra "utilitarian", không phải tông này.
      duration: const Duration(milliseconds: 200),
      curve: Curves.easeOutCubic,
      transform: lifted
          ? (Matrix4.identity()..translateByDouble(0, -3, 0, 1))
          : Matrix4.identity(),
      padding: widget.padding,
      decoration: BoxDecoration(
        color: cs.surface,
        borderRadius: BorderRadius.circular(AppRadius.card),
        border: Border.all(color: cs.outlineVariant),
        boxShadow: (lifted
                ? const [
                    BoxShadow(
                        color: Color(0x14101828),
                        blurRadius: 24,
                        offset: Offset(0, 14)),
                    BoxShadow(
                        color: Color(0x0F0B3E42),
                        blurRadius: 40,
                        offset: Offset(0, 24)),
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
      // `opaque`: mặc định `deferToChild` chỉ ăn khi trúng CHỮ/ICON — bấm vào
      // khoảng trống trong thẻ sẽ trượt im lặng.
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: widget.onTap,
        child: card,
      ),
    );
  }
}

/// Hiệu ứng vào màn: mờ dần + trượt lên 12px, mỗi item trong danh sách trễ thêm
/// 50ms (tối đa 8 bậc). Dùng: `AppFadeIn(index: i, child: ...)`.
class AppFadeIn extends StatelessWidget {
  final Widget child;
  final int index;
  const AppFadeIn({super.key, required this.child, this.index = 0});

  static const _run = 360; // thời lượng 1 item (ms) — soft: chậm hơn 320 cũ
  static const _step = 50; // trễ mỗi bậc

  @override
  Widget build(BuildContext context) {
    // Người bật "giảm chuyển động" chỉ được hiện ra, không bị trượt.
    final reduce = MediaQuery.maybeDisableAnimationsOf(context) ?? false;
    if (reduce) return child;
    // ponytail: trễ bằng Interval thay vì Timer — không cần State/controller.
    final delay = _step * (index.clamp(0, 7));
    final total = _run + delay;
    return TweenAnimationBuilder<double>(
      tween: Tween(begin: 0, end: 1),
      duration: Duration(milliseconds: total),
      curve: Interval(delay / total, 1, curve: Curves.easeOutCubic),
      child: child,
      builder: (_, t, child) => Opacity(
        opacity: t,
        child:
            Transform.translate(offset: Offset(0, 12 * (1 - t)), child: child),
      ),
    );
  }
}

/// Tiêu đề nhóm trong màn cài đặt/biểu mẫu.
class AppSectionTitle extends StatelessWidget {
  final String text;
  const AppSectionTitle(this.text, {super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 12, left: 2),
      child: Text(
        text.toUpperCase(),
        style: theme.textTheme.labelMedium?.copyWith(
          color: theme.colorScheme.primary,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.9,
        ),
      ),
    );
  }
}

/// Bọc vùng **sẽ được chụp thành PNG** trong theme SÁNG cố định.
///
/// Yêu cầu này ĐỘC LẬP với việc app có dark mode hay không: ảnh xuất ra đi vào
/// hồ sơ kết quả và được in ra giấy, nên vùng chụp phải luôn là nền trắng + chữ
/// tối, bất kể theme app hôm nay là gì. Hôm nay app chỉ có theme sáng nên nó
/// không đổi gì — nhưng nó là chỗ ghi bất biến đó, và là chỗ giữ lại cái bẫy
/// dưới đây.
///
/// **`Material` là BẮT BUỘC, không phải trang trí.** Bản vá đầu chỉ có `Theme(...)`
/// và test lúc ấy vẫn XANH — nhưng không sửa được gì: widget `Text` KHÔNG lấy
/// màu từ `ColorScheme`, nó lấy từ `DefaultTextStyle`; `Theme` chỉ bọc thêm
/// `IconTheme` + `DefaultSelectionStyle`, còn chính `Material` mới dựng
/// `AnimatedDefaultTextStyle(style: Theme.of(context).textTheme.bodyMedium)`.
///
/// ⚠️ Vẫn KHÔNG cứu được `style: Theme.of(context)...` nếu chỗ gọi cầm `context`
/// lấy từ NGOÀI vùng bọc — chỗ đó phải có `Builder` riêng.
class AppExportTheme extends StatelessWidget {
  final Widget child;
  const AppExportTheme({super.key, required this.child});

  @override
  Widget build(BuildContext context) => Theme(
        data: appTheme(),
        child: Material(type: MaterialType.transparency, child: child),
      );
}

/// Theme của app — **chỉ có bản SÁNG**. Gọi từ `main.dart`.
///
/// Dark mode bỏ hẳn 2026-08-19 theo yêu cầu chủ dự án. Cố ý KHÔNG để lại nhánh
/// tối "phòng khi cần": bộ tối cũ dựng theo bảng màu sáng CŨ (nền #F2F8F9), mà
/// bảng đó vừa được làm đậm hẳn — giữ lại là giữ một theme vừa lệch vừa không
/// đường nào vào được, đúng loại code trông như có bảo hành mà chưa ai chạy.
ThemeData appTheme() {
  final seed =
      ColorScheme.fromSeed(seedColor: kBrandInk, brightness: Brightness.light);
  final cs = seed.copyWith(
    primary: kBrandInk,
    onPrimary: Colors.white,
    primaryContainer: kPrimaryContainer,
    onPrimaryContainer: kOnPrimaryContainer,
    // `secondary` giữ TRUNG TÍNH ám xanh (nền phụ), không phải màu nhấn thứ hai
    // — hai màu nhấn cạnh nhau là thứ làm giao diện ồn.
    secondary: kMuted,
    onSecondary: kMutedInk,
    tertiary: kBrandTeal,
    onTertiary: Colors.white,
    surface: kCard,
    onSurface: kFg,
    onSurfaceVariant: kMutedFg,
    // HAI đường kẻ tách bạch — xem chú thích ở `kOutline`.
    outline: kOutline,
    outlineVariant: kBorder,
    error: kError,
    onError: Colors.white,
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
    extensions: const [AppSemantic.light],
    // Soft: nới toàn cục thay vì đi chỉnh padding từng màn.
    visualDensity: VisualDensity.comfortable,
    scaffoldBackgroundColor: kBg,
    appBarTheme: AppBarTheme(
      backgroundColor: kCard,
      foregroundColor: cs.onSurface,
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      titleTextStyle: TextStyle(
          fontFamily: 'DM Sans',
          fontSize: 20,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.2,
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
    listTileTheme: ListTileThemeData(
      shape: rBase,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      // Ô nhập là ô TRẮNG có viền, không còn là "mảng lõm ám xanh".
      //
      // Ẩn dụ lõm chỉ đọc được khi NỀN TRANG gần trắng: khi ấy một mảng xanh
      // nhạt lún xuống là ô nhập. Nền trang nay đậm (#DCE9EB) nên mảng nhạt lại
      // nổi LÊN, và đo ra thì nó chỉ hơn nền 1.10 — gần như không tách. Ô trắng
      // trên nền đậm được 1.24 và chữ mờ trong ô lên 6.38:1 (lõm chỉ 5.13).
      // Đổi nền thì ẩn dụ phải đổi theo, không giữ vì nó từng đúng.
      fillColor: kCard,
      isDense: true,
      contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
      // Viền ô nhập dùng `outline` (3:1) chứ KHÔNG `outlineVariant` — nó là thứ
      // nói ra "gõ được vào đây", không phải đường kẻ trang trí.
      border: bd(cs.outline),
      enabledBorder: bd(cs.outline),
      focusedBorder: bd(cs.primary, 1.6),
      errorBorder: bd(cs.error),
      focusedErrorBorder: bd(cs.error, 1.6),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
          shape: rBase,
          elevation: 0,
          padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 15),
          textStyle: const TextStyle(
              fontFamily: 'DM Sans', fontWeight: FontWeight.w600, fontSize: 14)),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
          shape: rBase,
          side: BorderSide(color: cs.outlineVariant),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 15),
          textStyle: const TextStyle(
              fontFamily: 'DM Sans', fontWeight: FontWeight.w600, fontSize: 14)),
    ),
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(
          shape: rBase,
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11)),
    ),
    segmentedButtonTheme: SegmentedButtonThemeData(
      style: SegmentedButton.styleFrom(
        shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(AppRadius.pill)),
        selectedBackgroundColor: cs.primaryContainer,
        selectedForegroundColor: cs.onPrimaryContainer,
        side: BorderSide(color: cs.outlineVariant),
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
      ),
    ),
    dividerTheme:
        DividerThemeData(color: cs.outlineVariant, space: 1, thickness: 1),
    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      showCloseIcon: true,
      backgroundColor: kBrandDeep,
      contentTextStyle: const TextStyle(
          fontFamily: 'DM Sans', color: Color(0xFFEAF1F8), fontSize: 14),
      // Nền bị ép TỐI ở cả hai theme (trên), nên mặc định M3 cho nút X và nút
      // hành động — vốn tính theo `inverseSurface`/`inversePrimary`, ở theme
      // tối là màu SÁNG — đi ngược hướng: nút X tụt còn ~1:1, biến mất hẳn,
      // người dùng phải ngồi chờ SnackBar tự tắt. Ghim tay cùng tông với chữ.
      closeIconColor: const Color(0xFFEAF1F8),
      actionTextColor: kBrand,
      shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppRadius.base)),
      insetPadding: const EdgeInsets.all(20),
    ),
    dialogTheme: DialogThemeData(
      backgroundColor: cs.surface,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(AppRadius.card),
        side: BorderSide(color: cs.outlineVariant),
      ),
    ),
    popupMenuTheme: PopupMenuThemeData(
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(AppRadius.base),
        side: BorderSide(color: cs.outlineVariant),
      ),
      elevation: 0,
      color: cs.surface,
    ),
    chipTheme: ChipThemeData(
      shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppRadius.pill)),
      side: BorderSide(color: cs.outlineVariant),
      backgroundColor: kMuted,
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
    ),
    tooltipTheme: TooltipThemeData(
      decoration: BoxDecoration(
        color: kBrandDeep,
        borderRadius: BorderRadius.circular(AppRadius.sm),
      ),
      textStyle: const TextStyle(
          fontFamily: 'DM Sans', color: Color(0xFFEAF1F8), fontSize: 12.5),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      waitDuration: const Duration(milliseconds: 500),
    ),
    navigationRailTheme: NavigationRailThemeData(
      backgroundColor: cs.surface,
      indicatorColor: cs.primaryContainer,
      indicatorShape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppRadius.base)),
      selectedIconTheme: IconThemeData(color: cs.onPrimaryContainer),
      unselectedIconTheme: IconThemeData(color: cs.onSurfaceVariant),
      selectedLabelTextStyle: TextStyle(
          fontFamily: 'DM Sans',
          color: cs.onSurface,
          fontWeight: FontWeight.w600,
          fontSize: 14),
      unselectedLabelTextStyle: TextStyle(
          fontFamily: 'DM Sans', color: cs.onSurfaceVariant, fontSize: 14),
    ),
    progressIndicatorTheme: ProgressIndicatorThemeData(
      color: cs.primary,
      linearTrackColor: kSunken,
      linearMinHeight: 3,
    ),
    scrollbarTheme: ScrollbarThemeData(
      thickness: const WidgetStatePropertyAll(9),
      radius: const Radius.circular(5),
      thumbColor:
          WidgetStatePropertyAll(cs.onSurfaceVariant.withValues(alpha: 0.3)),
    ),
  );
}
