# 2026-09-10 — Đồ thị: thang đọc bất biến theo màn hình + thanh trượt

Kế hoạch và số liệu khảo sát: [docs/plan/2026-09-10-chart-scale-bat-bien-va-thanh-truot.md](../plan/2026-09-10-chart-scale-bat-bien-va-thanh-truot.md).
Bản này là phần **đã làm** sau khi chốt 3 quyết định ở mục 6 của kế hoạch đó.

> **Bổ sung 2026-09-11:** công thức dưới đây lấy `runLen` = số phút đã có, đúng cho run **đã lưu**
> nhưng sai cho run **đang chạy** (chart Home cao 4 142 px ở vòng 6 rồi co dần). Thang của run live nay
> suy từ độ dài **dự kiến** — xem
> [2026-09-11-chart-scale-run-live-theo-do-dai-du-kien.md](2026-09-11-chart-scale-run-live-theo-do-dai-du-kien.md).

## Trước

Cả hai trục kéo giãn cho vừa khung, khung thì bằng viewport. Hệ quả: **hình dạng đường cong
là thuộc tính của cái điện thoại, không phải của mẫu**. Đo trên cùng một run đã lưu
(`node tools/probe_chart_scale.js`, mock `--slots tools/slots.txt --reboot`, run 39.67 phút):

| Kích thước | plot W × H | px/phút | px/nấc | **phút mỗi nấc lưới Y** | **độ dốc lift-off** |
| --- | --- | --- | --- | --- | --- |
| dọc 360×740 | 238 × 347 | 5.88 | 34.7 | 5.90 | **82.1°** |
| dọc 390×844 | 268 × 451 | 6.62 | 45.1 | 6.81 | **83.1°** |
| dọc 412×915 | 290 × 522 | 7.17 | 52.2 | 7.28 | **83.6°** |
| **ngang 844×390** | 587 × **93** | 14.51 | **9.3** | **0.64** | **38.1°** |
| tablet 768×1024 | 646 × 669 | 15.97 | 66.9 | 4.19 | 78.9° |
| desktop 1400×900 | 798 × 479 | 19.72 | 47.9 | 2.43 | 71.4° |

```text
min/step 0.64 .. 7.28   (lệch 11.38x)
slope    38.1° .. 83.6° (lệch 2.19x)
```

Xoay điện thoại là đổi cách đọc cùng một kênh. Trên máy gọi kết quả y tế, đó không phải
chuyện thẩm mỹ.

Nửa lớn hơn của vấn đề **không nằm ở trục X**: px/phút chỉ lệch 3.4×, px/đơn-vị lệch 5.6×.
Ở landscape plot chỉ cao **93 px** vì **117/210 px là chrome** — tiêu đề trục + legend + spacing.

## Nay

**Bất biến, phát biểu một câu:**

> Một nấc lưới Y luôn cao bằng đúng `CHART_MIN_PER_STEP` phút của trục X.

`tickPositioner` vốn đã bảo đảm **luôn đúng 10 nấc** với mọi biên độ dữ liệu, nên phát biểu
theo *nấc lưới* độc lập với biên độ — mạnh hơn phát biểu theo px/đơn-vị. Đây là **giấy kẻ ô**:
ô luôn cùng tỷ lệ, cửa sổ trượt trên tờ giấy.

Hai hằng số, `data/script.js`, đặt cạnh khối nút chỉnh baseline:

```js
var CHART_MIN_PER_STEP = 2.5;    // one Y gridline step == this many minutes of X (THE SHAPE)
var CHART_PX_PER_MIN_MIN = 10.4; // and never draw the run tighter than this
```

`2.5` = **đúng thang desktop vốn đang render** (đo 2.43) — chuẩn hoá theo cái kỹ sư vẫn đọc,
không phát minh thang mới.

`applyChartScale(v)`:

```text
pxMin      = max(CHART_PX_PER_MIN_MIN, plotWidth / runLen)
plotHeight = 10 * pxMin * CHART_MIN_PER_STEP     // chiều cao SUY RA từ scale
window     = min(plotWidth / pxMin, runLen)      // số phút nhìn thấy
```

Không đủ chỗ cho cả run → hiện **thanh trượt** để trượt cửa sổ. Màn to hơn thì phóng to cả hai
chiều cùng lúc, tỷ lệ không đổi.

Đo lại trên bản đã ship:

| Kích thước | plot W × H | px/phút | px/nấc | **phút/nấc** | cửa sổ/run | thanh trượt | **độ dốc** |
| --- | --- | --- | --- | --- | --- | --- | --- |
| dọc 360×740 | 238 × 260 | 10.4 | 26 | **2.50** | 22.9/39.7 | **có** | **71.9°** |
| dọc 390×844 | 268 × 260 | 10.4 | 26 | **2.50** | 25.8/39.7 | **có** | **71.9°** |
| dọc 412×915 | 290 × 260 | 10.4 | 26 | **2.50** | 27.9/39.7 | **có** | **71.9°** |
| ngang 844×390 | 587 × 371 | 14.8 | 37.1 | **2.51** | 39.7/39.7 | không | **71.9°** |
| tablet 768×1024 | 646 × 407 | 16.3 | 40.7 | **2.50** | 39.7/39.7 | không | **71.9°** |
| desktop 1400×900 | 798 × 504 | 20.1 | 50.4 | **2.51** | 39.7/39.7 | không | **71.9°** |

```text
min/step 2.499 .. 2.507  (lệch 1.003x)   <- was 11.38x
slope    71.9°  .. 71.9° (lệch 1.00x)    <- was 2.19x
```

Thanh trượt **chỉ hiện trên điện thoại dọc** — đúng chỗ cần; ở đó nó nâng 6.6 → 10.4 px/phút
(**+58% chi tiết ngang**) và trả lại 191 px chiều cao cho trang.

### Ba quyết định đã chốt (quyết định 1 đã ĐẢO trong ngày)

1. ~~Giữ đường xem cả run bằng nút `Fit run`~~ → **KHÔNG. Chỉ có MỘT thang đọc, và chỉ có
   thanh trượt.** Nút đã làm xong (`aria-pressed`, trả chiều cao về `--chart-h`,
   `setExtremes(null, null)`) rồi **gỡ bỏ ngay trong ngày** theo yêu cầu của chủ dự án.

   Lý lẽ ban đầu — *"bỏ cái nhìn tổng quan là bước lùi; nút làm khác biệt thành hiển ngôn"* —
   đứng vững ở phần thứ hai và **sập ở phần thứ nhất**. Hai thang đọc trong cùng một sản phẩm
   nghĩa là **độ dốc của một đường cong chỉ có nghĩa sau khi đã kiểm chế độ nào đang bật** —
   đúng cái điều kiện mà cả tính năng này sinh ra để xoá. Người vận hành đọc chart trên điện
   thoại của họ, không đọc `aria-pressed`; ảnh chụp gửi cho nhau cũng không mang theo trạng thái
   nút. Một nút tự nó là hiển ngôn vẫn để lại **hai bức ảnh khác nhau của cùng một run**, và
   không có cách nào biết bức nào là bức nào sau khi nó rời màn hình.

   Và cái "tổng quan" mất đi thì **không mất**: thanh trượt cho xem đúng run đó, từng cửa sổ
   một, ở đúng thang mà mọi máy khác đang dùng. Cái giá là phải kéo — đổi lại **không bao giờ
   phải hỏi "chart này đang ở chế độ nào"**.

   Hệ quả kỹ thuật: `applyChartScale()` không còn nhánh, `makeView()` không còn cờ `fit`,
   `.chart-fit` biến khỏi CSS, và **chiều cao chart do script sở hữu vĩnh viễn** (không còn
   đường trả nó về cho CSS).
2. **Landscape được phép cuộn**, kèm **bỏ `position: sticky` của header** dưới `max-height: 599px`
   (lấy lại 56 px). Thanh trượt vì thế đặt **TRÊN** chart, không phải dưới — dưới thì nó nằm
   ngoài màn đúng lúc cần nhất.
3. **`CHART_MIN_PER_STEP = 2.5`** (thang desktop).

### Kéo theo

- **Chrome bị thu hồi**: bỏ `xAxis.title` ("Time (min)"), `spacingBottom: 2`. Đơn vị và vị trí
  trong run chuyển sang **dòng chữ cạnh thanh trượt** (`13.9–39.7 of 39.7 min`) — nó nói thêm
  *đang xem đoạn nào*, thứ tiêu đề trục chưa bao giờ nói.
