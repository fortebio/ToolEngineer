# 2026-07-23 — Soát lại toàn bộ giao diện web dashboard (contrast + a11y + layout)

Rà soát tổng thể `data/index.html` + `data/style.css`. **Không** đổi stack: dashboard vẫn là
HTML/CSS/JS thuần nạp từ LittleFS (không npm, không build step, mỗi byte đều nằm trong flash).

## Cách đo, không đoán

Viết script tính WCAG cho **toàn bộ cặp màu**, và **hợp nhất alpha/opacity xuống nền thật
trước khi đo** — đây là chỗ mắt thường bỏ sót: một màu đạt chuẩn ở opacity 1 có thể tụt thảm
hại khi bị làm mờ. Kết quả ban đầu: **18/30 cặp dưới AA**.

Ảnh chụp thật qua CDP (`tools/ui_screenshot.js`). **Cờ `--screenshot` của Edge KHÔNG dùng được**
ở đây: trang giữ kết nối SSE mãi mãi nên `--virtual-time-budget` không bao giờ hết → browser
treo. Phải điều khiển qua DevTools Protocol như `test_full_run.js`.

## Đã sửa

**1. Badge kết quả P/N/S/E/B — nghiêm trọng nhất.** Đây là thông tin quan trọng nhất màn hình
mà contrast thấp nhất (**3.66–4.38**). Mã màu **giữ nguyên** (là dữ liệu, và khớp màu trên TFT:
P đỏ, N xanh, S vàng, E cam, B cyan — displayLCD.cpp:1297+), chỉ **làm đậm chữ theo đúng hue**
→ **5.01–5.09**. Vẫn cùng đỏ/cùng xanh, chỉ đọc được.

**2. Chú giải P/N/S/E/B.** Trước đây **không chỗ nào** giải thích. Nghĩa lấy từ nguồn sự thật
`src/Alg/AlgoData.h:40-43` + `sensor6035.cpp:329`, **không phải suy đoán** — và suy đoán sẽ sai:

| | nghĩa thật | dễ đoán nhầm thành |
| --- | --- | --- |
| S | **Slight Positive** | ~~Suspect~~ |
| B | **Break** (đường cong không hợp lệ) | ~~Blank~~ |

`result[i] = recordOut.outcome.outcome[0]` — chữ cái là **ký tự đầu của chuỗi outcome**.

**3. Nút Lysis/Amplification.** Chữ trắng trên nền màu chỉ đạt 4.27 (đỏ) / 3.17 (xanh), và
trạng thái **sáng lên khi nhấn** còn tệ hơn: **3.11 / 2.28** — tức khó đọc nhất đúng lúc người
dùng đang nhìn nút vừa bấm. Nền vẫn **bắt buộc đỏ/xanh khớp nút vật lý**, nên chỉ đổi sắc độ
cùng hue (`#e23b3b→#d92020`, `#1fa64d→#18803c`, đều 5.0+). Trạng thái `.on` **không làm sáng
nền nữa** — báo hiệu bằng **ring + glow**, rõ ngang cũ mà không tốn contrast.

**4. Trạng thái vô hiệu: dùng `grayscale`, KHÔNG dùng `opacity`.** `opacity` làm nhạt **cả chữ
lẫn nền** nên tỷ lệ sập từ hai phía — `.noact` opacity .45 đo được **1.26:1**, gần như vô hình,
mà người vận hành **vẫn cần đọc** nút chỉ đang tạm không dùng được để biết mình ở bước nào.
`grayscale()` **bảo toàn luminance** nên giữ nguyên khả năng đọc:

| | trước | sau |
| --- | --- | --- |
| `.noact` | opacity .45 → **1.26** | opacity .85 + grayscale .65 → **4.68** |
| `.locked` | opacity .5 → **1.47** | opacity .85 + grayscale .8 → **4.85** |

Kèm sửa `.noact:hover { filter: none }` — nó **xoá bộ lọc khi rê chuột**, làm nút vô hiệu trông
như đang hoạt động.

