# UI/UX Template — FBT_RAPID (Flutter / Material 3)

> ## ⚠️ ĐẠI TU 2026-08-19 — phần dưới file này CHƯA viết lại
>
> App vừa đổi toàn bộ hệ thị giác (tông **soft**, bảng màu **thương hiệu Forte Biotech**).
> **Nguồn sự thật vẫn là [lib/theme/app_theme.dart](lib/theme/app_theme.dart)** — phần mô tả
> bên dưới còn nói theo bảng màu navy/amber **CŨ**. Đọc code, đừng đọc phần dưới, cho tới khi
> nó được viết lại.
>
> **Cái gì đổi:**
>
> | | cũ | mới |
> |---|---|---|
> | Nguồn màu | tự chọn (navy + amber) | port từ `FBT-DXD243/data/style.css` — dashboard web của chính firmware |
> | Primary | `#0A1F47` navy | `#0F5F63` (`kBrandInk`, 7.40:1) |
> | Nhấn | `#E0A63A` amber | `#20C6D0` cyan logo — **CHỈ icon/mark**, 2.09:1 nên không bao giờ làm chữ |
> | Nền | `#F1F2F5` xám | `#F1F2F5` — quay lại xám lạnh, chủ dự án chọn |
> | Bo góc | 8 / 12 / 16 | 10 / 14 / 20 / pill (14 khớp `--radius` webapp) |
> | Display | Source Serif 4 | **bỏ** — DM Sans lo cả display lẫn body |
> | Mật độ | `standard` | `comfortable` |
> | Dark mode | có | **bỏ hẳn** — app chỉ còn theme sáng |
>
> **Vì sao lấy màu của firmware**: người dùng thấy hai giao diện cạnh nhau trong cùng buổi làm
> việc — điện thoại quét QR vào dashboard máy, và app trên máy tính. Bảng màu đó cũng đã được
> soát tương phản và ghi tỉ lệ đo được ngay trong CSS.
>
> **Tương phản có TEST**: [test/theme_contrast_test.dart](test/theme_contrast_test.dart) tính
> lại mọi cặp từ chính hằng số đang chạy. Đổi màu mà quên chạy nó là cách làm tụt khả năng đọc
> của một app đọc kết quả xét nghiệm mà không ai thấy.
>
> **Widget mới**: `AppTabScaffold` (`lib/widgets/app_tab_scaffold.dart`) — khuôn tiêu đề + dải
> mục con, giờ dùng ở **mọi** tab; `AppSearchBox` (`lib/widgets/app_search_box.dart`) — ô tìm
> dùng chung có khe `trailing`; `AppExportTheme` (`app_theme.dart`) — ghim theme SÁNG cho vùng
> chụp PNG. `AppSemantic` có thêm `mark` (màu dấu thương hiệu) và bộ `console*`.
>
> **Quy tắc mới đáng nhớ**: màn được nhúng trong `AppTabScaffold` **bỏ hẳn `AppBar`**; màn đẩy
> route thì giữ. Ba lỗi bắt được bằng ẢNH CHỤP chứ không phải suy luận: nhãn nav vỡ giữa từ ·
> mục nav nhảy chỗ khi rail bung · nhãn nổi ô nhập bị mép clip cắt mất nửa trên.
>
> **Cập nhật 2026-08-19 (lượt 2)**: **bỏ hẳn dark mode** và **làm đậm bảng màu**.
> Nền trang `#F2F8F9` → **`#F1F2F5`** (xám lạnh trung tính, chủ dự án chọn), chữ mờ
> `#6B7280` → **`#4F5A68`** (trên thẻ 4.83 → **7.01**), primary `#13757A` → **`#0F5F63`**
> (trắng trên nút 5.45 → **7.40**). Thêm **`kOutline`** tách khỏi `kBorder` — viền ô nhập
> giờ đạt 3:1 thay vì nhạt bằng đường kẻ trang trí. Ô nhập thành **ô trắng có viền**, không
> còn là "mảng lõm": ẩn dụ lõm chỉ đúng khi nền trang gần trắng.
> Logo công ty (`assets/images/forte-logo.png`) ở góc trái trên + màn đăng nhập; rail bo hai
> góc phải.
>
> **Tiến độ đại tu** (phạm vi B — dựng lại bố cục từng màn): ✅ **XONG**.
> tầng theme · khung app + đăng nhập · Kỹ Thuật (desktop + web) · Thư Mục · Quản lý máy ·
> Lịch sử + Cloud · Quản lý User · nhóm công cụ + màn chi tiết.
> `flutter analyze` 0 lỗi · `flutter test` **42/42 xanh**. Nhật ký: `.hallmark/log.json`.

