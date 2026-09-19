# Thương hiệu Forte Biotech — token dùng chung toàn sản phẩm

Nguồn sự thật: [`tokens.json`](tokens.json). Mọi phần (LCD Rapid4P, app Flutter FBT_RAPID, web
`/app/`, portal WiFi) lấy màu/chữ/khoảng cách từ đây; **không tự bịa mã màu trong code**.
Màu lấy trực tiếp từ file logo chuẩn 2000×1780 (2026-09-17); tọa độ 5 tam giác + ô chữ cũng ở
`tokens.json › logo` để vẽ lại logo dạng vector ở bất kỳ nền tảng nào.

## Bảng màu

| Token | Hex | Vai trò |
|---|---|---|
| `teal-500` | `#26C5CF` | **Chủ đạo**: hành động chính, tiêu đề, wordmark |
| `teal-700` | `#17A6BF` | Nhấn/pressed, đầu gradient |
| `green-500` | `#1BD1A5` | Thành công / đã xong |
| `mint-300` | `#6FE6C3` | Điểm nhấn sáng, cuối gradient |
| `on-brand` | `#06262A` | Chữ trên nền teal/green |
| `amber-500` | `#FFB300` | Cảnh báo (ngoài logo, ngữ nghĩa) |
| `red-500` | `#EF5350` | Nguy hiểm / **kết quả dương tính** (ngoài logo, ngữ nghĩa) |
| `navy-950/900/800/700/600` | `#0A1418 #10202A #152A35 #1F3A46 #2A3F4C` | Nền tối ám teal: nền · header/footer · thẻ · viền · nút phụ |
| `text` / `muted` | `#F2F8F9` / `#A9BCC4` | Chữ trên nền tối |
| `light-*` | xem JSON | Bộ nền sáng cho web/app |

Gradient thương hiệu: `#17A6BF → #26C5CF → #6FE6C3` (theo cột phải của logo: teal đậm trên, mint dưới).

## Áp dụng theo nền tảng

**LCD Rapid4P (LVGL)** — `firmware/rapid4p/main/ui/ui_theme.h` là bản C của bảng này; logo vector
ở `ui_logo.c`. Đổi token → sửa `tokens.json` trước, rồi `ui_theme.h`. Hai thang kích thước/chữ theo
màn ở `typography.scales`: `lcd-4.3in` (P4 800×480) và `lcd-2.8in` (S3 ES3N28P 320×240, 2026-09-19) —
`ui_theme.h` chọn thang compile-time theo `BOARD_LCD_H_RES` (`UI_SCALE_SMALL`).

**Flutter (apps/fbt_rapid)** — gợi ý `ColorScheme`:
```dart
const forteTeal = Color(0xFF26C5CF);
final scheme = ColorScheme.dark(
  primary: forteTeal, onPrimary: const Color(0xFF06262A),
  secondary: const Color(0xFF6FE6C3), tertiary: const Color(0xFF1BD1A5),
  surface: const Color(0xFF152A35), onSurface: const Color(0xFFF2F8F9),
  error: const Color(0xFFEF5350),
);
// Sáng: primary forteTeal, surface 0xFFFFFFFF, background 0xFFF4FBFC, onSurface 0xFF10202A.
```

**Web / Tailwind** — `tailwind.config.js`:
```js
theme: { extend: { colors: {
  brand: { DEFAULT: '#26C5CF', dark: '#17A6BF', green: '#1BD1A5', mint: '#6FE6C3', on: '#06262A' },
  navy:  { 950: '#0A1418', 900: '#10202A', 800: '#152A35', 700: '#1F3A46', 600: '#2A3F4C' },
}, fontFamily: { sans: ['Montserrat', 'ui-sans-serif', 'system-ui'] } } }
```
(hoặc `python .claude/skills/ui-styling/scripts/tailwind_config_gen.py --colors brand:#26C5CF`).

## Kiểm tương phản

Mọi cặp chữ/nền ≥ 4,5:1 (đã đo 2026-09-17: text/navy-950 17:1 · muted 9,5:1 · teal 8,9:1 ·
green 9,5:1 · red 5,4:1 · on-brand/teal 7,6:1). Đổi màu thì đo lại (công thức WCAG 2.1, ví dụ
script Python 10 dòng trong lịch sử `docs/history/2026-09-17-rapid4p-nap-may-that-lan-dau.md`).

## Quy tắc dùng

1. Teal chỉ cho **một** hành động chính mỗi màn; nút phụ dùng `navy-600`.
2. Không truyền nghĩa chỉ bằng màu: kèm icon/chữ (✓ Âm tính, ⚠ Dương tính).
3. Không vẽ lại/biến dạng logo; dùng tọa độ trong `tokens.json`. Trên nền tối giữ nguyên màu; trên
   nền sáng cũng giữ nguyên (logo gốc thiết kế cho nền trắng).
4. Chữ: Montserrat; tiếng Việt/Trung trên LCD dùng `lv_font_vimate_*` (Montserrat + dấu).
