# 2026-09-10 — Thiết kế lại điều kiện trả `!` (outcome `Error`), nhánh "tăng quá sớm"

**Trạng thái: KẾ HOẠCH, chưa có dòng code nào.** Kế hoạch song song:
[2026-09-10-auto-gain-auto-origin.md](2026-09-10-auto-gain-auto-origin.md) — xem mục
*Quan hệ với kế hoạch kia* ở cuối.

## Bối cảnh

`!` là glyph của outcome **`Error` (E)**, xuất hiện đúng hai nơi với người dùng: `|  ! |` trên TFT
(`displayLCD.cpp:1470`) và `"<tên> | !  | E"` trong payload upload (`Bluetooth.cpp:666`). Không có
`!` nào khác là kết quả.

`E` chỉ sinh ra ở **đúng 2 nhánh** trong `predict_outcome_core()`. Kế hoạch này chỉ đụng **nhánh 1**:

```cpp
// Algo.cpp:862-872
if (record.outcome.increase > parameters.min_increase)            // đủ lớn
  if (record.peak_features.main_peak.y > parameters.min_sharpness) // đủ dốc
    if (record.outcome.transition_time.x < parameters.detection_margin_time)
        strcpy(record.outcome.outcome, OutcomeError);              // <= '!'
```

Nghĩa hiện tại: **giếng đã khuếch đại thật** (qua cả `min_increase` 25 lẫn `min_sharpness` 8) nhưng
Ct rơi trước `detection_margin_time` = 4.0 phút → máy trả `!`.

Vấn đề: `!` nói với người vận hành *"máy hỏng / không đo được"*, trong khi thực tế máy **đo xong
xuôi** — có Ct, có `increase`, có plateau, thường có cả hai chân peak — rồi **từ chối** con số của
chính nó. Một phép đo thành công bị vứt đi dưới nhãn thất bại.

---

## Hai phát hiện quyết định thiết kế

### 1. Lý do được viện dẫn cho ngưỡng 4.0 KHÔNG có hiệu lực

`define.h:241-245` buộc bất biến `baseline_start + baseline_range == detection_margin_time`
(2+2 == 4.0) với lập luận: *"cửa sổ baseline phải KẾT THÚC ở chỗ Ct hợp lệ sớm nhất bắt đầu"*, vì
giếng lên sớm sẽ *"đo số 0 của nó trên chính đường đang lên"*.

**Lập luận này không đứng được với code hiện tại.** Hàm `baseline()` (`Alg/Algo.cpp`) trừ **một hằng
số duy nhất** khỏi mọi điểm:

```cpp
double baselineValue = mean(raw_data, baselineStart, baselineStop);
for (...) baselinedData[i] = raw_data[i] - baselineValue;
```

Hệ quả — hằng số đó **triệt tiêu khỏi mọi đại lượng quyết định kết quả**:

| Đại lượng | Vì sao không đổi theo baseline |
| --- | --- |
| `increase = plateau.y − transition.y` | là một **hiệu** → hằng số tự khử |
| `differential_data` | đạo hàm của hằng số = 0 |
| `main_peak.y`, `left_arm`, `right_arm` | đều đọc từ `differential_data` |
| `transition_time.i`, `plateau_point.i` | đều đọc từ `differential_data` |

Cửa sổ baseline **chỉ dịch đường cong trên chart**, không chạm vào một phép so nào trong
`predict_outcome_core()`. Nghĩa là ngưỡng 4.0 **không bị ràng buộc** vào cửa sổ baseline như tài liệu
tuyên bố — nó tự do di chuyển, và câu hỏi còn lại thuần tuý là *"Ct sớm nhất còn gọi được là bao
nhiêu"*, một quyết định **lâm sàng**, không phải xử lý tín hiệu.

> Đây cũng là lý do phương án "baseline lại rồi chấm lại" bị loại: nó sẽ không đổi một bit nào.

### 2. Nhánh 1 chặn TRƯỚC bằng chứng hình dạng, nên nó phủ quyết cả sigmoid đẹp

`find_sigmoidal_feature()` (`Algo.cpp:749-763`) đã **ép** peak nằm sau mốc margin
(`argmax(differential_data, discard_index)`) và chặn chân trái ở `discard_index - 1`. Nhưng
`transition_time` tìm bằng lời gọi **không có chặn dưới**:

```cpp
transition_time.i = find_crossing_lower_than_reversed(differential_data, crossing, main_peak.i);
```

Nên nhánh 1 chỉ bắt được đúng một dạng: **peak sau 4.0 phút, nhưng đi ngược xuống mốc 40% đỉnh thì
vượt qua 4.0 phút** — tức một đường lên **rộng và sớm**. Với dạng đó, giếng **vẫn có thể có
`left_arm` hợp lệ** (chân trái bị chặn ở `discard_index - 1`, còn Ct thì không), tức
`detected_shape()` là **true** — nhưng nhánh 1 nằm **trên** nó trong chuỗi if/else nên bắn trước.

