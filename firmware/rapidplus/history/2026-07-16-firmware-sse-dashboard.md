# 2026-07-16 — Wiring firmware: AsyncWebServer + SSE dashboard + điều khiển nút

Nối dashboard web (đã dựng ở [2026-07-15](2026-07-15-sse-live-dashboard.md)) vào
firmware ESP32 thật. Phạm vi v1: **Home live + điều khiển nút**. Đã **compile-check**
bằng `pio run` (chưa test trên máy).

## Đã thêm

### `src/webDashboard.h` + `src/webDashboard.cpp` (module mới)

- `AsyncWebServer dashServer(80)` + `AsyncEventSource dashEvents("/events")`.
- `dashboardBegin()` — mount `LittleFS`, `serveStatic("/")` phục vụ file `data/`,
  route `/home` (snapshot) + `/control`, `begin()`. Gọi 1 lần sau khi WiFi STA lên.
- `dashboardLoop()` — tự throttle 1s/lần, đẩy event `home`.
- `dashboardEnd()` — dừng server (gọi trước captive portal WiFiManager).
- Nội dung event `home`: `device`, `company`, `temps` (5 vùng), `status`
  (suy từ `type_infor`), `notify` (khi `escreenFinished`), `buttons`
  (sáng ~1.5s sau khi nhấn — dùng mốc `millis()` cục bộ).
- `/control?btn=red|blue|white` → `_buttonManager.postShortPress()`.

### Nguồn dữ liệu (dùng lại getter sẵn có, không tính lại)

- Nhiệt độ: `_PIDControl.getBottomTemperature()` = {lysis, ampLeft, ampRight};
  `_PIDControl.getHotlidTemperature()` = {topLeft, topRight, ambient}. Đây là dữ
  liệu đã bù offset + đúng thứ tự vùng (raw `thermometer.getTemperature()` thì chưa).
- Trạng thái: `_displayCLD.type_infor` (enum `e_statuslcd`). Map phase:
  heater ← {epreheating80, eheating67, epreheat67, eheatLysis, ecalibPreheating};
  amplification ← {eoptoreading}; finished ← {escreenFinished}; idle ← {escreenStart}.

## Đã sửa file cũ

- `button.h/.cpp` — thêm `void postShortPress(e_statusbutton)` (đặt
  `btnState[b].pendingEvent = BTN_EVENT_SHORT_PRESS`; `loop()` của InputTask
  Core 1 tự dispatch → an toàn thread) + `extern buttonManager _buttonManager`.
- `displayCLD.h` — thêm getter inline `lysisRemainSec()` / `ampRemainSec()`
  (đọc `timer10minEnd` / `timer30minEnd` riêng tư).
- `main.cpp` — bỏ `WebServer server(80)` (code chết); `#include "webDashboard.h"`;
  `dashboardBegin()` sau khi `WiFi.begin` STA lên; `dashboardLoop()` trong NetworkTask.
- `Bluetooth.h/.cpp` — bỏ `postData_Chart()` (0 caller, dead) + `extern server`;
  `server.stop()` → `dashboardEnd()`. `getData_toChart()` giữ lại cho `/readings` v2.

## Gotcha lúc compile (đã fix)

`HTTP_GET/HTTP_POST...` xung đột: ESPAsyncWebServer định nghĩa enum
`WebRequestMethod` trong `#ifndef WEBSERVER_H`, còn `http_parser.h` (IDF, kéo qua
`define.h → WebServer.h`) cũng định nghĩa `HTTP_*`. Fix: include các header dự án
(đặt `WEBSERVER_H`) **trước** `ESPAsyncWebServer.h` trong `webDashboard.cpp` — đúng
thứ tự Bluetooth.h vốn compile được.

## Kết quả build

`pio run -e esp32dev` → **SUCCESS**. RAM 22.8% (74588 B), Flash 67.9% (2.27 MB).

## Chưa làm / cần test trên máy thật

- **Định tuyến AsyncWebServer theo nhánh WEBSERVER_H** (dùng enum của WebServer):
  compile OK nhưng chưa chạy thực — cần xác nhận `/`, `/events`, `/control` khớp method.