Design system **đang chạy thật** của app này, trích từ [lib/theme/app_theme.dart](lib/theme/app_theme.dart)
(nguồn sự thật duy nhất). Gốc ý tưởng: [UI_UX_TEMPLATE.md](UI_UX_TEMPLATE.md) (bản web Next.js/Tailwind
của dự án Homies) — file này là **bản Flutter đã hiện thực hoá**, không phải bản phác.

- Đổi "look" của app → sửa `app_theme.dart`, KHÔNG sửa từng màn.
- Màn con luôn đọc `Theme.of(context).colorScheme.*` / `AppSemantic.of(context).*`.
- Hex là **xấp xỉ oklch** của template web (Flutter chưa có oklch); riêng navy `#0A1F47` là chính xác.

---

## PROMPT (dán khi nhờ AI dựng màn mới / dự án Flutter mới)

> Xây màn Flutter (Material 3) theo design system FBT_RAPID. Mọi màu/shadow/radius khai báo tập trung
> trong `lib/theme/app_theme.dart`; widget lấy qua `Theme.of(context).colorScheme` và
> `AppSemantic.of(context)` — **KHÔNG hardcode `Colors.red/green/orange/grey` hay `Color(0x..)`**
> (ngoại lệ duy nhất: `Colors.white` cho chữ trên nút màu, palette đồ thị `kSlotColors`/`kTempColors`,
> nền ảnh chart đen).
>
> - Palette: `primary` navy `#0A1F47` · `tertiary`+`secondary` amber `#E0A63A` (chữ trên amber =
>   `#4A3410`) · `error` `#D6483B`; ngoài ColorScheme M3 còn `success/warning/info/surfaceSunken`
>   qua `ThemeExtension` **`AppSemantic`**.
> - Có **light + dark** (`appTheme(Brightness)`), dark là tông xanh mực `#0C1220`/`#141C2E`.
> - **Card recipe** = widget `AppCard`: nền `surface`, bo **16**, viền `outlineVariant`, padding 20,
>   `kCardShadow`; thẻ bấm được thì `hover: true` (nhấc 2px, 160ms `easeOutCubic`).
> - Radius: nút/input/list **12** (`AppRadius.base`), thẻ **16** (`AppRadius.card`), chip nhỏ 8.
> - Font: **DM Sans** (mặc định toàn app) · **JetBrains Mono** cho số/log/JSON · **Source Serif 4**
>   cho tiêu đề display. Số và ngày dùng `FontFeature.tabularFigures()`.
> - Điều hướng: `NavigationRail` **ẩn hẳn, hover mép trái mới hiện dạng overlay trong `Stack`** +
>   `IndexedStack` cho tab nội dung; tab con dùng `SegmentedButton` + `IndexedStack`.
> - Chuỗi UI mới dùng `tr('key')` (`util/i18n.dart`); comment và UI **tiếng Việt**.
> - Nút ghi (Lưu/Đồng bộ/Xoá/Lấy-từ-máy) phải bọc `if (SessionStore.canWrite)`.

---

## 1. Token màu

| Vai trò (ColorScheme) | Hằng | Light | Dark |
|---|---|---|---|
| `primary` | `kNavy` | `#0A1F47` | *(từ seed M3)* |
| `tertiary` (amber `--accent` bản web) | `kAccent` | `#E0A63A` | `#E0A63A` |
| `onTertiary` | `kAccentInk` | `#4A3410` | `#4A3410` |
| `secondary` (nền phụ trung tính) | `kMuted` / `kMutedDark` | `#F5F6F8` | `#1B2438` |
| `onSecondary` | `kMutedInk` / `kFgDark` | `#43474F` | `#E7EAF0` |
| `error` | `kError` | `#D6483B` | `#D6483B` |
| `surface` (thẻ) | `kCard` / `kCardDark` | `#FFFFFF` | `#141C2E` |
| `onSurface` | `kFg` / `kFgDark` | `#2A2E37` | `#E7EAF0` |
| `onSurfaceVariant` | `kMutedFg` / `kMutedFgDark` | `#7C8088` | `#97A0B2` |
| `outline` / `outlineVariant` | `kBorder` / `kBorderDark` | `#E7E8EB` | `#263149` |
| `primaryContainer` | — | `#DCE3F1` | *(từ seed)* |
| scaffold background | `kBg` / `kBgDark` | `#F1F2F5` | `#0C1220` |

