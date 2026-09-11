# 2026-09-11 — Đồ thị run LIVE: thang phải suy từ độ dài run DỰ KIẾN, không từ số vòng đã về

Tiếp nối [2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md](2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md).
Bản 10/09 đo và ghim thang đọc trên **run đã lưu** (tab Result, 39.67 phút, độ dài đã biết).
Bản này sửa chỗ nó hỏng ngay ngày hôm sau khi có người **đứng nhìn một run đang chạy** ở Home.

Yêu cầu nguyên văn: *"sửa lại chart scale đang lỗi, hiện tại cần sửa 1 tí, bỏ fitrun đi chỉ muốn
kéo thanh slide"*. Nút **Fit run** đã bị gỡ từ 10/09 (HTML/JS không còn, guard 4a ghim sự vắng
mặt); phần "đang lỗi" là cái dưới đây.

## Trước

`applyChartScale()` lấy `runLen = runLengthMin(v)` = **số phút dữ liệu đã có** và suy thang từ đó:

```text
pxMin = max(10.4, plotWidth / runLen)
plotH = pxMin × 2.5 × 10
```

Với run đã lưu thì `runLen` là độ dài thật của run — đúng. Với run **đang chạy**, ở vòng 3
`runLen` = 0.67 phút, nên `pxMin = 268 / 0.67 = 400 px/phút` và chiều cao suy ra là **hàng
nghìn px**, rồi co lại mỗi 20 giây khi thêm một vòng. Đo trên mock `--full` (120 vòng × 20 s),
điện thoại dọc 390×844, chart ở Home:

| vòng | chiều cao chart | plot W × H | trục X | phút/nấc |
| --- | --- | --- | --- | --- |
| 3 | **3 472 px** | 268 × 3 350 | 0..2 | 2.5 |
| 6 | **4 142 px** | 268 × 4 020 | 0..1.67 | 2.5 |
| 13 | 1 797 px | 268 × 1 675 | 0..4 | 2.5 |
| 30 | 815 px | 268 × 693 | 0..9.67 | 2.5 |
| 60 | 463 px | 268 × 341 | 0..19.67 | 2.5 |
| 79 | 383 px | 268 × 261 | 0..26 | 2.53 |
| 81 → 120 | 383 px | 268 × 261 | trượt tới 13.9..39.67 | 2.5 |

Hai điều bảng này lộ ra:

- **Cột "phút/nấc" đọc 2.5 suốt** — bất biến của bản 10/09 vẫn "đúng" theo đúng nghĩa đen của nó,
  trong khi đường cong đổi hình dạng **mỗi vòng**. Bất biến phát biểu theo *nấc* nên nó không nói
  gì về việc thang px/phút có đứng yên hay không. Đó là lý do 5 section của guard đều xanh.
- Người vận hành thấy: trong ~25 phút đầu của run 40 phút, chart Home là một dải dọc cao gấp
  5-10 lần màn hình, co dần; độ dốc của cùng một cú lift-off khác nhau tuỳ đang ở phút thứ mấy.
  Đúng thứ bản 10/09 sinh ra để xoá, xuất hiện lại dưới dạng khác.

Thanh trượt cũng bị kéo theo: `maxStart = runLen − win` với `win = runLen` trong pha đầu → 0,
nên nó tắt suốt tới vòng ~79 rồi mới bật — tình cờ đúng, nhưng vì lý do sai.

## Nay

**Thang suy từ độ dài run sẽ có, thanh trượt và bám đuôi chỉ với tới dữ liệu đã có.** Hai độ dài,
cố ý tách:

```text
dataLen  = runLengthMin(v)                                   // số phút đã có
scaleLen = v.live ? max(dataLen, plannedRunMin()) : dataLen  // độ dài để suy THANG
plannedRunMin() = (amplification time − 1) × time per loop / 60000   // từ /config

pxMin    = max(10.4, plotWidth / scaleLen)                   // đứng yên suốt run
plotH    = pxMin × 2.5 × 10                                  // đứng yên suốt run
win      = min(plotWidth / pxMin, scaleLen)
maxStart = max(0, dataLen − win)                             // thanh trượt: tới đâu có data
```

- `homeView.live = true`, `resultView.live = false`. Run đã lưu vẫn **đo** — config có thể đã đổi
  sau khi nó được ghi (CLAUDE.md, `/reviewlast` quét độ dài thật thay vì tin `amplification_time`
  vì đúng lý do này).
- `(rounds − 1)` chứ không `rounds`: điểm cuối của run 120 vòng nằm ở `119 × 0.333 = 39.67`,
  nên độ dài dự kiến phải bằng đúng độ dài đo được lúc run kết thúc — không có cú reflow nào ở
  vòng cuối.
- `/config` được đọc **lúc boot** (`loadConfig()` trước khi mở SSE — trang tải lại giữa run nhận
  điểm đầu từ `/curve` trong vòng một giây, thang cho chúng cần config có sẵn trước) và đọc lại
  **ở sườn lên của thẻ chart** (run mới có thể theo sau một lần đổi Profile). Không đọc mỗi frame.
  Chưa có config → `plannedRunMin()` = 0 → rơi về `dataLen` (hành vi cũ) thay vì chết.
- Mock (`tools/sse_test_server.py`) nay để `/config` **mô tả đúng run của chính nó**
  (`amplification time = AMP_ROUNDS`, `time per loop = REPORT_INTERVAL_MS`) như máy thật, nơi
  `intervalMs` của `/curve` và `time per loop` cùng là `parameter.timePerLoop`. Trước đó mock
  thường (44 vòng × 1.2 s) khai 120 vòng × 20 s.

