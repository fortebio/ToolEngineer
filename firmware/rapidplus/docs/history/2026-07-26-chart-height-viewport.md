# 2026-07-26 — Chiều cao chart co giãn theo cửa sổ trình duyệt

## Vấn đề

Home → chart (lúc chạy run) **không lấp hết chỗ trống của trang**. Chart cao cố định
theo breakpoint, không liên quan gì tới cửa sổ thật:

| Màn | Chart cao | Ghi chú |
| --- | --- | --- |
| mobile 390×844 | 320 px | `.chart-container { height: 320px }` |
| landscape 844×390 | 210 px | media riêng |
| desktop 1280×860 | 436 px | `aspect-ratio: 16/9` theo bề rộng cột |
| wide 1600×1000 | 556 px | nt |

Đo trên desktop 1280×860 (mock `--full`, phase amplification): đáy card chart ở
`y = 604` trong khi viewport cao `860` → **thừa 256 px trống** ngay dưới đường cong,
đúng phần user chỉ ra. Chart 16:9 chỉ scale theo *bề rộng cột*, nên cửa sổ cao lên
bao nhiêu cũng không giúp gì.

## Sửa

**Một công thức duy nhất** cho cả chart Home lẫn Result, mọi kích thước
(`data/style.css`):

```css
.chart-container {
  width: 100%;
  height: calc(100vh  - var(--header-h) - var(--nav-h) - 6rem);
  height: calc(100dvh - var(--header-h) - var(--nav-h) - 6rem);
  min-height: 210px;
}
```

Kéo theo **xoá 4 override** (thực chất là dọn bớt CSS, không phải thêm):

- `@media desktop`: `.chart-container { height: 460px }`
- `@media desktop`: khối `#screen-home #homeChartCard .chart-container { aspect-ratio: 16/9; ... }`
- `@media desktop`: `#screen-result #resultChartCard .chart-container { height: calc(...) }`
  (đã trùng y hệt công thức nền)
- `@media landscape`: `.chart-container { height: 210px }` (min-height lo phần này)

Hai điểm nhỏ nhưng load-bearing:

1. **`body.nonav { --nav-h: 0px }`** — lúc chạy run nav bị ẩn và `padding-bottom` được thu
   hồi, nhưng công thức chart vẫn trừ 62 px cho một thanh nav **không còn ở đó** → hụt đúng
   62 px. Hạ token về 0 thì mọi chiều cao tính từ nó tự đòi lại dải đó. `.bottom-nav` cũng đọc
   token này nhưng nó đang `display:none` nên vô hại.
2. **`--header-h: 56px` trong media landscape** — media đó thu header xuống 56 px nhưng token
   vẫn là 80 px của portrait; không sửa thì chart bị trừ dư 24 px.

`100dvh` đứng sau `100vh` (progressive enhancement): điện thoại quét QR vào dashboard co
viewport khi thanh địa chỉ rụt lại, `dvh` bám theo còn `vh` thì không. Trình duyệt cũ bỏ qua
dòng sau, giữ dòng `vh`.

**Không đụng `script.js`**: Highcharts tự `reflow()` khi cửa sổ resize, và config
`makeChart()` không đặt `chart.height` nên CSS là nguồn duy nhất quyết định chiều cao.

## Đo lại (cùng probe, mock `--full`, phase amplification)

| Màn | Chart trước | Chart sau | Khoảng trống dưới card |
| --- | --- | --- | --- |
| mobile 390×844 | 320 | **668** | trang cuộn (chart nằm dưới bảng legend) |
| landscape 844×390 | 210 | **238** | trang cuộn |
| desktop 1280×860 | 436 | **684** | 256 px → **8 px** |
| wide 1600×1000 | 556 | **762** | 272 px → 70 px (nav hiện, phase finished) |

`6rem` (thay vì `5rem` như rule Result cũ) là phần trừ cho `card-head` + padding card +
padding trang: với `5rem` đáy card lố khỏi màn 8 px trên desktop.

## Kiểm

```bash
python tools/sse_test_server.py --full 8011
node tools/test_chart_ticks.js                      # trục Y vẫn đúng 10 nấc, sàn 50 — PASS
RAPID_URL=http://localhost:8011 node tools/test_full_run.js
```

`test_full_run.js`: **toàn bộ mục chart PASS** (chart hiện lúc confirm, 120 điểm, trục X
39.67 phút, `/curve` backfill khớp, chart ở lại sau finished, Result vẽ lại đủ run).

⚠️ Còn **3 FAIL có sẵn từ trước, không liên quan thay đổi này**: test còn tìm input
`.slot-name` trong `#namingBody`/`#slotBody`, trong khi cột naming đã đổi thành
`<select class="disease-sel">` + `.sample-name` (xem
[2026-07-24-slot-sample-name.md](2026-07-24-slot-sample-name.md)). Ba mục hỏng là
"typed name persisted", "confirmed name applied to the chart series", "Result table shows
the name set on Home" — cùng một nguyên nhân selector cũ, cần cập nhật test.

## Bẫy đã gặp

Mock cũ **đang chạy sẵn ở cổng 8000** (bản không `--full`) làm lần chạy test đầu đỏ hàng loạt
(44 vòng, 1200 ms/vòng thay vì 120 vòng, 20000 ms). `sse_test_server.py` nhận **tham số số =
cổng** → chạy `--full 8011` rồi `RAPID_URL=http://localhost:8011` là tránh được, khỏi phải giết
tiến trình của người khác.