**`AppSemantic`** (ngoài ColorScheme — `AppSemantic.of(context).success`):

| Field | Hằng | Giá trị |
|---|---|---|
| `success` | `kSuccess` | `#2E9E6B` |
| `warning` | `kWarning` | `= kAccent` `#E0A63A` |
| `info` | `kInfo` | `#3E7BC7` |
| `surfaceSunken` | `kSunken` / `kSunkenDark` | `#EBECEF` / `#0F1626` |

```dart
// Đăng ký trong appTheme(): extensions: [dark ? AppSemantic.dark : AppSemantic.light]
final sem = AppSemantic.of(context);   // fallback về light nếu thiếu extension
Icon(Icons.check_circle, color: sem.success);
```

## 2. Radius + bóng

```dart
class AppRadius { static const base = 12.0; static const card = 16.0; static const sm = 8.0; }

/// Bóng xếp lớp (port CARD_SHADOW của bản web). Dark KHÔNG dùng bóng — dựa vào viền.
const kCardShadow = <BoxShadow>[
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 0,  spreadRadius: 1),
  BoxShadow(color: Color(0x0A2A3345), blurRadius: 3,  offset: Offset(0, 3)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 12, offset: Offset(0, 12)),
  BoxShadow(color: Color(0x0A0E3F7E), blurRadius: 24, offset: Offset(0, 24)),
];
// hover: BoxShadow(Color(0x140E3F7E), blur 20, offset (0,12))
```

## 3. `AppCard` — recipe thẻ

```dart
AppCard(
  hover: true,                          // thẻ bấm được: nhấc 2px + bóng sâu
  onTap: () => ...,
  padding: const EdgeInsets.all(20),    // mặc định 20 (dải hợp lệ 16–24)
  child: ...,
)
```
Nội bộ: `AnimatedContainer` 160ms `Curves.easeOutCubic`, `color: cs.surface`, bo `AppRadius.card`,
`border: Border.all(color: cs.outlineVariant)`, bóng `kCardShadow` (light) / `null` (dark).
Dùng cho mọi thẻ "hero" **và thẻ trong danh sách chính** (máy / lần chạy); danh sách dày đặc
(vd file JSON) thì `ListTile` là đủ.

> ⚠️ `onTap` của `AppCard` bọc `GestureDetector(behavior: HitTestBehavior.opaque)`. Mặc định
> `deferToChild` chỉ ăn khi trúng CHỮ/ICON — `Container`/`DecoratedBox` KHÔNG tự hit-test →
> bấm vào khoảng trống trong thẻ **trượt im lặng**. Đừng bỏ `opaque` đi.

## 3b. `AppFadeIn` — hiệu ứng vào màn (stagger)

Bản Flutter của `.animate-slide-up` + `.stagger-1..8`: mờ dần + trượt lên 12px, `easeOutCubic`,
mỗi item trong danh sách trễ thêm 50ms (cap 8 bậc). Không dùng `AnimationController` — độ trễ
làm bằng `Interval` trong `TweenAnimationBuilder`.

```dart
ListView.builder(
  itemBuilder: (_, i) => AppFadeIn(index: i, child: AppCard(...)),
)
AppFadeIn(child: ListView(...))   // index mặc định 0 = fade cả màn
```
Lưu ý: item trong `ListView.builder` bị huỷ khi cuộn khỏi màn → cuộn ngược lại sẽ chạy lại
animation (giống "reveal on scroll"). Chấp nhận được; muốn tắt thì bỏ `AppFadeIn` ở danh sách đó.

## 3c. `AppSectionTitle` — tiêu đề nhóm

