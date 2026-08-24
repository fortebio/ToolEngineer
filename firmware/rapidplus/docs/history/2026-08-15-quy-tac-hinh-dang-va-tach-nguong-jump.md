# 2026-08-15 — Ngưỡng gọi kết quả, tách ngưỡng jump, và hai bản dựng AT / a

> **BẢN SỬA CUỐI NGÀY.** Quy tắc hình dạng hai nhánh mô tả ở phần 3 **ĐÃ BỊ RÚT**. Xem
> phần "Vì sao rút quy tắc hình dạng" ở cuối. Phần 1 (tách ngưỡng jump) vẫn nguyên giá trị.

## Vì sao

Khai đánh dấu ba giếng được máy kết luận Dương tính nhưng không giống khuếch đại thật:
`RPL02016` giếng 1, `RPL01003` giếng 1, `RPL01009` giếng 4. Chúng thuộc **hai dạng khác
nhau**, và không dạng nào bị bắt bởi ngưỡng hiện có.

- **Bậc nhỏ trên nền tăng chậm** — giếng trôi lên suốt lần chạy, đâu đó trong đoạn trôi có
  một gờ ngắn, thuật toán đọc gờ đó thành khuếch đại. `RPL02016` giếng 1: tổng leo 61 đơn
  vị, chỉ 25 (tức 42%) được tính là phản ứng.
- **Đường tăng không có bậc chuyển** (dạng "vai") — giếng tăng đều mười tới mười bảy phút
  và không hề tạo bậc, nên "điểm chuyển" chỉ là thời điểm dốc nhất của một đoạn dốc thoải.

## Thay đổi

### 1. Tách ngưỡng jump ra khỏi `min_increase` — LÀM TRƯỚC, ĐO TRƯỚC

`check_breakData()` trước đây nhận thẳng `parameters.min_increase` làm `crossing`. Một con
số gánh hai việc không liên quan. Nguy hiểm hơn vẻ ngoài: `checkJump()` dùng `crossing`
**hai lần theo hai chiều ngược nhau**

```
if (jump < crossing)       return false;   // ngưỡng cao hơn -> phát hiện ÍT jump hơn
if (total_rise > crossing) return false;   // ngưỡng cao hơn -> phát hiện NHIỀU jump hơn
```

nên tác động thực của việc đổi nó **không suy luận được, chỉ đo được**.

Nay là hằng số biên dịch `BREAK_JUMP_THRESHOLD = 20.0` (`Alg/Algo.h`), **không** phải tham
số runtime — `parastructure` đã chạm trần 402 byte (`ForteSetting.cpp:229`), và quan trọng
hơn: một bộ dò gánh nhiều trách nhiệm như vậy không nên sửa được từ file cấu hình.

**Bằng chứng** (25.219 giếng, chạy 30 phút):

| | Số Break |
| --- | --- |
| Ngưỡng ghim ở 20 (sau khi tách) | **104** |
| Nếu vẫn dính vào `min_increase` = 30 | 80 |

- Số giếng đổi chỉ số Break sau khi tách: **0** — tập Break giống hệt, cùng đường cong,
  cùng chỉ số.
- Nếu KHÔNG tách: **24 jump điện thật bị bỏ sót**, 1 jump đổi vị trí. Đúng thứ việc tách
  này sinh ra để ngăn.

### 2. `min_increase` 20 → 30, `amplification_time` 120 → 90 vòng (40 → 30 phút)

Đo trên 25.219 giếng, chấm trên lần chạy 30 phút, đối chiếu với giếng lặp ở vị trí ghép
cặp (toàn bộ ca Dương tính đồng thuận 61%):

| Dải tăng | Số giếng | Giếng lặp đồng thuận | Trung vị độ dốc |
| --- | --- | --- | --- |
| 20 – 25 | 79 | 32% | 7,8 |
| 25 – 30 | 91 | 34% | 8,1 |
| 30 – 35 | 88 | 45% | 12,3 |
| 35 – 40 | 90 | 51% | 10,2 |
| trên 60 | 2.749 | 64% | 38,0 |

