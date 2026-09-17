# Kế hoạch phát triển Rapid Reader **5 slot** (từ nền Rapid4P)

Ngày lập: 2026-09-17 · Trạng thái: **P0 (mặc định) + P1 + P4 ĐÃ LÀM 2026-09-17** — P2/P3 chờ bo mạch, P5 chờ Q4, P6 chờ phát hành · Phạm vi: firmware `firmware/rapid4p`,
bo cảm biến, registry/server/app, tài liệu. Nền tảng hiện tại: Rapid4P (ESP32-P4C5 + LCD 4,3"),
đã nạp máy thật, LCD/touch/WiFi/dashboard chạy, **chưa có bo cảm biến** — đây là thời điểm rẻ nhất
để đổi số khe vì chưa có máy nào phát hành, chưa có dữ liệu calib thật, chưa có hợp đồng payload.

## 0. Nguyên tắc

1. **Số khe là hằng phần cứng, không phải hằng sản phẩm**: `BOARD_SENSOR_SLOTS` trong board header
   quyết định; `R4P_SLOTS` chỉ là bí danh. Mọi mảng, vòng lặp, bố cục, payload phải suy ra từ đó —
   mục tiêu: đổi 4 → 5 (hay 6) chỉ sửa **một chỗ** + bo mạch.
2. Không hard-code "4" trong chuỗi hiển thị ("RAPID READER 4 SLOT", "4 khe") — dùng macro/format.
3. Payload lên Engineer Server tự khai `slots: N` và mảng `slot_*` N phần tử (đã làm), server không
   được ghim độ dài (đang đúng: `ARRAY_FIELDS` chỉ áp cho Rapid+).
4. Thay đổi phải qua `python tools/registry_check.py` và ghi `docs/history/`.

## 1. Quyết định cần chốt trước khi code (chủ sản phẩm)

> Mặc định đã áp dụng 2026-09-17 (đổi được, chưa phát hành): Q1 **giữ khoá `rapid4p`**, tên hiển thị theo
> số khe; Q2 **GPIO47**, mux kênh 4; Q3 giữ 3 vòng; Q4 khe 5 = mẫu thường; Q5 giữ LCD.

| # | Câu hỏi | Đề xuất | Ảnh hưởng |
|---|---|---|---|
| Q1 | **Tên/khoá sản phẩm**: giữ `rapid4p` hay đổi `rapid5p`? | Khoá `rapid4p` **chưa phát hành** (status `dev`) nên đổi được. Nếu marketing đặt tên "Rapid5P"/"Rapid Reader 5 Slot" → đổi khoá thành `rapid5p`, tiền tố mã máy `R5P`, SSID `FBT-Rapid5P`, thư mục giữ `firmware/rapid4p` → đổi tên `firmware/rapid5p` (git mv). Nếu tên thương mại chưa chốt → giữ khoá, chỉ đổi số khe (rẻ nhất). | registry, server `valid_product`, OTA `product=`, thư mục, tag `fw/<key>/` |
| Q2 | **Phần cứng bo cảm biến 5 khe**: 5 TCS34725 qua TCA9548 kênh 0..4, LED enable riêng từng khe (cần **5 GPIO**: 28/29/30/45 + **47**), PWM chung GPIO46 | Dùng GPIO47 (JP1 còn 47/48/49/50). Nguồn LED: 5 LED × dòng → đo trên bo thật (VCC3V3 hay BOOST_5V) | schematic bo con, `board_esp32p4_43lcd.h` |
| Q3 | **Chu trình đo**: 3 vòng × 5 khe (≈ 42 s, hiện 4 khe ≈ 34 s) hay giảm settle để giữ ≈ 35 s? | Giữ 3 vòng, chấp nhận ≈ 42 s (thuật toán ReaderPlus); tối ưu sau khi có số đo thật | `measure.c`, tiêu chuẩn "xong" §4 CLAUDE.md |
| Q4 | Khe thứ 5 dùng làm gì? (mẫu thứ 5 hay **chứng âm/chuẩn nội** cố định?) | Nếu là chứng chuẩn: UI đánh dấu khe 5 khác màu, ngưỡng có thể tính theo khe chuẩn — thay đổi thuật toán, cần chủ thuật toán quyết | `measure.c`, `ui_reader.c`, payload thêm `control_slot` |
| Q5 | Kích thước LCD giữ 4,3" 800×480? | Giữ. 5 ô 136 px vẫn đủ chữ 48 px cho số 4 chữ số | `ui_reader.c` |

## 2. Kiểm kê chỗ phụ thuộc số khe (đã rà `R4P_SLOTS` 42 chỗ + chuỗi cứng)

| Lớp | File | Việc | Rủi ro |
|---|---|---|---|
| Board | `main/boards/board_esp32p4_43lcd.h` | `BOARD_SENSOR_SLOTS 5`, `BOARD_SENSOR_MUX_CHANNELS {0,1,2,3,4}`, `BOARD_SLOT_LED_GPIOS {28,29,30,45,47}`; ghi nguồn (schematic §) | GPIO47 chưa đo; phải xác nhận không trùng chức năng khác trên P4C5 |
| Kiểu chung | `main/rapid4p.h` | `#define R4P_SLOTS BOARD_SENSOR_SLOTS` (hiện `4` cứng); `R4P_PRODUCT_NAME "RAPID READER %d SLOT"`; thêm `R4P_SLOT_CONTROL` nếu Q4 | include thứ tự board.h trước |
| Cảm biến | `sensor/sensor_bus.c`, `slot_led.c`, `tcs34725.c` | đã theo `R4P_SLOTS`/mảng board — kiểm `slot_led.c` LEDC: 5 enable GPIO + 1 PWM (vẫn 1 kênh LEDC) | không |
| Đo | `app/measure.c` | vòng lặp theo `R4P_SLOTS` (đã); log "xong" in 4 giá trị cứng (`%lu%s ×4`) → in vòng | thời gian chu trình ↑ 25 % |
| NVS | `app/calib_store.c` | blob `cal_min/cal_max/led_pwm` **đổi kích thước** (8 → 10 B, 4 → 5 B): `nvs_store_get_blob` với size mới sẽ lỗi → code hiện reset về 0 (mất calib). Thêm **migration**: đọc blob cũ nếu size = 4 khe, mở rộng, ghi lại | máy dev mất calib — chấp nhận, nhưng viết migration để không lặp lại khi lên 6 |
| Hàng đợi gửi | `network/result_upload.c` | JSON dài thêm ~100 B → `RQ_MAX_JSON 900` → **1100**; NVS: 8 × 1,1 KB ≈ 9 KB (ngân sách 20 KB, còn WiFi/calib/token) — OK | vượt `RQ_MAX_JSON` là mất bản ghi |
| Payload | `network/result_upload.c` | `slots: R4P_SLOTS`, mảng `slot_*` (đã động). Chốt **hợp đồng** `system/contracts/ingest-rapid4p.schema.json` với `minItems = maxItems = slots` (P2 đang treo) | server hiện không kiểm — an toàn |
| LCD | `ui/ui_reader.c` | `build_slot_tiles`: 5 ô → rộng `(760 − 4×16)/5 = 136` (hiện 176), chữ giá trị 48 giữ; dòng phụ 18 → 14 khi 5 khe; màn cân chỉnh "Khe n/5"; màn chuẩn bị; `RAPID READER 4 SLOT` → macro | 5 ô × 136 sát mép — kiểm qua webcam |
| Chuỗi | `ui/ui_strings.c` | "Đặt ống vào 4 khe…", "Xoá … 4 khe?", "Cảm biến n/4" → format `%d` (3 ngôn ngữ) | đổi chữ ký `r4p_str` (thêm biến) hoặc snprintf tại chỗ |
| Logo/header | — | không đổi | |
| Dashboard | `web/dashboard.html`, `network/dashboard.c` | `SLOTS=4` → đọc từ `sensors.total`; CSS `grid-template-columns: repeat(4,1fr)` → `repeat(auto-fit)`; bảng kết quả theo dữ liệu; `dash_mock.py` 5 khe | 5 ô ở 375 px: 62 px/ô → hạ chữ 22 px |
| Dev console | `core/dev_console.c` | không đổi | |
| Registry | `system/products.yaml` | `channels.optical_slots: 5`, tên, (Q1) khoá/tiền tố; `array_fields` giữ | `registry_check` hiện chỉ kiểm `optical_slots == 10` cho LEGACY (Rapid+) — thêm kiểm `rapid4p`: firmware `BOARD_SENSOR_SLOTS` == registry |
| Server | `server/app/logic.py` | không ghim độ dài cho `rapid4p` (đã). Khi có hợp đồng: validate theo `slots` trong payload | |
| App Flutter | `apps/fbt_rapid` | **chưa tham chiếu rapid4p** → không việc ở giai đoạn này; khi hiển thị kết quả phải đọc `slots` động | |
| Tài liệu | `firmware/rapid4p/CLAUDE.md`, `README.md`, `docs/HARDWARE-PINOUT.md`, `MAPPING-Rapid4P.md` | đổi "4 slot" → N, pinout JP1 thêm GPIO47 | |

## 3. Lộ trình

| Giai đoạn | Việc | Đầu ra / tiêu chí | Ước lượng |
|---|---|---|---|
| **P0 Chốt** | Trả lời Q1–Q5; nếu Q1 = đổi tên → tạo nhánh `claude/rapid5p-…`, `git mv`, đổi khoá registry + server `valid_product` | Biên bản trong `docs/history/` | 0,5 ngày |
| **P1 Firmware "N khe"** | Gỡ mọi "4" cứng: `R4P_SLOTS = BOARD_SENSOR_SLOTS`; chuỗi format; log; migration NVS; `RQ_MAX_JSON`; dashboard đọc `total`; mock 5 khe. **Build 2 lần**: `SLOTS=4` (không đổi hành vi, chụp webcam so sánh) và `SLOTS=5` | `BUILD_EXIT=0` cả hai; 13 màn + dashboard chụp đạt ở N=5 (khe giả `0/5` khi chưa bo) | 1,5 ngày |
| **P2 Bo cảm biến 5 khe** | Schematic bo con (mux 0x70 kênh 0..4, 5 TCS 0x29, 5 LED enable + PWM chung, pull-up 4,7 K, nguồn LED đo thật); cập nhật `board_esp32p4_43lcd.h` kèm nguồn | Bo mẫu + pinout chốt trong `HARDWARE-PINOUT.md` | phụ thuộc phần cứng (ngoài phần mềm) |
| **P3 Bring-up** | Nạp, log `slot 1..5: TCS34725 OK`, `measure task san sang, 5/5`; chu trình đo đủ 3 vòng × 5 khe, chạm vẫn ăn; đo thời gian thật; calib 5 khe qua UI | Tiêu chuẩn §4 CLAUDE.md rapid4p, cập nhật số 4/4 → 5/5 | 1 ngày sau khi có bo |
| **P4 Hợp đồng dữ liệu** | `system/contracts/ingest-rapid4p.schema.json` (mảng N theo `slots`); server validate theo registry; `registry_check` kiểm N firmware == registry; test POST vào `server/scripts/localtest.ps1` | CI `registry` xanh; localtest nhận bản ghi 5 khe | 1 ngày |
| **P5 Thuật toán (nếu Q4 = khe chuẩn)** | Chuẩn nội, ngưỡng tương đối, đánh dấu UI/dashboard, payload `control_slot` | Quyết định của chủ thuật toán + số liệu thật | 1–2 ngày |
| **P6 Phát hành** | `R4P_FW_VERSION` bump, tag `fw/<key>/vX.Y.Z`, OTA check với `product`/`hw` mới, README/CLAUDE.md, history | máy thật nhận OTA | 0,5 ngày |

Tổng phần mềm ≈ **5 ngày** làm việc, chưa kể thời gian bo mạch.

## 4. Rủi ro & cách né

- **Mất calib khi đổi kích thước blob NVS** → migration đọc blob cũ theo kích thước 4 khe (P1), và
  từ nay blob có **header version** để lần sau (6 khe) không phải đoán.
- **Bố cục 5 ô trên 800 px** (136 px/ô) chật với chữ Trung/Đài → dòng phụ 14 px; nếu vẫn chật, kết
  luận chỉ icon ✓/⚠ + màu viền và chữ đầy đủ ở bảng dashboard. Xác nhận bằng webcam.
- **Chu trình đo ≈ 42 s**: người dùng thấy lâu hơn Rapid4P; hiện tiến độ % + vòng/khe đã có.
- **Ngân sách NVS** 24 KB: 8 bản ghi × 1,1 KB + calib + WiFi blob + token ≈ 15 KB — vẫn đủ nhưng
  không tăng `RQ_CAP`; muốn hơn → chuyển hàng đợi sang partition `results` (SPIFFS) như đã ghi.
- **Đổi khoá sản phẩm sau khi đã có máy ngoài đồng** là cấm (quy tắc 2 CLAUDE.md gốc) → Q1 phải chốt
  **trước** máy đầu tiên xuất xưởng.
- **GPIO47** trên JP1 qua 100 Ω nối tiếp — đủ cho enable LED mức logic; nếu LED cần dòng lớn phải có
  transistor trên bo con (không kéo trực tiếp từ GPIO).

## 5. Tiêu chí nghiệm thu

1. `grep -rn "\b4\b" main/ui main/app main/network` không còn số khe cứng; đổi `BOARD_SENSOR_SLOTS`
   4↔5 chỉ sửa board header, build xanh cả hai.
2. Máy thật 5 khe: boot log đủ `5/5`, đo 3 vòng, kết quả 5 ô + dashboard 5 ô, upload bản ghi có
   `slots:5` và 5 mảng 5 phần tử, localtest server nhận.
3. Calib cũ (nếu có) không mất khi nâng firmware (migration).
4. `python tools/registry_check.py` xanh; tài liệu và history cập nhật cùng commit.

## 6. Việc đã có thể làm ngay (không chờ phần cứng)

P0 (chốt câu hỏi) → P1 toàn bộ → P4 hợp đồng. P2/P3/P5 chờ bo mạch và chủ thuật toán.