Chữ nhỏ IN HOA, `letterSpacing 0.8`, màu `primary` — dùng cho các nhóm trong màn Thiết lập/biểu mẫu
(`AppSectionTitle(tr('us.accountInfo'))`).

## 4. `appTheme(Brightness)` — theme builder DUY NHẤT

`main.dart` chỉ gọi `appTheme(...)`, không có `_theme` cục bộ. Cấu trúc:
`ColorScheme.fromSeed(seedColor: kNavy, brightness: b)` → `.copyWith(...)` override về token thương
hiệu → `ThemeData(useMaterial3: true, fontFamily: 'DM Sans')` → `.copyWith(...)` các component theme:

| Component | Chốt |
|---|---|
| `appBarTheme` | nền `surface`, `elevation 0`, `scrolledUnderElevation 1`, `centerTitle: false`, title 20/w600 |
| `cardTheme` | `CardThemeData` (KHÔNG phải `CardTheme` — Flutter mới đổi tên), elevation 0, margin 0, bo 16 + viền `outlineVariant`, `clipBehavior: antiAlias` |
| `inputDecorationTheme` | `filled`, `isDense`, fill trắng (light) / `kSunkenDark` (dark), bo 12, focus = viền `primary` 1.5px |
| `filledButtonTheme` | bo 12, padding `18×12` |
| `outlinedButtonTheme` | bo 12, padding `16×12` |
| `segmentedButtonTheme` | bo 12 |
| `listTileTheme` | bo 12 |
| `dividerTheme` | màu `outlineVariant`, `space/thickness = 1` |
| `snackBarTheme` | `behavior: fixed`, `showCloseIcon: true` |
| `dialogTheme` | bo 16 (`AppRadius.card`) |
| `popupMenuTheme` | bo 12, nền `surface` |
| `chipTheme` | bo 8, viền `outlineVariant` |
| `navigationRailTheme` | nền `surface`, chỉ báo `primaryContainer`, nhãn chọn w600 |
| `scrollbarTheme` | dày **8px** + bo 4 (port `::-webkit-scrollbar` bản web), thumb `onSurfaceVariant` 35% |

> **KHÔNG đè `border: OutlineInputBorder()` ở từng `InputDecoration`** — đè là mất fill + bo 12 +
> viền focus 1.5px của `inputDecorationTheme`, ô nhập trông khác nhau giữa các màn.

## 5. Typography

Font nhúng asset (variable TTF, khai trong `pubspec.yaml`):

```yaml
fonts:
  - family: DM Sans        # mặc định toàn app (fontFamily trong ThemeData)
    fonts: [{ asset: assets/fonts/DMSans.ttf }]
  - family: JetBrains Mono # số / log serial / JSON / mã máy
    fonts: [{ asset: assets/fonts/JetBrainsMono.ttf }]
  - family: Source Serif 4 # tiêu đề display
    fonts: [{ asset: assets/fonts/SourceSerif4.ttf }]
```

- Mono là `fontFamily: 'JetBrains Mono'` — **KHÔNG dùng `'Consolas'`** (không đồng nhất, thiếu trên web).
- Số/ngày trong bảng, thẻ số liệu: `TextStyle(fontFeatures: [FontFeature.tabularFigures()])` để cột
  số không nhảy.

## 6. Palette đồ thị (CỐ TÌNH không token-hoá)

`kSlotColors` (10 slot, [lib/widgets/ct_chart.dart](lib/widgets/ct_chart.dart)) và `kTempColors`
(6 kênh nhiệt, [lib/widgets/temp_chart.dart](lib/widgets/temp_chart.dart)) **đồng bộ với web UI**
(`sheet/`, `data/script.js`) — **đừng tô lại theo theme**, đổi màu là mất khả năng phân biệt
slot/kênh giữa app và web. Chỉ token-hoá phần khung: lưới, viền, mốc 0, slot tắt.

| | Màu |
|---|---|
| `kSlotColors` | `#00BFFF` `#FF0000` `#FFD000` `#32CD32` `#D2691E` `#00B3B3` `#9400D3` `#9ACD32` `#0000FF` `#FF69B4` |
| `kTempColors` | Lysis `#D32F2F` · Amp1 `#F57C00` · Amp2 `#FBC02D` · Hotlid1 `#1976D2` · Hotlid2 `#7B1FA2` · Ambient `#388E3C` |

