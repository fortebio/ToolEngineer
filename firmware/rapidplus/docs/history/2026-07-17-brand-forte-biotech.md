# 2026-07-17 — Áp logo + bảng màu Forte Biotech vào web

Nguồn: `src/download.png`. Client-only, **không đụng firmware**.

## Màu trích bằng cách đo pixel, không đoán bằng mắt

| Hex | Vai trò trong logo | Pixel |
| --- | --- | --- |
| `#20C6D0` | cyan chủ đạo (chữ + tam giác lớn) | 5129 |
| `#13A2BF` | teal đậm (tam giác tối) | 3044 |
| `#21DDBC` | mint green (tam giác dưới) | 1834 |
| `#00C2CD` · `#69EACA` | cyan thuần · mint nhạt | 830 · 761 |

## Vấn đề: màu logo KHÔNG dùng thẳng cho chữ được

Đo tương phản (WCAG AA cần **4.5** cho chữ thường, **3.0** cho chữ lớn/UI):

| Màu | Trên nền trắng | Chữ trắng trên nó |
| --- | --- | --- |
| `#20C6D0` | **2.09** FAIL | **2.09** FAIL |
| `#13A2BF` | 3.02 (chỉ chữ lớn) | 3.02 FAIL |
| `#21DDBC` | **1.73** FAIL | — |
| `#034078` (cũ) | 10.46 OK | — |

Đây là **thiết bị y tế** — người vận hành đọc nhiệt độ và kết quả P/N/S. Không thể hi
sinh khả năng đọc để khớp swatch. Giải pháp: **giữ đúng hue 183° của brand**, dẫn xuất
sắc độ đạt chuẩn.

## Token

```css
--brand:      #20C6D0;  /* cyan logo - accent, icon, mark (trang trí) */
--brand-mint: #21DDBC;  /* mint logo - accent phụ */
--brand-teal: #13A2BF;  /* teal logo - chỉ UI lớn */
--brand-ink:  #13757A;  /* CHỮ trên trắng - 5.45:1 */
--brand-deep: #083336;  /* nền tối (header) - chữ trắng 13.7:1 */
```

Map vào token cũ: `--navy` → `--brand-deep`, `--blue` → `--brand-ink`,
`--blue-600` → `#0F5F63` (hover **tối đi** để chữ trắng vẫn ≥7:1; trước đây hover sáng
lên, với brand thì sẽ rớt chuẩn). Header gradient `#0C494C → #083336`. Các mảng
wash xanh cũ (`#e8f0fd`, `#eef4fb`...) re-tint sang hue brand — chúng chỉ là nền trang
trí, không có chữ ngồi lên ở cỡ nguy hiểm.

## KHÔNG đổi (có chủ ý)

- **Nút Lysis xanh lá / Amplification đỏ** — màu **ngữ nghĩa khớp nút vật lý trên máy**.
  Đổi sang teal là phá mất mối liên hệ web ↔ phần cứng.
- **Badge kết quả P/N/S/E/B** và **10 màu series chart** — dữ liệu/ngữ nghĩa, cần phân
  biệt được với nhau; ép về brand sẽ hại.
- `--green`/`--amber`/`--red` semantic. Mint `#21DDBC` **không** thể làm màu "success"
  (contrast 1.73).

## Logo

- Gốc là **mode P, alpha toàn 255** → nền trắng nướng sẵn, thả lên header tối sẽ ra cục
  trắng. Đã tách nền trong suốt (giữ alpha mềm ở viền khử răng cưa).
- Cắt **chỉ phần mark** (tam giác, `x=24..96`; wordmark ở `x=102..232`): ở chiều cao
  header 40px, chữ trong logo sẽ chỉ ~6px — không đọc nổi. Wordmark giữ bằng text
  `.company-name` sẵn có.
- `data/logo.png` — 73×165, **2355 bytes**, nền trong suốt. Dùng cho cả header + favicon.
- Xoá `data/icons8-prawn-48.png` (favicon cũ, không còn ai tham chiếu).

## Kiểm chứng

Logo load OK (73×165), header gradient đúng, `.card-title` = `rgb(19,117,122)` =
`#13757A` đúng token, body bg `rgb(242,248,249)`. Đã nạp `uploadfs`.

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs`.