Điểm gãy nằm **giữa 30 và 35**. Ngưỡng 40 đã thử và **bị loại**: nó loại 348 giếng thay vì
170, và 178 giếng chênh lệch đồng thuận 45–51% — cắt cả dải sẽ loại nhầm phản ứng thật yếu.
Dải 30–40 để quy tắc hình dạng xử lý chọn lọc.

### 3. Quy tắc hình dạng — hai nhánh

Hai đại lượng mới, tính cho **mọi** giếng chứ không chỉ giếng bị đánh dấu (đây chính là dữ
liệu có nhãn hiện đang thiếu):

- `share` = mức tăng được tính / tổng mức leo sau mốc 4 phút. Trung vị toàn hệ **0,83**.
- `rise_width` = số phút đạo hàm đã làm mượt còn ở mức ≥ 35% đỉnh của chính nó. Phản ứng
  thật giữ 2–3 phút; giếng không tạo bậc giữ mười phút trở lên.

| Nhánh | Điều kiện | Bắt được |
| --- | --- | --- |
| A | `share < 0,45` VÀ `độ dốc < 15` | RPL02016 s1 |
| B | `rise_width > 10` VÀ `tăng < 45` | RPL01003 s1, RPL01009 s4 |

`rise_width` **cộng dồn từng khoảng lấy mẫu**, không lấy (cuối − đầu): một cái vai có thể
tụt xuống dưới dải rồi quay lại, và tính cả khoảng hở sẽ báo một pha tăng dài trong khi
thực tế là hai pha ngắn.

### 4. HAI BẢN DỰNG, một cây nguồn

| Env | Phiên bản | Giếng bị đánh dấu thì sao |
| --- | --- | --- |
| `esp32dev` | **v2.4.3AT** | Báo **`F` (Flagged)** — trạng thái riêng, không còn là `P`/`S` |
| `esp32dev_shape_neg` | **v2.4.3a** | Dương tính bị đánh dấu → **`N` (Âm tính)** |

**`F` là trạng thái THỨ SÁU, không phải `P` đeo thêm huy hiệu.** Bản đầu chỉ gắn một dấu `!`
cạnh badge `P`; như vậy gánh nặng đổ lên mọi hệ đọc phía sau là phải để ý một trường thứ hai,
và một giếng mà máy không dám bảo chứng vẫn bị đếm là ca dương bởi bất cứ thứ gì chỉ đọc chữ
cái. Nay `predict_outcome()` ghi thẳng `OutcomeFlagged` (`AlgoData.h`).

**Chữ cái mới bắt buộc phải được thêm vào MỌI nơi tiêu thụ** — `result[i] = outcome[0]` đi tới
ba đường: bảng TFT, `GET /slots`, và payload upload:

- `displayLCD.cpp` — chuỗi `if/else if` **không có nhánh `else`**, nên chữ cái lạ sẽ in ra **ô
  trống**, mất hẳn kết quả mà không báo gì. Nhánh `'F'` in `|14.3?|` màu **MAGENTA** (năm màu
  kia đều đã có nghĩa: đỏ = ca dương, vàng = dương yếu, cam = lỗi, xanh lá = âm, cyan = đường
  cong hỏng). Dấu `?` mang nghĩa cho người đọc màn đơn sắc hoặc mù màu.
- `webDashboard.cpp` — `'F'` **vẫn có CT** như `P`/`S`. Giấu CT đi là để người vận hành muốn
  phản biện kết luận mà không có gì để phản biện.
- `Bluetooth.cpp` — nhánh lỗi cảm biến `/E` cũng nhận `'F'`, vì `/E` là lỗi **quang học** của
  giếng chứ không phải phán quyết về khuếch đại.
- `data/` — badge `.res-F` màu tím (đo 5,06:1), chữ cái mang nghĩa nên không phụ thuộc màu.
  Badge giữ lời giải thích trong `title` + `aria-label`.

