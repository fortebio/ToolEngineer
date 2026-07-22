# CLAUDE.md

Hướng dẫn cho Claude khi làm việc trong repo này. (Docs tiếng Việt; code/UI/comment tiếng Anh.)

**Kiến trúc chi tiết + sơ đồ:** [docs/architecture/](docs/architecture/) (state machine, nhiệt/sensor, dashboard, mạng/upload).

## Tổng quan

Firmware ESP32 (PlatformIO / Arduino) cho máy **FBT RAPID** — xét nghiệm LAMP-PCR.
Đọc opto (VEML6035) đo khuếch đại, điều khiển nhiệt (PID heater + hotlid), màn TFT
(ILI9341), 3 nút vật lý, upload kết quả lên Google Sheet + API cloud. Có **web
dashboard** phục vụ từ thiết bị (AsyncWebServer + SSE).

## Build / nạp

```bash
pio run -e esp32dev                 # build firmware
pio run -e esp32dev -t uploadall    # nạp CẢ HAI (firmware + data/) - dùng cái này
pio run -e esp32dev -t upload       # chỉ firmware (khi đổi code src/)
pio run -e esp32dev -t uploadfs     # chỉ data/ vào LittleFS (khi đổi file UI)
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

- `Bluetooth.cpp` — BLE config (dead — nhả BT), EEPROM settings, WiFiManager portal
  (`Wifi_Connect`), upload TLS (`postData_GoogleSheet` → **3 đích**: GAS/Google Sheet,
  ingest `fbt.basa-luma` Bearer token, ERP `api.fortebio` **X-API-Key**; qua
  `postJsonRetry(url, payload, label, bearer, apiKey, outBody)`), release BT
  (`releaseBluetoothStack`, gọi sớm ở main.cpp — GOTCHA 1).
- `displayCLD/displayLCD.cpp` — máy trạng thái UI: `type_infor` kiểu `e_statuslcd`.
- `PIDControl.cpp` — nhiệt độ: `getBottomTemperature()` = {lysis, ampLeft, ampRight},
  `getHotlidTemperature()` = {topLeft, topRight, ambient}.
- `sensor6035.cpp` — đo opto; đường cong `sensor67Value[10][130]`, chỉ số vòng `COUNTER`.
- `button.cpp` — 3 nút `e_statusbutton {B_RED,B_BLUE,B_WHITE}`, short/long press.
- `webDashboard.cpp` — web dashboard (xem dưới).

## Web dashboard (`webDashboard.cpp` + `data/`)

`AsyncWebServer(80)` + `AsyncEventSource("/events")`. UI tĩnh trong `data/` (LittleFS).
Khởi động lazy trong `dashboardLoop()` khi STA lên **hoặc** SoftAP fallback bật.

3 màn (bottom nav / sidebar desktop): **Home** (nhiệt độ, trạng thái, nút điều khiển,
kèm **naming/chart** khi vào pha amp), **Result** (xem lại: bảng kết quả + chart đã lưu),
**Setting**.

**Ẩn nav khi đang chạy run**: `busy && !calib` → `body.nonav` (ẩn nav + thu hồi khoảng
trống dành cho nó: `padding-bottom` mobile, `padding-left` desktop vì nav **là** sidebar).
Không ẩn khi **calib** — wizard calib nằm trong tab Setting, ẩn nav là nhốt người dùng ở
đó. Run bắt đầu lúc user đang ở tab khác → tự chuyển về Home trước khi ẩn (`applyRunNav`).

Routes: `/` (static), `/events` (SSE), `/control?btn=red|green|white` (bấm nút →
`_buttonManager.postShortPress`; **green** = nút vật lý `B_BLUE`), `/home` (snapshot),
`/slots` (bảng kết quả: tên + CT + P/N/S), `/rename?slot=N&name=X` (đổi tên, lưu
`/slotnames.json` trên LittleFS), `/curve` (toàn bộ đường cong từ đầu run → backfill),
`POST /reviewlast` (nạp lại run cuối từ EEPROM để xem sau reboot — xem dưới).
Kết quả cache qua `dashboardSetResults()` gọi từ `screen_Result()`.

**Xem lại run sau reboot (`POST /reviewlast`)**: cache RAM (`gResultsReady`, `gCT`,
`lastRunLoops`) mất khi tắt máy, nhưng run cuối còn RAW ở `RECORDPOS`. Client mở Result,
thấy `/slots ready=false` (và không đang amp) → `POST /reviewlast`. AsyncTCP chỉ enqueue
`PEND_REVIEW`; **SettingTask** (guard idle) chạy `getDataAmplificationEEPROM()` →
`bResultGet` → `dashboardSetResults` + `setLastRunLoops(amplification_time)` để `/curve`
phục vụ đúng số vòng. Heuristic `10 < sensor67Value[0][0] < 60000` bỏ record rỗng
(EEPROM chưa ghi = `0xFFFF`) → máy mới hiện trống, không rác. **Không** đổi `type_infor`
(khác `resultOutput()`/`escreenReview` là bản Serial cướp màn TFT). Test:
`node tools/test_review_reboot.js` (mock `--reboot`).

**Bảng và chart phải cùng lifecycle** (bug desync 2026-07-21): `/slots` dùng `gResultsReady`
(cache dai), `/curve` dùng `lastRunLoops` (bị `clear()` đầu run mới đưa về 0). Nếu chỉ xóa
một cái → `/slots ready=true` nhưng `/curve count=0` = **bảng có, chart trống**, và client
**không tự chữa** vì `reviewStoredRun` chỉ kích khi `ready=false`. Vì vậy `clear()` đầu run
(button.cpp:481) gọi kèm **`dashboardClearResults()`** (đặt `gResultsReady=false`) để hai
cache cùng trống → lần vào Result kế `ready=false` → `/reviewlast` nạp lại **cả hai** đồng
bộ từ EEPROM. Chi tiết: [docs/history/2026-07-21-result-chart-table-desync.md](docs/history/2026-07-21-result-chart-table-desync.md).

**Đặt tên bệnh trước khi chạy — HAI điểm** (client theo `phase`):

- **`waitname`** (luồng Amplification): bấm Amplification (đỏ) ở `escreenStart` → state mới
  `ewaitname` (**CHƯA heating**) → card chọn bệnh → **Confirm** gửi `/control?btn=red` →
  firmware bắt đầu preheat 67°C. Đây là "đặt tên → heating → start" theo yêu cầu.
- **`waitamp`** (luồng Lysis): đặt tên **sau** heating như cũ, Confirm mở khoá Start.
- `confirmed` sống xuyên heating (chỉ reset khi về `idle`), nên named-ở-waitname thì
  waitamp không hỏi lại.
- **Tên = tập cố định bệnh tôm** `{PC,EHP,EMS,WSSV,TPD}` chọn bằng `<select>` (không gõ
  tự do). Ô trống → mặc định `#N`. `loadNamingSlots`/`loadResultSlots` build **1 lần** sau
  khi `/slots` về (bỏ pre-build rỗng) — pre-build 2 pha từng đè mất lựa chọn select.

