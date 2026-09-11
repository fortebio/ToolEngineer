# 2026-09-10 — Chất lượng đọc cảm biến quang: auto gain, auto origin, và ngân sách tích phân

**Trạng thái: KẾ HOẠCH, chưa có dòng code nào.** Kế hoạch song song:
[2026-09-10-dieu-kien-tra-error-tang-som.md](2026-09-10-dieu-kien-tra-error-tang-som.md) — xem mục
*Quan hệ với kế hoạch kia* ở cuối.

> **Bản sửa 2026-09-10 (sau khi ĐO).** Bản đầu của file này lập luận từ bảng thanh ghi
> VEML6035 và kết luận: auto-gain là đòn bẩy SNR chính (trần ×4), auto-origin *"KHÔNG THỂ đổi
> kết quả"*. Đem cùng câu hỏi đó hỏi **dữ liệu run thật** thì **cả hai kết luận đều sai lệch**,
> và đòn bẩy lớn nhất hoá ra **không cần đụng tới gain**. Mục *Đo được gì* dưới đây là số;
> mục *Ba điều bản đầu nói sai* nói rõ chỗ nào đổi và vì sao. Phần chặn cứng C1–C5 của bản đầu
> **đã kiểm lại bằng mã nguồn và đều đúng**, được giữ nguyên.

Mọi con số trong file này tái lập được bằng **một lệnh**:

```bash
python tools/probe_sensor_noise.py sheet/test.json
python tools/probe_sensor_noise.py tools/slots.txt --calibrated   # máy thứ hai, để đối chứng
```

Công cụ **không chép công thức từ firmware** — nó mô tả *dữ liệu firmware đã sinh ra*. Chỗ duy
nhất nó soi gương `Alg/Algo.cpp` là chuỗi baseline → Savitzky-Golay → đạo hàm → đỉnh, được đánh
dấu `MIRROR`, vì `sharpness` là đại lượng `min_sharpness` đem so, và một con số nhiễu tính bằng
count thô thì tự nó không nói lên điều gì.

---

## Đo được gì (RPL250701, run 30-07-2025, 10 kênh × 120 vòng)

| Đại lượng | Số đo | Nghĩa |
| --- | --- | --- |
| Mức lưu (tổng 8 lần đọc) | **212 – 568** | còn **115×** tới trần `uint16_t` |
| σ mỗi vòng, đã khử trôi | **≈ 2.0 count** | |
| σ **theo mức tín hiệu** | `σ = −0.0001·mean + 2.06`, **r = −0.03** | **CỘNG TÍNH** |
| Dải mức giữa các kênh | **1.80×** (307 → 553) | |
| Dải `slopes[]` | **1.19×** (1.30 → 1.55) | |
| Thành phần **chung cả 10 kênh** | **64%** phương sai mỗi vòng | |
| `sharpness` trên 10 đường **âm tính thật** | **3.12 – 5.40** (trung vị 4.41) | ngưỡng là **8.0** |
| `sharpness` sau khi bỏ thành phần chung | **1.65 – 4.13** (trung vị **2.30**) | |
| `sharpness` của đường **PHẲNG + nhiễu** ở σ hiện tại | tb **2.58**, p99 **4.37**, max **6.11** | `P(>8.0) = 0.000%` |
| `P(sharpness > 4.0)` từ nhiễu thuần | **1.6%/kênh → 14.9%/run** | giá của việc hạ ngưỡng xuống 4.0 |
| Bậc thang đồng bộ cả 10 kênh, vòng 5→6 (t = 1.7 phút) | **+109 count (+34%)**, mô hình **cộng tính** thắng (residual 16.0 vs 23.4) | |

### 1. Nhiễu là CỘNG TÍNH, không phải nhiễu bắn

σ **không nhúc nhích** khi mức tín hiệu đi từ 307 lên 553 (r = −0.03 trên dải 1.8×). Nhiễu bắn
sẽ cho σ ∝ √mức (tăng 1.34× trên dải đó); nhiễu nhân sẽ cho σ ∝ mức. Không cái nào xảy ra.

Quy về mỗi lần đọc: σ₀ ≈ 2.0/√8 ≈ **0.7 LSB** — đúng sàn lượng tử hoá + đọc của ADC.

**Hệ quả trực tiếp, và nó tốt hơn bản đầu tưởng:** nhân gain lên `k` thì tín hiệu ×`k` còn σ
đứng yên ⇒ **SNR cải thiện đúng `k` lần, không phải `√k`**. Bản đầu không biết điều này nên
không dám nói.

**Hệ quả thứ hai, và nó mới là điểm quan trọng:** nhiễu cộng tính là **mỗi LẦN CHUYỂN ĐỔI**,
không phải mỗi photon. Cho nên với **cùng một tổng thời gian tích phân**, *ít lần đọc dài* đánh
bại *nhiều lần đọc ngắn* — xem mục Đòn bẩy #1.

### 2. 64% nhiễu mỗi vòng là CHUNG cho cả mười giếng