**Hệ quả hướng ra ngoài — phải báo trước cho phía nhận dữ liệu.** Chuỗi upload gửi đi chữ cái
này (`"EHP | 14.3 | F"`), nên Google Sheet, ingest và ERP sẽ gặp một giá trị chưa từng thấy.
Bên nào phân loại kết quả bằng chữ cái đều phải biết `F` trước khi máy chạy bản AT ngoài thực địa.

Cờ `SHAPE_RULE_NEGATIVE` được đọc ở **đúng ba chỗ**: `predict_outcome()` (Algo.cpp),
`FirmwareVer` (define.h), và trường `shapeMode` của `GET /slots` (webDashboard.cpp). Hai bản
tính và xuất **cùng** `share` / `rise_width` / `shape_flag`, nên một lần chạy của bản này so
được từng dòng với bản kia.

Ở bản `a`, khi lật kết luận thì **mọi số đo phía sau vẫn giữ nguyên** — Ct, mức tăng, share,
rise_width vẫn mô tả đúng những gì đường cong đã làm. Người vận hành hỏi "sao nó rõ ràng có
lên mà lại Âm tính?" phải đọc được câu trả lời trên chính bản ghi đó.

### 5. Web portal

`GET /slots` thêm `share`, `rise` (phút), `shape` (0/1/2/3) và `shapeMode` ở cấp tài liệu.
Giá trị **null**, không phải −1, khi không đo được — client không được vẽ ra một số "−1".

**Xuất từ `bResultGet()`, không phải từ ba caller của nó.** `screen_Result()`, đường
Bluetooth và `POST /reviewlast` đều chạy đúng hàm đó, nên đây là cách sắp xếp duy nhất mà số
liệu hình dạng **không thể** mô tả một lần chạy khác với bảng CT/kết luận mà các caller kia
xuất ra. Cùng lý lẽ với việc `gErrRec` được chụp trong `dashboardSetResults`.

Trên bảng Result, giếng bị đánh dấu có thêm **một ký hiệu thứ hai** cạnh badge P/N/S/E/B —
**không** đổi màu chính badge đó. Badge P/N/S/E/B là tín hiệu đã có nghĩa cố định; nhuộm lại
nó để mang thêm nghĩa "và cũng bất thường" là âm thầm gán nghĩa thứ hai cho một mã cũ. Ký
hiệu mang lời giải thích trong `title` + `aria-label`, nêu rõ nhánh nào kích hoạt và hai con
số đằng sau.

## Đã sửa nhân tiện

`AlgoData::clear()` nay reset **mọi** trường mới. Đây đúng là lỗi từng để `plateau_point` sót
lại giữa các giếng ở v2.4.3.

## Đính chính một nhận định trước đó

Trước đây có ghi rằng `min_increase` có "ba giá trị mặc định xung đột, đường khởi tạo nào
chạy trước thì thắng". **Sai.** `DataIn::fromEEPROM()` luôn gọi `parameters.fromEEPROM()`,
hàm này chép từ `_ForteSetting.parameter`, nên **chỉ giá trị trong `define.h` mới thật sự đi
tới một kết luận**. Giá trị 30.0 trong `AlgoData::clear()` bị ghi đè trước khi dùng, còn số 5
trong `strJson` (Algo.h) thì thậm chí không bao giờ được parse vào `parameters` — nó chỉ là
dữ liệu mẫu. Cả ba nay đã đồng bộ để người đọc sau không bị dẫn sai, nhưng đó là dọn dẹp chứ
không phải sửa lỗi.

## Chưa làm

- **Không có đối chiếu giếng lặp trong firmware.** Mỗi phòng lab nạp khay một kiểu, và portal
  đã cho phép gán bệnh theo từng giếng, nên việc ghép cặp cố định `k ↔ k+5` không đưa vào
  máy. Mọi con số "giếng lặp đồng thuận" trong tài liệu này là **phân tích ngoại tuyến** dựa
  trên giả định đó, không phải hành vi của firmware.
