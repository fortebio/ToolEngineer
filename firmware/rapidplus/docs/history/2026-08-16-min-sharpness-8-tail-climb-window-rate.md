# min_sharpness 11 → 8, vá điểm mù đuôi, thay `share` bằng `window_rate` (2026-08-16)

## 1. `min_sharpness` 11.0 → 8.0 (+ `CONFIG_REV_THRESHOLDS` 1 → 2)

### Vì sao 11 là quá cao

Ngày 15/8 chạy hai đĩa thí nghiệm **có bố cục đã biết** trên RPL03001 và RPL03002 (slot 1 chứng
dương, slot 2 TPD dương, slot 3/4/7/8/9 TPD âm, slot 5/10 blank, slot 6 chứng dương). Đây là **sự
thật phòng lab**, không phải nhãn nhìn bằng mắt.

**RPL03002 slot 2 — một TPD dương đã xác nhận — có steepness 8.6.** Ở ngưỡng 11.0 máy gọi
**Negative**. Đó là **bỏ sót một ca dương thật**. Giếng này có increase **73.0**, lớn hơn cả hai
chứng dương trên cùng đĩa, và có plateau sạch: tỷ lệ tốc-độ-cuối / tốc-độ-đỉnh = **0.03**, đúng
bằng mọi ca dương khác.

**Nó không nông — nó CHẬM.** Cùng mẫu TPD đó trên RPL03001 có steepness 17.7, bề rộng pha lên
4.3 phút; trên RPL03002 là 8.6 và **13.3 phút**. Cùng lượng sản phẩm, trải ra gấp ba.

Đây là vấn đề **cơ chế**, không phải chọn sai số: steepness đo **tốc độ** phản ứng, mà tốc độ chủ
yếu phụ thuộc **nồng độ khuôn**. Nâng ngưỡng này luôn cắt **đầu yếu của nhóm dương trước tiên** —
đúng nhóm mẫu mà xét nghiệm sinh ra để bắt. Giữ nó làm **sàn**, không phải làm luật chính.

### Quét ngưỡng trên toàn bộ bằng chứng

| ngưỡng | REAL (nhãn) giữ | NSA (nhãn) loại | 20 giếng xác nhận 15/8 | fleet |
| --- | --- | --- | --- | --- |
| 5.0 (bản v2.4.3) | 18/18 | **1/37** | không lỗi | — |
| 6.5 | 18/18 | 21/37 | không lỗi | −2.7% |
| 7.5 | 18/18 | 26/37 | không lỗi | −4.7% |
| **8.0** | **18/18** | **27/37** | **không lỗi** | **−5.7%** |
| 8.5 | 18/18 | 28/37 | không lỗi | −6.5% |
| 9.0 | 18/18 | 28/37 | **BỎ SÓT RPL03002 s2** | −7.8% |
| 11.0 | **16/18** | 31/37 | **BỎ SÓT RPL03002 s2** | −11.7% |

Khả năng loại NSA **bão hoà quanh 8.5**; vượt qua đó là trả giá bằng ca dương thật mà không được
gì. 8.0 nằm dưới ca dương yếu nhất 0.6 và trên ca âm mạnh nhất 4.0.

**Cảnh báo phải giữ:** con số 8.6 là **n=1**. Cần thêm ca dương yếu để chốt, không nên chốt bằng số
học trên một giếng. Bất cứ giếng nào rơi vào 8–12 chính là thứ trạng thái **F** của v2.4.3AT sinh
ra để xử lý — gắn cờ, chạy lại, đừng âm thầm bỏ.

### Đã thử và BÁC BỎ (đừng đưa lại nếu chưa có bằng chứng mới)

- **Settle ratio** (tốc độ cuối / tốc độ đỉnh): **174 / 666 lỗi xếp hạng**, trong khi steepness là
  23 và ngẫu nhiên là 333. Nó **không phân biệt được "phản ứng đã xong" với "tín hiệu đang tắt"**:
  12 trong 37 đường NSA lên đỉnh rồi photobleach, đọc ra y hệt một phản ứng kết thúc. Nó trông hoàn
  hảo trên 40 đường ngày 15/8 chỉ vì không đường âm nào hôm đó bị bleach.
- **steepness HOẶC settle** (đạt một trong hai là dương): **bị trội hoàn toàn**. Mọi biến thể loại
  được **ÍT** NSA hơn so với chỉ đơn giản hạ ngưỡng steepness, mà bản `>11 OR settle` vẫn mất một
  ca REAL.

### `CONFIG_REV_THRESHOLDS` phải lên 2 — bắt buộc

Rev 1 **đã nạp lên RPL03001 và RPL03002** ngày 15/8. Dấu stamp chính là thứ chặn migration chạy
lại, nên nếu không bump thì hai máy đó **giữ 11.0 vĩnh viễn**, và `configSelfCheck` sẽ báo **PASS**
trong khi chấm theo một thước không ai dùng.