**5. Focus bàn phím.** `:focus-visible` trước đó **0 lần**, nhiều input đặt `outline: none` rồi
chỉ đổi border 1px. Thêm ring 3px màu brand; đặt **cuối file** vì các rule `:focus` cùng độ đặc
hiệu phía trên chỉ thua khi rule mới đứng sau.

**6. `aria-live`.** 5 vùng tự cập nhật (status Online/Offline, banner thông báo, banner trạng
thái, `set-msg` Saved/Error, banner khoá Setting) trước đây đổi nội dung **im lặng**.

**7. `prefers-reduced-motion`** — 9 transition + keyframes fade nay tôn trọng thiết lập OS.

**8. Vùng chạm.** Checkbox **15×15 → 24×24** (tối thiểu WCAG 2.5.8; phòng lab còn đeo găng).

**9. Tàn dư palette cũ.** 4 chỗ còn `rgba(3, 64, 120, ...)` (navy trước rebrand) trong bóng đổ
`#viewChartBtn`/`.save-btn` và nền tab active → đổi sang hue brand `rgba(19, 117, 122, ...)`.
Thêm `--green #16a34a → #11803a` (nút Confirm và chữ "Saved" đều chỉ đạt 3.30) và
`f-unit #9aa4b2 → #637083` (**2.52** → 5.03).

**10. Layout desktop.** Sidebar trước đây **trắng trên nền trắng** với một mảng trống phía trên
vì header bắt đầu **sau** cột sidebar. Nay header chạy hết chiều ngang (`margin-left` âm +
`padding-left` bù), sidebar bắt đầu **dưới** header (`top: var(--header-h)`) và có nền `#f7fbfc`
để tách khỏi nội dung. `body.nonav` được xử lý riêng (giữa run sidebar bị ẩn). Hàng nút bị chặn
`max-width: 780px` — trên màn 1280 mỗi nút từng giãn ~600px, đọc như ba banner hơn là ba nút.

## Kết quả đo lại (đọc thẳng từ file đã sửa)

Toàn bộ **PASS**: badge 5.01–5.09 · nút 5.01–5.03 · Confirm/set-msg 5.03 · f-unit 5.03 ·
`.noact` 4.68 · `.locked` 4.85. Kèm: 11 rule `:focus-visible`, 1 `prefers-reduced-motion`,
0 tàn dư navy, checkbox 24px, 5 vùng `aria-live`, 5 mục legend.

Chi phí flash (sau gzip, tức phần thực nạp): `style.css` **8 354 B**, `index.html` **2 819 B**.

## Bổ sung: làm tab Result gọn lại

Ba thứ khiến bảng "ồn", sửa cả ba mà không đụng logic:

**1. Mười ô select rỗng có viền.** Đây là thứ ồn nhất: cái khung hút mắt ngang với dữ liệu,
trong khi cả 10 ô đều đang trống. Nay ô **chưa gán bệnh bỏ hẳn viền + nền**
(`.disease-sel:not(.assigned)`), chỉ hiện lại khi hover/focus; ô **đã gán** vẫn là chip xanh
như cũ. Dấu `—` được căn giữa, vì khi mất khung nó nằm sát trái còn chevron sát phải, đọc
thành hai ký hiệu rời nhau.

**2. Mọi hàng nhấn ngang nhau.** Hàng có phát hiện (`P`/`S`) nay được `tr.hit`: nền hồng rất
nhạt `#fff7f5`, số CT in đậm màu ink, số slot hết xám. Ý nghĩa **vẫn nằm ở chữ cái badge** —
sắc nền chỉ dẫn mắt, không mang thông tin (chênh 1.057 so với hàng trắng). Contrast trên nền
này vẫn đạt: chữ mờ 4.58 · badge P 5.90 · CT 13.76. Selector dùng `#slotBody` để thắng rule
zebra.

**3. Bảng trải hết 1500px trên desktop.** Checkbox và badge kết quả của **cùng một hàng** cách
nhau gần cả màn hình, mắt phải quét ngang để đọc một dòng. `#screen-result .card` nay giới hạn
**980px** (Home vẫn full width vì nó là lưới dashboard).