- **Chưa chạy trên phần cứng.** Cả hai bản mới chỉ biên dịch sạch.
- `currentVersion` trong `updateOTA.cpp` **vẫn giữ 19** — không bản nào được chào qua OTA.
  Lưu ý `FirmwareVer` chính là **nhánh** mà `checkFirmware()` đọc, và kho `FBTRapidplusOTA`
  chưa có nhánh nào tên `v2.4.3AT` hay `v2.4.3a`, nên máy sẽ báo "kiểm tra thất bại". Đúng
  như mong đợi với một bản chạy thử.
- 839 đường cong "đỉnh → tụt → lại tăng" (23,6% ca Dương tính) **không** bị quy tắc nào ở
  đây chạm tới. Chưa rõ đoạn tụt là quang học, nhiệt, hay thật.


---

## Vì sao rút quy tắc hình dạng (bổ sung cuối ngày)

Có **68 đường cong được gán nhãn bằng mắt** (thật / nghi ngờ / không đặc hiệu), trong đó 56 đường
chấm **mù** — không hiện số đo, không hiện tên máy, thứ tự xáo trộn. Đây là dữ liệu có nhãn đầu
tiên của cả quá trình phân tích, và nó lật lại phần lớn kết luận trước đó.

### Cách chấm: đếm cặp xếp sai

Với 8 giếng "thật" và 11 giếng "không đặc hiệu" có 88 cặp. Với mỗi cặp, hỏi: đại lượng này có
xếp giếng thật CAO HƠN giếng không đặc hiệu không? Nếu không, đó là một cặp sai. **44/88 là mức
tung đồng xu.**

| Đại lượng | Cặp xếp sai |
| --- | --- |
| **Độ dốc (steepness)** | **2 / 88** |
| Mức tăng đếm được | 9 / 88 |
| Tỷ phần sigmoid | **45 / 88** ← đúng bằng ngẫu nhiên |

### Nhánh A sai ở đâu

`share < 0.45 VÀ steepness < 15`, chấm trên 62 giếng Dương tính có nhãn:

| Nhãn | Nhánh A đánh dấu | Nhánh A bỏ qua |
| --- | --- | --- |
| thật | **0** | 18 |
| nghi ngờ | **0** | 13 |
| không đặc hiệu | 9 | **22** |

Nhánh A **không hề kết tội nhầm một giếng thật nào** — sai lệch của nó hoàn toàn một chiều. Nhưng
trong **cả 22 lần bỏ sót**, số hạng độ dốc đã nằm sẵn trong vùng không đặc hiệu còn số hạng
`share` phủ quyết. 5 trong 22 có `share > 1.0`: đường cong kết thúc THẤP HƠN plateau của chính
nó, mẫu số co lại, tỷ số thôi còn là một phép đo. `min_sharpness = 11` một mình bắt 15/22.

### Thay bằng gì

| | Trước | Sau |
| --- | --- | --- |
| `min_increase` | 30.0 | **25.0** |
| `min_sharpness` | 5.0 (không bao giờ kích hoạt) | **11.0** |
| Quy tắc hình dạng | nhánh A + nhánh B | **bỏ hẳn** |
| `share`, `rise_width` | quyết định kết quả | **chỉ đo và xuất ra** |

Chấm trên 68 nhãn: bỏ **25/31** giếng không đặc hiệu, mất **1 thật + 1 nghi ngờ** (cả hai chết vì
độ dốc 10.4 và 10.5, không phải vì kích thước). Chi phí toàn hệ: 473 giếng, 13,1% ca Dương tính,
khoảng 59 ca/tháng. Trong đó **87 giếng có mức tăng > 60** — không ngưỡng kích thước nào với tới.

### Cổng đánh giá lại (F / N)

`shape_flag` nay mang **một** ý nghĩa: "ngưỡng mới đã loại giếng này". Giếng đó lẽ ra là Dương
tính dưới ngưỡng của v2.4.3 (`LEGACY_MIN_INCREASE 20`, `LEGACY_MIN_SHARPNESS 5`) nhưng trượt
ngưỡng hiện tại. Bản **AT** báo `F`, bản **a** báo `N`. Mọi số đo phía sau giữ nguyên ở cả hai.

