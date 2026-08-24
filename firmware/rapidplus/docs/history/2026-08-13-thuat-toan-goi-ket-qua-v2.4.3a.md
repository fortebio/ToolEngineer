# 2026-08-13 — Thuật toán gọi kết quả: cửa sổ đường nền, kiểm tra hình dạng, ngưỡng nhảy, dấu cảnh báo

## Bối cảnh

Khách hàng Uni-President (Bình Đại, Vĩnh Châu, Long An, Cà Mau) nhiều lần phản hồi máy báo
**Dương tính** ở những mẫu họ tin là âm. Câu hỏi đặt ra: chỗ nào trong thuật toán gọi kết quả
đang sai, và siết được gì mà **không** mất ca dương thật.

Cơ sở đo: **39.307 đường cong** ghép từ hai sheet `Copy of RAPIDPlus Data.xlsx` (ghép theo
device + timestamp, sai số ±10 s, khớp 96,5%). Phạm vi tính toán: **29.015 đường cong đã hiệu
chuẩn**, 01/01–12/08/2026, **loại máy nguyên mẫu**.

**Bẫy đầu tiên, và nó làm sai mọi con số biên độ**: sheet lưu **giá trị ADC thô**, còn firmware
chia cho hệ số quang của từng giếng **trước khi** phân tích (`sensor6035.cpp`, GOTCHA 9). Mô hình
đối chiếu ban đầu bỏ qua bước này nên mọi biên độ bị thổi lên ~1,6 lần. Sau khi chia đúng
(`raw / FORTE_SLOPES[i]`), mô hình khớp log máy thật tới chữ số thập phân: `increase` **35,55** so
với **35,6** của firmware. Hệ số toàn đội máy trải **0,50–3,50**, trung vị **1,58**.

## Bốn chỗ đã sửa

### 1. Cửa sổ đường nền: 3–7 phút → 2–4 phút (`define.h`)

Cửa sổ cũ kéo dài **quá 3 phút** so với `detection_margin_time = 4.0` — tức thời điểm Ct sớm nhất
mà chính thuật toán chấp nhận. Mẫu khuếch đại ở phút 4–5 bị lấy điểm gốc **ngay trên đoạn đang
tăng của chính nó**.

Đo trên toàn đội máy: nhiễu đường nền **10,90** ở ca dương sớm so với **1,63** ở ca dương muộn —
chênh **6,7 lần** do cửa sổ gây ra, **không** phải do mẫu. Ở 2–4 phút hai giá trị bằng nhau.

Ví dụ một giếng của RPL02008 ngày 26/3: cùng đường cong, cửa sổ 3–7 cho nhiễu **85,9**; cửa sổ
2–4 cho **0,7**.

**Nguyên tắc chốt lại: cửa sổ phải ĐÓNG ở đúng chỗ Ct hợp lệ sớm nhất bắt đầu**, tức
`baseline_start + baseline_range == detection_margin_time` (2 + 2 == 4.0). Đây là ràng buộc, không
phải con số đẹp — đổi `detection_margin_time` thì phải đổi kèm.

Điểm **bắt đầu = 2** trùng với chart web, nơi thời gian ổn định quang học đã được đo độc lập và
chốt ở ~2 phút — xem [2026-08-02-chart-baseline-start-window.md](2026-08-02-chart-baseline-start-window.md).
**Nhưng độ dài thì khác**: chart dùng `range = 4` (cửa sổ [2, 6)), mà chính bảng đo trong tài liệu
đó có kênh lift-off ở **phút 4,3** — nằm trong cửa sổ. Với chart việc đó chỉ làm đường vẽ hơi lệch;
với thuật toán nó là điểm gốc lấy trên tín hiệu. **Hai nơi cố ý khác nhau, đừng "đồng bộ" lại.**

### 2. `arm_percentile`: 0,9 → 0,5 (`define.h`)

Đây là phép kiểm tra đường cong có đúng hình dạng khuếch đại thật hay không. Ở **0,9** nó đo một
**lát mỏng ngay dưới đỉnh**: trên 4.000 đường cong, bề rộng đo được chỉ nhận **15 giá trị khác
nhau** — tức phép kiểm gần như không phân biệt được gì.

Ở **0,5** nó đo đúng pha tăng theo cấp số nhân, và bề rộng trở thành đại lượng chẩn đoán được:
**dưới 1 phút** là nhiễu cảm biến chứ không phải phản ứng; **trên 8 phút** là đường tăng chậm
không đặc hiệu.