Tách thành phần nhanh (bỏ trôi chậm bằng trung bình trượt 21 điểm) rồi lấy trung bình theo vòng
qua 10 kênh: σ của thành phần chung là **1.63** trên σ mỗi kênh **2.03** ⇒ **64% phương sai mỗi
vòng là chung**.

Mười giếng độc lập về sinh học. Thứ chung cho cả mười là **của máy**. Và nó nằm đúng dải tần mà
`sharpness` đo: bỏ nó đi thì `sharpness` của 10 đường âm tính tụt từ **3.12–5.40 xuống
1.65–4.13**, trung vị **4.41 → 2.30**.

⚠ **Con số này phụ thuộc máy/run.** Trên `tools/slots.txt` (máy khác) thành phần chung chỉ
**9%**. Nên đây **không phải** hằng số của thiết kế mà là **một tình trạng** — lý do càng mạnh
để máy **tự đo** nó mỗi run thay vì giả định.

### 3. `min_sharpness = 8.0` an toàn — nhưng **4.0 thì chưa**, và đây là cái giá đo được

Nói chính xác, vì chỗ này dễ nói quá:

- **8.0 KHÔNG nằm trong dải nhiễu.** Đường phẳng + nhiễu Gauss ở σ hiện tại, 4000 lượt mô phỏng:
  `P(sharpness > 8.0) = **0.000%**`, cực đại quan sát **6.11**. Ngưỡng hiện tại đứng vững.
- Nhưng [2026-08-21](../history/2026-08-21-asf-va-quy-tac-hinh-dang.md) ghi dương tính **qPCR đã
  xác nhận** ở ASF nằm tại **4.0 – 8.4** — đó là **lý do duy nhất** có người muốn hạ ngưỡng. Và
  ở **4.0**:

| σ (calibrated) | `P(>4.0)` mỗi kênh | **`P(>4.0)` mỗi run (10 kênh)** |
| --- | --- | --- |
| **1.42 — hôm nay** | 1.60% | **14.9%** |
| 1.01 — nhẹ hơn 1.4× | 0.15% | 1.5% |
| **0.71 — nhẹ hơn 2×** | 0.000% | **0.00%** |

Đọc bảng này là đọc **giá của việc hạ ngưỡng xuống 4.0**: hôm nay, **cứ 7 run thì 1 run có ít
nhất một giếng vượt 4.0 chỉ bằng nhiễu**, không có gì khuếch đại cả. Đó là dương tính giả, trên
một thiết bị y tế.

⇒ **Hạ `min_sharpness` xuống 4.0 là không chấp nhận được ở mức nhiễu hôm nay, và trở nên chấp
nhận được ở mức nhẹ hơn 2×.** Nhẹ hơn 2× **đúng bằng** thứ mà `2 × 400 ms` ở Đòn bẩy #1 cho —
không cần gain, không cần recalibrate.

Đây là mắt xích duy nhất nối công việc này với bài toán ASF đang chặn phát hành, và nó **có cơ
sở đo đạc** chứ không phải lập luận. ⚠ Nó cũng **chỉ nói về nhiễu**: hạ ngưỡng còn phải trả lời
câu hỏi *đường cong không đặc hiệu* có vượt 4.0 không, mà bộ nhãn trả lời câu đó
(`tools/algo_labels.tsv`, 726 kênh) **không tồn tại trong branch này**. Không có nó thì con số ở
đây là **điều kiện cần, không phải điều kiện đủ**.

---

## Ba điều bản đầu nói sai

### A. "Auto-origin KHÔNG THỂ đổi kết quả" — chỉ đúng với origin là HẰNG SỐ

Chứng minh của bản đầu (`O/slope` là hằng cộng nên triệt tiêu khỏi mọi hiệu) **đúng và vẫn
đúng** — cho **giai đoạn 1**, tức ghi `cal_calib[2]` một lần vào `origins[slot]`.

Nó **không áp dụng** cho **giai đoạn 2**: một phép đo nền tối **mỗi vòng** là một **chuỗi thời
gian**, không phải hằng số. Trừ một chuỗi thời gian thì **mọi sai phân đều đổi** — kể cả
`differential_data` và `sharpness`. Số đo ở mục 2 chỉ ra chính xác nó đổi bao nhiêu: trung vị
`sharpness` **4.41 → 2.30**.

⇒ **Giai đoạn 2 không phải "tuỳ chọn" và không phải tính năng hiển thị. Nó là đòn bẩy lớn nhất
đo được trong toàn bộ khảo sát này.** Bản đầu xếp nó xuống cuối vì áp nhầm chứng minh của giai
đoạn 1 lên cả hai.

### B. "Trần auto-gain là ×4 (DG ×2 · IT ×2)" — nửa số đó có thể là ảo

`DG` là **digital gain**: nhân đôi **sau** ADC. Nếu đúng vậy thì nó nhân **cả tín hiệu lẫn nhiễu
lượng tử** ⇒ **SNR không đổi một chút nào**, chỉ đổi thang (và kéo theo toàn bộ nghĩa vụ bù
slope, không đổi lại được gì).