**Naming gate cũ (còn đúng)**: `ewaitampTube` → `phase "waitamp"`. Client hiện **card đặt
tên slot** và **khoá nút Start (đỏ)** bằng class `.locked` (chặn phía web; **nút vật lý vẫn chạy**).
Bấm **Confirm** (`#confirmNamesBtn`) → mở Start, hiện **chart live**, và nhiệt độ **thu về
1 strip nhỏ gọn** (`#tempCompact`, ẩn các card nhiệt đầy đủ). Ô tên trống → mặc định
`#1`–`#10`. Client tự quản (`confirmed`/`naming`/`chartMode` trong `renderHome`); khi
`phase == "amplification"` tự coi là đã confirm (reload giữa run không kẹt).

`chartMode` gồm cả **`phase == "finished"`** → run xong **chart vẫn ở lại Home**; chỉ mất
khi **nút trắng** (máy hoặc chip web) rời màn finished: `escreenFinished` → `escreenRestart`
(không có trong `fillStatus` → `default` → `phase "idle"`). Trạng thái máy là nguồn duy nhất
điều khiển việc này — client không giữ cờ riêng.

SSE events (client `data/script.js`):

- `home` (1s/lần): `{device, company, temps{lysis,ampLeft,ampRight,topLeft,topRight},
  status{phase,title,subtitle}, notify{show,title,subtitle}, buttons{red,green,white},
  actions{red,green,white}}`. `phase` suy từ `type_infor`; nút sáng ~1.5s sau khi nhấn.
  `actions` = **chức năng của từng nút ở trạng thái hiện tại** (`fillActions()` map theo
  `type_infor`, mirror `handleShortPress_*`) — chip web hiện nhãn này, rỗng thì mờ đi.
