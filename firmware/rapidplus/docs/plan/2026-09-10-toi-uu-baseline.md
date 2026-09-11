# 2026-09-10 — Tối ưu baseline: replay driver, vá cửa sổ suy biến, tham chiếu theo vòng

**Trạng thái: KẾ HOẠCH, chưa có dòng code nào** — cả ba phase. Mọi con số trong file này đo bằng
`tools/probe_sensor_noise.py` trên dữ liệu có sẵn trong repo, không phải bằng code mới.

Kế hoạch song song:
[2026-09-10-dieu-kien-tra-error-tang-som.md](2026-09-10-dieu-kien-tra-error-tang-som.md) ·
[2026-09-10-auto-gain-auto-origin.md](2026-09-10-auto-gain-auto-origin.md).

---

## Bối cảnh

Kế hoạch [điều kiện trả Error](2026-09-10-dieu-kien-tra-error-tang-som.md) chốt một câu: *"cửa sổ
baseline đã chứng minh không ảnh hưởng kết quả; đổi nó chỉ đổi chart"*, rồi xếp baseline vào mục
**KHÔNG đụng**. Câu đó **đúng** — đã kiểm lại từng phép so trong `predict_outcome_core()` và mọi hàm
nó gọi, bảng ở dưới. Nhưng nó đúng theo một cách đóng cửa sai vấn đề: nó chứng minh *chỉnh hai con
số 2/2 là vô ích*, rồi để lại ba việc chưa ai làm.

1. **Chứng minh đó chỉ tồn tại dưới dạng văn xuôi.** Branch này không có `tools/replay_algo.cpp`,
   `tools/audit_logs.py`, `tools/test_algo_accuracy.py`, `tools/algo_labels.tsv` — `git log --all`
   trả **0 hit** cho cả bốn, trên mọi ref. Không có cách nào phát lại `Alg/Algo.cpp` để kiểm bất kỳ
   phát biểu nào về baseline.
2. **Baseline CÓ đúng một đường đổi được kết quả, và đó là đường hỏng**: cửa sổ suy biến →
   `mean(raw_data, -1, 0)` đọc ngoài mảng.
3. **Thứ nên gọi là "tối ưu baseline" không phải cửa sổ**, mà là câu hỏi *số 0 nằm ở đâu theo thời
   gian*. Hôm nay câu trả lời là **một hằng số cho cả run**; đo trên máy thật thì nhiễu của máy
   không phải hằng số — 64% phương sai mỗi vòng là **chung cho cả mười giếng**.

Kết quả mong muốn: một pipeline mà (a) mọi phát biểu về baseline chạy lại được bằng một lệnh,
(b) không cấu hình nào làm nó đọc ngoài mảng, (c) tham chiếu theo vòng gỡ được nhiễu **của máy** mà
không chạm vào tín hiệu **của mẫu**.

**Ràng buộc cố ý: không thêm field EEPROM, không bump `CONFIG_REV_THRESHOLDS`.** `parastructure` ở
400/402 B, và bump rev sẽ ép lại **8 field** trên mọi máy chưa đạt rev mới — kể cả máy người vận
hành đã cố ý chỉnh `amplification_time` hay `min_sharpness` (`ForteSetting.cpp:882-932`, một khối
all-or-nothing). Toàn bộ kế hoạch này nằm trong RAM của một run.

---

## Bằng chứng đã có

Chạy lại được bằng hai lệnh, không cần máy:

```bash
python tools/probe_sensor_noise.py sheet/test.json
python tools/probe_sensor_noise.py tools/slots.txt --calibrated
```

| Đại lượng | RPL250701 (`sheet/test.json`) | máy thứ hai (`tools/slots.txt`) |
| --- | --- | --- |
| Thành phần chung 10 kênh | **64%** phương sai mỗi vòng | **9%** |
| Bậc thang đồng bộ | +109 count @ 1.7′, **mô hình cộng thắng** (residual 16.0 vs 23.4) | không có |
| Trung vị `sharpness` sau khi bỏ thành phần chung | 4.41 → **2.30** | 3.92 → **4.02** |
| Kênh dương tính mạnh nhất | (10 kênh đều âm tính) | 19.53 → **18.69** (−4.3%) |
| Kênh sát cổng `min_sharpness` 8.0 | — | 8.27 → **8.22** (vẫn qua) |