`SENS` và `GAIN` khuếch đại **trước** ADC nên là thật — nhưng **cả hai đã kịch trần**
(`SENS ×1`, `GAIN ×2`). Trục analog duy nhất còn lại là **`ALS_IT`**.

**Phép thử dứt điểm, làm được trong 5 phút trên bench:** đặt `DG = ×2` rồi đọc thanh ghi ALS.
Nếu mọi giá trị đều **chẵn** → nhân đôi thuần digital → **loại `DG` khỏi thang gain**. Nếu có
giá trị lẻ → có độ phân giải thật → giữ lại. **Đừng đưa `DG` vào thang trước khi có kết quả
này.**

### C. "`slopes` không hấp thụ chênh lệch quang học ~2× nên khe tối khó ra Positive hơn"

Số nói ngược lại. Mức nền chênh **1.80×** nhưng `slopes` chỉ chênh **1.19×**. Nếu chênh lệch đó
là **độ nhạy** thì `slopes` phải chênh xấp xỉ bằng ngần ấy. Nó không.

⇒ Chênh lệch giữa các khe **chủ yếu là OFFSET** (ánh sáng tạp, huỳnh quang nền của buffer), chứ
không phải độ nhạy. `slopes` **đang** mô tả đúng đáp ứng, và **không có** thiên lệch hệ thống
nào giữa các khe về khả năng đạt `min_increase`.

Điều này **không làm yếu** lý do làm auto-origin — nó làm **mạnh thêm**: offset ấy lớn tới
**307–553 count**, tương đương **220–400 nM thuốc nhuộm ảo**, và đó **chính xác** là đại lượng
`origins[]` sinh ra để giữ. Hôm nay nó bằng 0 ở cả 10 kênh.

---

## Đòn bẩy, xếp theo (lợi ích đo được ÷ rủi ro)

### #1 — Chia lại ngân sách tích phân: `N × IT` giữ nguyên tích số

Giá trị lưu = tổng `N` lần đọc, mỗi lần **tỉ lệ tuyến tính với `ALS_IT`**. Giữ `N × IT = 800 ms`
thì **con số không đổi một đơn vị nào** — `slopes`, `origins`, `min_increase`, `min_sharpness`,
cổng chấp nhận của wizard calib, **tất cả giữ nguyên nghĩa**. Chỉ nhiễu đổi, vì số hạng cộng
tính ở mục 1 là **mỗi lần chuyển đổi**:

| `N × IT` | Thang | Nhiễu | So với nay | Dwell sàn | ×10 kênh |
| --- | --- | --- | --- | --- | --- |
| **8 × 100 ms** *(nay)* | 8× | 2.83·σ₀ | 1.00× | 900 ms | ~11.6 s |
| 4 × 200 ms | 8× | 2.00·σ₀ | **1.41×** | 1000 ms | ~11.8 s |
| **2 × 400 ms** | 8× | 1.41·σ₀ | **2.00×** | 1200 ms | ~13.4 s |
| 1 × 800 ms | 8× | 1.00·σ₀ | **2.83×** | 1600 ms | ~17.2 s |

**Không cần recalibrate. Không cần bù gain. Không đụng một thanh ghi gain nào.** Đây là thứ
mạnh hơn toàn bộ thang auto-gain mà bản đầu đề xuất, và rẻ hơn nhiều.

- **Khuyến nghị: đi `4 × 200 ms` trước** (×1.41). Nó gần như không tốn thêm thời gian vòng và
  **vẫn còn 4 mẫu** cho phép lọc trung vị.
- **`2 × 400 ms` (×2.00) là bước hai**, và **chỉ sau khi `filterOdds` được thiết kế lại**: với
  N = 2, trung vị là trung bình của đúng hai mẫu, cả hai lệch bằng nhau, nên hoặc **giữ cả hai
  hoặc xoá cả hai** — xoá cả hai thì vector rỗng, rơi thẳng vào C9. Với N = 2 phép kiểm đúng là
  `|a − b| ≤ ngưỡng` rồi đọc lại, **không phải** lọc trung vị.
- `1 × 800 ms` (×2.83) mất hoàn toàn khả năng phát hiện mẫu hỏng. Chỉ cân nhắc nếu #2 và #3
  không đủ.