## Đính chính phần 1: tách ngưỡng jump lấy mất 36 break

Phần 1 khẳng định "0 đường cong đổi chỉ số break". Đúng, **nhưng đó là so pinned-20 với
pinned-20** — không phải so với hành vi v2.4.3 **thực sự xuất xưởng**, vốn dùng
`min_increase / slope`.

| | Số break |
| --- | --- |
| v2.4.3 như đang xuất xưởng (`20/slope`) | **136** |
| v2.4.3a sau khi ghim 20.0 | **104** |

**Mất 36, được thêm 4.** Nhóm mất nằm trên các giếng có slope cao (trung vị 1,91), nơi ngưỡng cũ
thấp tới **5,8**. Trong đó có `RPL01004` giếng 2 ngày 24/07: slope 1,81, ngưỡng cũ 11,05, bước
nhảy +19,34 — trước đây kích hoạt, nay thì không.

Hướng sửa vẫn đúng (cùng một bước nhảy vật lý không nên cần 5,8 đơn vị ở giếng này và 16,9 ở
giếng kia), nhưng 20.0 được ghim ở **mép trên** của dải cũ. Lời giải đúng là **trung hoà bước
nhảy (climb neutralisation)** với `MIN_CLIMB = 8.0` — độc lập với slope và phủ đúng dải 8–20 mà
36 break bị mất đang nằm. **Chưa cài đặt**; nằm ở bản giao việc riêng.

---

## Trung hoà bước nhảy (climb neutralisation) — đã cài đặt, cả hai bản

Đây là lời giải cho 36 break bị mất ở phần "Đính chính" bên trên, và cho một lỗ hổng rộng hơn.

### Ca hỏng cụ thể

`RPL01004` giếng 2, 24/07 12:45. Đường cong nằm phẳng ở 207,6 (đã calibrate), nhảy **+19,34
trong đúng một lần đọc** ở phút 5,0, rồi nằm phẳng ở 229,3 suốt 25 phút còn lại. **Không hề có
khuếch đại ở đâu trong đó.**

`checkJump()` không kích hoạt vì 19,34 thiếu **0,66** so với `BREAK_JUMP_THRESHOLD` — và cái sàn
đó là **test duy nhất trong năm test của nó** mà bước nhảy này trượt. Thoát khỏi đường break, bậc
nhảy đi tới bộ chấm điểm, nơi Savitzky-Golay trải một lần đọc ra chín điểm và biến cạnh dựng đứng
thành một cái vai: mức tăng 24,2, độ dốc đỉnh 14,7 → **DƯƠNG TÍNH**.

### Vì sao SỬA chứ không CẮT

`check_breakData()` trả về chỉ số, caller cắt đường cong ở đó và giếng bị báo Break — **vứt cả
lần chạy**. Trung hoà thì vá chỗ đứt rồi để bộ chấm điểm bình thường đọc phần còn lại: giếng nào
mà "khuếch đại" **chính là** cái nhiễu thì rơi xuống Âm tính, giếng nào có khuếch đại thật bên
dưới thì giữ được kết quả đọc được.

### Hai loại, phân biệt bằng phần CÒN LẠI chứ không bằng bước nhảy thô

Với `b0 = median(a[i-5..i])`, `b1 = median(a[i+3..i+8])`, `lvl = b1 - b0`:

- `|lvl| >= 0,35 × |bước nhảy|` → **OFFSET** (dịch mức). Hiệu chỉnh bằng **`lvl` đo được**, không
  phải bằng bước nhảy thô.
- ngược lại → **SPIKE** (xung nhất thời).

Phân loại theo bước nhảy thô để lại một **vùng chết**: một climb giữ lại 0,69 của chính nó thì
không phải offset sạch cũng không phải spike sạch, và bị bỏ qua im lặng.

### Ba chi tiết không được bỏ