- `new_readings` (chỉ khi `eoptoreading`, mỗi vòng đo): `{i:idx, "#1":num,...,"#10":num}` =
  giá trị **calibrated** `(sensor67Value[i][COUNTER-1] - origins[i]) / slopes[i]`.
  Phải là **scalar** (client gọi `Number()`), KHÔNG phải mảng. `i` = **chỉ số vòng đo**
  (`COUNTER-1`) → trục X (client bỏ qua key không bắt đầu bằng `#`).

`GET /curve` → `{count, intervalMs, series:[[cal...] ×10]}`: **toàn bộ** run tới hiện tại
(cùng công thức calibrate). Client gọi khi mở chart và mỗi lần SSE reconnect → vẽ lại đủ
run kể cả đoạn bị mất kết nối. **Thiết bị là nguồn sự thật**, client không tự giữ lịch sử.
Trục X = `i * (intervalMs/60000)` phút — cùng gốc cho backfill và điểm live nên nối liền.
`count = getCurrentLoop()`; **fallback `getLastRunLoops()` chỉ khi `= 0` VÀ
`type_infor != eoptoreading`** (xem GOTCHA 7 — `COUNTER` cũng = 0 ở vòng đầu của run mới).

`GET /slots` trả `ready = gResultsReady && type_infor != eoptoreading`: đang chạy run mới
thì **giấu kết quả cache của run cũ** (máy chỉ giữ 1 run) → bảng và chart trên Result luôn
cùng một run.

## Tab Setting (7 card)

WiFi · Device ID · Test profile · LED · Calib · PID/heater · Other. Master-detail: lưới
card → bấm mở panel form.

**Chỉ 4 route cho cả 7 card**, vì `JsonDataConfig()` áp dụng **chỉ các key có mặt**:

- `GET /config` → toàn bộ parameter (`paraToJson()`, tách từ `paraDisplay()`; có thêm
  `top heater PWM` mà bản gốc bỏ sót) → đổ vào form.
- `POST /config` → validate → **xếp hàng** (`_ForteSetting.postConfigJson`) → SettingTask
  áp bằng `JsonDataConfig()`. Mỗi card chỉ gửi tập key của nó, thiết bị merge phần còn lại.
- `GET /wifiscan` (async: 202 khi đang quét, 200 + list khi xong), `POST /wifi`,
  `POST /deviceid`, `POST /calib?action=start|next|measure|slot&n=|cancel`.

**Bắt buộc nhớ:**

1. **`"para version"` là key bắt buộc** — thiếu nó `JsonDataConfig()` áp dụng **KHÔNG GÌ
   CẢ** nhưng vẫn **`return true`** (im lặng giả vờ thành công). `handleConfigPost` tự chèn.
2. **AsyncTCP KHÔNG được đụng `parameter`/EEPROM** — chỉ validate + enqueue + trả lời.
   Mọi ghi EEPROM (parameter, WiFi, id) dồn về **SettingTask** (`drainPending()`), nếu
   không `EEPROM.begin/end` của 2 task sẽ giải phóng buffer 4096B dưới chân nhau.
3. **`status.busy`** (allowlist trong `isBusy()`), **không** dùng `phase`: calib/OTA/chờ
   ống đều báo `phase "idle"`. Khoá client là **vô nghĩa** (curl/tab cũ) → server chặn
   **409** *và* SettingTask **kiểm lại** trước khi áp (đóng TOCTOU).
4. **Firmware không validate khoảng nào** → `handleConfigPost` là **trust boundary của
   heater**: `amplification time` phải ≤ **130** (không thì tràn `sensor67Value[10][130]`
   giữa run), mật khẩu WiFi ≤ **54** ký tự (khe EEPROM, không phải 63 của WPA2),
   `device ID` ≤ **9** (`char[10]`), mảng phải **đúng độ dài** (loop không check bounds).