- **Coexistence heap**: SSE giữ socket lâu dài + lúc `postData_GoogleSheet` cần
  ~40KB liền mạch cho TLS. Theo dõi `ESP.getMaxAllocHeap()` khi vừa mở dashboard
  vừa upload.
- **Chart `new_readings` (tab Process)**: ĐÃ LÀM (xem mục "Chart v2" cuối file).
  Chỉ chạy khi có run amplification → cần assay thật để verify hiển thị.
- Long-press nút qua web (Setting/Calibration/Review) — chưa làm.

## Cách nạp

```bash
pio run -e esp32dev -t upload     # nạp firmware
pio run -e esp32dev -t uploadfs   # nạp data/ vào LittleFS (BẮT BUỘC, nếu không UI 404)
```

Mở `http://<IP-thiết-bị>/` (IP in ra Serial: `[dash] dashboard on http://...`).

## Bug tìm được khi test máy thật + fix

**Server không bind port 80** dù thiết bị đã lên WiFi (ping được, TCP :80 đóng).
Nguyên nhân: vòng chờ WiFi trong `setup()` chỉ 1.1s (20×50ms) nhưng WiFi mất 1–3s
mới associate → lúc kiểm `WiFi.status()==WL_CONNECTED` vẫn chưa connect →
`dashboardBegin()` bị bỏ qua vĩnh viễn (WiFi lên sau đó nên máy vẫn có IP).

Fix (root cause): khởi động server ngay trong `dashboardLoop()` khi WiFi lên
(NetworkTask gọi mỗi 10ms), không phụ thuộc race lúc boot. `dashboardBegin()`
idempotent nên gọi lại vô hại.

## Đã verify trên thiết bị thật (192.168.1.13)

- `GET /` → 200, 6060 B (UI serve từ LittleFS OK).
- `GET /home` → data thật: `device: RPL03010`, temps ~28°C (lysis/amp/top), phase idle.
- `POST /control?btn=white` → `{"ok":true}`, `/home` ngay sau có `buttons.white=true`
  (~1.5s). WHITE lúc idle = no-op nên máy không bị tác động.
- Screenshot render đúng bố cục desktop.

Log `open(): /littlefs/xxx.gz does not exist` là **vô hại**: `serveStatic` thử bản
nén `.gz` trước rồi phục vụ file thường. Muốn hết log + serve nhanh hơn thì gzip
file `data/` trước khi uploadfs (tùy chọn).

## Cảnh báo khi test nút RED/BLUE (tự bấm)

Từ trạng thái idle (`escreenStart`), bấm nút **thật sự chạy quy trình**:
RED → preheat 67 (bật heater); BLUE → preheat 80 (bật heater); WHITE → no-op.
Nên chỉ bấm RED/BLUE khi sẵn sàng cho máy gia nhiệt.

## Chart v2 — stream `new_readings` (tab Process)

Cho tab Process hiện đường cong khuếch đại live.

Luồng acquisition (`sensor6035::eSensor1stReadingFunc`): mỗi vòng ghi
`sensor67Value[i][COUNTER]` (raw Word) rồi `COUNTER++`; giá trị chart hiển thị =
calibrated `(raw - origins[i]) / slopes[i]` (`sensor6035.cpp:1861-1862`,
`COUNTER++` ở `:1877`). Vòng hoàn tất mới nhất = `COUNTER-1`.

- `sensor6035.h` — thêm getter public `getCurrentLoop()` trả `COUNTER` (chỉ số
  riêng tư). `sensor67Value` + `_ForteSetting.parameter.origins/slopes` vốn đã public.
- `webDashboard.cpp` — `buildReadingsJson(idx)` build `{"#1":cal,...,"#10":cal}`
  (calibrated). Trong `dashboardLoop`: chỉ khi `type_infor==eoptoreading`, đẩy 1
  event `new_readings` **mỗi khi `COUNTER` đổi** (1 điểm/vòng, tránh điểm trùng phẳng).
  Client (`data/script.js`) đã có sẵn listener `new_readings` → `plotResult`.

Compile SUCCESS. **Cần chạy assay thật (eoptoreading) để verify chart hiển thị** —
không kiểm được bằng compile hay lúc idle. Chỉ đổi firmware (`data/` giữ nguyên) →
nạp `pio run -e esp32dev -t upload` (không cần uploadfs lại).

