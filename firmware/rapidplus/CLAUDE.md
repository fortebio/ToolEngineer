# CLAUDE.md

Hướng dẫn cho Claude khi làm việc trong repo này. (Docs tiếng Việt; code/UI/comment tiếng Anh.)

## Tổng quan

Firmware ESP32 (PlatformIO / Arduino) cho máy **FBT RAPID** — xét nghiệm LAMP-PCR.
Đọc opto (VEML6035) đo khuếch đại, điều khiển nhiệt (PID heater + hotlid), màn TFT
(ILI9341), 3 nút vật lý, upload kết quả lên Google Sheet + API cloud. Có **web
dashboard** phục vụ từ thiết bị (AsyncWebServer + SSE).

## Build / nạp

```bash
pio run -e esp32dev                 # build firmware
pio run -e esp32dev -t upload       # nạp firmware (khi đổi code src/)
pio run -e esp32dev -t uploadfs     # nạp data/ vào LittleFS (khi đổi file UI)
pio device monitor -b 115200        # Serial
pio test -e esp32dev_test -v        # unit test on-device (FreeRTOS)
```

Board `esp32dev`, framework arduino, filesystem **littlefs**, flash 8MB.

## Kiến trúc RTOS (main.cpp)

`setup()` connect WiFi STA rồi tạo 6 task; `loop()` idle. Mỗi task 1 vòng vô hạn:

| Task | Core | Prio | Việc |
| --- | --- | --- | --- |
| ControlTask | 1 | 5 | `_PIDControl.loop()` + `_Fan.loop()` mỗi 100ms |
| SensorTask | 1 | 2 | `_sensor6035.loop()` mỗi 20ms (mutex I2C) |
| DisplayTask | 0 | 2 | `_displayCLD.loop()` (TFT) mỗi 100ms |
| NetworkTask | 0 | 1 | `updateFirmware()` (OTA) + `dashboardLoop()` mỗi 10ms |
| InputTask | 1 | 3 | `_buttonManager.loop()` + buzzer mỗi 5ms |
| SettingTask | 0 | 1 | cấu hình qua Serial JSON mỗi 10ms |

Globals chính: `_displayCLD`, `_PIDControl`, `_sensor6035`, `_ForteSetting`,
`_buttonManager`, `_bottomThermometer`/`_topThermometer`, `error`, `SerialBT`,
`ssid`/`password`/`id_device`.

## Module chính

- `Bluetooth.cpp` — BLE config, EEPROM settings, WiFiManager portal (`Wifi_Connect`),
  upload TLS (`postData_GoogleSheet`), release BT (`releaseBluetoothStack`).
- `displayCLD/displayLCD.cpp` — máy trạng thái UI: `type_infor` kiểu `e_statuslcd`.
- `PIDControl.cpp` — nhiệt độ: `getBottomTemperature()` = {lysis, ampLeft, ampRight},
  `getHotlidTemperature()` = {topLeft, topRight, ambient}.
- `sensor6035.cpp` — đo opto; đường cong `sensor67Value[10][130]`, chỉ số vòng `COUNTER`.
- `button.cpp` — 3 nút `e_statusbutton {B_RED,B_BLUE,B_WHITE}`, short/long press.
- `webDashboard.cpp` — web dashboard (xem dưới).

## Web dashboard (`webDashboard.cpp` + `data/`)

`AsyncWebServer(80)` + `AsyncEventSource("/events")`. UI tĩnh trong `data/` (LittleFS).
Khởi động lazy trong `dashboardLoop()` khi STA lên **hoặc** SoftAP fallback bật.

Routes: `/` (static), `/events` (SSE), `/control?btn=red|green|white` (bấm nút →
`_buttonManager.postShortPress`; **green** = nút vật lý `B_BLUE`), `/home` (snapshot).

SSE events (client `data/script.js`):

- `home` (1s/lần): `{device, company, temps{lysis,ampLeft,ampRight,topLeft,topRight},
  status{phase,title,subtitle}, notify{show,title,subtitle}, buttons{red,green,white}}`.
  `phase` suy từ `type_infor`; nút sáng ~1.5s sau khi nhấn.
- `new_readings` (chỉ khi `eoptoreading`, mỗi vòng đo): `{"#1":num,...,"#10":num}` =
  giá trị **calibrated** `(sensor67Value[i][COUNTER-1] - origins[i]) / slopes[i]`.
  Phải là **scalar** (client gọi `Number()`), KHÔNG phải mảng.

SoftAP fallback: STA fail → `dashboardStartAP()` phát `RAPID-<id>` (192.168.4.1).
Log heap mỗi 10s: `[dash] heap free=.. maxAlloc=.. clients=.. ap=..`.

## GOTCHAS (quan trọng)

1. **BT release 1 chiều**: `releaseBluetoothStack()` nhả ~60KB, chỉ gọi 1 lần
   (`gBtReleased` guard). Sau đó KHÔNG dùng `SerialBT.*` nữa (reboot mới có lại).
2. **TLS cần ~32-40KB liền mạch**: `postData_GoogleSheet` release BT, hủy JsonDocument
   và gọi **`dashboardSuspend()`** trước handshake, `dashboardResume()` sau. Nếu không →
   `-32512 SSL memory allocation failed`. Dashboard và upload TLS **loại trừ nhau**.
3. **Xung đột `HTTP_GET`**: include header dự án (kéo `define.h→WebServer.h`, đặt
   `WEBSERVER_H`) **trước** `<ESPAsyncWebServer.h>` (guard `#ifndef WEBSERVER_H`),
   nếu không clash với `http_parser.h`.
4. **`data/` cần `uploadfs`** riêng; đổi code cần `upload`. serveStatic tự phục vụ
   bản `.gz` nếu có (VD `highcharts.js.gz`).
5. **WiFi STA connect chậm (1-3s)**: đừng gate khởi động server bằng check 1s lúc boot;
   `dashboardLoop()` tự start khi network lên.
6. **Chart cần Highcharts**: đã nhúng `data/highcharts.js(.gz)` nội bộ (chạy offline/AP).

## Quy ước

- **Code/UI/comment: tiếng Anh** (an toàn font thiết bị). **Docs (.md): tiếng Việt.**
- **Mỗi thay đổi → 1 file `.md` có ngày trong `history/`**; giữ README/CLAUDE cập nhật.
- Ưu tiên dùng `codebase-memory` MCP tools để khám phá code (nhanh hơn grep).

## Test không cần phần cứng

`python tools/sse_test_server.py` — mock ESP32, serve `data/` + stream SSE giả
(vòng đời heating→amplification→finished). Mở <http://localhost:8000>.
`python tools/sse_test_server.py selftest` để tự kiểm.