5. **`parastructure` = 400B / giới hạn 402B** (`sizeof(parameter) > 512-110`) → **không
   thêm field mới** vào struct.
6. **Device ID nằm ở HAI nơi**: global `id_device` (EEPROM 170, dashboard + upload dùng)
   và `parameter.device_id` (EEPROM 512). `PEND_ID` ghi cả hai.
7. **Calib là wizard người-trong-vòng-lặp**, không phải routine: BLUE **long-press**
   (`postLongPress`, mới thêm) → RED sấy 55°C → chờ **5 phút** → chọn slot → **4 lần đo,
   mỗi lần thay ống thật** (300/200/100/0). Máy **không có đường cancel sạch** (WHITE gọi
   `ESP.restart()`) → `/calib?action=cancel` tự gỡ cờ (`flag_calib_done`, `type_calib`),
   thiếu bước này lần calib sau sẽ **chết cứng**. Calib **chỉ ghi `slopes`**, không ghi
   `origins`. Chọn slot bằng cách ghi thẳng `_displayCLD.slot` (bấm RED nhiều lần sẽ bị
   gộp: `pendingEvent` chỉ có **1 ô**).
8. **`.hide` phải `!important`** — utility 1 class sẽ thua rule component 1 class định
   nghĩa sau (`.set-grid{display:grid}`), panel sẽ render **dưới** menu thay vì thay thế nó.

SoftAP fallback: STA fail → `dashboardStartAP()` phát `RAPID-<id>` (192.168.4.1).
Log heap mỗi 10s: `[dash] heap free=.. maxAlloc=.. clients=.. ap=..`.

## GOTCHAS (quan trọng)

1. **BT release 1 chiều — nhả NGAY ĐẦU `setup()`, TRƯỚC `WiFi.begin()`**:
   `releaseBluetoothStack()` (`esp_bt_mem_release(ESP_BT_MODE_BTDM)`, ~60KB) chỉ gọi 1 lần
   (`gBtReleased` guard). Sau đó KHÔNG dùng `SerialBT.*` nữa (reboot mới có lại). BT là
   **code chết** (không `SerialBT.begin()` lúc boot; `eSettingBluetooth`/`connectBLE` đã hỏng
   vì gặp guard; web Setting tab thay cấu hình BT). Macro `info_display*` (define.h) bọc
   `if (!gBtReleased)` quanh nhánh `SerialBT` — USB serial (`DEBUG_COM`) vẫn chạy.
   **QUAN TRỌNG (heap)**: nhả BT **SỚM** (main.cpp, trước `WiFi.begin`) thay vì muộn (trong
   `dashboardBegin`) → allocator xếp WiFi/lwIP/AsyncWebServer quanh vùng đầy → block liền mạch
   internal **68KB** (trước chỉ 40-47KB, tùy board). Đây là fix gốc rễ của `-32512` lúc upload
   (mbedTLS cần ~42KB liền mạch — xem GOTCHA 2). `releaseBluetoothStack()` trong `dashboardBegin`
   giờ là no-op. Đo bằng `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` — **KHÔNG**
   phải `ESP.getMaxAllocHeap()` (số kia có thể tính cả cap khác). Chi tiết:
   [docs/history/2026-07-21-erp-upload-and-bt-early-release-heap-fix.md](docs/history/2026-07-21-erp-upload-and-bt-early-release-heap-fix.md).
2. **TLS cần ~42KB liền mạch (INTERNAL)**: đo thực tế mbedTLS fail ở `intLargest=40948`, chỉ
   lọt ở `42996`. `postData_GoogleSheet` hủy JsonDocument (chỉ giữ `jsonPost` ~7KB) trước
   handshake. Sau khi **nhả BT sớm** (GOTCHA 1), block liền mạch idle ~**68KB** → upload còn
   ~55-60KB → thừa xa → hết `-32512`. Trước đó (nhả BT muộn) chỉ 40-47KB → sát ngưỡng →
   `-32512` chập chờn. `dashboardSuspend()`/`dashboardResume()` vẫn bọc quanh upload (hạ
   AsyncWebServer để async_tcp không bị đói — GOTCHA 11), nhưng nay không còn là nút thắt heap.