**Một giếng có pha lag nhận dạng được, đủ lớn, đủ dốc, hai chân đầy đủ, vẫn bị gọi `!`** chỉ vì mốc
40% rơi ở phút 3.9. Bằng chứng hình dạng mạnh hơn mốc cắt 40% — thứ tự hiện tại đang đảo ngược.

---

## Thiết kế đề xuất

### Đổi 1 — Tách hằng số (KHÔNG đổi kết quả nào)

`detection_margin_time` đang gánh **ba** việc khác nhau:

| Nơi dùng | Nghĩa thật |
| --- | --- |
| `find_sigmoidal_feature` → `discard_index` | sàn tìm peak (xử lý tín hiệu) |
| `predict_outcome_core:868` | **sàn Ct gọi được (lâm sàng)** |
| `sensor6035.cpp:322` / `:496` → `check_risingData` | chỉ số bắt đầu quét rising |

Tách việc thứ hai ra hằng số riêng **`MIN_CALLABLE_CT`** trong `Alg/Algo.h`, gieo **4.0** = giá trị
hôm nay.

Theo đúng tiền lệ `BREAK_JUMP_THRESHOLD` (`Algo.h:48-65`) — cùng bệnh "một số hai việc", cùng cách
chữa, và **cùng lý do để là hằng số biên dịch chứ không phải field EEPROM**: `parastructure` đang ở
trần 400/402 byte, và một cổng lâm sàng không nên sửa được từ web.

Đổi này **phải cho 0 delta** khi phát lại bộ nhãn — đó chính là bằng chứng phép tách sạch.

### Đổi 2 — Đưa phép thử xuống SAU bằng chứng hình dạng

Trong khối `min_sharpness`, bỏ nhánh Error ở đầu, giữ nguyên nhánh 2 (`detected_ea`, ngoài phạm vi):

```cpp
if (!detect_shape)          -> Positive
else if (detected_shape())  -> Positive     // có pha lag => tin hình dạng
else if (detected_ea())     -> Error        // nhánh 2, GIỮ NGUYÊN
```

rồi áp chính sách Ct sớm lên kết quả Positive vừa gán:

```cpp
if (Positive && transition_time.x <  MIN_CALLABLE_CT)          -> Flagged (lý do = early rise)
if (Positive && transition_time.x >= min_slight_positive_time) -> Slight Positive
```

Hai mốc loại trừ nhau (4.0 < 22.0) nên thứ tự giữa chúng không đổi kết quả — nhưng **phải đặt cả hai
trong khối `min_sharpness`**: phép hạ Slight Positive hiện nằm **ngoài** khối đó (`Algo.cpp:888`),
vô hại hôm nay nhưng là bẫy ngay khi có nhánh gán Positive thứ hai.

### Đổi 3 — Giếng bị chặn thành `F`, kèm MÃ LÝ DO

`F` đã được định nghĩa đúng nghĩa cần dùng — *"một trạng thái THỨ BA, máy không đứng ra bảo đảm cho
giếng này, hãy lặp lại mẫu"* (`Alg/AlgoData.h:44-47`). Giếng lên sớm là ca sách giáo khoa của nghĩa
đó. `E` được trả lại cho đúng việc: *"phân tích không chạy được"*.

Nhưng `F` hôm nay mang **một** nguyên nhân (rơi vào dải ngưỡng cũ/mới, `removed_by_new_gate()`).
Thêm nguyên nhân thứ hai mà không phân biệt được là làm `F` thành nhãn mù. Vì vậy mở rộng
`shape_flag` (`uint8_t`, `AlgoData.h` — **nằm trong `DiagnosticOutcome` chứ không phải
`parastructure`** nên không đụng trần 402 byte) từ cờ 0/1 thành enum nhỏ:

| `shape_flag` | Nghĩa |
| --- | --- |
| `0` | không gắn cờ |
| `1` | dải ngưỡng cũ/mới (`removed_by_new_gate()` — giữ nguyên, không sửa) |
| `2` | **tăng sớm: Ct < `MIN_CALLABLE_CT`** |

Đường dẫn cờ đã có sẵn: `shapeFlag[i]` → `dashboardSetShape()` (`sensor6035.cpp:409-421`) →
`shapeNote()` trong `data/script.js`. Chỉ cần `shapeNote()` gọi tên nhánh nào đã bắn.

### Đổi 4 — Bỏ comment nhánh `F` và fallback `| ?? |` trên TFT (BẮT BUỘC, làm TRƯỚC)