## SoftAP fallback + log heap

Cho dùng dashboard khi không có WiFi, kèm log heap để đo tải thực tế.

- `main.cpp` — sau vòng chờ STA, nếu `WiFi.status() != WL_CONNECTED` →
  `dashboardStartAP()` (thay lời gọi `dashboardBegin` cũ; `dashboardLoop` tự start
  server khi STA hoặc AP lên).
- `webDashboard.cpp`:
  - `dashboardStartAP()` — `WiFi.mode(WIFI_AP)` + `softAP("RAPID-<id>")` (mở, không
    mật khẩu), log free/maxAlloc heap + IP AP (192.168.4.1).
  - `networkUp()` = STA connected HOẶC AP active → gate khởi động server.
  - `dashboardBegin()` log heap + IP + chế độ (AP/STA).
  - `dashboardLoop()` log heap mỗi 10s: `[dash] heap free=.. maxAlloc=.. clients=.. ap=..`.
  - `dashboardEnd()` tắt luôn AP (`softAPdisconnect`) trước captive portal.

**Vì sao ít rủi ro:** fallback-AP → AP và upload Google Sheet (TLS ~40KB liền mạch)
**loại trừ nhau** (AP = không internet = không upload). Worst-case heap không xảy ra.

**Đo:** Serial 115200 → dòng `[dash] heap ...` mỗi 10s. So free/maxAlloc giữa STA và
AP, có/không client. Test AP: boot khi không có WiFi (hoặc sai SSID) → nối phone vào
`RAPID-<id>` → mở `http://192.168.4.1/`.

Nạp firmware SoftAP: `pio run -e esp32dev -t upload`.

## Nhúng Highcharts nội bộ (chart chạy offline / chế độ AP)

Trước dùng CDN `code.highcharts.com` → ở AP không internet thì tab Process trống.
Đã bundle vào `data/`:

- `data/highcharts.js` (Highcharts 11.4.8, 272 KB) + `data/highcharts.js.gz` (96.6 KB).
  serveStatic tự phục vụ bản `.gz` (Content-Encoding: gzip) → tải ~97 KB thay vì 272.
- `data/index.html` — đổi `<script src="https://code.highcharts.com/...">` sang
  `<script src="highcharts.js">` (nội bộ). Không còn tham chiếu CDN.
- Chỉ đổi client (`data/`) → cần **`uploadfs`** (không cần build lại firmware cho phần này).

Verify cục bộ qua mock: `/highcharts.js` → 200 (272 KB), index.html 0 CDN ref.
Giờ toàn dashboard (kể cả chart) chạy được offline trên SoftAP.

## BUG NGHIÊM TRỌNG: dashboard làm hỏng upload Google Sheet (đã fix)

Sau khi có dashboard, upload TLS fail:
`[E][ssl_client.cpp] (-32512) SSL - Memory allocation failed`. AsyncWebServer +
SSE socket giữ heap → mbedTLS không cấp được ~32KB liền mạch cho handshake. Đây
đúng là hazard coexistence đã cảnh báo. Upload là chức năng lõi → phải fix.

Fix (mutual-exclusion, giống release-BT-trước-TLS):

- `webDashboard.cpp` — `dashboardSuspend()` đóng SSE clients (`dashEvents.close()`)
  và `dashServer.end()` để giải phóng heap (set cờ `suspended`); `dashboardResume()`
  bật lại. `dashboardLoop()` return ngay khi `suspended` → NetworkTask không restart
  server giữa lúc upload. Handler đăng ký 1 lần (`handlersReady`) nên begin lại không nhân đôi.
- `Bluetooth.cpp postData_GoogleSheet` — `dashboardSuspend()` ngay đầu (trước release
  BT + build JSON, tối đa heap), `dashboardResume()` trước CẢ 3 điểm return.

Trong lúc upload (~40-60s) dashboard tắt → trình duyệt hiện Offline rồi tự
reconnect (EventSource auto-retry) sau khi xong. Upload hiếm (sau mỗi run) nên OK.

**Cần verify trên máy:** chạy 1 upload sau khi nạp → không còn `-32512`, và dashboard
quay lại sau đó. (Nếu vẫn thiếu heap: giảm `client.setBufferSizes()` cho TLS.)