Đọc bảng này theo chiều an toàn: phép trừ thành phần chung **tự giới hạn**. Máy sạch → gần như
không làm gì (trung vị 3.92 → 4.02, một kênh còn *tăng*). Máy nhiễu → gỡ đúng phần nhiễu. Trên kênh
có tín hiệu thật, nó lấy đi **4.3%**.

⚠ Hai run là **hai tình trạng**, không phải hai mẫu của một hằng số. `slots.txt` còn cho luật nhiễu
**phụ thuộc mức** (r = −0.62) trong khi RPL250701 là cộng tính thuần (r = −0.03). Đây là lý do
nghiệm thu đòi ≥ 5 run từ ≥ 3 máy.

⚠ **`4.41 → 2.30` là CẬN TRÊN, không phải lợi ích sẽ nhận được.** Nó đo bằng cách bỏ thành phần
chung của **cả dải nhanh** (định nghĩa bởi trung bình trượt 21 điểm). Bộ lọc thật sẽ hẹp hơn để
không chạm vào đoạn dốc của sigmoid — xem [ba lớp chặn](#ba-lớp-chặn-và-lớp-thứ-ba-mới-là-lớp-chịu-lực).

### Bất biến của baseline — kiểm từng dòng, không phải suy luận

`baseline()` (`Alg/Algo.cpp:124-145`) trừ **một hằng số duy nhất** khỏi mọi điểm. Hằng số đó triệt
tiêu khỏi mọi đại lượng quyết định:

| Đại lượng quyết định | Nơi | Vì sao hằng số tự khử |
| --- | --- | --- |
| `increase` vs `min_increase` | `Algo.cpp:859,862` | `plateau.y − transition.y`, một **HIỆU** |
| `main_peak.y` vs `min_sharpness` | `Algo.cpp:865` | đọc `differential_data` |
| `transition_time.x` vs margin / `min_slight_positive_time` | `Algo.cpp:868,888` | trục **THỜI GIAN** |
| `detected_shape()` / `detected_ea()` / hai arm | `Algo.cpp:760-773`, `AlgoData.h:83,97` | đạo hàm, và ngưỡng là **tỉ lệ của đỉnh** |
| `window_rate`, `rise_width`, `nsa_score`, `removed_by_new_gate` | `Algo.cpp:969-1092` | HIỆU / đạo hàm / thời gian |
| `checkJump`, `check_breakData`, `check_risingData`, `neutralise_climbs` | `Algo.cpp:344-723` | HIỆU — **và chạy TRƯỚC baseline**, trên `recordIn.raw_data` |

`sg_smooth` bảo toàn hằng số (`sgsmooth.cpp:553-604`) nên *trừ rồi mượt* == *mượt rồi trừ*;
`differentiate` toàn sai phân nên không thấy baseline. **Không tồn tại một phép so nào trên giá trị
tuyệt đối của tín hiệu đã baseline.**

> Hệ quả cho lập luận ở `define.h:241-245` (*"cửa sổ phải KẾT THÚC ở chỗ Ct hợp lệ sớm nhất bắt
> đầu"*): bất biến `baseline_start + baseline_range == detection_margin_time` **không có hiệu lực
> nào lên kết quả**. Nó vẫn được `/selfcheck` (`ForteSetting.cpp:1056`) và
> `tools/test_config_migration.py:121-135` gác. Kế hoạch này **không phá** nó — gỡ nó là việc của kế
> hoạch điều-kiện-Error.

---

## Phase 0 — Replay driver

Không có nó thì mọi số ở Phase 1 và Phase 2 đều là văn xuôi.

### 0a — Máy build

`g++` **không có trên máy này**. Chỉ có cross-compiler của PlatformIO
(`toolchain-xtensa-esp-elf`) — sinh mã ESP32, không chạy được trên PC.

Hậu quả đã tới rồi, không phải giả định: `tools/check.py:108-109` trả `None` khi thiếu `g++`, nên
**4 guard `.cpp` đang nằm sẵn trong repo đang bị bỏ qua âm thầm** — `test_wifi_store.cpp`,
`test_wifi_bars.cpp`, `test_readcmd_overflow.cpp`, `test_curve_length.cpp`. `check.py` báo `SKIP`,
exit 0, và không ai đọc dòng đó.

⇒ Cài MinGW-w64 là **điều kiện tiên quyết** của kế hoạch này, và đồng thời bật lại 4 guard đó.

```powershell
winget install -e --id MSYS2.MSYS2          # hoặc BrechtSanders.WinLibs.POSIX.UCRT
```

### 0b — `tools/replay_algo.cpp`

Chạy **CHÍNH `src/Alg/Algo.cpp`**, không chép công thức sang Python: bản chép sẽ tự đồng ý với chính
nó trong khi bản ship đã hỏng — đúng thứ tool này sinh ra để bắt.

Phụ thuộc đã kiểm, đều giải được:

- `Algo.cpp` include `"../ForteSetting.h"` → **stub** chỉ cần `_ForteSetting.parameter` với 11 field
  mà `DiagnosticParameters::fromEEPROM()` đọc (`Algo.cpp:156-169`).
- `AlgoData.h` include `<ArduinoJson.h>` → header-only, đã có sẵn trong
  `.pio/libdeps/esp32dev/ArduinoJson/src` (cùng khuôn `test_json_key_present.cpp` đang dùng).
- `Algo.h:176` khai `const String strJson` ở namespace scope → stub `using String = std::string;`.
- ⚠ `Algo.h:14` khai `float mean(..., uint8_t, uint8_t)` trong khi định nghĩa ở `Algo.cpp:80` là
  `(int, int)` — **hai overload, bản `uint8_t` không có thân hàm**. Ai gọi `mean()` với đối số
  `uint8_t` sẽ lỗi link. Sửa prototype ở Phase 1b.

Driver **mang bản sao khối tiền-xử-lý của `sensor6035.cpp`** (calibrate → `neutralise_climbs` →
`check_breakData` / `check_risingData` → cắt cửa sổ), vì `bResultGet()` cần cả firmware mới chạy
được. Đặt **một chỗ duy nhất, có đánh dấu `MIRROR`**.

⚠ **Và nó phải chạy được CẢ HAI bản sao.** `sensor6035.cpp:322` truyền `detection_margin_time`
(**PHÚT**) còn `:496` truyền `detection_margin_time * (60000 / OPTO_INTERVAL)` (**MẪU**) vào cùng một
hàm `check_risingData` — mẫu 4 so với mẫu 12. Nghĩa là **màn TFT và bản upload có thể bất đồng về
cùng một giếng**, và hôm nay không ai đo được điều đó. Driver in cả hai và báo chỗ lệch.

Output: TSV mỗi kênh — `slot, verdict, ct, increase, sharpness, window_rate, rise_width, shape_flag`.

### 0c — `tools/test_algo_replay.py`

Tên `test_*` để `check.py` tự tìm thấy (`check.py:82`). Ba việc:

1. **Quét cửa sổ baseline → 0 delta.** Chạy `(start, range)` qua ít nhất
   `{(0,2), (1,3), (2,2), (2,4), (3,4), (5,5)}` trên cả hai dataset và khẳng định **verdict, Ct,
   increase, sharpness không đổi một bit**. Đây là chứng minh bất biến ở dạng chạy được — và là thứ
   chặn người sau "tối ưu" lại hai con số đó. Negative test của chính nó: gieo một phép so tuyệt đối
   vào `predict_outcome_core` thì guard phải đỏ.
2. **Cấu hình bệnh lý → hữu hạn, không đọc ngoài mảng.** Build thêm một bản
   `-fsanitize=address,undefined`; chạy `baseline_start ∈ {29, 30, 40, 255}` trên run 30′, và chạy
   trên curve **đã bị cắt đầu** để ép cửa sổ co về 1 mẫu.
3. **Hai bản sao `sensor6035.cpp` cho cùng một chữ** trên mọi kênh.

---

## Phase 1 — Vá cửa sổ suy biến

Đây là **đường duy nhất** baseline đổi được một kết quả, và nó đổi theo hướng tệ nhất.

### 1a — `baseline()` (`Alg/Algo.cpp:124-145`)

`find_crossing_higher_than` khai trả `size_t` nhưng `return -1` khi không thấy; gán vào `int` → −1.

| Ca | Điều kiện | Hậu quả hiện tại |
| --- | --- | --- |
| **A** | `baseline_start` > độ dài run (run 30′ → t_max 29.67′) | `discardIndex = -1` → `mean(raw, -1, 0)`: vòng `for (int i = -1; i < 0; i++)` **chạy đúng một lần**, `sum += vec[-1]` → **đọc ngoài mảng** |
| **B** | chỉ `start + range` vượt run | `baselineStop = 0`, mẫu số `(0 − 6) = −6` → `baselineValue = 0`, im lặng không trừ gì |

Ca A: baseline thành rác. Rác **hữu hạn** vẫn triệt tiêu (mọi phép so là HIỆU) — nhưng rác
**NaN/inf** thì `differentiate` ra NaN, `argmax` khởi tạo `max_y = 0.0` nên không tìm thấy đỉnh nào,
`detected_peak()` false → **cả mười giếng thành Negative, im lặng**.

Sửa: kẹp `discardIndex < 0 → 0`; `stop < 0 → time_data.size()`; đảm bảo
`baselineStop > baselineStart`; kẹp `baselineStart` trong `[0, raw_data.size())`.

**Ngữ nghĩa fallback chốt là "mẫu ĐẦU của curve hiện có", không phải 0.** Ca curve bị cắt đầu
(`sensor6035.cpp:334-335`, `timeBegin = begin() + breakIndex`) hôm nay **đã** rơi vào đúng hành vi
đó — `discardIndex = 0`, `baselineStop = 1`, cửa sổ co về đúng một mẫu. Chọn cùng ngữ nghĩa là làm
hành vi sẵn có thành **phát biểu**, không phải đổi hành vi.

### 1b — `mean()` (`Alg/Algo.cpp:80-93`)

Kẹp `startIndex < 0 → 0`, `endIndex > size → size`, và **trả 0 khi `endIndex <= startIndex`** — dải
rỗng phải trả 0, không thì mẫu số bằng 0. Sửa prototype `Algo.h:14` về `(int, int)` cùng lúc.

⚠ `CLAUDE.md` (mục "Khối phân tích slot chỉ có MỘT bản") **đã tuyên bố** `mean()` và
`find_crossing_lower_than_reversed()` "nay chặn chỉ số âm". Trên branch này **không có guard nào**.
Doc đang mô tả một cây khác — sửa dòng đó cùng lần chạm.

### 1c — Chặn ở trust boundary, cả hai cửa

- `webDashboard.cpp:1190-1194` hiện chỉ kiểm `0..255`. Thêm chặn theo **độ dài run** máy đang cấu
  hình (`amplification_time * timePerLoop / 60000`): cửa sổ không được nằm **trọn** ngoài run. Đây
  là chặn **an toàn**, độc lập với tranh luận `start + range == detection_margin_time`.
- `ForteSetting.cpp:335-347` (`JsonDataConfig`) — Serial/BT vào **thẳng** hàm này, không qua validate
  nào (đúng khuôn Setting #4). Kẹp lại ở đây nữa.

### 1d — `/selfcheck` phát riêng `start` và `range`

`ForteSetting.cpp:1092-1093` hôm nay chỉ phát **tổng** (`baseline window closes`). Nghĩa là `1/3` và
`2/2` **không phân biệt được từ web** — không có cách nào xác nhận một đợt triển khai đã tới máy
nào. Thêm hai field. (`GET /config` qua `paraToJson` đã có, nhưng `/selfcheck` là chỗ người ta nhìn.)

---

## Phase 2 — Tham chiếu theo VÒNG (thành phần chung 10 kênh)

### Ý tưởng

Mười giếng độc lập về sinh học. Thứ **chung** cho cả mười là **của máy**. Nhưng lấy trung bình 10
kênh rồi trừ thẳng là sai: run mà cả mười giếng cùng khuếch đại (một mẫu chia mười khe, hay mười
chứng dương) sẽ bị **xoá sạch tín hiệu**.

### ⚠ Lập luận an toàn bằng TẦN SỐ không đủ — hai dải chồng nhau

Bản đầu của mục này viết: *"sigmoid không có thành phần ở thang vòng-tới-vòng, nên phép trừ về mặt
cấu trúc không chạm được vào nó"*. **Sai, và phải ghi lại vì nó nghe rất thuyết phục:**

- `sharpness` là đạo hàm của **SG 9 điểm** (`sg_window = 4`, `define.h:238-239`) → nó nhạy ở dải
  đặc trưng rộng **~3–9 vòng**. Đó chính là dải mà nhiễu phải bị gỡ khỏi thì ngưỡng mới hạ được.
- Đường lên **thật** rộng bao nhiêu: `Algo.cpp:896-907` định nghĩa `arm_width` *"dưới 1 phút là
  transient của cảm biến, trên 8 phút là đường lên không đặc hiệu chậm"* → khuếch đại thật rộng
  **1–8 phút = 3–24 vòng** ở 20 s/vòng.

**Hai dải chồng nhau.** Bộ lọc nào gỡ được nhiễu đủ để `sharpness` giảm thì cũng chạm vào đoạn dốc
của một sigmoid hẹp. Tách theo tần số là **một** lớp, không phải lớp quyết định.

Kèm theo, một phép hiệu chỉnh kỳ vọng: con số **4.41 → 2.30** đo bằng cách bỏ thành phần chung của
tín hiệu đã khử trôi bằng trung bình trượt **21 điểm**, tức bỏ **cả dải nhanh**. Nó là **cận trên**
của lợi ích, không phải lợi ích sẽ nhận được ở bề rộng bộ lọc thực sự ship. Bề rộng đó là **tham số
phải quét bằng replay driver** (Phase 0), không phải hằng số chọn trước.

### Ba lớp chặn, và lớp thứ ba mới là lớp chịu lực

1. **Trung vị**, không phải trung bình → chịu được tới 5 giếng khuếch đại mà `m[j]` không nhúc nhích.
2. **Chỉ trừ phần nhanh** — `common[j] = m[j] − smooth(m)[j]`. Giới hạn thiệt hại ở ca ≥ 6 giếng
   cùng lên, **không xoá được nó**. Bề rộng `smooth` là thứ đánh đổi *lợi ích ÷ thiệt hại*, quét ở
   nghiệm thu #3b.
3. **Quyền phủ quyết bằng chính bộ chấm**: chạy `m[j]` (đường trung vị 10 kênh, coi như một giếng
   giả) qua **đúng `post_process_curve` → `find_sigmoidal_feature` → `predict_outcome`**. Nếu đường
   trung vị **tự nó** được chấm là `P`/`S`, thì "thành phần chung" đang chứa sinh học chứ không phải
   nhiễu → **tắt phép trừ cho run đó** và báo ra. Không có hằng số mới, không có bộ phát hiện thứ
   hai để lệch với bộ thứ nhất: dùng lại đúng thuật toán đang gọi kết quả.

Lớp 3 là thứ đóng được ca "mười giếng cùng khuếch đại" một cách kiểm chứng được; lớp 1 và 2 chỉ làm
ca đó hiếm và nhẹ đi.

Và toàn bộ vẫn **tự giới hạn** ở chiều còn lại: máy không có thành phần chung thì `common[j] ≈ 0` và
phép trừ là no-op — đo được đúng như vậy trên `slots.txt` (trung vị 3.92 → 4.02).

### Loại trừ với auto-gain #2

[auto-gain](2026-09-10-auto-gain-auto-origin.md) #2 (đo nền tối mỗi vòng, ghi `origins[i]` trong RAM)
và Phase 2 ở đây **nhắm cùng một đại lượng**. Land cả hai mà không nói chuyện với nhau là **trừ hai
lần** cùng một nhiễu — và lần thứ hai trừ vào thứ đã sạch, tức bơm nhiễu ngược vào. Chốt: **cái nào
land trước thì cái kia phải đo lại từ đầu**, không được cộng dồn kỳ vọng của hai bên.

### Đặt ở đâu

**KHÔNG ở `baseline()`** — hàm đó chỉ thấy một giếng. Phải là một pre-pass trên
`sensor67Value[10][loops]`, **sau** calibrate và **trước** `neutralise_climbs`:

- `bResultGet()` — quanh `sensor6035.cpp:285`
- `bResultPutToGoogleSheet()` — quanh `sensor6035.cpp:479`

**MỘT hàm, hai call site.** Hai bản sao 55 dòng ở đây đã bất đồng một lần về đơn vị (`:322` vs
`:496`); thêm một thuật toán nữa vào cả hai **bằng cách chép** là mời lại đúng lỗi đó.

Bộ nhớ: **đừng** materialise ma trận 10×130 double (10.4 KB). Tính `m[j]` tại chỗ từ `sensor67Value`
(10 lần đọc `uint16_t` + calibrate mỗi vòng, sort 10 phần tử), giữ hai `std::vector<double>` cỡ
`loops` (~1 KB mỗi cái).

Hằng số đặt ở `sensor6035.h` — **compile-time, không vào `parastructure`**:
`COMMON_MODE_SMOOTH_ROUNDS`. **Đừng gieo 21 rồi coi là xong**: 21 là bề rộng
`probe_sensor_noise.py` dùng để *mô tả* dữ liệu, không phải bề rộng đã được chọn để *sửa* dữ liệu.
Giá trị ship là kết quả của phép quét ở nghiệm thu #3b, và nếu phép quét cho thấy không bề rộng nào
vừa hạ được sàn nhiễu vừa giữ nguyên kênh dương tính thì **Phase 2 dừng ở đó** — đó là một câu trả
lời hợp lệ, không phải một thất bại cần đi vòng.

Lớp 3 (phủ quyết) không cần hằng số nào: nó gọi lại `predict_outcome` với **chính** `recordIn.parameters`.

### Chart phải đi theo, không được để lệch âm thầm

`/curve` (`webDashboard.cpp:1816-1845`) phục vụ calibrate thô — **không** climb-repair, **không**
trim. Nếu máy trừ thành phần chung mà chart không, người đứng ở máy và người cầm điện thoại nhìn hai
đường cong khác nhau về cùng một giếng.

Chốt: `/curve` phát thêm mảng `common:[...]` (cùng `count`), `drawSmoothed` (`script.js:728-744`)
trừ nó khi có mặt. **Thiết bị là nguồn sự thật**, client không tự tính lại. Vẫn phải đi qua
`CurveWriter` chunked (GOTCHA 10) — thêm một mảng `count` số, ~+10% payload.

⚠ Ghi nhận sẵn, **không** sửa trong lần này: web còn hai chỗ khác biệt với firmware — floor
`y < 0 → 0` (`script.js:738`, **phi tuyến**, nên web *thật sự* nhạy với cửa sổ baseline trong khi
firmware thì không) và SG **7 điểm** so với **9 điểm** của firmware.

### Payload phải tự mô tả

`Bluetooth.cpp:583-610` hôm nay upload `slopes` / `origins` / `LED_power` và **không** upload tham số
thuật toán nào. Hệ quả: `sheet/test.json` là `V2.2.9` và **không ai biết nó chạy cửa sổ baseline
nào** — 3/4 cũ hay 2/2. Mọi log thu từ hôm nay trở đi phải mang `baseline start`, `baseline range` và
cờ *"đã trừ thành phần chung"*, nếu không đợt kiểm định hồi cứu kế tiếp sẽ trộn hai pipeline mà
không biết. Ba dòng.

---

## Cái gì KHÔNG đụng

- **Giá trị `2 / 2`.** Bằng chứng nói 0 tác dụng lên verdict; chi phí là bump
  `CONFIG_REV_THRESHOLDS` → ép lại 8 field trên mọi máy. Chỉnh nó là trả giá thật để mua số không.
- **`min_sharpness` 8.0 / `min_increase` 25 / `detection_margin_time` 4.0.** Hạ ngưỡng là quyết định
  **lâm sàng** riêng, cần số riêng và người chịu trách nhiệm riêng. Phase 2 **hạ sàn nhiễu**; hạ
  ngưỡng sau đó là việc khác.
- **Bất biến `baseline_start + baseline_range == detection_margin_time`** — không phá.
- **`origins[]` / `slopes[]` trong EEPROM**, `led_power`, wizard calib.
- **Auto-origin bằng đo nền tối (LED off)** — [auto-gain](2026-09-10-auto-gain-auto-origin.md) mục
  #2. Nó cần phần cứng và một run bench để biết nguồn là cộng hay nhân. Phase 2 ở đây là **phiên bản
  phần mềm thuần** của cùng ý tưởng: tham chiếu **không gian** (9 giếng kia) thay cho tham chiếu
  **quang học** (đèn tắt) — 0 dwell, 0 thanh ghi, và dữ liệu để chấm đã có sẵn.

---

## Đo & nghiệm thu

Thứ tự bắt buộc — Phase 0 xong mới có số để nói về Phase 2.

1. **Phase 1 phải cho 0 delta** trên cả hai dataset ở cấu hình hiện tại. Một verdict đổi = phép vá đã
   đổi hành vi chứ không chỉ chặn biên.
2. **Quét cửa sổ baseline → 0 delta** (0c mục 1).
3. **Phase 2 trên ≥ 5 run từ ≥ 3 máy.** Con số 64% mới có **một** máy chống lưng; máy thứ hai cho
   9%. Không tái lập thì Phase 2 tụt hạng, chứ không phải cứ làm.
3b. **Quét bề rộng bộ lọc** `COMMON_MODE_SMOOTH_ROUNDS ∈ {3, 5, 9, 15, 21, ∞}` (∞ = trừ thẳng trung
   vị, không lọc). Với mỗi bề rộng, báo **hai** cột đặt cạnh nhau: sàn `sharpness` của kênh phẳng
   (lợi ích) và Δ`sharpness` của kênh dương tính đã biết (thiệt hại). Bề rộng ship là bề rộng có
   khoảng cách lớn nhất giữa hai cột đó — **và nếu không bề rộng nào cho khoảng cách dương thì
   Phase 2 dừng**. Đây là phép quét mà Phase 0 sinh ra để phục vụ; không có nó thì con số 21 chỉ là
   một con số ai đó gõ vào.
4. **Bảng Δsharpness từng kênh** + **danh sách đích danh mọi kênh đổi verdict**. Không có bộ nhãn thì
   "0 thay đổi" không phải mục tiêu — mục tiêu là **mọi thay đổi đều giải thích được** và đều theo
   chiều gỡ một hiện vật của máy.
5. **Điều tra dân số vùng sát cổng**: đếm kênh có `sharpness` trong ±10% quanh 8.0, trước và sau.
   Đo được hôm nay: `slots.txt` có một kênh ở **8.27 → 8.22**, biên 0.27. Nếu số kênh trong dải này
   tăng, phép trừ đang **đẩy giếng qua cổng** chứ không phải gỡ nhiễu.
6. **Ba phép thử tổng hợp** (không cần máy, không cần nhãn) — chúng kiểm **ba lớp chặn**, mỗi phép
   một lớp:
   - **10 sigmoid giống hệt nhau** → đường trung vị tự nó được chấm `P` → **lớp 3 phủ quyết**, phép
     trừ tắt, verdict không đổi. Đây là ca mà lớp 1 và lớp 2 **không** đóng được; nếu phép thử này
     đỏ thì thiết kế sai chứ không phải hằng số sai.
   - **5 sigmoid + 5 đường phẳng** → trung vị không nhúc nhích (**lớp 1**), 5 kênh dương tính giữ
     nguyên verdict và Δ`sharpness` phải nhỏ hơn ngưỡng chốt ở #3b.
   - **10 đường phẳng + một bậc thang đồng bộ** (tái lập +109 count @ 1.7′ đo được) → bậc thang bị
     gỡ, verdict không đổi. ⚠ Bậc thang là **thay đổi mức**, không phải nhấp nhô: một bộ lọc thông
     cao biến nó thành một cạnh chứ không xoá nó. Nếu phép thử này đỏ thì đó **không** phải lỗi —
     nó có nghĩa việc gỡ bậc thang thuộc về `neutralise_climbs` (đã có, `CLIMB_*`), và Phase 2 phải
     **thôi nhận công** cho ca đó trong tài liệu.
7. **Trên máy thật**: một run 30′, và **TFT so với bản upload phải ra cùng một chữ** trên cả 10
   giếng — hôm nay không có gì đảm bảo điều đó (xem 0b).

---

## Rủi ro

- **Không có bộ nhãn → không chứng minh được dương tính không bị hại.** Bằng chứng trực tiếp duy
  nhất đang có là **một kênh**: 19.53 → 18.69 (−4.3%) trên `slots.txt`. Mục 4/5/6 là thứ thay thế và
  chúng yếu hơn. Nếu muốn chặn cứng thì Phase 2 dừng **sau khi có số, trước khi ship**.
- **Dải nhiễu và dải tín hiệu chồng nhau** — rủi ro trung tâm của Phase 2, không gỡ được bằng thiết
  kế bộ lọc. `sharpness` nhạy ở ~3–9 vòng; khuếch đại thật rộng 3–24 vòng (`arm_width` 1–8 phút).
  Mọi bộ lọc hạ được sàn nhiễu đều **chạm** vào sigmoid hẹp nhất. Lớp 3 (phủ quyết bằng chính bộ
  chấm) và phép quét #3b là hai thứ duy nhất biến rủi ro này thành con số; nếu #3b không tìm được bề
  rộng nào có khoảng cách dương thì **kết luận đúng là không ship Phase 2**.
- **Trùng lặp với auto-gain #2** — hai kế hoạch nhắm cùng một đại lượng. Land cả hai mà không đo lại
  là trừ hai lần cùng một nhiễu.
- **Pipeline branch này KHÁC pipeline mà 99.09% được đo trên đó.** `sensor6035.cpp` ở đây chưa có ba
  bản vá B2 mà CLAUDE.md mô tả (`+ JUMP_SETTLE_SKIP`, `breakIndex > risingIndex + RISING_WINDOW`,
  lưới cứu bắt `'E'`), và `analyseSlotCurve()` / `marginSamples()` / `BREAK_MIN_INCREASE_RAW` **không
  tồn tại**. Mọi số đo ở đây phải ghi rõ là đo trên cây `v2.4.5`.
- **`sg_smooth` trả mảng TOÀN 0 khi `size < 2*sg_window + 2` (= 10)** và dòng báo lỗi bị comment
  (`sgsmooth.cpp:537-541`). Curve bị cắt ngắn → `processed_data ≡ 0` → `increase = 0` → Negative, im
  lặng, bất kể baseline. Phase 1 chạm đúng vùng này; ghi nhận, không sửa trừ khi phép thử bệnh lý ở
  0c chạm phải.
- **`differential_data` không được clear giữa hai lượt `post_process_curve`** (`sensor6035.cpp:359`
  và `:385`): `differentiate()` chỉ `push_back`, còn `post_process_curve` chỉ clear
  `processed_data`. Mảng báo cáo ra JSON **dài gấp đôi**. Quyết định đã chốt trước đó nên verdict
  không sai, nhưng **dữ liệu báo cáo thì sai**. Phát hiện khi khảo sát; cần một quyết định riêng.

---

## Tài liệu — khi CODE thật sự thay đổi

- **Mỗi phase một commit**, subject trả lời *tại sao*.
- Khi code landed: `docs/history/<ngày>-toi-uu-baseline.md` — trước/sau, bảng đo trên ≥ 5 run, danh
  sách kênh đổi verdict, **phương án đã loại** (trừ thẳng trung bình 10 kênh — ăn mất tín hiệu khi cả
  mười cùng khuếch đại; và chỉnh cửa sổ 2/2 — vô hiệu vì hằng số tự khử), rồi **link vào CLAUDE.md**.
- **Sửa drift của CLAUDE.md** phát hiện khi khảo sát, mỗi cái một dòng: chart là `2/2` chứ không
  `2/4`; `mean()` / `find_crossing_lower_than_reversed()` **chưa** chặn chỉ số âm;
  `analyseSlotCurve()` không tồn tại.
- Guard mới vào bảng Test của CLAUDE.md: `tools/test_algo_replay.py`.