`displayLCD.cpp:1477-1494` đang bị comment (commit `cf6e66e` *"disable display class F"*) — **cả
nhánh `F` lẫn nhánh `else` in `| ?? |`**. Trong khi đó env mặc định `esp32dev` **là** bản sinh ra `F`
(`-DSHAPE_RULE_NEGATIVE` chỉ có ở `esp32dev_shape_neg`, `platformio.ini`).

Hệ quả hôm nay: một giếng `F` **vẽ ô TRỐNG trên màn máy** trong khi `/slots` và payload upload đều
báo `F`. Đúng thứ `AlgoData.h:49-52` cảnh báo: *"chuỗi if của displayLCD không có nhánh else, nên một
chữ lạ in ra ô trống chứ không phải thứ gì nhìn thấy được."*

Đẩy thêm giếng vào `F` trước khi mở lại nhánh này = ship một thay đổi **vô hình trên máy**. Đây là
điều kiện tiên quyết, không phải việc kèm theo.

### Đổi 5 — Đóng chênh lệch giữa HAI bản sao `sensor6035.cpp`

`analyseSlotCurve()` mà CLAUDE.md mô tả **không tồn tại**; khối 55 dòng vẫn nằm nguyên **hai bản**
(`sensor6035.cpp:288-411` và `:482-593`), và chúng **đang bất đồng về chính
`detection_margin_time`**:

```cpp
:322  check_risingData(raw, parameters.detection_margin_time, RISING_WINDOW);                  // = mẫu 4  ≈ 1.3 min
:496  check_risingData(raw, parameters.detection_margin_time * (60000 / OPTO_INTERVAL), ...);  // = mẫu 12 = 4.0 min
```

`check_risingData` nhận **chỉ số mẫu**. Bản TFT truyền **phút**. Hai đường bắt đầu quét ở hai chỗ
khác nhau → **màn hình và bản upload có thể bất đồng về cùng một giếng**. Comment ở `:491-495` thừa
nhận và để lại *"chờ đo"*.

Không thể tách `detection_margin_time` (Đổi 1) trong khi hai consumer của nó đang hiểu nó theo hai
đơn vị — phép tách sẽ chỉ đóng băng lại sự bất đồng. Chốt về **một đơn vị** (chỉ số mẫu, qua một hàm
quy đổi dùng chung có chặn chia-0 vì `timePerLoop` đặt được từ web/Serial), áp cho **cả hai** bản sao.

---

## Cái gì KHÔNG đụng

