# 2026-07-27 — Bỏ badge "preferred", gate Connect theo mạng đang nối

## Vấn đề

Panel WiFi (Setting → WiFi, danh sách mạng đã lưu) gắn **hai** nhãn: `preferred` cho hàng 0
và `connected` cho mạng đang nối. Hỏi từ người dùng: *"đã có connected thì preferred có cần
nữa không"*. Soát ra ba thứ, một trong đó là bug thật.

**Hai thứ cùng tên "preferred", không phải một:**

- **Cơ chế** — cặp `ssid`/`password` trong EEPROM, `connectSavedNetworks()` thử **đầu tiên**
  lúc boot với cửa sổ 4s (list NVS chỉ 2.5s/mạng). **Vẫn cần, không đụng tới**:
  `Wifi_Connect()` (portal WiFiManager, `Bluetooth.cpp:380-383`) ghi cặp EEPROM mà **không**
  thêm vào list NVS → bỏ nó là mất mạng cấu hình bằng portal. Cửa sổ 4s cũng cần cho DHCP
  (1-3s, GOTCHA 5).
- **Nhãn trên web** — `i === 0` của list NVS, tức **vị trí trong list**, không đọc EEPROM.

**Chúng lệch nhau được**: `main.cpp:255-263` (fallback boot) nối được `nets[i]` nhưng
**không ghi EEPROM, không đổi thứ tự list**. Máy đang chạy trên hàng 3 mà hàng 0 vẫn đeo
"preferred".

## Bug thật: không có đường quay về hàng 0

`if (i !== 0)` giấu nút **Connect** ở hàng 0. Ghép với tình huống lệch ở trên: hàng 0 là
preferred-nhưng-không-connected, và người dùng **không có cách nào bấm quay lại nó từ web**
— nút bị giấu đúng ở hàng cần bấm. Điều kiện đúng là "ẩn Connect ở hàng **đang connected**",
không phải theo chỉ số.

## Đã sửa (`data/script.js`, `data/style.css`)

1. **Bỏ badge `preferred`.** Thứ tự list (trên = thử trước) đã diễn đạt ưu tiên; nhãn kia
   99% trùng `connected`, và đúng lúc nó khác thì người vận hành đọc thành "máy đang nối sai
   mạng".
2. **Gate Connect theo `onNow`, không theo index.** Hiện ở mọi hàng **trừ** hàng đang nối.
3. **MỘT nguồn sự thật cho mỗi lần render**: `live = d.current` (từ `/wifilist`, tươi khi
   server trả lời), `curNet` (SSE, trễ tới 1s) chỉ là fallback. Trước đó quyết định
   **từng hàng** bằng `n.active || curNet khớp` — hai nguồn lệch pha khiến **hai hàng cùng
   badge "connected"**, và tệ hơn là **giấu luôn nút Connect của hàng thứ hai**. Đây là lỗi
   sinh ra trong chính lần sửa này, test bắt được (xem dưới).
4. **`.wifi-badge-on` phải đứng SAU `.wifi-badge`** (`style.css`). Cả hai là selector 1 class
   → cùng độ đặc hiệu → **rule đứng sau thắng**. Xếp trước, màu nền của rule base đè lên nên
   badge "connected" **chưa bao giờ thực sự xanh**. Cùng họ với bẫy `.hide` cần `!important`
   (Setting #8). Trắng trên `--green #11803a` = **5.03:1**, đạt WCAG AA.
5. **`wifiPick` từ module-scope → biến cục bộ.** `renderWifi()` reset `wifiPick = null` mỗi
   lần render panel, mà callback của Save chạy **sau** khi thiết bị poll xong → đọc phải
   `null` → thông báo *"Rebooting to test **null**"*. Biến này ngoài handler đó không ai đọc
   nên xoá hẳn (dòng gán ở phần chọn mạng cũng là code chết — ô SSID mới là nguồn).

## Mock + test

`tools/sse_test_server.py`:

- **`_wifi_live()` — một nguồn cho cả `/home` `net.ssid` lẫn `/wifilist` `current`/`active`.**
  Trên máy thật cả hai đọc `WiFi.SSID()`; để mock cho chúng trôi khỏi nhau sẽ dựng ra trạng
  thái **phần cứng không bao giờ tạo được** (hai hàng cùng connected) và test đuổi theo ma.
- **`POST /wifilist?current=<ssid>`** — hook cho test ghim "máy đang nối mạng KHÔNG phải hàng
  đầu", tức mô phỏng fallback boot. Trước đây mock hard-code `live = _saved_wifi[0]`
  (*"preferred = connected"*) nên trạng thái làm lộ bug **không biểu diễn được** — đó là lý
  do bug sống lâu.
- `parse_qs(body, **keep_blank_values=True**)`: `current=` (rỗng) là lệnh **gỡ ghim**, mà
  mặc định `parse_qs` **vứt luôn key có giá trị rỗng** → lệnh reset im lặng không làm gì, và
  test kế tiếp chạy trên trạng thái cũ mà vẫn "pass".

`tools/test_wifi_e2e.js`:

- Assertion cũ `firstBadge: !!rows[0].querySelector('.wifi-badge')` **vẫn xanh sau khi bỏ
  badge** — vì badge "connected" cũng mang class `.wifi-badge`. Test xanh mà vô nghĩa. Thay
  bằng: không có chữ "preferred" ở đâu, và **đúng một** hàng có `.wifi-badge-on`.
- **Guard mới**: ghim current sang mạng thứ hai → khẳng định hàng 0 **vẫn có** Connect, và
  Connect vắng mặt ở **đúng** hàng đang nối.
- Test Connect chọn hàng **theo "có nút Connect"**, không theo index, và khẳng định thêm
  `beforeConnect[0] !== picked` — nếu không, bấm đúng mạng đã ở đầu list sẽ "pass" mà chẳng
  chứng minh gì.
- `openWifiPanel()` **xoá `#wifiSaved` trước** khi mở lại, để vòng chờ mang nghĩa "render mới
  đã tới" chứ không phải "render cũ còn nguyên trên màn". Thiếu bước này, test đọc DOM cũ.
- Chờ thông báo *"rebooting to test"* lắng rồi mới mở lại panel: luồng Save poll thiết bị nên
  thông báo của nó **tới muộn** và đè lên notice trial:failed đang được kiểm.

**Đã kiểm chứng guard không phải đồ trang trí**: tạm đổi gate về `nets.indexOf(n) !== 0` →
2 assertion mới **đỏ ngay** (`connect btn: false,true` — nút nằm đúng hàng đang nối và mất ở
hàng 0), trả lại `!onNow` thì xanh.

## Không đổi

Firmware. Cặp EEPROM preferred, `connectSavedNetworks()`, thứ tự thử lúc boot, trial-then-commit
— giữ nguyên. Đây thuần là lớp hiển thị + hai bug trong đó.