- **`spacingTop: 10`, KHÔNG phải 2.** Nhãn trục trên cùng vẽ **căn giữa** đường lưới trên cùng,
  nên spacing 2 px cắt đôi chữ `200`. Bắt được bằng ảnh chụp, không bằng guard.
- **Legend: tắt ở Result, GIỮ ở Home.** Không phải lựa chọn thẩm mỹ — trên điện thoại, Home
  **ẩn bảng slot** khi chart lên (media rule có từ 2026-07-26), và legend `#1..#10` của Highcharts
  chính là thứ thay thế nó. Result thì bảng slot với chấm màu nằm ngay đó nên legend là thông tin
  lặp, mà ở landscape nó ăn 42 của 210 px.
- **Thẻ bảng ở Result (desktop) theo `--chart-card-h`**, token do `applyChartScale` ghi từ chiều
  cao **thật** của thẻ chart. Trước đây cả hai thẻ ghim vào `--chart-h`; nay chiều cao chart là
  **suy ra**, nên `--chart-h` không còn là sự thật. Vẫn **một nguồn duy nhất** (chart), không có
  công thức thứ hai để lệch. Rule cũ bị bỏ khỏi chính `#resultChartCard` (nó tự co theo nội dung).
- `--chart-nav-h: 40px` trừ vào `--chart-h` để hàng điều khiển không đẩy thẻ chart dài ra.
  **`--chart-h` nay chỉ còn là chiều cao của chart RỖNG** (chưa có run thì không có độ dài để
  suy chiều cao ra); có dữ liệu là script ghi inline và không bao giờ nhả lại.
- **Live run tự bám đuôi**: cửa sổ trôi theo điểm mới, trừ khi người dùng đã kéo đi; kéo về sát
  cuối thì **tái vũ trang**. Thiếu mục này thì chart Home đứng yên trong khi run vẫn chạy.
- `resetView()` đưa `panStart = 0`, `follow = true` (run mới thì vị trí trượt của run cũ vô
  nghĩa). Không còn trạng thái xem nào khác để giữ — thang đọc là cố định.

## Hai lỗi guard bắt được (không phải lỗi guard)

1. **Thanh trượt không bao giờ chạm được đầu cuối.** `<input type=range>` chỉ nhận giá trị dạng
   `min + n*step`, và trần thật của nó là `min + floor((max-min)/step)*step`. Với step = một vòng
   (0.333 phút) và max 13.9, kéo hết sang phải chỉ tới **13.65** — hụt đúng một vòng. Vì
   tail-follow tái vũ trang theo điều kiện "đang ở max", nó **không bao giờ** tái vũ trang được.
   Sửa: một nấc **mỗi vòng**, `notches = round(maxStart / minPerRound)`, rồi chia dải cho số nấc đó.
   - **Phải `floor` cái nấc rồi suy `max` TỪ nó**, không được `toFixed`. `toFixed(6)` có thể làm
     tròn **lên**, và nấc chỉ cần lớn hơn một hạt là `notches*step > max` → trình duyệt bỏ nấc
     cuối và lỗi tái hiện, chỉ lùi thêm một chữ số thập phân. Đã dẫm đúng bẫy này một lần.
2. **Nhãn trục trên cùng bị cắt đôi** (mục `spacingTop` ở trên) — cái này ảnh chụp bắt, guard không.

## Đã loại

- **Highcharts Stock navigator** — cần `highstock.js`; bundle hiện tại (`highcharts.js` v11.4.8
  base) **không có** module Navigator/Scrollbar, và flash đang 72.9%.
- **Pinch-zoom + kéo (`chart.zooming`/`panning`)** — có sẵn, 0 dòng, nhưng không khám phá được,
  đá nhau với zoom trang (CLAUDE.md đã cấm `maximum-scale=1`), và không có đường bàn phím.
- **`chart.scrollablePlotArea`** — có trong bundle, khoá được px/phút với 0 dòng JS, nhưng **không
  đụng tới nửa lớn hơn của vấn đề** (chiều cao) và không có tên khả truy cập.
- **Kẹp chiều cao plot theo chỗ trống của viewport** — đã thử, **phá chính bất biến**: rút ngắn
  plot không rút ngắn trục X, nên run lại giãn ra lấp hết bề rộng và `min/step` tụt **2.50 → 1.76**
  trong khi chart trông vẫn bình thường. **Chiều cao phải suy ra từ scale, không bao giờ ngược
  lại.** Comment cảnh báo đã đặt ngay tại chỗ tính `pxMin`.