3. **Xung đột `HTTP_GET`**: include header dự án (kéo `define.h→WebServer.h`, đặt
   `WEBSERVER_H`) **trước** `<ESPAsyncWebServer.h>` (guard `#ifndef WEBSERVER_H`),
   nếu không clash với `http_parser.h`.
4. **`data/` cần `uploadfs`** riêng; đổi code cần `upload`. serveStatic tự phục vụ
   bản `.gz` nếu có (VD `highcharts.js.gz`).
5. **WiFi STA connect chậm (1-3s)**: đừng gate khởi động server bằng check 1s lúc boot;
   `dashboardLoop()` tự start khi network lên.
6. **Chart cần Highcharts**: đã nhúng `data/highcharts.js(.gz)` nội bộ (chạy offline/AP).
7. **`COUNTER` bị xoá ngay khi run xong**: `sensor6035.cpp:1908` set `COUNTER = 0` cùng
   dòng với `type_infor = escreenFinished`, nên `getCurrentLoop()` trả 0 **dù
   `sensor67Value` vẫn còn nguyên đường cong**. Vì vậy `handleCurve` phải fallback
   `getLastRunLoops()`, nếu không tab Result vẽ **chart trắng**. Mock từng che lỗi này
   → mock giờ mirror đúng (`curve_count()`).
   **Gốc rễ đã sửa**: `clear()` giờ được gọi ở **đầu mỗi run** (button.cpp, ngay trước
   `type_infor = eoptoreading`, xoá **trước** để SensorTask chưa kịp đọc) → buffer +
   `COUNTER` + `lastRunLoops` sạch mỗi run, hết **dữ liệu thừa run cũ**. Xoá ở **đầu run**
   chứ không ở idle: idle xoá sẽ mất run cho tab Result xem lại (đó là lý do dòng cũ ở
   displayLCD.cpp:1400 bị comment). Run trước vẫn nằm EEPROM tới khi run này ghi đè lúc kết
   thúc. Hai guard `/curve` dưới đây **giữ lại làm lớp phòng thủ**, giờ dữ liệu nền đã sạch.

   **Lưu ý cũ (trước fix)**: `clear()` từng **gần như không chạy** (chỉ lúc boot) →
   `lastRunLoops` sống xuyên run, buffer chỉ ghi đè dần. Nạp firmware **không xoá EEPROM**
   nên bản ghi run cũ + demo initializer `sensor67Value[0]={1,2,..40}` (sensor6035.h:71)
   trông như "dữ liệu test". Sinh ra **hai khe hở**, vẫn chặn ở **hai tầng**:
   - `waitamp` của run mới: `/curve` trả run trước. Đúng cho tab **Result** (máy thật sự
     vẫn giữ run cũ), **sai cho chart live** → client chỉ `loadCurve` khi `phase` là
     `amplification`/`finished` (`curveReady`).
   - **`COUNTER` cũng = 0 ở vòng đầu của run MỚI** (~20s đầu amplification) → fallback sẽ
     ném run cũ vào run vừa bắt đầu. Vì vậy `handleCurve` gác thêm
     **`&& type_infor != eoptoreading`**: đang chạy thì `COUNTER` là sự thật duy nhất,
     0 nghĩa là **chưa có gì**, không phải "lấy run cũ ra".