**`Classification.color`** (`models/test_result.dart`) cũng thuộc nhóm này — màu DỮ LIỆU, cố tình
không token-hoá: dương `#D32F2F` · âm `#388E3C` · dương nhẹ `#F57C00` · lỗi `#616161`. Đây là nguồn
màu cho `ResultBadge` và chip đếm ở `cloud_runs_screen`. (Đổi sang `kError/kSuccess/kWarning` đã CÂN
NHẮC và BỎ: amber `#E0A63A` làm màu CHỮ trên nền sáng tương phản quá kém.)

Nền "console" (log esptool, UART thô) LUÔN tối `#1E1E1E` + chữ `#D4D4D4` bất kể theme — như terminal.

## 7. Kiến trúc UI (shell + điều hướng)

```
_AuthGate → LoginScreen | HomeShell
HomeShell = Scaffold(body: Stack(
  Positioned.fill(IndexedStack(tab nội dung theo vai trò)),   // nội dung KHÔNG relayout
  if (!_railVisible) Positioned(left:0, width:14, MouseRegion) // vùng hover + "tay nắm" 4px
  AnimatedPositioned(left: _railVisible ? 0 : -_railWidth, NavigationRail(...)),
))
```

- **Rail là overlay trong `Stack`, KHÔNG phải `Row`** — bung/thu thanh nav không relayout nội dung
  → tránh giật `fl_chart`.
- Rail chỉ chứa **tab nội dung** theo vai trò (Lịch sử · Thư Mục · +Kỹ Thuật cho nhân sự · +Quản lý
  User cho root). **Thiết lập + Đăng xuất nằm trong menu icon tài khoản** ở `trailing`
  (shield = nhân sự, person = khách hàng).
- Chọn tab xong → tự ẩn rail (`_railVisible = false`).
- **Tab con** dùng `SegmentedButton` + `IndexedStack` (giữ state), mẫu chuẩn ngắn nhất ở
  [lib/screens/folder_screen.dart](lib/screens/folder_screen.dart):

```dart
Column(children: [
  Padding(
    padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
    child: SegmentedButton<int>(
      showSelectedIcon: false,
      segments: [ButtonSegment(value: 0, icon: Icon(...), label: Text(tr('...')))],
      selected: {_seg},
      onSelectionChanged: (s) => setState(() => _seg = s.first),
    ),
  ),
  Expanded(child: IndexedStack(index: _seg, children: [...])),
])
```
Thêm mục con = thêm 1 `ButtonSegment` + 1 màn vào `IndexedStack`, không đụng gì khác.
Màn dùng chung phần cứng (COM) thì truyền cờ `active: _seg == i` để màn không-active tự nhả.

## 8. Theme + ngôn ngữ runtime

- `AppPrefs.instance` (`ChangeNotifier` toàn cục, lưu `shared_preferences`); `MaterialApp` bọc trong
  `AnimatedBuilder(animation: AppPrefs.instance)`.
- Theme áp **live qua prop** (`theme`/`darkTheme`/`themeMode`) — không cần key.
- **Đổi ngôn ngữ PHẢI key `MaterialApp` theo locale** (`key: ValueKey('locale_$code')`); hệ `tr()` tự
  viết (không dùng `Localizations`) nên rebuild `MaterialApp` thường KHÔNG rebuild route `home`.

## 9. UX theo phân quyền (đặc thù app này)

| Quyền | Ảnh hưởng UI |
|---|---|
| `SessionStore.canWrite` (nhân sự) | Ẩn/hiện nút **ghi**: Lưu, Đồng bộ, Xoá, Lấy-từ-máy; hiện segmented chọn nguồn cloud |
| `SessionStore.canManageUsers` (root) | Hiện tab Quản lý User |
| `UserSession.canSee(id)` | Lọc danh sách máy/kết quả (enforce ở **client**) |
| `SessionStore.canSaveCharts` (mọi vai trò) | Cho lưu ảnh đồ thị — KHÁC `canWrite` |

## 10. Gotcha UI (đã gặp thật)

