# 2026-09-10 — Đồ thị đổi hình dạng khi xoay điện thoại: thang đọc bất biến + thanh trượt

> **Trạng thái: ĐÃ LÀM (2026-09-10).** Ba quyết định ở mục 6 đã chốt đúng theo đề xuất
> (landscape cho cuộn + bỏ header sticky · `CHART_MIN_PER_STEP = 2.5`). **Quyết định 1 đã bị
> đảo cùng ngày**: nút `Fit run` làm ra rồi **gỡ bỏ** — lý do ở file lịch sử.
> Kết quả, cái giá thực tế và guard: [docs/history/2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md](../history/2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md).
> Tài liệu này giữ lại làm **hồ sơ khảo sát** — số đo "trước khi sửa" và các phương án đã loại.

## 1. Triệu chứng

Cùng một run, cùng một máy: dựng điện thoại dọc thì đường khuếch đại **dốc đứng**, xoay
ngang thì **thoai thoải**. Người vận hành đọc *hình dạng* đường cong để phân biệt khuếch đại
thật với trôi quang / bậc thang — mà hình dạng đó đang là thuộc tính của **cái khung**, không
phải của **dữ liệu**.

## 2. Đo được (mock `--slots tools/slots.txt --reboot`, tab Result, run 39.7 phút)

Lệnh: `node tools/probe_chart_scale.js` (cần mock chạy sẵn). Độ dốc = góc lớn nhất giữa hai
mẫu liên tiếp, đo trên **chính instance Highcharts đang chạy** — không chép lại công thức
hình học vào probe, vì bản chép sẽ tự đồng ý với chính nó trong khi chart ship đã méo.

| Kích thước | plot W × H | px/phút | px/nấc | **phút mỗi nấc lưới Y** | **độ dốc** |
| --- | --- | --- | --- | --- | --- |
| dọc 360×740 | 238 × 347 | 5.88 | 34.7 | 5.90 | **82.1°** |
| dọc 390×844 | 268 × 451 | 6.62 | 45.1 | 6.81 | **83.1°** |
| dọc 412×915 | 290 × 522 | 7.17 | 52.2 | 7.28 | **83.6°** |
| **ngang 844×390** | 587 × **93** | 14.51 | **9.3** | **0.64** | **38.1°** |
| ngang 915×412 | 587 × 93 | 14.51 | 9.3 | 0.64 | 38.1° |
| tablet 768×1024 | 646 × 669 | 15.97 | 66.9 | 4.19 | 78.9° |
| desktop 1400×900 | 798 × 479 | 19.72 | 47.9 | 2.43 | 71.4° |

```text
slope    38.1deg .. 83.6deg   (spread 2.19x)
min/step 0.64    .. 7.28      (spread 11.38x)
```

**Cùng một cú lift-off vẽ ra 38.1° hay 83.6° tuỳ cái khung**, và "một nấc lưới đáng bao nhiêu
phút" **lệch 11.4 lần** giữa hai hướng cầm cùng một cái điện thoại.

**Nửa lớn hơn của vấn đề KHÔNG nằm ở trục X.** px/phút chỉ lệch 3.4× (5.88→19.7), còn
px/đơn-vị lệch **5.6×** (0.465→2.61). Thủ phạm chính là chiều cao plot ở landscape:
**93 px**, tức mỗi nấc lưới cao 9.3 px, nhãn cách nhau 18.6 px.

### 2b. Ở landscape, hơn nửa chiều cao chart là nhãn với legend

Container 210 px (`min-height` đỡ, vì `--chart-h` = 390−56−62−96 = 176). Đo bằng cách tắt
từng phần trên chính chart đang chạy rồi đọc lại `plotHeight`:

| Bỏ gì | plot H | chrome | px mỗi nấc |
| --- | --- | --- | --- |
| như đang ship | 93 | **117** | 9.3 |
| − tiêu đề trục X ("Time (min)") | 113 | 97 | 11.3 |
| − tiêu đề − legend | 155 | 55 | 15.5 |
| − tiêu đề − legend − spacing 2px | **176** | 34 | 17.6 |

Thu hồi chrome **một mình** đã đưa plot 93 → 176 px (**+89%**), không cần thêm UI nào.
Legend 10 mục ở đây **trùng chức năng** với cột chấm màu trong bảng slot và ô "All slots" —
nó đang lấy 42 px để nói lại thứ bảng ngay trên nó đã nói.

## 3. Gốc rễ

Cả hai trục đều **kéo giãn cho vừa khung**: X luôn là 0..hết run, Y luôn là 0..đỉnh, còn
khung thì bằng viewport. Nên

```text
độ dốc = atan2(Δy · plotH/yTop , Δx · plotW/runLen)
```