Kèm: cột CT dùng `tabular-nums` để dấu thập phân thẳng hàng, và CT rỗng dùng `.res-empty` cho
mờ đi thay vì `-` đen đậm ngang giá trị thật.

## Đổi cấu trúc bảng: 5 cột → 3 cột

Bước trên mới là trang trí; bước này đổi **cấu trúc**.

**Trước**: `Show | Slot | Disease | CT | Result`. Trên desktop, checkbox và badge kết quả của
**cùng một hàng** cách nhau gần cả màn hình — đọc một dòng phải quét hết bề ngang.

**Sau**: `Sample | CT | Result`, với cột Sample gộp **chấm + số slot + ô chọn bệnh** — tức mọi
thứ định danh mẫu nằm cạnh nhau.

**Cột "Show" không bị xoá, nó biến thành thứ hữu ích hơn**: checkbox vẫn là
`<input type="checkbox">` thật (bàn phím, screen reader, `onToggle`/đồng bộ hai bảng đều
nguyên vẹn), chỉ đổi diện mạo thành **chấm mang đúng màu đường của slot đó trên chart**
(`--series` đặt theo hàng trong `script.js`; `SERIES_COLORS` được tách khỏi `buildSeries()`
để hai nơi dùng chung). Đầy = đang vẽ, rỗng = đang ẩn. Trước đây bảng **không hề cho biết**
slot nào ứng với đường màu nào — phải dò qua legend của chart.

### Hai cái bẫy gặp phải (đều phát hiện bằng cách ĐO, không phải nhìn)

**1. `display: flex` đặt thẳng lên `<td>`** làm ô mất vai trò table-cell → đường kẻ hàng không
còn thẳng across các cột. Fix: flex nằm ở `div.sample-cell` **bên trong** `td`.

**2. Trên mobile ô chọn bệnh mất hẳn nội dung** (chỉ còn mũi tên). Nhìn ảnh chỉ thấy "mất chữ",
đo DOM mới ra nguyên nhân thật:

| | trước | sau |
| --- | --- | --- |
| cột Sample | 110 px | 152 px |
| select | 35 px | 78 px |
| **vùng chữ trong select** | **2 px** | **50 px** |

Riêng padding của select đã ăn 33 px, nên ở 35 px thì **không còn chỗ nào để vẽ chữ**. Nguyên
nhân gốc: hai cột CT/Result cố định 5.5rem mỗi cột ăn hết bề ngang màn hẹp. Nay mobile để
**3.2rem**, desktop mới trả về 5.5rem; kèm thu gap/dot/số slot và bớt gutter chevron. Kiểm lại
bằng chuỗi dài nhất thực tế (`WSSV` = **37 px** < 50 px) chứ không ước lượng bằng mắt.

## Điện thoại xoay ngang: breakpoint chỉ theo bề ngang là sai

**Triệu chứng**: xoay ngang điện thoại thì màn hình vỡ.

**Nguyên nhân**: điện thoại nằm ngang rộng **844–915px** → **vượt** `@media (min-width: 820px)`
nên trình duyệt nhận layout **desktop**, trong khi chiều cao chỉ còn **~390–430px**. Hậu quả:

- **Sidebar cụt giữa trang**: nó cao `100vh - var(--header-h)` = ~310px, mà nội dung cuộn dài
  hơn nhiều → phần dưới sidebar là một mảng trắng lửng lơ.
- Sidebar 224px ăn **27% bề ngang** của màn vốn đã ngắn.
- `.chart-container` giữ **460px** của desktop trên màn cao 390px → chart cao hơn cả màn hình.
- Header 80px + strip nhiệt độ tràn 2 dòng, ăn nốt chiều cao còn lại.

**Fix gốc**: layout desktop phải thoả **CẢ HAI** chiều —
`@media (min-width: 820px) and (min-height: 600px)`. Điện thoại ngang không còn lọt vào đó.

