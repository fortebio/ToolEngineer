# UI/UX Template — Flutter port

Bản Flutter của [UI_UX_TEMPLATE.md](UI_UX_TEMPLATE.md). Giữ nguyên **giá trị thiết kế** (màu, radius, font, shadow, easing); đổi hiện thực web → **Material 3 `ThemeData`**.

⚠️ Hex dưới là **xấp xỉ** từ oklch (Flutter chưa hỗ trợ oklch). `primary` navy là chính xác; các màu khác tinh chỉnh lại bằng mắt nếu cần.

---

## PROMPT (dán khi bắt đầu dự án Flutter)

> Xây app Flutter (Material 3) theo design system sau. Định nghĩa toàn bộ màu/khoảng cách trong **`ThemeData` tập trung** (`lib/theme/app_theme.dart`) — KHÔNG hardcode `Color(0x..)` rải rác trong widget, luôn lấy qua `Theme.of(context).colorScheme`.
>
> - Palette: `primary` navy `#0A1F47`, `secondary/surface` xám lạnh rất nhạt, `tertiary`(accent) amber, `error` đỏ; thêm custom `success / warning / info / surfaceSunken` qua `ThemeExtension`.
> - **Card recipe:** bo góc **16** (`BorderRadius.circular(16)`), nền `surface`, viền `outlineVariant` mảnh, padding 16–24, `boxShadow` = `kCardShadow` (danh sách shadow xếp lớp mờ — xem dưới).
> - Font: **DM Sans** (body), **JetBrains Mono** (số/mã). Radius gốc 12, card 16.
> - Animation: dùng `AnimatedSwitcher`/`AnimatedOpacity` với `Duration ~350ms` và `Curves.easeOutCubic` (tương đương `cubic-bezier(0.16,1,0.3,1)`); danh sách vào lần lượt bằng stagger delay 50ms.
> - Kiến trúc: `NavigationRail` (desktop) / `NavigationBar` (mobile) + `IndexedStack` các màn — thay cho router khi số section cố định. Gate auth bằng một `AuthProvider` (Provider/Riverpod): loading → login → shell.
> - Dữ liệu chia sẻ đi qua service/store (`ChangeNotifier` hoặc Riverpod provider), mock/`shared_preferences` mặc định.

---

## Token → Flutter (`lib/theme/app_theme.dart`)

```dart
import 'package:flutter/material.dart';

const kNavy    = Color(0xFF0A1F47); // primary (chính xác)
const kBg      = Color(0xFFF1F2F5); // background  ~oklch(.965)
const kFg      = Color(0xFF2A2E37); // foreground  ~oklch(.20)
const kCard    = Color(0xFFFFFFFF);
const kBorder  = Color(0xFFE7E8EB); // ~oklch(.92)
const kMutedFg = Color(0xFF7C8088); // muted-foreground ~oklch(.55)
const kAccent  = Color(0xFFE0A63A); // amber ~oklch(.78 .18 85)
const kError   = Color(0xFFD6483B); // ~oklch(.60 .20 25)
const kSuccess = Color(0xFF2E9E6B); // ~oklch(.60 .17 155)
const kInfo    = Color(0xFF3E7BC7); // ~oklch(.55 .15 250)

/// Shadow xếp lớp — port của CARD_SHADOW.
const kCardShadow = <BoxShadow>[
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 0,  spreadRadius: 1),
  BoxShadow(color: Color(0x0A2A3345), blurRadius: 3,  offset: Offset(0, 3)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 12, offset: Offset(0, 12)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 24, offset: Offset(0, 24)),
];

ThemeData buildAppTheme() {
  final scheme = ColorScheme.fromSeed(
    seedColor: kNavy,
    brightness: Brightness.light,
  ).copyWith(
    primary: kNavy,
    surface: kCard,
    background: kBg,
    onSurface: kFg,
    outlineVariant: kBorder,
    error: kError,
    tertiary: kAccent, // accent amber
  );

  return ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: kBg,
    fontFamily: 'DM Sans',
    cardTheme: CardTheme(
      color: kCard,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
        side: const BorderSide(color: kBorder),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: kNavy, foregroundColor: Colors.white,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      ),
    ),
  );
}
```

### Card widget mẫu

```dart
class AppCard extends StatelessWidget {
  final Widget child;
  const AppCard({super.key, required this.child});
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(20),
    decoration: BoxDecoration(
      color: kCard,
      borderRadius: BorderRadius.circular(16),
      border: Border.all(color: kBorder),
      boxShadow: kCardShadow,
    ),
    child: child,
  );
}
```

---

## Không mang sang được (khác nền tảng)

| Web (template gốc) | Flutter thay bằng |
|---|---|
| Tailwind `@theme` / class utility | `ThemeData` + `colorScheme`, không có class utility |
| shadcn/ui + Radix | widget Material 3 (hoặc [shadcn_ui](https://pub.dev/packages/shadcn_ui) nếu muốn giống hệt) |
| oklch màu | hex xấp xỉ (bảng trên) — tinh chỉnh bằng mắt |
| router-less `Section` union | `IndexedStack` + `NavigationRail`/`NavigationBar` |
| `useSyncExternalStore` store | `ChangeNotifier` / Riverpod |
| CSS keyframes + `cubic-bezier(.16,1,.3,1)` | `AnimatedSwitcher` + `Curves.easeOutCubic` |

## Setup

```bash
flutter create homies_app
# pubspec.yaml: thêm font DM Sans + JetBrains Mono (google_fonts hoặc asset),
#   provider hoặc flutter_riverpod, shared_preferences.
```
```

Còn Supabase: dùng `supabase_flutter` — schema/RLS bên [server/supabase/schema.sql](../server/supabase/schema.sql) tái dùng nguyên vẹn.