Ví dụ RPL03017 giếng 7 ngày 10/6 (ca dương rõ): bề rộng **1,67 phút** ở 0,9 so với **7,00 phút**
ở 0,5 trên cùng một đường cong.

### 3. Ngưỡng phát hiện nhảy tín hiệu không còn bị chia hai lần (`sensor6035.cpp`)

`raw_data` đã được chia cho `FORTE_SLOPES[i]` ngay phía trên, nhưng `check_breakData` lại nhận
`min_increase / FORTE_SLOPES[i]` — **chia lần thứ hai**.

Hệ quả: ngưỡng thực tế trải **40 xuống 5,7** giữa các giếng, và chênh **gấp đôi ngay trong cùng
một máy** — RPL02001 giếng 1 (hệ số 1,29) loại các cú nhảy trên **15,5**, giếng 6 (hệ số 2,62)
chỉ loại từ **7,6**. Cùng một xung nhiễu điện bị loại ở giếng này, được chấp nhận ở giếng kế bên.

Sửa ở **cả hai** call site (`bResultGet`, `bResultPutToGoogleSheet`). Không kết quả lịch sử nào
thay đổi — nó sửa tính **nhất quán giữa các giếng**, không sửa mức.

### 4. `total_rise` khai báo `int` (`Alg/Algo.cpp`, `checkJump`)

Hai toán hạng là `double`, so sánh cũng với `double`, nhưng biến trung gian là `int` → cắt về 0.
Mức tăng 20,9 được đem so như 20 và **lọt** qua ngưỡng 20. Guard này sinh ra để bắt break thật,
và nó đang bỏ sót đúng những ca sát ngưỡng.

## Thêm mới: dấu cảnh báo — KHÔNG đổi kết quả

`DiagnosticOutcome` có thêm **`suspect_score` (0–6)** và **`arm_width`** (phút, -1 nếu không tìm
được tay). Sáu phép kiểm: tăng nông · khởi phát muộn · biên độ nhỏ · pha trễ kéo dài · lâu đạt ổn
định · không rời khỏi đỉnh. Ngưỡng ở `Alg/Algo.h` (`NSA_*`).

**Không có gì trong `predict_outcome()` đọc hai trường này**, nên chúng **không thể** đổi một kết
quả. Điểm ≥ 4 đánh dấu ca Dương tính cần xem lại: trên 4.354 ca dương trong phạm vi, nó đánh dấu
**413 ca (9,5%)** và báo nhầm **0,24%** số ca mạnh nhất.

**Vì sao chỉ đánh dấu mà không đổi kết quả**: chưa có kết quả đối chứng (trại hoặc PCR) để hiệu
chỉnh. Đánh dấu trước, rồi so các dấu này với kết quả thực tế — đó là cách xây bằng chứng.

**Cấu trúc `predict_outcome` bị tách làm đôi có lý do**: `predict_outcome_core()` giữ nguyên phần
phân loại, `predict_outcome()` gọi nó rồi gắn hai trường. Hàm phân loại có **ba đường thoát** (không
có đỉnh · biên độ dưới ngưỡng · nhánh chính) — luồn lời gọi qua từng `return` là kiểu bỏ sót một
đường mà không ai nhận ra.

## Vì sao GIỮ NGUYÊN `min_increase` (20) và `min_sharpness` (5)

Mọi phương án siết chặt đã thử đều mất **6 đến 9 ca Dương tính thật** cho mỗi ca nghi ngờ loại
được. Trên mọi mức thử, chỉ **2 trên 274** ca bị ngưỡng RFU loại là thực sự khớp đặc điểm khuếch
đại không đặc hiệu.

Hai ngưỡng này là **sàn tối thiểu, không phải thước đo chất lượng** — việc phân biệt phải dựa vào
**hình dạng** đường cong, và đó chính là chỗ ba thay đổi trên tác động vào.

## Ảnh hưởng đo được

Chấm lại **cả 29.015 đường cong** theo v2.4.0 và theo v2.4.3a:

| | |
|---|---|
| Kết quả thay đổi | **8** trên 29.015 (**0,028%**) |
| Dương tính → Lỗi | 7 |
| Dương tính → Âm tính | 1 |
| Ca Dương tính bị đánh dấu xem lại | 413 / 4.354 (**9,5%**) |
| Độ nhạy mất đi | không đo được mức nào |

