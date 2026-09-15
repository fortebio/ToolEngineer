# 2026-08-02 — Chart vọt bất thường ở mấy phút đầu: baseline cần điểm BẮT ĐẦU, không chỉ độ dài

## Triệu chứng

Quang học chỉ thực sự hoạt động sau hơn 2 phút; mấy phút đầu chart có một cú vọt bất thường rồi
mới ổn định.

## Nguyên nhân

Baseline cũ = trung bình **N vòng đầu tiên tính từ vòng 0**. Nhưng vòng 0 là lúc máy còn nguội:
trên run thật, kênh 1 đi **311 → 360 → 393 → 416 → 424 → 427** trong 5 vòng rồi mới phẳng ở ~432.

Trung bình một cửa sổ **bắt đầu từ vòng 0** vì thế bị mấy giá trị thấp đó kéo **xuống dưới** mức
phẳng. Chart vẽ `giá trị − baseline`, nên chính **đoạn ổn định-hoá hiện lên như tín hiệu đang lên**.

Đo trên đúng dữ liệu người dùng gửi (8 kênh × 120 vòng, 20 s/vòng = 40 phút), pipeline **đang chạy**
lúc đó (`start=0`, 20 vòng):

| | ch1 | ch2 | ch3 | ch4 | ch5 | ch6 | ch7 | ch8 |
|---|---|---|---|---|---|---|---|---|
| baseline | 419.6 | 311.7 | 412.7 | 438.6 | 379.9 | 325.8 | 583.1 | 646 |
| **vọt trong 2 phút đầu** | 7.1 | 0 | **21.7** | **13.6** | 4.7 | 6.2 | **20** | **17.2** |
| lift-off (phút) | 1.7 | 4.3 | **1.3** | **1.3** | 2 | **1.7** | **1.3** | **1** |

Hai dòng dưới mới là vấn đề thật: **ch4 và ch6 phẳng suốt run** mà vẫn "lift-off" ở phút 1.3–1.7.
Trên máy xét nghiệm, một đường âm tính trông như đang lên là thứ tệ hơn nhiều so với chỉ xấu.

## Sửa: cửa sổ có điểm bắt đầu, tính bằng PHÚT

```js
var BASELINE_START_MIN = 2; // bỏ qua bấy nhiêu phút đầu run
var BASELINE_RANGE_MIN = 4; // rồi lấy trung bình bấy nhiêu phút
```

Hai số này **là núm chỉnh, cố ý để chỉnh** — thời gian quang học ổn định là tính chất của **máy**,
không phải của code. Đổi ở đầu phần chart trong `data/script.js`.

Quy đổi ra vòng bằng `minPerRound` (lấy từ `intervalMs` của `/curve`), nên đổi `time per loop`
không cần ai quy đổi lại hai số trên. Run ngắn hơn cả `start` thì lùi về 0 thay vì ra chart phẳng.

### Đo lại với 2 / 4 — qua CHÍNH chart thật

Chặn `fetch("/curve")` trả dữ liệu thật rồi gọi `loadCurve()`, đọc lại `chart.series[].data`
(không chép lại công thức — bản chép sẽ xanh trong khi bản chạy thật đã hỏng):

| ch | baseline | vọt 2 phút đầu | lift-off | đỉnh |
|---|---|---|---|---|
| 1 | 432.3 | **0** | 6.7 min | 215 |
| 2 | 321.1 | **0** | 4.3 min | 48 |
| 3 | 445.7 | **0** | 8.3 min | 31 |
| 4 | 454.8 | **0** | **không bao giờ** | 1 |
| 5 | 389.6 | **0** | 7.3 min | 231 |
| 6 | 336 | **0** | **không bao giờ** | 0 |
| 7 | 618.6 | **0** | 6.7 min | 30 |
| 8 | 669.3 | **0** | 7.7 min | 10 |

Vọt = 0 ở **cả 8 kênh**. Hai kênh phẳng **hết lift-off**. Hai kênh khuếch đại thật (ch1, ch5) vẫn
lên đủ biên độ 215/231 và lift ở ~7 phút. Mô hình thử độc lập và chart thật ra **cùng một** con số
baseline tới từng chữ số — hai đường tính khác nhau đồng ý là bằng chứng mạnh hơn một đường.

## Đổi kèm: `rawY` giữ giá trị RAW, trừ baseline lúc VẼ

Trước đây `rawY[ch][idx] = y - baseline` — **trừ sẵn lúc lưu**. Nhưng baseline chỉ chốt sau khi cửa
sổ chạy hết, nên mỗi điểm sớm bị đóng băng theo baseline **tại thời điểm nó tới**, và không bao giờ
được tính lại. Đó là bug cũ đã ghi trong CLAUDE.md: *"reload giữa chừng thì đoạn đầu nhích khác đi"*.

Với cửa sổ có điểm bắt đầu, giữ nguyên cách cũ sẽ **hỏng hẳn**: những vòng trước `start` không có
baseline nào để trừ (`baseline == 0`), nên chúng sẽ được lưu nguyên giá trị thô ~300-600 và chart
nhảy vọt lên rồi rơi xuống khi baseline tới.

Nay: lưu **raw**, `drawSmoothed()` trừ baseline mỗi lần vẽ. Hệ quả:

- Cửa sổ đầy dần thì **cả đoạn phía sau được vẽ lại** theo baseline mới — hết chuyện điểm sớm bị
  trừ bằng một trung bình nửa vời.
- Trước khi cửa sổ mở, `baseCount == 0` → vẽ **phẳng 0**. Đúng bằng thứ mấy vòng đó sẽ floor về sau
  khi baseline có, nên **không có cú nhảy nào** lúc chuyển.
- Vòng bị thiếu (`undefined`) trả 0 thay vì `NaN` — `NaN` làm Highcharts ngắt đường.

## Lưu ý khi chọn `x`

Kênh 2 của run này có một **bước nhảy thật trong dữ liệu thô** ở vòng 14 (309 → 351, +42 trong một
vòng). Cửa sổ 2/4 nằm vắt ngang bước đó nên baseline rơi vào giữa hai mức, và chart hiện phần sau
bước cao hơn ~29. **Không có giá trị `x` nào sửa được chuyện đó** — đó là dữ liệu, không phải cách
tính. Đẩy `x` lên 5 phút thì cửa sổ nằm hẳn sau bước, nhưng lúc đó nó chạm vào đoạn khuếch đại của
ch1 (bắt đầu lên ~vòng 24) và làm hỏng baseline kênh khác. 2/4 là điểm cân bằng đo được.

## Không đụng tới

- **Kết quả CT và badge P/N/S/E/B vẫn do máy tính** (`bResultGet`, có `baseline start`/`baseline
  range` riêng trong `parastructure`). Baseline của web **chỉ để nhìn**. Đường trông phẳng mà bảng
  báo `P` vẫn có thể xảy ra và không mâu thuẫn — hai baseline khác nhau.
- **Ngưỡng nhiễu `< 2 → 0` giữ nguyên**, vẫn áp **trước** SG.
- `test_chart_ticks.js` (trục Y 10 nấc, sàn 200) vẫn xanh.