⚠ **Luật `√N` này đứng trên kết quả mục 1** (nhiễu cộng tính, độc lập mức). Phải **đo lại ở điểm
làm việc mới** (nghiệm thu #3). Nếu ở IT dài nhiễu bắn trở nên đáng kể thì lợi ích co lại.

⚠ Bước qua ranh giới `{25,50} ↔ {100,200,400,800}` là chỗ C5 cắn. **Đường 100 → 200/400/800 nằm
trọn trong một nhóm nên KHÔNG dính C5** — nhưng vẫn phải sửa C5 trước, vì đọc lại để xác minh
(`VEML6035_GET_ALS_IT_Bits()`) là bắt buộc.

### #2 — Đo nền tối mỗi vòng (auto-origin giai đoạn 2)

Đòn bẩy lớn nhất đo được: trung vị `sharpness` **4.41 → 2.30** trên máy có thành phần chung
mạnh. Và bậc thang +109 count ở phút 1.7 **hợp mô hình cộng tính** (residual 16.0 so với 23.4)
⇒ **một phép đo LED-tắt sẽ nhìn thấy nó**.

- **Một phép đo tham chiếu mỗi VÒNG là đủ, không cần mỗi kênh** — theo định nghĩa thành phần
  chung là chung. Chi phí ≈ một dwell (~1.2 s/vòng).
- Ghi vào `origins[i]` **trong RAM cho run đó**, **không** ghi EEPROM.
- ⚠ **Chưa chứng minh được nguồn là cộng tính hay nhân.** Dải mức chỉ 1.8× nên hai mô hình khớp
  ngang nhau (β/mức CV 20%, β CV 21%). Bậc thang thì nghiêng hẳn về cộng tính. **Phép thử dứt
  điểm là bench** (nghiệm thu #7): chạy một run tắt LED ở các vòng chẵn rồi tương quan.
  Nếu hoá ra là **nhân** (LED đổi cường độ) thì nền tối **không** bắt được và phải dùng tham
  chiếu có đèn.

### #3 — Chặn heater đáy trong lúc đọc quang

`MaintainHotlid23()` (`PIDControl.cpp:1657`) **đã** tắt hotlid khi đang đọc, kèm comment nói
thẳng lý do: *"Opto sensor reading now, stop heating hotlid, to make sure the power is stable"*.

Nhưng `Maintain2_67()` và `Maintain3_67()` — **hai heater đáy giữ khối phản ứng ở 65 °C, tức hai
tải lớn nhất máy** — **không hề kiểm `bSensorReadingGet()`**. Chúng PWM suốt mọi lần đọc quang.
Bất đối xứng này là ứng viên số một cho thành phần chung ở mục 2.

- **Đừng blanking thẳng tay.** Vòng đọc chiếm ~11–17 s trong 20 s; cắt nhiệt ngần ấy có thể kéo
  `CURRENT_TEMP_PID` xuống dưới `UNDERHEAT_THRESHOLD2` → `rerun()` + báo lỗi, tức biến một cải
  tiến nhiễu thành hỏng run.
- **Phương án nhẹ hơn, giữ nguyên công suất trung bình: ĐÓNG BĂNG duty** trong suốt vòng đọc —
  tính PID một lần trước vòng, giữ nguyên giá trị đó tới hết vòng. Nhiệt lượng không đổi nên
  không có rủi ro underheat; chỉ bỏ đi phần *thay đổi* giữa các vòng.
- **Phép thử rẻ và dứt điểm trước khi sửa gì**: firmware đã in `PWM %d` mỗi lần maintain. Bắt
  serial một run rồi tương quan duty heater với thành phần chung mỗi vòng do
  `probe_sensor_noise.py` xuất ra. Nếu tương quan cao thì đây là nguyên nhân; nếu không thì
  #3 vô ích và #2 vẫn đúng.

### #4 — Auto-gain thật (chỉ còn trục `ALS_IT`)

Sau #1, `ALS_IT` **đã bị tiêu cho việc giảm nhiễu**, nên auto-gain per-channel còn lại rất ít
chỗ xoay: `SENS` và `GAIN` kịch trần, `DG` có thể vô dụng (mục B).

⇒ **Auto-gain tụt từ "tính năng chính" xuống "chống bão hoà + cân bằng khe"**, và ngay cả giá
trị cân bằng khe cũng đáng ngờ sau mục C. Với biên **115×** tới trần và không kênh nào bão hoà,
**bài toán nó giải hiện không tồn tại trên fleet**.

**Khuyến nghị: hoãn auto-gain.** Giữ lại phần **phát hiện bão hoà** (C1/C2) — phần đó rẻ, và là
lưới an toàn cho chính #1 (đổi `N × IT` giữ nguyên thang nên không đổi biên bão hoà, nhưng phép
kiểm vẫn nên có).

### #5 — Tần số PWM của LED

LED chạy `analogWrite()` = **LEDC 1 kHz, 8 bit** (`esp32-hal-ledc.c`, core 2.0.11), duty 150/255,
**chạy tự do so với đồng hồ tích phân của VEML6035**. Sai số pha bị chặn ở
`Tp·(1−d)/T = 1 ms × 0.412 / 100 ms ≈ **0.41%**` mỗi lần đọc — cùng bậc với σ đo được (0.4–0.7%),
nên **có thể là một phần đáng kể** của nhiễu nền.

- Ở `IT = 400 ms` (#1) sai số này tự động chia 4 → **#1 đã bịt phần lớn rồi**.
- ⚠ **Đừng gọi `analogWriteFrequency()`** — nó là **toàn cục** và sẽ kéo luôn PWM của cả 6 chân
  heater sang tần số mới. Muốn nâng riêng LED thì cấp cho nó một kênh LEDC riêng
  (`ledcSetup`/`ledcAttachPin`/`ledcWrite`) ở **nhóm 0** — `analogWrite` cấp phát từ kênh 15 đi
  xuống nên với 7 chân đang dùng, cả nhóm 0 (kênh 0–7) còn trống.
- Xếp cuối vì #1 đã hưởng phần lớn lợi ích.

### #6 — Kênh WHITE: đang BẬT và chưa bao giờ được đọc

`Config_CHANNEL = VEML6035_WHITE_CH_EN` (`sensor6035.h:24`), nhưng code chỉ đọc thanh ghi `ALS`.
Kênh WHITE **đã được đo song song, không tốn thêm một mili-giây tích phân nào** — đọc nó là
thêm đúng một giao dịch I²C.

Nó cho một ước lượng **độc lập** của cùng luồng sáng với trọng số phổ khác ⇒ tỉ số ALS/WHITE
tách được *"phổ đổi"* (LED già đi, trôi theo nhiệt) khỏi *"lượng đổi"* (huỳnh quang). Rẻ tới mức
đáng lấy — **nhưng phải đặc trưng hoá trên bench trước**, vì chưa biết bộ lọc quang của máy đặt
ở kênh nào.

---

## ⛔ Chặn cứng phải sửa TRƯỚC

C1–C5 là của bản đầu, **đã kiểm lại từng cái bằng mã nguồn và đều đúng**. C6–C11 là mới.

### C1 — Trần bão hoà thật là ~8191 count/lần đọc, không phải 65535 ✔ đã xác nhận

`sensor67Value[i][j]` giữ **TỔNG 8 lần đọc** trong một `uint16_t`
(`acquisition.cpp:181` `getSum()` trả `Word`; `sensor6035.cpp:1763`). Vượt trần → cuộn vòng
mod 65536 → đường cong **gập xuống**, trông y hệt huỳnh quang đang giảm. Đo thật: cao nhất
**568**, còn **115×**.

### C2 — Không có phép kiểm cận trên nào trong lúc chạy ✔ đã xác nhận

`Snapshot()` chỉ phân loại ở hai điểm mút và **chỉ chạy lúc boot**
(`sensor6035.cpp:1318-1336`). Trong run (`:1716-1730`) chỉ có `== 0` và `<= 2`.
`errorWrongData` (`errorCheck.h:25`) **đã định nghĩa nhưng chưa từng được raise** — chỗ trống
sẵn cho điều kiện bão hoà.

### C3 — `filterOdds` ngưỡng TUYỆT ĐỐI 3 count ✔ đã xác nhận, và lý do sâu hơn bản đầu nghĩ

`acquisition.h:14` `threshold = 3`, `acquisition.cpp:102`.

Nhưng **nó đang làm HAI việc, không phải một**. Đọc `calib_sensor()` (C10) sẽ thấy: bộ lọc này
là thứ **duy nhất** dọn các mẫu chụp trúng lúc LED đang lên. Ngưỡng ±3 count chặt như vậy
**không phải** để loại ngoại lai thống kê — nó là **cổng "mẫu này đã được chiếu sáng đủ chưa"**.

⇒ **Nới nó ra là mất cổng đó.** Cách sửa đúng là **tách hai việc**: bắt buộc thời gian ổn định
(≥ 2·IT) ở **mọi** đường acquisition, rồi mới cho phép bộ lọc thành tương đối.

*(Đã kiểm và loại một nghi ngờ: `abs(*it - median)` — `acquisition.cpp` không kéo `Arduino.h` nên
sợ rơi vào `abs(int)` làm cụt phần thập phân. Biên dịch thử bằng chính `xtensa-esp32-elf-g++`
với đúng bộ include đó: nó chọn overload `double`. **Không phải lỗi.**)*

### C4 — Reconfig giữa run ghi đè gain bằng `Config_*` biên dịch ✔ đã xác nhận, và tệ hơn

`reConfigSingleSensor()` (`sensor6035.cpp:1947`) áp **hằng số biên dịch**. Nó được gọi từ
`reConfigSingleSlotSensor()`, mà đường đó chạy **từ trong vòng đo** — và không chỉ khi
`maxErrors`: **một lần đọc trả 0 là đủ** (`sensor6035.cpp:1716-1719`). Kèm theo
`delay(20)` ×2 + reset I²C, tức **~40 ms chặn ngay giữa vòng**.

### C5 — `VEML6035_SET_ALS_IT()` hỏng khi bước qua ranh giới ✔ đã xác nhận

`ALS_IT` là trường **4 bit vắt qua hai byte** (bit 9:6 của `ALS_CONF_0`); setter chỉ ghi **một**
byte tuỳ giá trị (`VEML6035_ALS.cpp:126-142`). Đặt 100 ms khi đang ở 25 ms → trường thành `1100`
= **vẫn 25 ms**. Đặt 25 ms khi đang ở 800 ms → `1111` = **mã không hợp lệ**.

Đường của #1 (100 → 200/400/800) nằm trọn trong nhóm byte 0 nên **không** dính — nhưng vẫn phải
sửa, và mọi lần đặt IT phải **đọc lại xác minh** bằng `VEML6035_GET_ALS_IT_Bits()`.

### C6 — 🆕 Khoảng cách giữa hai lần đọc là **hằng số 100 ms**, không suy từ `ALS_IT`

`sensor6035.cpp:1758` `sensor67ValueTime = millis() + 100;`

Nâng IT mà không nâng chỗ này thì 8 lần đọc **trả về cùng MỘT lần chuyển đổi**. Ở IT = 800 ms:
tổng = 8 × C₈₀₀ = **64 × C₁₀₀** ⇒ **sai thang 8 lần**, mọi ngưỡng bay, và **không có phép kiểm
nào bắt được**. Đây là chặn cứng của #1.

### C7 — 🆕 `LED_DELAY_TIME` phải ≥ **2 × ALS_IT**, và không ai ghi điều đó ở đâu

Khi LED bật, lần chuyển đổi **đang dở** bị nhiễm (một phần tối). Lần chuyển đổi **đầu tiên được
chiếu sáng trọn vẹn** hoàn tất ở **2·IT**.

Hôm nay `LEDDuration = 200 ms` (`define.h:284`) và `ALS_IT = 100 ms` — **đúng bằng biên**. Đó là
lý do duy nhất lần đọc đầu sạch, và nó **trông như trùng hợp** chứ không phải một ràng buộc
được phát biểu. Nâng IT mà quên nâng settle → **mọi lần đọc đầu thấp một cách hệ thống, im
lặng**.

### C8 — 🆕 `store()` có điều kiện là một mệnh đề LUÔN ĐÚNG

```cpp
// acquisition.cpp:37
if (value != 0 || value < 1000) { values.push_back(value); }
else { numErrors += 1; }        // <-- KHÔNG BAO GIỜ CHẠY
```

`value == 0` → vế trái sai, vế phải `0 < 1000` **đúng** → OR đúng → **giá trị 0 vẫn được lưu**.
Với mọi `value` khác 0 thì vế trái đúng. Nhánh `else` là **code chết**.

⚠ **Và đừng "sửa" nó thành `&&`.** Làm thế là dựng một **trần cứng 1000 count mỗi lần đọc**,
tức tăng gain hay tăng IT là mọi mẫu bị tính thành lỗi → `maxErrors` → C4. Cách sửa đúng là
**bỏ hằng số 1000**, giữ lại phép kiểm `!= 0`, và thay trần bằng phép kiểm bão hoà thật của C2.

### C9 — 🆕 `fixValuesErrors()` giải tham chiếu con trỏ NULL khi vector rỗng

```cpp
// acquisition.cpp:248
uint8_t tmp = values.size() / 2;
while (values.size() < repeats) { values.push_back(values[tmp]); }
```

`values` rỗng → `tmp = 0` → `values[0]` trên vector rỗng → `_M_start` là `nullptr` →
**LoadProhibited**.

`filterOdds` **có thể** làm rỗng vector: dữ liệu hai cụm (ví dụ `{10,10,10,10,90,90,90,90}`) cho
trung vị 50, cả 8 mẫu lệch 40 > 3 → **xoá sạch**. Đường chạy run có chắn
(`getSizeValues() > 0`, `sensor6035.cpp:1736`) nhưng **`calib_sensor()` và `testShot()` thì
không** — chúng chỉ `return` khi `isMaxErrorReached()`, và `fixValuesErrors` ở đó không được
gọi. Chắn là ngẫu nhiên, không phải thiết kế.

### C10 — 🆕 `calib_sensor()` KHÔNG chờ LED ổn định

```cpp
// sensor6035.cpp:2312-2321
_LED.LED_on_unguarded(slot);
openSensorChannel(slot);
acquisitionControl.clear();
while (!acquisitionControl.isFinished()) { ...đọc ngay... delay(100); }
```

Không có `LED_DELAY_TIME`, không có bất kỳ độ trễ nào trước lần đọc đầu — khác hẳn đường chạy
run (`:1689`). Lần đọc đầu trả về một lần chuyển đổi **hoàn tất trước khi LED bật** (tối), lần
thứ hai vắt qua sườn lên (một phần tối).

Hiện `filterOdds` đang cứu nó (hai mẫu thấp bị xoá, hai mẫu mới được đọc bù) — **đó chính là
việc mà ngưỡng ±3 đang gánh, xem C3**. Nghĩa là **hiệu chuẩn đang phụ thuộc vào một tác dụng
phụ của bộ lọc ngoại lai** để không đo hụt. Nới bộ lọc mà không thêm settle = `slopes` sai
~12% ở lần calib kế tiếp, im lặng, trên đại lượng chia cho mọi thứ.

### C11 — 🆕 Đọc ngoài mảng ở nhánh "tất cả đều lỗi", và số 8 bị hardcode

```cpp
// sensor6035.cpp:1752
acquisitionControl.store((sensor67Value[iChannel][COUNTER - 1] / 8));
```

`COUNTER` là `uint8_t`; ở vòng đầu `COUNTER == 0` nên `COUNTER - 1` thăng lên `int` = **−1** →
`sensor67Value[0][-1]` đọc **ngoài mảng**. Và `/ 8` là `repeats` bị chép cứng — #1 không đổi
`repeats` nhưng bất kỳ thay đổi nào sau này cũng sẽ trượt ở đây.

### C12 — 🆕 `GainProcess()` ghi vào kênh mux nào đang mở

Bản đầu đã ghi ở mục Rủi ro; nâng lên thành chặn cứng vì nó là **đường Serial ghi cấu hình mà
không chọn kênh** (`sensor6035.cpp:1010-1035`) ⇒ lệnh `GAIN Double` hôm nay áp vào một kênh ngẫu
nhiên và để nó lệch với 9 kênh còn lại, vĩnh viễn, không dấu vết.

*(Ghi chú nhỏ, không phải chặn: comment doxygen của `VEML6035_GET_ALS_DATA_I2C_Res` nói
`return true on success`, trong khi hàm trả thẳng `int` của `ReadI2C_Bus` với **0 = thành công**.
Mọi caller đang dùng `!flagres` nên **đúng**; chỉ là comment mời người sau viết sai.)*

---

## Cái gì KHÔNG đụng

- **`led_power[10]`** — giữ 150. Đẩy LED mạnh hơn làm **photobleach** mẫu; `define.h` đã ghi
  nhận hiện tượng (*"12 of 37 non-specific curves peak then photobleach"*). Photobleaching là
  suy giảm **theo thời gian**, không phải hệ số tỉ lệ, nên **slope không bù được**. Quyết định,
  không phải bỏ sót.
- **`slopes[]` trong EEPROM** — mọi bù trừ làm ở **chỗ đọc**, không ghi đè giá trị hiệu chuẩn.
- **Thang lưu** — #1 cố ý giữ `N × IT` bất biến để không phải recalibrate. Đây là ràng buộc
  thiết kế, không phải sự tiện tay.
- **`min_increase` / `min_sharpness`** — **không đổi trong bản này**. Mục 3 nói rõ vì sao hôm
  nay chưa hạ được; hạ sau khi có sàn nhiễu mới là một quyết định riêng, cần số riêng, và cần
  người chịu trách nhiệm lâm sàng.
- **Wizard calib 4 điểm** — chỉ thêm settle (C10) và ràng buộc *"chạy ở điểm làm việc tham
  chiếu"*, không đổi thuật toán.
- **Không thêm field EEPROM.** `parastructure` ở trần 400/402 B.

---

## Đo & nghiệm thu

1. **Trước khi sửa gì: chạy `probe_sensor_noise.py` trên ≥ 5 run thật từ ≥ 3 máy.** Con số
   64% thành phần chung mới chỉ có **một** máy chống lưng, và máy thứ hai cho 9%. Nếu nó không
   tái lập thì #2 và #3 tụt hạng.
2. **`DG` là digital hay không** (mục B): đặt `DG = ×2`, đọc thanh ghi ALS, xem giá trị có luôn
   chẵn không. Quyết định `DG` có được vào thang gain hay không.
3. **Luật `√N` ở điểm làm việc mới**: chạy lại mục 2 của công cụ sau khi đổi sang `4 × 200 ms`.
   σ phải **không đổi theo mức** và **giảm đúng 1.41×**. Không giảm đủ = nhiễu bắn đã lên tiếng.
4. **Thời gian vòng thật**: đo dwell 10 kênh ở cấu hình mới, khẳng định `< timePerLoop`, và chặn
   IT khi `timePerLoop` bị hạ từ web. ⚠ Nếu vòng đọc **tràn** `OPTO_INTERVAL` thì
   `time_data[i] = i * OPTO_INTERVAL / 60000` **nói dối** — trục thời gian giả định đúng 20 s mỗi
   vòng, và Ct sai theo.
5. **Thang bất biến, end-to-end**: cùng một mẫu, `8 × 100` và `4 × 200` phải cho **cùng verdict
   và cùng Ct** trong dung sai. Đây là phép thử duy nhất chứng minh #1 an toàn.
6. **Tỉ lệ loại mẫu của `filterOdds`** trước/sau (C3). Nếu `numErrors` tăng thì
   `reConfigSingleSlotSensor()` sẽ bắn giữa run (C4) và dwell phình ra (nghiệm thu #4).
7. **Nền tối là cộng tính hay nhân** (#2): chạy một run **tắt LED ở các vòng chẵn**, tương quan
   chuỗi tối với thành phần chung mà công cụ xuất ra. Quyết định xây tham chiếu tối hay tham
   chiếu có đèn.
8. **Heater có phải nguồn không** (#3): bắt serial `PWM %d` một run, tương quan duty heater đáy
   với thành phần chung mỗi vòng. Rẻ, và làm được **trước** khi sửa một dòng nào.
9. **Biên tràn `getSum()`**: ghi lại **tổng cao nhất trên 10 kênh × cả run**, khẳng định
   `< 32768`. Kèm một phép thử tiêm: ép một kênh vượt trần và xác nhận nó bị **bắt** (C2) chứ
   không gập xuống.
10. **Guard tĩnh**: (a) không lời gọi đổi cấu hình cảm biến nào nằm trong đường `eoptoreading`;
    (b) `VEML6035_SET_ALS_IT` ghi đủ 4 bit **và** có đọc lại xác minh; (c) `Config_*` và bộ chuỗi
    `*Set` (`define.h:397-402`) khớp nhau; (d) đường reconfig áp cấu hình **của run** (C4);
    (e) **`LED_DELAY_TIME >= 2 * ALS_IT`** và khoảng cách giữa hai lần đọc **suy từ `ALS_IT`**
    (C6/C7); (f) mọi đường acquisition — run, calib, testShot — dùng **cùng một** hàm settle.

---

## Rủi ro

- **Payload cũ mất khả năng phát lại** nếu quên đưa cấu hình quang thực dùng (`N`, `IT`, gain)
  vào payload upload. Rủi ro **một chiều**: dữ liệu đã upload không mang thông tin để phục hồi.
  Với #1 thang không đổi nên hậu quả nhẹ hơn bản đầu tưởng — nhưng **vẫn phải ghi**, vì sàn
  nhiễu đổi và mọi phân tích hồi cứu về nhiễu sẽ trộn hai chế độ mà không biết.
- **Cổng chấp nhận của wizard calib** (slope 0.5–3.5, R² ≥ 0.95, `displayLCD.cpp:2216-2265`)
  được chỉnh theo điểm làm việc hôm nay, và `slopes` có đơn vị *"tổng-8-count trên nM"* — phụ
  thuộc **cả gain lẫn số lần lặp lẫn IT**. #1 giữ nguyên tích `N × IT` nên cổng này **an toàn**;
  bất kỳ thay đổi nào **không** giữ tích số thì phải chỉnh cổng, nếu không **một lần hiệu chuẩn
  đúng sẽ bị máy từ chối** và người vận hành không có cách nào biết vì sao.
- **Nếu #2 hoá ra là nhiễu nhân** thì nền tối không cứu được, và lợi ích lớn nhất trong khảo sát
  này bốc hơi. Nghiệm thu #7 phải chạy **trước** khi viết code cho #2.
- **#3 có thể đổi một bài toán nhiễu thành một bài toán nhiệt.** `UNDERHEAT_THRESHOLD2` gọi
  `rerun()`. Đóng băng duty (không blanking) là đường ít rủi ro hơn, nhưng vẫn phải đo nhiệt độ
  khối qua vài run đầy trước khi tin.
- **`parastructure` = 400 B, `alignof` = 8 → kích thước hợp lệ kế tiếp là 408 > trần 402.**
  Không thêm được field mới ở cuối, bất kể kiểu gì. Nếu về sau buộc phải lưu state per-channel:
  chỗ duy nhất sạch là `double empty[2]` (offset 384, `define.h:448-449`). Thiết kế này cố ý
  **không cần** tới đó.
- **Migration `ADDR_CONFIG_REV` có `calibIntact` chỉ bảo vệ `slopes`/`origins`/`led_power`**
  (`ForteSetting.cpp:906-909`). State per-channel mới nào được lưu thì **phải vào danh sách
  keep/compare đó**, nếu không lần bump `CONFIG_REV_THRESHOLDS` kế tiếp sẽ âm thầm ghi đè.

---

## Quan hệ với kế hoạch kia

[2026-09-10-dieu-kien-tra-error-tang-som.md](2026-09-10-dieu-kien-tra-error-tang-som.md) không
đụng cùng dòng code nào, nhưng nối vào ở hai chỗ, cả hai chảy theo chiều **kế hoạch này → kế
hoạch kia**:

1. **Ca lỗi chung.** RPL02001 Bình Đại 12/02 — cả mười giếng trôi lên, bốn giếng bị gọi `Error`.
   Đó vừa là nhánh `!` mà kế hoạch kia thiết kế lại, vừa là thứ mà **mục 2 của khảo sát này vừa
   đo được ở dạng nhỏ hơn**: 64% nhiễu mỗi vòng là chung cho cả mười giếng, cộng một bậc thang
   +109 count đồng bộ. Sửa bên kia một mình chỉ **đổi nhãn** cho ca này; #2 cho máy khả năng
   **nhận ra** nó.
2. **Số để chỉnh ngưỡng.** Mục 3 là con số duy nhất có thể biện minh cho việc chạm vào
   `min_sharpness` hoặc `MIN_CALLABLE_CT` — và nó hiện đang nói **chưa được chạm**.

→ Nếu làm cả hai: **chạy `probe_sensor_noise.py` trên kho log trước khi chốt bất kỳ ngưỡng nào
bên kia.**

---

## Tài liệu — khi CODE thật sự thay đổi (không phải bước này)

File này là **kế hoạch**. Khi code landed thì viết ghi chú **lịch sử** riêng,
`docs/history/<ngày landed>-<slug>.md`: trước/sau, bảng `N × IT` kèm số nhiễu **đo lại** ở điểm
làm việc mới, kết quả nghiệm thu #2 (`DG`), #7 (tối cộng tính hay nhân) và #8 (heater), các chặn
cứng C1–C12 đã sửa thế nào, và **quyết định không đụng `led_power`** kèm lý do photobleach.