**Năm trong bảy** ca Dương tính → Lỗi thuộc máy **RPL02001 tại Bình Đại ngày 12/02** — bốn trong
số đó nằm trong **cùng một lần chạy lúc 15:18**, khi cả mười giếng cùng trôi lên như nhau. Ở lần
chạy đó, chỉ giếng 7 có tốc độ tăng hạ xuống dưới 0,5 lần đỉnh trong khoảng [4 phút, đỉnh] — và
giếng 7 là giếng **duy nhất** giữ kết quả Dương tính. Bốn giếng kia đã tăng **từ trước khi máy bắt
đầu đo**, nên nay báo Lỗi thay vì Dương tính.

## Hai lỗi PHÁT HIỆN THÊM nhưng CHƯA sửa

### `check_risingData` — hai call site lệch đơn vị

`check_risingData(_array, int start_index, int window)` nhận **chỉ số mẫu**. Nhưng:

- `bResultGet` (đường **màn hình**) truyền `detection_margin_time` = 4.0 → **mẫu 4** ≈ 1,3 phút
- `bResultPutToGoogleSheet` (đường **upload**) truyền `detection_margin_time * (60000 / OPTO_INTERVAL)`
  = 12 → **mẫu 12** = 4,0 phút

Cùng một đường cong, hai điểm bắt đầu quét khác nhau tuỳ theo đang hiện lên màn hay đang đẩy lên
sheet. Bản upload đúng, bản màn hình sai.

**Cố ý chưa sửa**: sửa nó **đổi kết quả trên đường màn hình**, mà chưa đo được đổi bao nhiêu. Đã
để `NOTE` tại chỗ trong `sensor6035.cpp`. Đây cùng họ với chuyện màn hình / email / graph là ba
đường mã khác nhau — kiểm trên máy thật trước khi động vào.

### `DiagnosticOutcome::fromJSON` nạp nhầm mảng

```cpp
JsonArray _json_differential = _doc["differential_data"].as<JsonArray>();
loadVectorFromJSON(differential_data, _json_processed);   // <-- sai mảng
```

`differential_data` được nạp từ `_json_processed`. Lỗi có sẵn từ v2.4.0, ảnh hưởng mọi đường nạp
lại record từ JSON. **Chưa sửa** vì nằm ngoài phạm vi lần này và cần rà xem đường nào đang thực sự
gọi `fromJSON`.

*(Đã sửa kèm một lỗi cùng loại nhưng chắc chắn: `DiagnosticOutcome::clear()` không reset
`plateau_point` trong khi reset mọi trường khác → outcome tái sử dụng mang plateau của giếng trước
sang giếng sau.)*

## Kiểm chứng

```
pio run -e esp32dev          SUCCESS   RAM 23.6%   Flash 72.2%   0 cảnh báo từ src/
tools/test_phase0_guards.py  PASS      (-Wformat còn bật — GOTCHA 0)
tools/test_no_method_branch.py PASS    tools/test_web_assets.py     PASS
tools/test_no_runtime_wifi_begin.py PASS  tools/test_ota_guards.py  PASS
tools/test_status_coverage.py PASS     tools/test_device_id.py      PASS
tools/test_qr_payload.py     PASS
```

**CHƯA chạy trên máy thật.** Chưa có `pio test -e esp32dev_test` (cần phần cứng), chưa có run đối
chứng nào. `currentVersion` trong `updateOTA.cpp` **cố ý giữ nguyên 19** — bump lên là chào bản này
qua OTA cho cả đội máy khi nó chưa được kiểm chứng.

*(Ghi chú: `tools/test_upload_targets.py` được CLAUDE.md nhắc tới nhưng **không có trong cây mã
v2.4.3** — lệch tài liệu, không liên quan thay đổi này.)*

## Trước khi phát hành

1. Nạp lên một máy, chạy đối chứng với mẫu đã biết kết quả — **nhất là ca dương yếu**, vì đó là
   chỗ thay đổi cửa sổ đường nền tác động mạnh nhất.
2. Kiểm hai đường `check_risingData` trên máy thật rồi quyết định sửa hay không.
3. `suspect_score` / `arm_width` hiện chỉ có trong JSON. Muốn dùng được thì phải đưa lên bảng
   Result của web và/hoặc payload upload — chưa làm.