Đo lại, cùng mock `--full`:

| kích thước | plot W × H vòng 3 → 120 | trục X vòng 3 | thanh trượt | so với run lưu (10/09) |
| --- | --- | --- | --- | --- |
| dọc 390×844 | **268 × 260, không đổi** | 0..25.77 | tắt tới vòng 79, rồi bám đuôi | 268 × 260 ✔ |
| ngang 844×390 | **587 × 371, không đổi** | 0..39.67 | tắt (vừa khung) | 587 × 371 ✔ |
| desktop 1400×900 | **798 × 504, không đổi** | 0..39.67 | tắt | 798 × 504 ✔ |

Điện thoại dọc: trục 0..25.77 hiện sẵn từ vòng đầu, đường cong **điền dần từ trái sang**; tới khi
dữ liệu vượt cửa sổ (vòng 80) thanh trượt bật và cửa sổ trôi theo điểm mới như trước.

## Guard

`node tools/test_chart_scale.js` thêm **section 6** — chạy một run thật trên mock qua `/control`
(`ampname → red → heater → red`) rồi đo chart **Home** ở vòng 3 và vòng 6, so với chính số đo của
run lưu ở section 1 trên cùng kích thước:

- dọc 390×844: plot **cao bằng** run lưu (260 px), `min/step` bằng, cửa sổ bắt đầu ở phút 0,
  thanh trượt đang tắt, và chiều cao **không nhúc nhích** giữa vòng 3 và vòng 6, trục X không giãn
  theo vòng mới;
- desktop 1400×900: trục X phủ **đúng run dự kiến** (39.67) chứ không phải số vòng đã về, và tỷ lệ
  cao/rộng của plot bằng run lưu. Desktop là ca sàn 10.4 px/phút **không che được**: ở đó
  px/phút = plotWidth / độ dài, lấy sai độ dài là sai cả tỷ số chứ không bị kẹp.

Negative test — gieo lại lỗi (`plannedRunMin()` trả 0): **5 check đỏ** (plot 10 050 px ở vòng 3,
11 835 px trên desktop), và cột `min/step` **vẫn 2.5** — bằng chứng vì sao 5 section cũ không thấy.

**Ba bẫy trong chính guard, đừng lặp lại:**

- **So chiều cao tuyệt đối giữa Home và Result trên desktop là sai đề.** Home là lưới 2 cột nên
  card chart hẹp hơn Result ~10 px (788 vs 798) → chiều cao suy ra khác 7 px dù thang đúng. Bất
  biến là **cao/rộng** (= thang), không phải số px. Bản đầu của check này đỏ vì chính lý do đó.
- **Vòng chờ resize phải giống `resize()` của section 1**: kiểm `innerWidth` đã là kích thước
  yêu cầu **và** hai lần đọc liên tiếp giống nhau, hết giờ thì **ném lỗi có chẩn đoán**. Bản đầu
  hết vòng lặp rồi lấy số cuối cùng → đọc ra hình học **điện thoại dọc** ở bước "desktop" và báo
  2 lỗi sản phẩm không có thật.
- **Guard chạy một run thì phải trả mock về idle.** Mock `--slots` chạy 1.2 s/vòng → 144 s
  bận sau khi guard xong; lần chạy kế `/slots ready=false` và section 1 báo "no curve data".
  Nay mock có **`POST /__reset`** (test hook, không có trên máy — cùng loại với
  `POST /wifilist?current=`), guard gọi nó **đầu và cuối**. Đã kiểm: chạy lại 3 lần liên tiếp
  trên cùng mock đều xanh.

## Đã loại

- **Lấy tổng số vòng từ `status.subtitle` ("round 5/120")** — parse chuỗi hiển thị; chuỗi đó
  thuộc về `fillStatus` và đổi được bất cứ lúc nào.
- **Thêm `total` vào `/curve` (firmware)** — làm được, nhưng đây là lỗi phía client và `/config`
  đã có sẵn đúng hai tham số cần; đổi firmware cho một việc client tự lo được là mở rộng phạm vi
  của một bản vá web.
- **Coi Home sau `finished` là "đo" thay vì "dự kiến"** — không cần: khi run xong `dataLen ==
  planned` nên `max()` cho cùng một số; và phân nhánh theo phase là thêm một trạng thái nữa phải
  đồng bộ với `renderHome`.
- **Sửa `test_full_run.js`** — nó đỏ ở bước 2 (bấm GREEN rồi chờ `heater → waitamp`) vì mock đã
  đổi luồng Lysis thành `waitlysis → lysisrun → waitphase2` từ 07/08 (`8b43397`), còn test sửa lần
  cuối 03/08. **Lỗi có sẵn, không do bản này**; để lại đúng phạm vi.

## Kiểm lại

```bash
python tools/sse_test_server.py --slots tools/slots.txt --reboot   # shell khác
node tools/test_chart_scale.js                                     # 6 section, ~60 s
python tools/check.py                                              # static, xanh
```

Đã chạy sau khi sửa: `test_chart_scale.js` ×3 xanh · `test_chart_ticks.js` · `test_setting_a11y.js`
· `test_no_hscroll.js` · `test_home_error_table.js` xanh · `pio run -e esp32dev` 0 cảnh báo
(flash 72.9%).