8. **KHÔNG gọi `WiFi.begin()` trong vòng lặp** — mỗi lần gọi lại vào
   `esp_wifi_set_mode()`/connect, giằng xé WiFi+lwIP stack ⇒ task **`async_tcp`** (phục vụ
   dashboard) kẹt chờ tcpip core lock, ngừng nuôi watchdog ⇒
   `task_wdt: async_tcp (CPU 1)` → **abort → reboot**. `screen_Result` từng gọi 50 lần
   trong 5s (= đúng ngưỡng WDT 5s) và **luôn chạy đủ 50 vòng khi ở SoftAP** (STA không bao
   giờ nối được) ⇒ reboot ở **cuối mỗi run**. Gọi **1 lần** rồi `delay()` chờ (delay nhường
   CPU), và **bỏ qua hẳn khi `dashboardIsAP()`**: ở SoftAP thì STA vô vọng, mà
   `WiFi.begin()` còn phá luôn AP mà browser đang bám. **Bug có sẵn — chỉ lộ khi có
   dashboard để bỏ đói.**
   **Tương tác với GOTCHA 11 (chế độ hỏng MỚI)**: `screen_Result('f')` vẫn còn 1 lần
   `WiFi.begin()` (reconnect khi STA rớt) **trước** khi gọi `postData_GoogleSheet`. Cú
   đó vẫn thrash `async_tcp` y hệt — nhưng vì đã gỡ watchdog (`CONFIG_ASYNC_TCP_USE_WDT=0`)
   nên **không còn abort→reboot**, thay vào đó dashboard **treo câm vĩnh viễn** (server nhận
   TCP nhưng không bao giờ trả lời) tới khi tắt/bật nguồn. `postData` có `dashboardSuspend()`
   nhưng chạy **SAU** khối reconnect → quá muộn. Fix: `dashboardSuspend()` **ngay đầu** khối
   `WiFi.begin()` retry (hạ server trước khi thrash), và `dashboardResume()` ở nhánh
   **không-upload** (`bResultGet`, vì nhánh đó không đi qua `postData` nên không ai resume).
   Lộ khi bấm **"Up Data"** (giữ đỏ → đỏ) lúc STA vừa rớt.
9. **`sensor67Value` là RAW**, không phải calibrated (kể cả sau
   `getDataAmplificationEEPROM()` nạp lại từ EEPROM). Luôn calibrate **1 lần** khi đọc:
   `(raw - origins[i]) / slopes[i]`.
10. **KHÔNG materialise payload lớn trong route handler** — dựng cả `JsonDocument` rồi
   `serializeJson` ra String (AsyncWebServer còn **copy** lần nữa cho response) ngốn
   **~30KB** ở run đầy 120 vòng. Heap thật lúc dashboard chạy: `free≈90KB`,
   **`maxAlloc≈55KB`** → sau `/curve` chỉ còn ~25KB < 32-40KB TLS cần ⇒ **reboot lúc cuối
   run** (đúng lúc `screen_Result` gọi `postData_GoogleSheet`). `handleCurve` vì vậy dùng
   **`beginChunkedResponse` + `CurveWriter`** (sinh JSON từng token qua buffer nhỏ):
   **29 792 B → 1 488 B**. Đo bằng `pio test -e esp32dev_test -f test_webcurve -v`.
   Route nào trả nhiều dữ liệu sau này cũng phải theo khuôn này.
11. **`async_tcp` bị watchdog trong lúc TLS chặn** → reboot cuối mỗi upload. `AsyncTCP`
   tự `esp_task_wdt_add` (mặc định `CONFIG_ASYNC_TCP_USE_WDT=1`), mà `postData_GoogleSheet`
   **cố ý bỏ đói** task đó: `dashboardSuspend()` hạ server rồi chặn DisplayTask trong
   mbedTLS ~30s handshake + ~60s POST (GAS mất 35-40s). Task async ngồi chờ lwIP > 5s
   → `task_wdt: async_tcp` → abort. Fix: `build_flags = -DCONFIG_ASYNC_TCP_USE_WDT=0`
   (gỡ watchdog khỏi task ta CHỦ ĐỘNG park; task khác vẫn được canh). **Ẩn ở SoftAP**
   vì AP không có internet nên upload không chạy. Verify flag tới đúng `AsyncTCP.cpp`:
   `pio run -v` → dòng compile phải có `-DCONFIG_ASYNC_TCP_USE_WDT=0`.
12. **`serveStatic` đăng ký CUỐI CÙNG**: handler thử theo thứ tự đăng ký. Đặt trước API
   → mỗi `/curve`/`/config` tốn 4 lần mở file LittleFS hỏng (spam `vfs_api ... does not
   exist`) rồi mới rơi xuống route; và file trong `data/` trùng tên API sẽ **che khuất**
   route. `/home` sau khi sửa: ~21ms.