Guard mới trong `test_config_migration.py`: `min_sharpness` và `min_increase` phải **giống nhau ở
cả ba chỗ** (define.h / migration / `kExpectMinSharpness`), và `CONFIG_REV_THRESHOLDS` phải khớp
số rev cao nhất ghi trong comment. Negative test 2/2 đỏ đúng chỗ.

## 2. Vá điểm mù ở đuôi của bộ dò climb

### Cơ chế thật không phải "bước nhảy nằm lại trong dữ liệu"

`find_first_climb()` cũ dừng ở `size-5`, để lại **4 transition cuối** không với tới. Ở run 40 phút
đó là thời gian chết; ở run 30 phút đó là **cuối xét nghiệm**.

Thiệt hại là **bước nhảy TRỞ THÀNH ĐỈNH**: `find_sigmoidal_feature()` quét đạo hàm tới hết đường
cong, cái spike thắng, chiều cao của nó đặt ra ngưỡng tìm transition, và transition trượt về cuối.

Đo trên 25.219 đường ở 30 phút: **641** đường có bước > `CLIMB_MIN_STEP` trong 5 điểm cuối, **9**
đường qua được cả cổng nhiễu cục bộ, **4** đường đổi kết quả — và **cả 4 đều đổi theo hướng đúng**:

| | trước | sau | |
| --- | --- | --- | --- |
| RPLMAU s3 | S | **P** | sigmoid sạch, Ct 28.0 → 18.3 |
| RPL01004 s3 | S | **P** | sigmoid sạch, Ct 28.0 → 18.3 |
| RPL02014 s4 | S | P | Ct 28.0 → 17.3 |
| RPL03016 s9 | S | **N** | đường phẳng — spike CHÍNH LÀ toàn bộ tín hiệu. Loại được một dương giả |

Không có artefact hệ thống ở cuối run: bước nhảy trung vị phẳng ~1.05 RFU suốt 10 điểm cuối.

### GIỮ MỨC, không cắt

`CLIMB_TAIL_MIN 6` — số điểm phải theo sau một bước để phân loại được. Ít hơn thì không phân biệt
được offset với spike và không xác nhận được đã ổn định, nên **giữ phẳng ở mức cuối cùng đã biết**.

Cắt bớt thì `raw_data` và `time_data` được resize cùng nhau trong `sensor6035.cpp` sẽ **lệch nhau**.
Giữ mức bảo toàn độ dài và cho **kết quả y hệt** phép cắt trên cả 4 đường. Không mất gì gọi được:
một pha lên bắt đầu muộn như vậy có Ct vượt `min_slight_positive_time` và không có plateau phía sau.

Kiểm chứng bằng cách port ngược C++ mới sang Python trước khi nạp: **11 lần giữ đuôi, 0 vi phạm bất
biến**, cả 4 đường đổi đúng như dự đoán.

## 3. `share` → `window_rate`

`share` (phần tăng được đếm / tổng tăng sau detection margin) **chưa từng ship** — nó chỉ tồn tại
trong working tree từ phiên làm việc này (`git log --all -S"amplification_share"` = 0 commit). Chấm
trên toàn bộ 666 phép so REAL-vs-NSA của bộ 68 nhãn thì nó **xếp hạng ngang ngẫu nhiên** → phát nó
ra là đang thu thập nhiễu.

Thay bằng **`window_rate`**: mức tăng trung bình lớn nhất trên **bất kỳ cửa sổ 4 phút nào** sau
detection margin, đơn vị RFU/phút. Nó xếp **14 / 666** lỗi so với **23** của đạo hàm đỉnh tức thời —
tức là **đo cùng thứ mà `min_sharpness` đo, nhưng theo cửa sổ, và xếp hạng tốt hơn**. Một phản ứng
chậm mà tổng lớn được ghi nhận ở đây, đúng nhóm mà đỉnh tức thời đọc hụt.

Vẫn là **ADVISORY** — không gì đọc nó. Nó là phép đo để chốt `min_sharpness` lần sau bằng số liệu
thay vì bằng một giếng.

Số điểm mỗi cửa sổ lấy từ **khoảng lấy mẫu của chính đường cong** (`time_data[1]-time_data[0]`),
không giả định 20 s: `timePerLoop` là tham số runtime.

Khoá JSON đổi `"share"` → `"window_rate"`; `/slots` đổi `share` → `rate`; `script.js` đổi chú thích
của badge F thành "fastest sustained climb N RFU/min".

## Kiểm chứng

- Build cả hai biến thể: Flash 72.6%, RAM 23.6%, **0 cảnh báo**.
- 9 guard Python xanh, kể cả `test_config_migration.py` với 2 check mới.
- Không còn `"share"` / `amplification_share` trong bin (2 chuỗi `share` còn lại là của mbedTLS,
  "pre-shared key").
- Đối chiếu fleet so với bin đang chạy trên RPL03001/03002 (rev 1: 25/11, chưa vá đuôi):
  **229 / 25.219 = 0.91%** đổi kết quả, chủ yếu **N→P (186)** — đúng hướng, đó là những giếng
  trước đây bị loại oan.