- **Nhánh 2 (`detected_ea()` → Error)** — ngoài phạm vi đã chốt.
- **`removed_by_new_gate()`** (`Algo.cpp:1065-1092`) — nó trả lời câu hỏi khác ("trước đây có
  Positive không"). Cờ tăng-sớm gắn ở chỗ mới, không nhét vào hàm này.
- **Cửa sổ baseline `2+2`** — đã chứng minh không ảnh hưởng kết quả; đổi nó chỉ đổi chart.
- **`min_increase` 25 / `min_sharpness` 8**, `BREAK_JUMP_THRESHOLD` 20.0, hệ số `checkJump` 2.5 —
  hai cái sau là **tranh chấp mở** đã ghi trong
  `git show 82e6523:docs/history/2026-08-24-port-thuat-toan-v243at-len-v244.md`, chỉ đóng bằng số.
- **Đường override `/E` do lỗi cảm biến quang** (`displayLCD.cpp:1434-1448`,
  `Bluetooth.cpp:645-662`) — đó là sự cố quang học của khe, không phải phán quyết, và nó khác `!`.

---

## Ảnh hưởng downstream

| Nơi | Trạng thái với `F` |
| --- | --- |
| TFT | **ĐANG HỎNG** — ô trống. Đổi 4 phải sửa trước. |
| `GET /slots` | đã có `F` |
| `data/script.js` badge `res-F` + `shapeNote()` | đã có; cần thêm tên lý do |
| Payload upload | `F` đi cùng nhóm có CT (`Bluetooth.cpp:655`) |
| Sheet / ingest / ERP | **CHƯA được báo về chữ `F`** — doc `2026-08-24` liệt nó là việc tồn trước phát hành. Thay đổi này làm `F` phổ biến hơn nên đây thành **chặn cứng**. |

---

## Đo & nghiệm thu

CLAUDE.md: *"Đổi thuật toán chẩn đoán thì PHẢI có số, không được lập luận suông."* Kho log + bộ nhãn
kỹ sư đã có sẵn, nên từng bước dưới đây phát lại qua **chính `src/Alg/Algo.cpp`** (không chép công
thức sang Python — bản chép sẽ tự đồng ý với chính nó):

1. **Bảng phân bố quyết định — chạy TRƯỚC khi sửa dòng nào.** Với mọi kênh có
   `increase > 25 && main_peak.y > 8`, lập bảng `Ct` × `nhãn kỹ sư` trong dải `[0, 4.0)`. Bảng này
   **một mình quyết định** `MIN_CALLABLE_CT` nên giữ 4.0, dời, hay bỏ hẳn. Không chọn số trước khi
   nhìn bảng.
2. **Đổi 1 (tách hằng số): bắt buộc 0 delta.** Một verdict đổi = phép tách chưa sạch.
3. **Đổi 2 + 3**: báo cáo số kênh `E → P`, `E → F`, `P → F`, kèm ba chốt của bộ nhãn (tổng đúng,
   dương tính giả, bỏ sót dương tính). Nếu số liệu bác việc đảo thứ tự, phương án lùi là **giữ
   nguyên thứ tự, chỉ đổi chữ `E` → `F`** (Đổi 3 độc lập với Đổi 2).
4. **Đổi 5**: phát lại theo **cả hai** đường (`bResultGet` và `bResultPutToGoogleSheet`) và ghim rằng
   chúng cho **cùng một chữ** trên mọi kênh — hôm nay chúng không đảm bảo điều đó.
5. Trên máy thật: một run 30′, theo chữ `F` đi đủ **TFT → web → Sheet/ERP**.

**Guard phải viết kèm** (CLAUDE.md yêu cầu, và `check.py --list` đang tự báo `test_algo_accuracy.py`
là *"DOCUMENTED IN CLAUDE.md BUT ABSENT"*): tối thiểu một guard tĩnh ghim (a) `MIN_CALLABLE_CT` chỉ
có **một** nơi so sánh, (b) hai bản sao `sensor6035.cpp` truyền `check_risingData` **cùng đơn vị**,
(c) nhánh `F` và nhánh `else` trên TFT **không bị comment**.

---

## Rủi ro kề cận (phát hiện khi khảo sát, cùng đường code)

**Chữ kết luận có thể bị cắt mất khỏi payload upload.** `Bluetooth.cpp:640` khai
`char resultConfig[30]` và cả 4 format dùng `%s` trần cho tên bệnh. `/rename` cho phép **32 ký tự**,
nên `"<32 ký tự> | 04.7 | F"` = 44 ký tự → `snprintf` giữ 29 + NUL và **nuốt đúng chữ kết luận**,
im lặng.

CLAUDE.md mô tả lỗi này **đã được sửa** thành `[64]` + `%.32s`; trong cây v2.4.5 nó **chưa từng được
sửa**, và guard `tools/test_result_string_fits.py` **không tồn tại**. Việc này nằm ngoài phạm vi đã
chốt, nhưng nó vô hiệu hoá đúng thứ kế hoạch này đang làm — chọn cho đúng chữ trả về — nên cần một
quyết định riêng.

---

## Quan hệ với kế hoạch kia

[2026-09-10-auto-gain-auto-origin.md](2026-09-10-auto-gain-auto-origin.md) không đụng cùng dòng code
nào với kế hoạch này, nhưng nối vào ở hai chỗ, cả hai chảy theo chiều **auto-gain → kế hoạch này**:

1. **Ca lỗi chung.** Máy RPL02001 ở Bình Đại, run 15:18 ngày 12/02: **cả mười giếng trôi lên cùng
   nhau**, bốn giếng bị gọi `Error`
   ([2026-08-13](../history/2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md)). Đó chính là nhánh `!`
   mà tài liệu này thiết kế lại. Sửa ở đây sẽ **đổi nhãn** cho ca đó; auto-origin bên kia cho máy
   khả năng **nhận ra** rằng mười kênh cùng trôi là sự kiện của **máy**, không phải của mẫu.
2. **Số để chỉnh ngưỡng.** Phép đo sàn nhiễu bên kia là con số duy nhất có thể biện minh cho việc
   chạm vào `MIN_CALLABLE_CT` (hoặc `min_sharpness`).

→ Nếu làm cả hai: **chạy phép đo sàn nhiễu trước khi chốt bất kỳ ngưỡng nào ở đây.** Ngược lại, kế
hoạch này vẫn làm được độc lập — nó không phụ thuộc kế hoạch kia để đúng.

---

## Tài liệu — khi CODE thật sự thay đổi (không phải bước này)

File này là **kế hoạch**. Khi code landed thì viết thêm một ghi chú **lịch sử** riêng,
`docs/history/<ngày landed>-dieu-kien-tra-error-tang-som.md`: trước/sau, **bằng chứng baseline là
hằng số** (lập luận trung tâm), bảng phân bố Ct đo được ở mục Đo, phương án đã loại (baseline lại rồi
chấm lại — vô hiệu vì hằng số tự khử), rồi link nó vào CLAUDE.md.