13. **`http.getString()` treo VĨNH VIỄN cuối mỗi upload** (máy đơ tới khi tắt nguồn, cả
   auto lẫn "Up Data"). `postData_GoogleSheet` gọi `http.getString()` đọc body response,
   mà `writeToStreamDataBlock` chạy `while(connected() && len==-1){ ... else delay(1); }`
   **không timeout** ở nhánh chưa có data. Response ingest dùng `Connection: close` +
   `useHTTP10` → **không có Content-Length** (`_size=-1`), reverse-proxy giữ socket mở
   **không gửi FIN** → `connected()` mãi true, `available()` mãi 0 → DisplayTask kẹt
   `delay(1)` vô tận. `setTimeout`/`setHandshakeTimeout` **không phủ** vòng này. Fix:
   `readBodyDeadlined(http, 5000)` — tự đọc body với deadline 5s idle, thay `getString()`
   ở **cả 2 chỗ** (ingest + nhánh lỗi GAS). Marker xác nhận: kẹt ngay sau
   `[up] ingest POST begin` (không tới `server feedback:`); sau fix thấy đủ
   `server feedback: {...}` → `POST OK code=200` → dashboard resume.
14. **`dashboardSuspend()` self-deadlock ĐỆ QUY khi có SSE client** (treo vĩnh viễn, chỉ tắt
   nguồn cứu; cắn khi **≥1 browser mở `/events`**). `AsyncEventSource::close()` giữ
   `_client_queue_lock` (`std::mutex`, **không đệ quy**) suốt vòng `c->close()`. `c->close()` →
   `AsyncClient::_close()` gọi `_discard_cb(...)` **ĐỒNG BỘ trên CÙNG task** → SSE `_onDisconnect`
   → `_handleDisconnect()` **khóa LẠI đúng mutex đó** → hang. **`delay()` VÔ DỤNG** (không phải
   ABBA queue-đầy như tưởng ban đầu); bỏ lock thì thành use-after-free. Là **bug thư viện**
   ESPAsyncWebServer fork. Fix của ta: **bỏ hẳn `dashEvents.close()`** trong `dashboardSuspend()`,
   chỉ `dashServer.end()` (chỉ đóng listen pcb — `AsyncServer::end` không đụng client). Marker:
   treo ngay sau `[up] begin` (chưa tới `[up] suspended`). **Rủi ro `-32512`** do giữ SSE socket
   (phân mảnh heap) **nay hết** vì nhả BT sớm cho block liền mạch 68KB (GOTCHA 1/2) — thừa cho
   TLS dù còn SSE socket. Chi tiết:
   [docs/history/2026-07-21-erp-upload-and-bt-early-release-heap-fix.md](docs/history/2026-07-21-erp-upload-and-bt-early-release-heap-fix.md).

## Brand (Forte Biotech)

Logo gốc `src/download.png` → mark tách nền: `data/logo.png` (73×165, 2.3KB, header +
favicon). Màu logo: `#20C6D0` cyan · `#13A2BF` teal đậm · `#21DDBC` mint.

**Không dùng thẳng màu logo cho chữ**: trên nền trắng chúng chỉ đạt contrast
**2.09 / 3.02 / 1.73** (WCAG AA cần 4.5). Đây là thiết bị y tế → dùng token dẫn xuất
**cùng hue 183°**: `--brand-ink #13757A` (chữ trên trắng, 5.45:1) và `--brand-deep #083336`
(nền tối, chữ trắng 13.7:1). `--brand #20C6D0` chỉ cho **accent/icon/mark**.

**Giữ nguyên có chủ ý**: nút Lysis xanh lá / Amplification đỏ (khớp **nút vật lý**),
badge P/N/S/E/B và 10 màu series chart (dữ liệu, cần phân biệt). Chi tiết:
[docs/history/2026-07-17-brand-forte-biotech.md](docs/history/2026-07-17-brand-forte-biotech.md).

## Quy ước

- **Code/UI/comment: tiếng Anh** (an toàn font thiết bị). **Docs (.md): tiếng Việt.**
- **Mỗi thay đổi → 1 file `.md` có ngày trong `docs/history/`**; giữ README/CLAUDE cập nhật.
- Tài liệu ở `docs/` (`docs/history/`, `docs/GUI_SSE/GUI.md`).
- Ưu tiên dùng `codebase-memory` MCP tools để khám phá code (nhanh hơn grep).