Thêm một media riêng cho **rộng-nhưng-thấp** (`min-width: 700px` **and** `max-height: 599px`):
giữ layout mobile (bottom nav) nhưng **tiêu bề ngang thừa** (`--maxw` 480→**700px**, nếu không
cột nội dung nằm giữa với hai lề trống lớn) và **giành lại chiều cao** — header 80→56px, logo
56→38px, banner min-height 84→64px, chart 320→**210px**.

Bài học chung: với thiết bị cầm tay, `min-width` **không** đồng nghĩa "màn hình lớn". Chiều cao
mới là thứ khan hiếm khi xoay ngang.

## Bố cục màn rộng: logo sát trái · 2 cột · chart full width

- **Logo về sát mép trái**: header vốn đã trải hết chiều ngang, nhưng bị đệm
  `padding-left: side-w + 1.6rem` nên thương hiệu bị đẩy vào giữa-trái. Nay `padding-left: 1.6rem`
  — sidebar bắt đầu **dưới** header nên không đụng nhau.
- **Setting 2 cột** khi đủ rộng (`.set-grid { grid-template-columns: 1fr 1fr }`); trước đó mỗi
  card một hàng, bỏ phí nửa màn.
- **Chart Result full width**, bảng vẫn giữ 760px cho dễ đọc. **Cần HAI id**
  (`#screen-result #resultChartCard`): rule `#screen-result .card` phía trên là (1,1,0) nên
  một selector 1-id (1,0,0) **thua** và chart vẫn bị kẹp 760px — lần đầu áp đã dính đúng bẫy này.

### Bonus: nhãn trục Y chồng thành vệt mờ

`yAxis.tickInterval: 5` là cố định. Ổn khi đường cong còn quanh 0, nhưng một run thật lên tới
**580** → **~116 nhãn** chồng lên nhau, không đọc được. Thay bằng **`tickPixelInterval: 36`** để
Highcharts tự chọn bước tròn vừa với chiều cao đang có — cũng tự đúng luôn cho chart 210px ở chế
độ điện thoại nằm ngang. Kết quả: `0, 50, 100 … 600`.

## Trục tung: sàn 50, giãn theo dữ liệu, LUÔN đúng 10 nấc

**Yêu cầu**: trục tung tối thiểu tới 50, tăng theo giá trị đường, và **luôn có 10 nấc**
(4 nấc có nhãn ở giữa, còn lại là vạch phụ).

**`tickInterval` KHÔNG làm được**: nó là *bước cố định*, nên số vạch thay đổi theo dữ liệu —
đúng cái ngược lại với "luôn 10 nấc". Phải tự sinh vị trí vạch bằng **`tickPositioner`**:

```js
var top = Math.max(this.dataMax || 0, 50);                 // sàn 50, rồi bám dữ liệu
var mag = Math.pow(10, Math.floor(Math.log(top)/Math.LN10) - 1);
top = Math.ceil(top / (mag * 5)) * (mag * 5);              // làm tròn lên để /10 ra số sạch
// -> 11 vị trí, tức đúng 10 nấc
```

Nhãn đặt ở **mỗi nấc thứ hai** (`labels.formatter`) → 4 nhãn giữa, các vạch còn lại thành lưới
phụ; gắn nhãn cả 10 nấc thì trục chật, nhất là chart 210px ở chế độ nằm ngang.

Làm tròn lên **bội của `mag*5`**, không phải `mag*10`: bản đầu dùng `mag*10` khiến 120 → **200**
và 1234 → **2000**, phí chỗ. Nay 120 → 150, 1234 → 1500.

| dataMax | đỉnh | nhãn |
| --- | --- | --- |
| 0 / 12 / 49.9 / 50 | **50** | 0, 10, 20, 30, 40, 50 |
| 57.3 | 60 | 0, 12, 24, 36, 48, 60 |
| 120 | 150 | 0, 30, 60, 90, 120, 150 |
| 580 | 600 | 0, 120, 240, 360, 480, 600 |
| 1234 | 1500 | 0, 300, 600, 900, 1200, 1500 |

**Đã thử `max: 50` trước đó và bỏ**: nó **cắt cứng** — đường vượt 50 mất luôn phần trên, trên máy
xét nghiệm là giấu dữ liệu. Sàn-mà-giãn giữ được cả hai: thang ổn định để so sánh các run, mà
không bao giờ xén.