phụ thuộc `plotW` và `plotH` — hai đại lượng của thiết bị người xem, không phải của mẫu.

**Không thể cùng lúc có cả ba**: (a) lấp đầy cửa sổ · (b) thấy cả run · (c) hình dạng bất
biến. Code hiện tại chọn (a)+(b) và **mất (c)**. Thanh trượt chính là cơ chế cho phép **bỏ
(b)** — xem một *cửa sổ* thay vì cả run — để giữ (a)+(c).

## 4. Bất biến đề xuất

> **Một nấc lưới Y luôn cao bằng đúng `MIN_PER_STEP` phút của trục X.**

`tickPositioner` đã đảm bảo **luôn đúng 10 nấc** (guard `test_chart_ticks.js`), nên phát biểu
theo *nấc lưới* mạnh hơn và đơn giản hơn phát biểu theo px/đơn-vị: nó không phụ thuộc biên độ
dữ liệu. Đây là **giấy kẻ ô** — ô luôn cùng tỷ lệ, cửa sổ trượt trên tờ giấy.

Hai hằng số, đặt cạnh `BASELINE_START_MIN` trong `data/script.js` (cùng khuôn "TUNING KNOBS"):

```js
var CHART_MIN_PER_STEP = 2.5;    // one Y gridline step == this many minutes of X  (THE SHAPE)
var CHART_PX_PER_MIN_MIN = 10.4; // never draw the run tighter than this
```

`2.5` = đúng thang **desktop đang có** (bảng mục 2) — tức chọn cái người ta vốn đang đọc làm
chuẩn, không phát minh thang mới.

Quy tắc:

```text
pxPerMin   = max(CHART_PX_PER_MIN_MIN, plotWidth / runLen)  // scale, không bao giờ dưới sàn
plotHeight = 10 * pxPerMin * CHART_MIN_PER_STEP             // chiều cao SUY RA từ scale
window     = min(plotWidth / pxPerMin, runLen)              // số phút nhìn thấy
```

Màn to hơn thì **phóng to cả hai chiều cùng lúc** — vẫn cùng tỷ lệ, chỉ là to hơn.

## 5. Đo lại sau khi áp thử

Lệnh: `node tools/probe_chart_scale.js --after` — probe **áp quy tắc live lên chính chart
đang chạy** rồi đo lại y hệt mode BEFORE, nên hai bảng so được trực tiếp.

| Kích thước | plot W × H | px/phút | px/nấc | **phút/nấc** | cửa sổ/run | thanh trượt | **độ dốc** | card/chỗ-trống |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| dọc 360×740 | 238 × 260 | 10.4 | 26 | **2.5** | 22.9/39.7 | **có** | **71.9°** | 367/595 ✔ |
| dọc 390×844 | 268 × 260 | 10.4 | 26 | **2.5** | 25.8/39.7 | **có** | **71.9°** | 367/699 ✔ |
| dọc 412×915 | 290 × 260 | 10.4 | 26 | **2.5** | 27.9/39.7 | **có** | **71.9°** | 367/770 ✔ |
| ngang 844×390 | 587 × 370 | 14.81 | 37 | **2.5** | 39.6/39.7 | không | **71.9°** | 478/272 ⚠ |
| ngang 915×412 | 587 × 370 | 14.81 | 37 | **2.5** | 39.6/39.7 | không | **71.9°** | 478/294 ⚠ |
| tablet 768×1024 | 646 × 407 | 16.29 | 40.7 | **2.5** | 39.7/39.7 | không | **71.9°** | 515/879 ✔ |
| desktop 1400×900 | 798 × 562 | 20.13 | 56.2 | 2.79 | 39.6/39.7 | không | 73.7° | 662/755 ✔ |

```text
slope    71.9deg .. 73.7deg   (spread 1.03x)   <- was 2.19x
min/step 2.5     .. 2.79      (spread 1.12x)   <- was 11.38x
```

**38.1°–83.6° → 71.9° ở mọi kích thước.** Cột "phút/nấc" bằng 2.5 khắp nơi chính là bất biến,
đọc thẳng ra được.

Hai điều bảng này lộ ra:

- **Thanh trượt chỉ xuất hiện trên điện thoại DỌC** — đúng chỗ cần. Ở đó nó đổi 6.6 → 10.4
  px/phút (**+58% chi tiết ngang**) và trả lại 191 px chiều cao cho trang.
- **desktop lệch 2.79** vì rule `#screen-result … > .card { height: var(--chart-h) }` vẫn ghim
  card theo token cũ. Chiều cao chart nay **suy ra từ scale**, nên rule "hai thẻ cao bằng nhau"
  phải đọc chiều cao *thực tế của chart* thay vì `--chart-h`. Cùng hợp đồng "một token" đã ghi
  trong CLAUDE.md, chỉ là token đổi nguồn — **không được để hai công thức**.