1. **Offset KHÔNG dịch từ `i+1`.** Các điểm giữa mức cũ và mức mới đã ổn định là một pha quá độ,
   có thể là dropout. Tìm `j` đầu tiên mà `|a[j] - b1| <= 0,25 × |lvl|`, trừ `lvl` từ `j` trở đi,
   rồi **nối tuyến tính** đoạn `i+1 .. j-1`. `RPL02020` giếng 1 đi 133 → 35 → 265; áp hiệu chỉnh
   +132 lấy từ đáy dropout kéo nó xuống **−97**, bịa ra một dao động chưa từng được ghi.
2. **Đo lại sau mỗi lần sửa.** Mức dịch của climb sau phải đo trên đường cong **như nó đang là**.
   Dùng lại giá trị tính trên bản gốc làm đường nhiều climb **tệ đi**: `RPL02019` giếng 2 từ biên
   độ 46 thành 84. 15 đường cong trong hệ có nhiều hơn một climb.
3. **Bất biến kiểm theo BƯỚC NHẢY, không theo biên độ.** Gỡ một nhiễu đi xuống thì biên độ **tăng
   lên là đúng**: `RPL02019` giếng 2 lên 212→251, tụt 43, rồi tiếp tục lên — bỏ cú tụt ra lộ một
   pha tăng liền mạch 82 đơn vị. Kiểm theo biên độ đánh dấu **15 lần sửa đúng** là hỏng. Kiểm theo
   `max|diff|` thì **0 trong 236 đường** vi phạm.

### Bắt được cả chiều XUỐNG

`checkJump` test 2 là `jump >= crossing` với `crossing` dương, nên **một dropout là vô hình về mặt
cấu trúc** với nó, dù lớn đến đâu. 58 trong 248 climb của hệ là đi xuống — cả một loại mà bộ dò
hiện tại không thể thấy.

### Đo được (25.219 đường cong, chạy 30 phút)

| | |
| --- | --- |
| Đường cong chứa ít nhất một climb | **236 (0,94%)** — 99,06% toàn hệ không bị đụng |
| Số climb đã áp dụng | 252 |
| Đường cong đổi kết luận | 96 (chưa tính đường break) |
| Lần sửa làm bước nhảy **tệ hơn** | **0** |

Chuyển đổi chính: `P→N` 64, `E→N` 12, `E→P` 7, `S→N` 5.

### Ba fixture hồi quy

| Giếng | Kỳ vọng | Kết quả |
| --- | --- | --- |
| `RPL01004` s2, 24/07 | P → N, tăng 24,2 → ~4 | 1 climb, **P → N, 24,2 → 4,0** |
| `RPL01004` s5, 13/01 | phát hiện climb, giữ được kết quả | 1 climb, tăng 37,2 → **38,6** |
| `RPL01004` s1, 10/08 | KHÔNG có climb, giữ nguyên | 0 climb, **byte giống hệt** |

### Vị trí trong luồng

`neutralise_climbs()` chạy trên `recordIn.raw_data` **sau** khi calibrate và **trước**
`check_breakData()`, ở **cả hai** đường (`bResultGet` và `bResultPutToGoogleSheet`). Thứ tự là
bắt buộc: một bậc nhảy sống sót tới bộ chấm điểm sẽ bị làm mượt thành cái vai và đọc thành khuếch
đại. `BREAK_JUMP_THRESHOLD` **giữ nguyên 20,0** — cái này chạy phía trước nó, không thay thế nó.
`CLIMB_MIN_STEP = 8,0` độc lập với slope, nên nó phủ đúng dải 8–20 nơi 36 break bị mất đang nằm.

### Còn để ngỏ

- **Sửa xong có nên vẫn đánh dấu không?** Một giếng chỉ đọc ra Dương tính **sau** khi hiệu chỉnh
  một bậc nhảy điện 33 đơn vị thì đáng để người vận hành biết, dù kết quả đã đọc được. Hiện tại
  việc sửa là **im lặng**.
- **Chưa chạy trên phần cứng.** Cả hai bản chỉ mới biên dịch sạch.
