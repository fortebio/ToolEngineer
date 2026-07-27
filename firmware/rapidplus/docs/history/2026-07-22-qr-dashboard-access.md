# 2026-07-22 — Quét QR để vào web dashboard (có WiFi lẫn không có WiFi)

## Yêu cầu

Quét QR trên máy là vào được web dashboard, **cả khi có WiFi lẫn khi không có WiFi**.

## Thiết kế

**Vào màn QR: nút TRẮNG ở màn hình chính.** Nút này trước đó **không làm gì**
(`handleShortPress_White` chỉ `return; // Already at start screen`, button.cpp) nên dùng được
mà không giẫm lên chức năng nào. Bấm TRẮNG lần nữa → quay lại màn chính.

**Nội dung QR đổi theo chế độ mạng** (sinh lại mỗi lần vào màn, nên IP đổi do DHCP không bao
giờ bị cũ):

| Chế độ | QR chứa | Người dùng làm gì |
| --- | --- | --- |
| **STA** (có WiFi) | `http://<ip>/` | Quét → mở thẳng dashboard (điện thoại cùng WiFi) |
| **SoftAP** (không WiFi) | `WIFI:T:nopass;S:RAPID-<id>;;` | Quét → tự nối AP → **captive portal tự mở dashboard** |

SoftAP là **AP mở** (`WiFi.softAP(ap)` không mật khẩu) nên mã WiFi dùng `T:nopass`.

**Captive portal (chế độ AP)**: chuẩn QR WiFi chỉ nối mạng, **không** mở được URL — nên thêm
`DNSServer` trả mọi truy vấn DNS về IP của máy, cộng `onNotFound` chuyển hướng về `/`. Điện
thoại vừa nối AP sẽ tự thăm dò (`/generate_204` Android, `/hotspot-detect.html` iOS,
`/connecttest.txt` Windows) → dính redirect → **OS tự bật dashboard**. Ở STA thì
`onNotFound` vẫn trả 404 bình thường (không cướp mạng thật).

## Hiện thực

- `src/displayCLD.h`: thêm state `eShowQR` (đặt **cuối enum** để không đổi giá trị state cũ),
  khai báo `screen_QR()`.
- `src/button.cpp` `handleShortPress_White`: `escreenStart → eShowQR`, và `eShowQR → escreenStart`.
- `src/displayLCD.cpp`: `screen_QR()` + case trong switch. Dùng **`ricmoo/QRCode`** — thư viện
  **đã có sẵn trong `lib_deps` nhưng chưa từng dùng**. QR **version 3 (29×29), ECC_LOW** đủ cho
  cả 2 payload (URL ~21 ký tự, mã WiFi ~32), buffer ~106 B trên stack. Vẽ **tối trên nền sáng**
  kèm **quiet zone 4 module** (máy quét cần tương phản + lề này) — nền trắng `fillRect`, module
  đen; `scale = 5` px/module → mã 145 px, tổng 185 px, vừa 320×240 và còn chỗ cho chữ bên phải.
- `src/webDashboard.cpp`:
  - `eShowQR` vào **allowlist `isBusy()`** → xem QR **không** bị coi là bận (nếu thiếu, mở màn QR
    sẽ chặn đổi cấu hình / `/reviewlast` vì mặc định `default → busy`).
  - `fillActions`: nhãn nút web `escreenStart → white "QR / Web"`, `eShowQR → white "Back"`.
  - `DNSServer` + `dnsServer.start(53,"*",softAPIP())` trong `dashboardStartAP()`;
    `dnsServer.processNextRequest()` trong `dashboardLoop()` **phía trên throttle 1 s** (thăm dò
    của điện thoại timeout trước 1 s → portal không bật); `onNotFound` redirect khi `apActive`.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS**. Chi phí: RAM +96 B (22.9%), Flash +7,8 KB (68.8% → **69.1%**).
- Nạp COM18, kiểm luồng nút qua web (`/home` `actions.white`):

| Bước | `actions.white` | busy |
| --- | --- | --- |
| Màn chính | `QR / Web` | false |
| Bấm TRẮNG | `Back` (đã ở màn QR) | false |
| Bấm TRẮNG lần nữa | `QR / Web` (về màn chính) | false |

- **Ép sang SoftAP để test** (board RPL03018): `POST /wifi` **chặn SSID rỗng**
  (`ssid must be 1..32 chars`) nên không "xoá" được — cách dùng là **trỏ sang SSID không tồn
  tại** (`ssid=FBT_NO_WIFI_TEST`), board tự reboot rồi rơi AP. Serial xác nhận:
  `[wifi] STA not up after 15 s -> SoftAP fallback` → `[dash] SoftAP 'RAPID-RPL03018' at
  http://192.168.4.1/` → `ap=1`. **Khôi phục**: vào `http://192.168.4.1/` → Setting → WiFi →
  nhập lại SSID/mật khẩu thật (SSID cũ đã bị ghi đè, không đọc lại được từ máy).
- **Giới hạn 2 thiết bị trên AP** (yêu cầu vận hành): `WiFi.softAP(ap, NULL, 1, 0, 2)` — dùng
  đúng tham số `max_connection` sẵn có của core (mặc định 4), thiết bị thứ 3 bị từ chối **lúc
  associate**. **Không phải** vì heap: đo trên board RPL, mỗi SSE client chỉ tốn **~660 B**
  (free 87 528 → 82 184 ở 8 client) và **`intLargest` đứng yên 51 188** cả khi 0 lẫn 8 client
  → client **không phân mảnh** block liền mạch nên ngưỡng TLS 42KB (GOTCHA 2) không bị đe doạ;
  `/home` 19→34 ms, `/curve` 89 ms, đóng hết thì heap về đúng 87 528 (không rò rỉ).
  Giới hạn này **chỉ áp dụng ở SoftAP**; ở STA cả LAN vẫn vào được.
- **Bẫy chẩn đoán**: `curl http://ip/events` trả **"Not found"** — trông như `onNotFound` mới
  thêm làm hỏng SSE, thực ra `AsyncEventSource::canHandle` đòi `request->isSSE()` tức header
  `Accept: text/event-stream`; browser luôn gửi, curl phải thêm tay
  (`curl -H "Accept: text/event-stream"` → 200 + stream).
- **Còn cần kiểm bằng mắt/điện thoại**: hình QR trên màn TFT và việc quét thực tế (nối AP +
  captive portal tự mở dashboard), và việc điện thoại thứ 3 bị từ chối.

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