- **`PopupMenuButton` trong overlay tự-ẩn-theo-hover**: mở menu rồi rê chuột tới menu = rời rail →
  `onExit` ẩn rail → gỡ button khỏi tree → **huỷ menu**. Fix: cờ `_menuOpen` (bắt
  `onOpened`/`onSelected`/`onCanceled`), menu mở thì không ẩn rail.
- **Xuất PNG đồ thị không đồng nhất**: `result_detail` chụp layer off-screen riêng nền trắng → PNG
  **luôn trắng**; còn `curve_compare`/`temperature_log` bọc `RepaintBoundary` thẳng widget đang hiển
  thị → dark mode ra **PNG nền tối**. Muốn luôn trắng thì bọc vùng chụp trong light-theme cố định.
- **Phân trang + lọc `canSee` ở client** → trang lọc ra rỗng làm user hạn chế "kẹt"; `_load` phải tự
  tải tiếp khi trang rỗng mà còn trang (có cap vòng lặp).
- Trên **web**: ẩn mục "Nơi lưu file" trong Cài đặt; "Lưu" = tải về Downloads (`ResultExport.saveRun`
  trả `''` → caller ẩn nút "Mở" trong snackbar).
- **`SizedBox(width:)` quanh `TextField` cắt NHÃN chứ không cắt giá trị**: nhãn nổi bị ellipsis
  ("Bootloader @ of…") trong khi ô vẫn nhìn "vừa" vì giá trị ngắn — rất dễ lọt review nếu chỉ đọc
  code. Nhãn dài (≈20 ký tự) cần **≥200px**. Dính ở CẢ `flasher_screen` lẫn `web_flasher_screen`
  (2 bản sinh đôi) → sửa 1 bên nhớ sửa bên kia.
- **Nút phá huỷ phải tô `colorScheme.error`** (viền + chữ cho `OutlinedButton`, nền cho nút xác nhận
  trong dialog) — mẫu ở `user_management_screen._delete` và `flasher_screen` ("Xóa flash" đỏ, "Xóa
  log" giữ trung tính vì chỉ xoá text).

## Checklist mỗi màn/widget mới

- [ ] Không hardcode màu — chỉ `colorScheme.*` / `AppSemantic.of(context)` (trừ 3 ngoại lệ đã nêu).
- [ ] Thẻ hero dùng `AppCard`; radius theo `AppRadius` (12 / 16 / 8).
- [ ] Danh sách: `AppFadeIn(index: i, …)`; ô nhập KHÔNG đè `border:`.
- [ ] Chữ mono dùng `'JetBrains Mono'` — KHÔNG `'monospace'`/`'Consolas'`.
- [ ] Số & ngày: `JetBrains Mono` + `tabularFigures()`.
- [ ] Chuỗi qua `tr('key')`, comment + UI tiếng Việt.
- [ ] Nút ghi bọc `SessionStore.canWrite`; danh sách lọc `canSee`.
- [ ] Thử **cả light lẫn dark** (nhất là chỗ có bóng — dark bỏ bóng, dựa viền).
- [ ] Nếu dùng `dart:io`/serial → đi qua `util/platform_files.dart` hoặc có bản `_web`, kẻo vỡ build web.

## Không mang được từ bản web sang

| Web (UI_UX_TEMPLATE.md) | Flutter thay bằng |
|---|---|
| Tailwind `@theme` + class utility | `ThemeData` + `colorScheme` + `AppSemantic` |
| shadcn/ui + Radix | widget Material 3 thuần |
| màu `oklch` | hex xấp xỉ (bảng mục 1) |
| `Section` union không router | `IndexedStack` + `NavigationRail`/`SegmentedButton` |
| `useSyncExternalStore` | `ChangeNotifier` (`AppPrefs`) + singleton static (`SessionStore`, `StoragePaths`) |
| CSS keyframes `cubic-bezier(.16,1,.3,1)` | `AnimatedContainer`/`AnimatedPositioned` + `Curves.easeOutCubic` |
| `card-hover` class | `AppCard(hover: true)` |
| `.animate-slide-up` + `.stagger-1..8` | `AppFadeIn(index: i)` |
| `cva` variants (default/outline/ghost/…) | `FilledButton` / `OutlinedButton` / `TextButton` M3 |
| `--chart-1..5` | palette dữ liệu riêng (`kSlotColors`/`kTempColors`/`Classification.color`) |