## 6. Ba quyết định cần chốt

**Q1 — Có giữ chế độ "xem cả run trong một màn" không?**
Hôm nay nó là mặc định (và là cách duy nhất). Sau thay đổi, điện thoại dọc chỉ thấy ~26 phút.
Đề xuất: **giữ**, bằng một nút chuyển `Fit ⇄ 1:1` cạnh "All slots" — bỏ hẳn cái nhìn tổng quan
là một bước lùi, mà để nó xảy ra *ngầm* theo hướng cầm máy còn tệ hơn. Nút làm sự khác biệt
thành **hiển ngôn**.

**Q2 — Landscape được phép cuộn dọc không?**
Đây là chỗ đắt nhất: điện thoại ngang chỉ còn **272 px** dùng được (390 − header sticky 56 −
nav fixed 62), trong khi card ở thang chuẩn cao **478 px** → thiếu **206 px**. Ba đường:

- **(a) cho cuộn** — đơn giản nhất, trang vốn đã cuộn (`scrollHeight` 991 ở landscape). Nhưng
  thanh trượt nằm dưới chart sẽ **ra ngoài màn** → phải đặt thanh trượt **trên** chart.
- **(b) thu hồi header** — `@media (max-height: 599px) { .header { position: static } }` lấy
  lại 56 px. Vẫn thiếu ~150.
- **(c) hạ scale ở landscape cho vừa** — hình dạng **vẫn bất biến** (nấc và px/phút co cùng
  nhau), chart chỉ nhỏ hơn; cái giá là run 39.7 phút chỉ chiếm 310/587 px bề rộng, thừa một
  khoảng trống bên phải, hoặc phải thu bề rộng chart lại và căn giữa.

Đề xuất: **(a) + (b)**, thanh trượt đặt trên chart.

**Q3 — `CHART_MIN_PER_STEP` lấy 2.5 (thang desktop) hay số khác?**
Đây là hằng số quyết định "đường cong trông thế nào" cho cả fleet. 2.5 là thang kỹ sư đang
nhìn trên desktop. Nếu anh vẫn đọc theo máy khác thì chốt theo máy đó — phải chốt **một** số.

## 7. Việc phải làm (sau khi chốt)

1. `data/script.js` — hai hằng số + `applyChartScale(view)`; gọi ở `loadCurve`, `plotPoint`,
   và trên `resize`/`orientationchange`.
2. Thanh trượt: `<input type="range" class="chart-pan">`, `step = minPerRound`,
   `aria-label`/`aria-valuetext` đọc ra "phút X tới Y". Dùng input thật để có sẵn phím mũi
   tên/Home/End và screen reader (Setting #9-#12) — **không** tự chế div, và **không** dùng
   `chart.scrollablePlotArea` (thanh cuộn native quá mảnh cho thao tác đeo găng, và không có
   đường bàn phím).
3. **Live run phải tự bám đuôi**: cửa sổ trôi theo điểm mới, trừ khi người dùng đã kéo — kéo
   về sát cuối thì bám lại. Không có mục này thì chart Home đứng yên trong khi run chạy tiếp.
4. Thu hồi chrome: bỏ `xAxis.title`, tắt legend trên mobile, `spacingTop/Bottom` 2px.
5. Sửa rule "hai thẻ Result cao bằng nhau" theo mục 5.
6. Guard `tools/test_chart_scale.js` — quét 6 kích thước, assert **phút/nấc bằng nhau ở mọi
   kích thước** và thanh trượt chỉ hiện khi cửa sổ < độ dài run. Đo trên chart thật (như
   `test_chart_ticks.js`), không chép công thức.
7. `docs/history/` + link vào CLAUDE.md khi làm xong.

## 8. Đã cân nhắc và loại

- **Highcharts Stock navigator** — cần `highstock.js`, flash đang **74.8%**, và bundle hiện tại
  (`highcharts.js` v11.4.8 base) **không có** module Navigator/Scrollbar.
- **Pinch-zoom + kéo (`chart.zooming`/`panning`)** — có sẵn, 0 dòng, nhưng không khám phá được,
  đá nhau với zoom trang (và CLAUDE.md đã cấm `maximum-scale=1`), không có đường bàn phím.
- **`chart.scrollablePlotArea`** — có trong bundle, khoá được px/phút với 0 dòng JS, nhưng
  **không đụng tới nửa lớn hơn của vấn đề** (chiều cao) và không có a11y. Dùng được làm bước
  thử nhanh, không dùng làm đích.
- **Ép tỷ lệ khung plot cố định (16:9)** — giữ hình dạng *trong một run* nhưng thang vẫn trôi
  theo biên độ dữ liệu giữa các run, và không giải quyết được việc landscape chỉ cao 93 px.