**Guard**: `node tools/test_chart_ticks.js` — chạy trên **chart thật** (đẩy data vào
`Highcharts.charts`, đọc `yAxis[0].tickPositions`), 11 mức dữ liệu, kiểm số nấc = 10, đỉnh đúng,
bắt đầu từ 0 và các nấc cách đều. Không chép lại công thức vào test: bản chép sẽ pass trong khi
bản chạy thật đã hỏng.

Chart cũng **tràn theo chiều dọc** trên desktop: `height: calc(100vh - var(--header-h) - 8rem)`,
`min-height: 360px`.

## Bỏ sidebar: bottom nav ở MỌI kích thước

Trước đây từ 820px trở lên, thanh điều hướng biến thành **sidebar dọc 224px**. Nay **mọi kích
thước đều dùng bottom bar** như điện thoại — một pattern điều hướng duy nhất, thói quen bấm
chuyển thẳng từ điện thoại sang máy bàn.

Đổi lại còn **xoá luôn một lớp bug**: sidebar đặt `height: 100vh` trong khi trang cuộn dài hơn,
nên nó **cụt giữa trang** thành một mảng trắng lửng lơ (chính là thứ làm hỏng chế độ điện thoại
nằm ngang trước đó).

Kéo theo, dọn được kha khá:

- `--side-w` **xoá hẳn** (không còn tham chiếu nào trong `style.css`).
- `.header` hết cần `margin-left` âm + `padding-left` bù để thoát khỏi cột sidebar; logo nằm sát
  trái đơn giản vì **không còn gì đứng trước nó**.
- `body` desktop quay về `padding-bottom: var(--nav-h)` như mobile; `body.nonav` cũng chỉ còn một
  nhánh thay vì `padding-left`/`padding-bottom` tuỳ kích thước.
- Desktop chỉ nới `.bottom-nav { max-width: 620px }` cho khỏi lọt thỏm dưới trang 1600px.
- Chart Result trừ thêm `var(--nav-h)` khi tính chiều cao, nếu không card chui xuống dưới thanh nav.

**Đo thay vì nhìn** (probe CDP đọc `getBoundingClientRect` của `.bottom-nav`):

| viewport | nav rộng | cách đáy | flex-direction |
| --- | --- | --- | --- |
| 390×900 (dọc) | 390 | **0** | row |
| 844×390 (ngang) | 700 | **0** | row |
| 1280×900 | 620 | **0** | row |
| 1600×1000 | 620 | **0** | row |

Breakpoint `min-width: 820px and min-height: 600px` **vẫn giữ** — không còn để đổi nav, mà vì các
lưới 2 cột và chart cao bên dưới nó đều cần chiều cao thật.

## Bố cục Home trên màn rộng

| | cột trái | cột phải |
| --- | --- | --- |
| **Chưa có chart** (`1fr 1fr`) | Current status · Notification · Buttons | Temperatures (Lysis + Amplification) |
| **Có chart** (`1fr 2fr`) | tất cả phần chữ + dải nhiệt độ gọn — **1/3** | chart — **2/3** |

Bảng đặt tên (`#namingCard`) **span cả hai cột** ở mọi trạng thái: mười hàng slot + select bệnh
mà nhét vào nửa (hay một phần ba) cột thì select mất chữ — đúng kiểu thiếu bề ngang đã đo được ở
bảng Result.

### Lúc amplification: chart cao hết màn, bảng slot xuống cột trái

Bảng slot phục vụ **hai vai**, cùng một DOM:

| | trước khi chạy | trong lúc chạy |
| --- | --- | --- |
| tiêu đề | "Name the samples" | **"Samples"** |
| gợi ý + nút Confirm | hiện | **ẩn** |
| vị trí | span cả 2 cột | **cột trái**, nối tiếp các thẻ trạng thái |
| vai trò | form đặt tên | **legend của chart** (cùng chấm màu series) |