## Cái giá đã biết — landscape

Điện thoại ngang chỉ còn **272 px** dùng được (390 − header 56 − nav 62) trong khi thẻ chart ở
thang chuẩn cao **~530 px**. Đây là đánh đổi đã chốt ở quyết định 2, nhưng **thực tế xấu hơn con
số "cuộn ~200 px" gợi ra**: sàn trục Y là **200** trong khi run này đỉnh ~110, nên **nửa trên của
plot vốn đã trống** — mở tab Result ở landscape thấy **ba đường lưới trắng, không có đường cong
nào**, phải cuộn mới thấy dữ liệu.

Đường sửa nếu muốn đổi: hạ `pxMin` xuống đúng sàn 10.4 ở landscape (plot 260 px, **vừa** 272 px)
và **không kẹp cửa sổ về `runLen`** — run chiếm 413/587 px bề rộng, thừa một khoảng trống bên
phải. Bất biến vẫn giữ nguyên; đó chính là phương án (c) đã cân nhắc và loại ở quyết định 2.

## Guard

`node tools/test_chart_scale.js` (**cần mock `--slots tools/slots.txt --reboot` chạy sẵn**) —
đo trên **chính instance Highcharts đang chạy**, không chép lại công thức hình học:

1. `min/step` **giống nhau ở cả 6 kích thước** (ngưỡng 1.02×) ← chính là bất biến
2. thanh trượt bật/tắt **đúng khi và chỉ khi** cửa sổ ngắn hơn run
3. kéo thanh trượt thật sự dời cửa sổ, và **giữ nguyên bề rộng cửa sổ + thang đọc**
4. **không có đường quay lại** thang kéo giãn: `querySelectorAll('.chart-fit, [id$=ChartFit]')`
   phải **rỗng**, và chiều cao chart phải còn là inline `<số>px` (nhả về CSS = hành vi cũ theo
   đường khác)
5. tail-follow tái vũ trang khi trượt về cuối
6. trục Y vẫn **đúng 10 nấc** ở mọi kích thước (không phá `test_chart_ticks.js`)

**Negative test (2 hạt gieo, cả hai đều đỏ đúng chỗ):**

- gieo `fit: true` (quay về view kéo giãn cũ) → `min/step` spread **6.936×** (1.213..8.413), 16 fail
  *(hạt gieo này thuộc bản có nút; nay nhánh đó không còn tồn tại để gieo)*
- gieo lại bug step cũ (`step = một vòng`, `max = maxStart`) → đúng **3 fail** của tail-follow
- gieo lại **chính cái nút** (`<button class="chart-fit">` vào `#resultChartNav`) → 1 fail, đúng
  check 4a; gieo `el.style.height = ""` (trả chiều cao cho CSS) → 1 fail, đúng check 4b

**Hai bẫy trong chính guard, đã sửa, đừng lặp lại:**

- **Đổi kích thước rồi ngủ cố định là không đủ.** Trang debounce resize; cú chuyển
  desktop → portrait (đồng thời lật cờ `mobile` của CDP) lâu hơn 850 ms, nên phép đọc kế tiếp trả
  **hình học cũ** và 5 check đỏ vì một lý do. Phải **poll**. Và điều kiện dừng phải kiểm
  `innerWidth === bề rộng yêu cầu` — "hai lần đọc giống nhau" **không phân biệt được** *đã ổn định*
  với *chưa hề đổi*.
- **`--user-data-dir` cố định là bẫy.** Chạy lại khi browser của lần trước còn sống thì Edge
  **không mở cái mới, nó gắn vào cái cũ**, và tab còn sót (còn zoom, còn kích thước cũ) trả
  lời mọi truy vấn — nhìn ra y hệt **16 lỗi sản phẩm**, thực chất là một cửa sổ cũ. Nay profile
  **duy nhất theo `process.pid`**.

Đã đăng ký trong `tools/check.py` (tier `mock`).

## Đo lại bất cứ lúc nào

```bash
python tools/sse_test_server.py --slots tools/slots.txt --reboot   # shell khác
node tools/probe_chart_scale.js            # bảng "as shipped"
node tools/probe_chart_scale.js --after    # áp thử một bộ hằng số khác
MIN_PER_STEP=3.5 node tools/probe_chart_scale.js --after           # dò giá trị khác
```
