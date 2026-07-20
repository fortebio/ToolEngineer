# 2026-07-15 — Dashboard trực tiếp qua SSE (Home / Process / Setting)

## Mục tiêu

Thay trang chart kiểu polling bằng dashboard nhiều màn hình đẩy dữ liệu qua SSE
(Server-Sent Events), theo bố cục trong `docs/GUI_SSE/GUI.md`. Dựng trước bản chạy được
trên trình duyệt (server giả lập, không cần nạp firmware).

## Đã thay đổi

### Client (`data/`, phục vụ từ LittleFS của ESP32)

- `index.html` — viết lại thành trang đơn 3 màn hình:
  - **Header**: tên thiết bị + tên công ty (cỡ ~1/3) + badge kết nối.
  - **Home**: khung thông báo (tự ẩn khi không có), khung trạng thái hiện tại
    (icon nhiệt cho pha gia nhiệt, icon đồng hồ khi đang chạy quy trình), thẻ nhiệt
    độ (Lysis = 1 ô, Amplification = 4 ô: Amp Left/Right, Top Left/Right), trạng
    thái nút (đèn Green/Red/White).
  - **Process**: biểu đồ khuếch đại LAMP thời gian thực (Highcharts).
  - **Setting**: thông tin thiết bị/công ty + nút reload.
  - **Bottom nav**: Home / Process / Setting.
- `style.css` — ưu tiên mobile (rộng tối đa 480px), thẻ card, lưới nhiệt độ, đèn
  trạng thái, thanh nav cố định dưới.
- `script.js` — JS thuần: chuyển màn qua bottom-nav (kèm `chart.reflow()` khi mở tab
  Process), client SSE cho 2 event, badge kết nối, vẽ biểu đồ.

Lưu ý: chữ hiển thị trong `data/` giữ **tiếng Anh** (quy ước: code/UI tiếng Anh để
tránh lỗi font trên thiết bị; chỉ tài liệu mới dùng tiếng Việt).

### Hợp đồng dữ liệu SSE (2 event trên `/events`)

- `event: home` — trạng thái màn Home:

  ```json
  {
    "device": "RAPIDPlus", "company": "Fortebiotech",
    "temps": { "lysis": 65.0, "ampLeft": 62.9, "ampRight": 63.1, "topLeft": 104.8, "topRight": 105.0 },
    "status": { "phase": "heater|amplification|idle", "title": "...", "subtitle": "..." },
    "notify": { "show": true, "title": "...", "subtitle": "..." },
    "buttons": { "green": true, "red": false, "white": false }
  }
  ```

- `event: new_readings` — biểu đồ khuếch đại, **giá trị vô hướng theo từng kênh**:
  `{"#1": 120.4, ... "#10": 88.1}`.
  LƯU Ý: phải là số vô hướng, không phải dạng mảng như `/getdata` trả về, vì client
  gọi `Number(value)` cho mỗi series (mảng sẽ thành NaN).

### Công cụ test

- `tools/sse_test_server.py` — mock ESP32 bằng Python stdlib. Phục vụ chính các file
  `data/` thật và stream cả 2 event, chạy theo vòng đời giả
  (Lysis heating → Amplification → finished + thông báo), lặp lại để xem được mọi
  trạng thái giao diện.
  - Chạy: `python tools/sse_test_server.py` → mở <http://localhost:8000>
  - Tự kiểm tra: `python tools/sse_test_server.py selftest`

## Nguồn dữ liệu trong firmware (cho bước wiring sau này)

- Nhiệt độ: `_bottomThermometer` / `_topThermometer` → `getTemperature()`
  (`src/thermometer.h`).
- Trạng thái quy trình/thông báo: liên quan `_displayCLD.type_infor`
  (`src/Bluetooth.cpp`).
- Trạng thái nút/đèn LED: `src/LED.cpp`.

## Trạng thái

- Xong: dashboard test được trên trình duyệt + mock, đã verify end-to-end (trang,
  asset, cả 2 event SSE stream đúng). WiFiManager giữ nguyên (chỉ lo cấu hình).