`setHomeMode` vì vậy hiện `namingCard` khi `naming || chartMode`, và `namingBuilt` cũng phải
dựng bảng cho cả hai pha (trước đó chỉ dựng ở pha đặt tên). Chart lấy
`height: calc(100vh - header - nav - 5rem)`.

**Hai bẫy grid, cả hai chỉ lộ khi đo:**

1. Bảng rơi xuống **y=855** (dưới đáy chart) thay vì nối tiếp cột trái kết thúc ở y=425 — vì
   cột phải chỉ chiếm **một** hàng. Phải cho `.home-side { grid-row: 1 / span 2 }`.
2. Thêm `grid-row` đó khiến **hai cột đảo chỗ** (chart nhảy sang trái): một item có vị trí xác
   định *một phần* được thuật toán đặt **trước** các item auto. Phải ghim `grid-column` cho cả
   `.home-main` (1) và `.home-side` (2).

Sau khi sửa: cột trái status→notify→controls→temps→bảng, mọi gap **16px**, bảng bắt đầu **y=441**
ngay dưới thẻ cuối (đáy 425); chart cột phải cao **839px** (container 778px = viewport 1000 trừ
header 80, nav 62, padding 80).

### Thẻ ở cột trái bị "tách rời nhau" — lưới cào bằng chiều cao hàng

**Triệu chứng**: các thẻ cột trái cách nhau những khoảng trống lớn, rõ nhất khi chart đang mở.

**Nguyên nhân**: mỗi thẻ là **một grid item trực tiếp**, nên mỗi hàng cao bằng thẻ **cao nhất
trong cả hai cột**. Thẻ nhiệt độ 139px ép hai thẻ 84px bên cạnh giãn ra **71px** thay vì 16px;
chart 521px thì xé toạc hẳn cột trái. Cho chart `grid-row: span 4` chỉ vá được đúng trường hợp
có chart — vẫn sai khi chưa có chart.

**Fix gốc**: bọc mỗi cột trong một wrapper (`.home-main` / `.home-side`) và cho **wrapper** làm
flex column. Hai cột từ đó **độc lập chiều cao**, không còn hàng lưới chung để cào bằng.

Wrapper **tàng hình trên điện thoại**: `display: contents` gỡ hộp khỏi layout nên các thẻ xếp y
hệt trước khi có wrapper (mỗi thẻ giữ margin riêng); chỉ trong media desktop chúng mới thành cột
flex thật.

Đo lại (1600×1000): mọi khoảng cách thẻ = **16px** ở cả hai cột, cả hai trạng thái (trước: 71px).

Hai điểm kỹ thuật:

- **Thứ tự đọc dùng `order`, không phải thứ tự DOM.** Trong HTML, Notification đứng trước
  Buttons còn Current status nằm sau cả hai; thứ tự mong muốn là status → notification → buttons.
  Đặt `order` trong lưới giữ nguyên DOM nên `script.js` không phải sửa gì.
- **`grid-auto-flow: row dense` là bắt buộc.** Thiếu nó, thẻ đầu của cột phải bị đẩy xuống hàng 3
  (con trỏ lưới không quay lui sau ba thẻ cột trái) → trống hẳn góc trên bên phải.

**Cân đối nội dung trong bảng full-width**: bảng trải hết ngang nhưng chỉ có **một** cột, nên nội
dung nằm sát trái và ~3/4 mỗi hàng bỏ trống. Nay `.sample-cell` giới hạn `30rem` + `margin: 0 auto`
(căn giữa) và select nới lên `20rem` — bảng vẫn full ngang, thứ nằm trong nó thì cân.

## Chưa làm (có chủ ý)

- **Dark mode**: máy dùng trong phòng lab sáng, thêm `prefers-color-scheme` tốn thêm CSS mà lợi
  ích không rõ. Để ngỏ.
- **Khoảng trống lớn khi idle trên desktop**: do nội dung ít thật, không phải lỗi layout — không
  bịa thêm nội dung để lấp.
- 10 màu series chart giữ nguyên (dữ liệu, cần phân biệt — như ghi chú brand cũ).

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs --upload-port COMxx`.