## Test

### Không cần phần cứng

```bash
python tools/sse_test_server.py            # mock ESP32 -> http://localhost:8000
python tools/sse_test_server.py --full     # scale THẬT: 120 vòng x 20s = run 40 phút
python tools/sse_test_server.py --reboot   # boot như vừa tắt/bật: run cũ ở EEPROM, RAM trống
python tools/sse_test_server.py selftest   # tự kiểm các hàm thuần
python tools/test_no_runtime_wifi_begin.py # guard: KHÔNG WiFi.begin() runtime ngoài setup()
g++ -O2 -std=c++17 tools/test_readcmd_overflow.cpp -o t && ./t  # readCommand không tràn recvData[2048]
node tools/test_full_run.js                # E2E full quy trình (chạy với --full)
node tools/test_review_reboot.js           # E2E xem lại run sau reboot (tự bật mock --reboot)
```

**`test_no_runtime_wifi_begin.py`** khoá bất biến chống bug "Up Data treo dashboard"
(GOTCHA 8/11): `WiFi.begin()` **chỉ** được phép ở `main.cpp` (boot). Bất kỳ caller runtime
nào (screen_Result, task khác) → deadlock `async_tcp`. Cũng assert `setAutoReconnect(true)`
còn đó (core tự reconnect STA nền → không cần app gọi begin). Thêm lại begin ngoài setup →
test đỏ ngay (host-side, không cần máy). Chi tiết:
[docs/history/2026-07-20-updata-wifi-reconnect-hang.md](docs/history/2026-07-20-updata-wifi-reconnect-hang.md).

### Debug treo dashboard trên máy thật (bán tự động)

```bash
python tools/probe_dashboard_hang.py 192.168.1.10      # poll /home + SSE, in timeline treo/hồi
python tools/probe_dashboard_hang.py 192.168.1.10 60   # tự dừng sau 60s + in tóm tắt
```

Biến "màn treo" thành dữ liệu: poll `/home` + giữ SSE như browser, in **đúng thời điểm**
dashboard ngừng trả lời và có hồi hay không. Chạy nó rồi **kích bug** (bấm "Up Data" lúc
WiFi chập chờn, hoặc rút nguồn AP vài giây). Verdict: *stayed ALIVE* (không treo) /
*DEAD rồi RECOVERED* (transient, đúng với fix gốc rễ) / *DEAD không hồi* (deadlock tái hiện,
phải tắt/bật nguồn = còn bug).

Mock **được web điều khiển** như máy thật: `waitamp` **đứng chờ** tới khi bấm Start (đỏ),
`finished` đứng chờ tới khi bấm White. Nhờ vậy gate đặt tên + chart sau run mới test được.
`--full` giữ **đúng khối lượng dữ liệu và trục X thật** (120 vòng, 20s/vòng → 39.67 phút,
`/curve` ≈ 7.7KB) nhưng nén đồng hồ còn ~20s.

`tools/test_full_run.js` — E2E qua Edge headless + DevTools Protocol (chỉ dùng node
stdlib, poll DOM nên không race SSE). Đi hết: heater → waitamp (Start khoá, đặt tên) →
Confirm → **bấm Start** → amplification đủ 120 vòng → finished (chart ở lại) → Result
(đọc lại) → **bấm White** (chart mới mất).

### Trên máy thật

```bash
pio test -e esp32dev_test -v                        # tất cả
pio test -e esp32dev_test -f test_webcurve -v       # heap của /curve ở scale full run
```

Test **self-contained** (`test_build_src = no`, không link `src/`) — tái tạo pattern thay
vì gọi thẳng. `test_webcurve` dựng dữ liệu 120 vòng rồi **đo heap** của đường serialize
`/curve`, vì nó chạy **cùng lúc** với `postData_GoogleSheet` (TLS cần 32-40KB liền mạch,
GOTCHA 2) ở cuối run → nghi phạm gây reboot lúc 40 phút.
**Lưu ý: `pio test` nạp firmware test đè lên máy** → nạp lại firmware thật sau khi test.