- Chưa làm: wiring firmware — đổi `WebServer` sync sang `AsyncWebServer` +
  `AsyncEventSource` (thư viện đã có trong `platformio.ini`), `LittleFS` serveStatic
  `data/`, và gọi `events.send(json, "home"/"new_readings", ...)` tại chỗ cập nhật
  nhiệt độ/số đọc. Phải phát đúng dạng JSON ở trên.

## Rà soát tự động (workflow) + sửa lỗi

Chạy workflow review phản biện (9 agent) đối chiếu GUI.md + tính đúng đắn. Xác nhận
1 lỗi gốc nghiêm trọng:

- **Highcharts lỗi CDN làm sập cả màn Home**: `new Highcharts.Chart(...)` chạy ở
  top-level TRƯỚC phần SSE. Khi CDN không tải được (thường gặp khi điện thoại nối
  SoftAP của ESP32, không có internet) → `Highcharts` undefined → throw → phần còn
  lại của `script.js` (kể cả `new EventSource`) không chạy → Home đứng im, badge kẹt
  "Offline". Sửa: guard `var chartT = window.Highcharts ? new Highcharts.Chart(...)
  : null;`, null-guard trong `reflow()` và `plotResult()`. Home giờ chạy độc lập với
  chart.

## Design pass — Clinical Light

Áp design system vào `style.css` (CSS thuần, không CDN, giữ nguyên class nên không
đụng HTML/JS):

- Token màu (navy brand, xanh dương, trung tính sạch, màu trạng thái), thang chữ,
  bo góc, đổ bóng nhẹ, khoảng cách nhịp 4/8px.
- Header navy gradient sticky; badge kết nối dạng pill.
- Banner: icon trong ô bo góc có nền tint; viền trái đổi màu theo pha (amber khi
  gia nhiệt, xanh khi đang chạy).
- Thẻ nhiệt độ: số lớn dùng `tabular-nums`, nhãn uppercase muted.
- Đèn nút có vòng glow khi bật; bottom nav có thanh chỉ báo tab đang chọn.

## Sửa lỗi icon (font/encoding)

Icon ban đầu dùng emoji (chuông, nhiệt kế, đồng hồ, nav) → vỡ vì phụ thuộc
font/encoding, đúng thứ cần tránh cho UI thiết bị. Sửa tận gốc:

- Thay toàn bộ emoji bằng **inline SVG** (vector, dùng `currentColor`, không phụ
  thuộc font). Icon trạng thái đồng hồ/nhiệt kế do JS đổi qua `innerHTML`.
- Thêm `<meta charset="utf-8">` vào `index.html`.
- Dấu `°` dựng bằng `String.fromCharCode(176)`, chấm trạng thái bằng CSS
  (`.status-dot`), bỏ mọi ký tự non-ASCII (kể cả em-dash trong comment).
- Kết quả: 3 file phục vụ (`index.html`, `script.js`, `style.css`) **100% ASCII**
  → không lỗi font/encoding ở bất kỳ trình duyệt/thiết bị nào.

## Responsive desktop

Thêm breakpoint `@media (min-width: 820px)` vào `style.css` (chỉ CSS, không đụng
HTML/JS):

- Bottom nav (mobile) → **sidebar trái** cố định 224px; item chuyển sang hàng
  ngang icon + chữ; chỉ báo tab active đổi thành thanh dọc bên trái.
- Nội dung: cột giữa rộng tối đa 1080px, thoáng hơn.
- Màn Home: banner span full width, các thẻ (Lysis/Amplification/Buttons) tự dàn
  thành nhiều cột bằng `grid auto-fit minmax(240px, 1fr)`.
- Chart cao 460px; thẻ Process/Setting giới hạn 900px cho dễ đọc.

Mobile (<820px) giữ nguyên layout cột + bottom nav.

### Review desktop bằng screenshot thật (Edge headless) + sửa

Chụp desktop 1366/1440px, phát hiện và sửa:

- **Header lệch nội dung:** trước dùng `margin: 0 auto` căn giữa `.screens` nhưng
  header thì không → thẻ bị đẩy phải ~68px so với chữ header. Sửa: nội dung căn
  trái (`margin: 0`, `max-width: 940px`) khớp `padding` header.
- **Thẻ dồn trái, chừa trống phải:** `grid auto-fit minmax(240px,1fr)` không giãn
  như ý. Đổi sang **lưới 2 cột** `1fr 1fr` cân đối.
- **Khoảng trống lệch dưới Lysis:** Amplification (2x2) cao hơn đẩy Buttons xuống.
  Sửa: trên desktop Amplification xếp **4 ô 1 hàng** (cao bằng Lysis), Buttons xuống
  **full-width** (`.card:last-child { grid-column: 1/-1 }`).
- **Chỉ báo tab active lệch góc:** bỏ `::before` nổi, thay bằng `box-shadow: inset`
  (vạch trái) + nền tint — luôn canh đúng.

Đã verify lại bằng screenshot: desktop gọn gàng, header khớp, không còn khoảng
trống lệch. Mobile chụp ở 480px (layout width) vừa khít (bản 390px bị cắt chỉ do
Edge headless kẹp cửa sổ tối thiểu ~500px rồi crop — không phải lỗi CSS).

## Đổi kiểu nút (Buttons)

Đổi chỉ báo nút từ chấm tròn + nhãn dưới sang **chip chữ nhật bo góc** chứa nhãn
(GREEN/RED/WHITE):

- Off: nền xám nhạt, chữ muted, viền mảnh.
- On: tô màu nút (xanh lá/đỏ) chữ trắng + đổ bóng; nút White dùng nền sáng + ring
  để nổi trên nền thẻ trắng.
- Giữ nguyên id `bGreen/bRed/bWhite` → hàm `dot()` trong JS không đổi. Chip dãn đều
  bằng flex.

## Nút bấm điều khiển (click-to-control)

Cho phép bấm nút trên web để điều khiển máy.

- **Sửa cho khớp phần cứng:** nút vật lý trong `button.h` (`e_statusbutton`) là
  **RED / BLUE / WHITE** — KHÔNG có GREEN. Đổi 3 chip GREEN/RED/WHITE thành
  RED/BLUE/WHITE (id `bRed/bBlue/bWhite`), chip GREEN cũ bỏ (không map được nút thật).
- **Client:** chip đổi thành `<button>`; click gọi `POST /control?btn=red|blue|white`.
  SSE là 1 chiều nên lệnh đi bằng request riêng; trạng thái đèn quay lại qua event
  `home`. Có phản hồi bấm (`:active` scale + `brightness`).
- **Kiểu nút (cập nhật):** nút điều khiển RPL **luôn tô màu** (RED đỏ, BLUE xanh,
  WHITE trắng viền), chữ trắng/đậm. Khi nhấn/được bật (`.on`) thì **sáng hơn + glow**.
  Mock: buttons mặc định tắt, chỉ sáng ~2s khi nhận `/control` (đúng "nhấn thì sáng").
- **Mock:** thêm endpoint `/control` (GET + POST); khi nhận press thì bật đèn nút đó
  trong SSE ~2s để test thấy vòng lặp đóng lại. Buttons trong `home` đổi keys sang
  `red/blue/white`.
- **Bước firmware (sau này):** route `/control` của `AsyncWebServer` gọi
  `handleShortPress_Red/Blue/White()` (hiện là private trong `buttonManager` — cần
  thêm hook public, hoặc bơm `BTN_EVENT_SHORT_PRESS` vào hàng đợi sự kiện). Mỗi nút
  còn có long-press (Red/Blue → Setting/Calibration, White → Review) nếu muốn thêm
  giữ-để-long-press.

## Ghi chú

- Highcharts tải từ CDN → phía client (điện thoại/máy tính) cần internet; ESP32 chỉ
  phục vụ file cục bộ. Đã guard để mất chart không làm sập Home. Muốn chạy offline
  hoàn toàn (SoftAP): nhúng `highcharts.js` vào LittleFS và trỏ nội bộ thay vì CDN.
