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

**Lần đầu / máy mới**: `src/secrets.h` (endpoint upload + API token) **gitignored** — thiếu nó
build fail. Copy template rồi điền: `cp src/secrets.example.h src/secrets.h`. Token thật KHÔNG
bao giờ commit (đã lộ trong history cũ → cần rotate server-side; xem
[docs/history/2026-07-22-secrets-out-of-source.md](docs/history/2026-07-22-secrets-out-of-source.md)).

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

3 màn (**bottom nav ở MỌI kích thước** — không còn sidebar desktop): **Home** (nhiệt độ, trạng thái, nút điều khiển,
kèm **naming/chart** khi vào pha amp), **Result** (xem lại: bảng kết quả + chart đã lưu),
**Setting**.

**Ẩn nav khi đang chạy run**: `busy && !calib` → `body.nonav` (ẩn nav + thu hồi
`padding-bottom` dành cho nó — mọi kích thước đều là bottom bar).
Không ẩn khi **calib** — wizard calib nằm trong tab Setting, ẩn nav là nhốt người dùng ở
đó. Run bắt đầu lúc user đang ở tab khác → tự chuyển về Home trước khi ẩn (`applyRunNav`).

Routes: `/` (static), `/events` (SSE), `/control?btn=red|green|white` (bấm nút →
`_buttonManager.postShortPress`; **green** = nút vật lý `B_BLUE`), `/home` (snapshot),
`/slots` (bảng kết quả: `{name, sample, ct, result}` ×10), `/rename?slot=N&name=<bệnh>&sample=<mẫu>`
(`name`→`/slotnames.json`, `sample`→`/slotsamples.json`; **hai trường độc lập**, gửi cái nào áp cái
đó, cap 32 ký tự; xem [docs/history/2026-07-24-slot-sample-name.md](docs/history/2026-07-24-slot-sample-name.md)),
`/curve` (toàn bộ đường cong từ đầu run → backfill),
`POST /reviewlast` (nạp lại run cuối từ EEPROM để xem sau reboot — xem dưới),
`GET /ota` + `POST /ota?action=check|update` (cập nhật firmware — xem dưới).
Kết quả cache qua `dashboardSetResults()` gọi từ `screen_Result()`.

**Xem lại run sau reboot (`POST /reviewlast`)**: cache RAM (`gResultsReady`, `gCT`,
`lastRunLoops`) mất khi tắt máy, nhưng run cuối còn RAW ở `RECORDPOS`. Client mở Result,
thấy `/slots ready=false` (và không đang amp) → `POST /reviewlast`. AsyncTCP chỉ enqueue
`PEND_REVIEW`; **SettingTask** (guard idle) chạy `getDataAmplificationEEPROM()` →
`bResultGet` → `dashboardSetResults` + `setLastRunLoops(len)` để `/curve` phục vụ đúng số
vòng. `len` = **quét record tìm độ dài thật** (vòng cuối cùng slot-0 raw ∈ `10..60000`),
**KHÔNG** tin `amplification_time` (config đổi giữa run+review → đọc rác/cụt; xem
[docs/history/2026-07-22-curve-length-scan.md](docs/history/2026-07-22-curve-length-scan.md)). Heuristic `10 < sensor67Value[0][0] < 60000` bỏ record rỗng
(EEPROM chưa ghi = `0xFFFF`) → máy mới hiện trống, không rác. **Không** đổi `type_infor`
(khác `resultOutput()`/`escreenReview` là bản Serial cướp màn TFT). Test:
`node tools/test_review_reboot.js` (mock `--reboot`).

**Review mất ~8 GIÂY, không phải ~10ms**: đo trên máy thật `POST /reviewlast` → `/slots ready`
= **8101 ms** (đọc record EEPROM + `bResultGet` chạy lại thuật toán trên cả 10 slot). Client
`reviewStoredRun` vì vậy poll **60 × 250ms = 15s**; ngân sách cũ 15×200ms = 3s **hết giờ trước
khi data về** → bảng/chart trống tới khi reload trang (bug "View Chart lần đầu không xem được").
`loadCurve` hiện `chart.showLoading()` khi `/curve` còn 0 điểm mà đang `reviewing`. Chi tiết:
[docs/history/2026-07-22-review-poll-timeout-chart-empty.md](docs/history/2026-07-22-review-poll-timeout-chart-empty.md).

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
- **Ô "tên mẫu" (free-text) nằm CẠNH dropdown bệnh** trong cả bảng naming lẫn Result: input
  `.sample-name` (class **riêng**, KHÔNG `.slot-name`, để `fitNameColumn` chỉ đo chữ bệnh),
  lưu song song `/slotsamples.json` qua `/rename?...&sample=`. `.sample-cell` là `flex-wrap`:
  cạnh nhau khi cột rộng, wrap xuống dòng khi hẹp (bảng Result mobile). **Chart legend vẫn = tên
  bệnh** (`applyNamesTo` dùng `slotNames`); tên mẫu là label per-slot, **web-only** (không lên
  upload/TFT). Chi tiết: [docs/history/2026-07-24-slot-sample-name.md](docs/history/2026-07-24-slot-sample-name.md).

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
`count` = **`getCurrentLoop()` CHỈ khi `type_infor == eoptoreading`** (đang đo, `COUNTER` là
sự thật; = 0 ở ~20s đầu → chart mở trống, đúng), **ngược lại `getLastRunLoops()`** (idle/
preheat/maintain/finished/review). Vì `COUNTER` **dùng chung cho cả đếm vòng preheat**
(`eSensorPreheat`, tăng sau boot tới `PREHEATLOOPS` rồi maintain giữ nguyên) nên tin nó ngoài
`eoptoreading` khiến `/curve` báo ~15 vòng preheat lúc idle **và cắt cụt run review** còn 15
điểm. Chi tiết: [docs/history/2026-07-22-curve-counter-preheat.md](docs/history/2026-07-22-curve-counter-preheat.md).

`GET /slots` trả `ready = gResultsReady && type_infor != eoptoreading`: đang chạy run mới
thì **giấu kết quả cache của run cũ** (máy chỉ giữ 1 run) → bảng và chart trên Result luôn
cùng một run.

## Tab Setting (8 card)

WiFi · Device ID · Test profile · LED · Calib · PID/heater · Other · **Firmware (OTA)**. Master-detail: lưới
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
   **Nhưng dồn về SettingTask KHÔNG đủ**: `error.saveErrorToEEPROM()` chạy từ **ControlTask**
   (~17 đường PID safety, mỗi 100ms) và run-end write từ **SensorTask** — cùng chạm buffer
   4096B đó. Vì vậy **mọi** `EEPROM.begin..end` runtime nay bọc **`eepromLock()/eepromUnlock()`**
   (mutex `gEepromMutex`, `define.h`, tạo cạnh `gI2CMutex` **trước** `xTaskCreate`). Đây là
   fix cho bug `/reviewlast` fail lúc cold-boot (error-save của ControlTask double-free buffer
   giữa `getDataAmplificationEEPROM` → probe đọc rác). Section **không được lồng** (plain mutex).
   Chi tiết: [docs/history/2026-07-22-eeprom-mutex-reviewlast-race.md](docs/history/2026-07-22-eeprom-mutex-reviewlast-race.md).
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

**Cập nhật firmware từ web (`/ota`)**: `GET /ota` trả `{version, versionCode, state, hasUpdate,
busy, checked, online, newVersion, notes}`; `POST /ota?action=check|update`. **Không đổi cơ chế
OTA** — `checkFirmware()` vẫn đọc `updateOTA.json` trên GitHub, `updateFirmware()` vẫn chạy ở
NetworkTask. Chỉ thêm đường vào:

- **`check` phải qua `PEND_OTACHECK` (SettingTask)** — HTTPS **blocking** vài giây, chạy trên
  AsyncTCP là treo dashboard (GOTCHA 8/11). `drainPending()` vốn guard `dashboardDeviceBusy()`
  nên tự động không hỏi GitHub giữa run.
- **`update` thì AsyncTCP ghi thẳng `otaState = OTA_USER_ACCEPTED`** — *một byte volatile*,
  NetworkTask tự nhặt; việc tải không đụng task web.
- `checkFirmware(bool promptOnDevice = true)`: web gọi **`false`** để **không cướp màn TFT**
  (bấm từ xa mà chiếm màn là bỏ rơi người đang đứng ở máy). `otaLastCheck` phân biệt
  **"chưa kiểm tra"** với **"đã kiểm tra, không có bản mới"** — `OTA_IDLE` gộp cả hai.
- Cả hai POST **409** khi busy hoặc mất internet: OTA kết thúc bằng `ESP.restart()`.
- Panel **không đợi "cài xong"**: thành công = reboot, trang chỉ mất kết nối rồi tự nối lại.

**Nạp bằng file .bin** (`POST /otaupload`, multipart field `firmware`) — cho máy **không có
internet** hoặc build chưa publish. `onUpload` nhận body **theo chunk ~1-4KB** → `Update.write()`
từng khúc (không giữ 2,3MB ở đâu). Ba điểm bắt buộc: chunk **đầu** (`index==0`) là chỗ duy nhất
từ chối được (busy / không phải `.bin`); bị từ chối thì **vẫn phải đọc hết** các chunk sau (không
rút cạn socket → browser thấy connection reset thay vì lỗi); và **hoãn `ESP.restart()`**
(`otaRestartAt`, `dashboardLoop()` lo) — restart trong handler làm mất reply 200 nên bản cập nhật
thành công lại báo lỗi mạng. UI dùng **XHR** (chỉ XHR có upload progress).

Chi tiết: [docs/history/2026-07-24-web-ota-update.md](docs/history/2026-07-24-web-ota-update.md).

**QR vào dashboard**: nút **TRẮNG ở màn chính** (`escreenStart`, nút này vốn không làm gì) →
state `eShowQR` → `screen_QR()` (displayLCD.cpp) vẽ QR bằng `ricmoo/QRCode` (version 3, ECC_LOW,
**tối trên nền sáng + quiet zone 4 module** — thiếu là máy quét không đọc được). Nội dung theo
chế độ: **STA** → `http://<ip>/`; **SoftAP** → `WIFI:T:nopass;S:RAPID-<id>;;` (AP mở) rồi
**captive portal** (`DNSServer` + `onNotFound` redirect, chỉ khi `apActive`) tự bật dashboard.
`dnsServer.processNextRequest()` phải nằm **trên throttle 1s** của `dashboardLoop`. `eShowQR`
nằm trong allowlist `isBusy()` (xem QR không phải "bận"). Chi tiết:
[docs/history/2026-07-22-qr-dashboard-access.md](docs/history/2026-07-22-qr-dashboard-access.md).

SoftAP fallback: STA fail → `dashboardStartAP()` phát `RAPID-<id>` (192.168.4.1).
Log heap mỗi 10s: `[dash] heap free=.. maxAlloc=.. clients=.. ap=..`.

**Tên miền cố định (`http://<id>.local/`)**: DHCP đổi IP → dùng **mDNS + `setHostname`**
(cả hai có sẵn trong ESP32 core, 0 lib_deps). `dashboardHostname()` sinh DNS-safe label từ
`id_device` (lowercase, chỉ `[a-z0-9-]`, cắt `-` đầu, fallback `"rapid"`). `main.cpp`
`WiFi.setHostname(...)` **sau `WiFi.mode(WIFI_STA)`, trước `WiFi.begin()`** (thứ tự bắt buộc);
`dashboardBegin()` `MDNS.begin(...)` + `addService("http","tcp",80)` **chỉ khi STA**
(`!apActive`, SoftAP đã có 192.168.4.1 + captive portal) và **1 lần** (`static bool mdnsUp` —
dashboardBegin chạy lại sau mỗi suspend/resume upload). `.local` chỉ trong cùng LAN. Đo thật:
`[dash] mDNS up -> http://rpl.local/` → Windows phân giải `/home` OK. Chi tiết:
[docs/history/2026-07-24-mdns-stable-hostname.md](docs/history/2026-07-24-mdns-stable-hostname.md).

**Nhiều client cùng xem — đo thật (board RPL, STA)**: mỗi SSE client tốn **~660 B**
(free 87 528 → 82 184 với 8 client) và **`intLargest` KHÔNG đổi (51 188 suốt)** → client
**không phân mảnh** block liền mạch, nên ngưỡng TLS 42KB (GOTCHA 2) vẫn an toàn khi đông
người xem. `/home` 19→34 ms, `/curve` 89 ms ở 8 client; đóng hết thì heap về đúng 87 528
(không rò rỉ). **Giới hạn người xem = `MAX_VIEWERS` (2), ép ở HAI tầng** vì không tầng nào phủ cả 2 chế độ:

1. **SoftAP**: `WiFi.softAP(ap, NULL, 1, 0, MAX_VIEWERS)` (mặc định core là 4) — thiết bị thứ 3
   bị từ chối **lúc associate**, không lấy được IP.
2. **STA** (`max_connection` vô nghĩa, cả LAN vào được): **`ViewerCapHandler`** —
   `AsyncWebHandler` tự viết, `addHandler` **TRƯỚC `dashEvents`**, chỉ nhận `/events` **khi đã
   đủ người** (`isSSE() && url=="/events" && dashEvents.count() >= MAX_VIEWERS`) → trả **503**;
   chưa đủ thì `canHandle` false → rơi xuống SSE handler thật.

Đây là chính sách *ai được xem*, **không phải** giới hạn heap. Lưu ý **1 tab = 1 client**.
Người bị từ chối **vẫn xem được trang** (static + `/home` + `/curve`), chỉ không có live; và
`EventSource` của browser **tự retry** nên có người rời là vào được ngay — cũng nhờ vậy mà
reload trang không bị kẹt slot. Đo thật: 2 người → #3 nhận `503 Too many viewers`, sau khi
2 người rời thì #3 vào lại `200`.

**Hai cái bẫy khi làm giới hạn này** (đừng lặp lại):

- **KHÔNG dùng `dashEvents.onConnect(...)` + `client->close()`** (idiom của thư viện) →
  **self-deadlock treo máy**: `_addClient()` gọi `_connectcb` **trong khi giữ**
  `_client_queue_lock`, mà `close()` re-enter đúng mutex không đệ quy đó qua
  `_handleDisconnect` — đúng hình dạng GOTCHA 14. `count()` cũng lấy lock đó → **chỉ được gọi
  ngoài callback**.
- **`dashServer.on("/events", ...).setFilter(...)` KHÔNG chạy**:
  `AsyncCallbackWebHandler::canHandle` mở đầu bằng `!request->isHTTP() → return false`, mà
  request SSE là `RCT_EVENT` chứ không phải HTTP → handler không bao giờ thấy `/events`
  (đo được: client #3 vẫn `200`). `AsyncEventSource::canHandle` lại là `final` nên cũng không
  subclass được → phải tự viết `AsyncWebHandler`. `curl` không thấy `/events` (rơi 404 xuống `onNotFound`) vì `canHandle` đòi
`request->isSSE()` = header `Accept: text/event-stream` — browser luôn gửi, curl thì phải
thêm tay; **đây không phải lỗi**.

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
   **ĐÃ XÓA HẲN `WiFi.begin()` khỏi `screen_Result` (Option C, 2026-07-22)**: trước đây
   `screen_Result('f')` còn 1 lần `WiFi.begin()` reconnect trước `postData`; nó thrash
   `async_tcp`, và vì đã gỡ watchdog (`CONFIG_ASYNC_TCP_USE_WDT=0`) nên thay vì reboot thì
   dashboard **treo câm vĩnh viễn** (ping được, HTTP không trả lời, chỉ reset cứu) =
   triệu chứng "máy bật mà không vào web". Band-aid `dashboardSuspend()` trước `WiFi.begin`
   **chưa đủ** (còn khe residual → treo lại). Fix gốc: **bỏ `WiFi.begin()`**, để core lo
   reconnect (`WiFi.setAutoReconnect(true)`, main.cpp), thay bằng **chờ thụ động** bounded
   (`for(i<50 && key=='f' && !AP && !CONNECTED) delay(100)` — không begin, không suspend,
   dashboard phục vụ suốt). Guard `tools/test_no_runtime_wifi_begin.py` khoá bất biến này.
   Chi tiết: [docs/history/2026-07-22-option-c-remove-wifi-begin.md](docs/history/2026-07-22-option-c-remove-wifi-begin.md).
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

Logo gốc `src/download.png` (259×194, nền trắng) → **logo ĐẦY ĐỦ tách nền**:
`data/logo.png` (208×178, 6.9KB, header + favicon). Màu logo: `#20C6D0` cyan ·
`#13A2BF` teal đậm · `#21DDBC` mint.

**Lịch sử (2026-07-22)**: bản cũ là *mark tách nền* 73×165 — nhưng nó chỉ cắt được **cụm
tam giác đầu** (`x 24..96` của ảnh gốc), **mất** cụm tam giác bên phải → logo hiển thị cụt.
Vì chữ nằm xen giữa các tam giác nên không crop mark "sạch" được; nay dùng **nguyên logo**,
xoá nền trắng bằng ngưỡng luminance có dốc mềm (không để viền trắng trên header tối), phóng
`.brand-mark img` **40px → 56px** cho chữ trong logo đọc được, và **ẩn `.company-name`**
(logo đã có chữ "FORTE BIOTECH" → trước đó header in tên công ty **hai lần**). Element
`.company-name` vẫn giữ trong DOM vì `script.js` còn đổ dữ liệu `company` từ SSE vào đó.

**Không dùng thẳng màu logo cho chữ**: trên nền trắng chúng chỉ đạt contrast
**2.09 / 3.02 / 1.73** (WCAG AA cần 4.5). Đây là thiết bị y tế → dùng token dẫn xuất
**cùng hue 183°**: `--brand-ink #13757A` (chữ trên trắng, 5.45:1) và `--brand-deep #083336`
(nền tối, chữ trắng 13.7:1). `--brand #20C6D0` chỉ cho **accent/icon/mark**.

**Giữ nguyên có chủ ý**: nút Lysis xanh lá / Amplification đỏ (khớp **nút vật lý**), mã màu
badge P/N/S/E/B (khớp màu trên TFT) và 10 màu series chart (dữ liệu, cần phân biệt). Chi tiết:
[docs/history/2026-07-17-brand-forte-biotech.md](docs/history/2026-07-17-brand-forte-biotech.md).

**Soát contrast/a11y 2026-07-23** (18/30 cặp màu từng dưới AA → nay **toàn bộ đạt**, đo bằng
script hợp nhất alpha xuống nền thật):

- **Làm mờ bằng `grayscale`, KHÔNG bằng `opacity`.** `opacity` nhạt **cả chữ lẫn nền** nên tỷ lệ
  sập hai phía: `.noact` ở .45 đo được **1.26:1** (vô hình). `grayscale()` **bảo toàn luminance**
  → `.noact` = opacity .85 + grayscale .65 = **4.68:1**. Đừng quay lại opacity.
- **Nút `.on` (sáng khi nhấn) không làm sáng nền** — nền sáng từng kéo trắng-trên-đỏ xuống 3.11
  và trắng-trên-xanh xuống **2.28**, tệ nhất đúng lúc người dùng nhìn nút vừa bấm. Báo hiệu bằng
  **ring + glow**.
- **Badge P/N/S/E/B**: giữ hue, chỉ đậm chữ (3.66–4.38 → **5.01–5.09**). Nghĩa các chữ cái lấy từ
  `src/Alg/AlgoData.h:40-43` + `sensor6035.cpp:329`: **S = Slight Positive** (không phải Suspect),
  **B = Break** (không phải Blank) — `result[i]` là **ký tự đầu của chuỗi outcome**. Legend đã có
  trong tab Result.
- `:focus-visible` phải đặt **cuối file** (nhiều input `outline:none` ở `:focus` cùng độ đặc hiệu,
  chỉ thua rule đứng sau). Có `aria-live` ở 5 vùng tự cập nhật, `prefers-reduced-motion`,
  checkbox 24px.
- **Nav là bottom bar ở MỌI kích thước** (2026-07-23, trước đó desktop biến nó thành sidebar
  224px). Một pattern điều hướng duy nhất cho cả điện thoại lẫn máy bàn, và gỡ luôn một lớp bug:
  sidebar cao `100vh` trong khi trang cuộn dài hơn nên **cụt giữa trang**. Desktop chỉ nới
  `.bottom-nav { max-width: 620px }` cho đỡ lọt thỏm. Đo bằng probe: 390/844/1280/1600 px đều
  `gap-to-bottom = 0`, `flex-direction: row`. Kéo theo: header hết cần `margin-left` âm, `--side-w`
  bị xoá hẳn, chart Result trừ thêm `var(--nav-h)` khi tính chiều cao.
- **Chiều cao chart = cửa sổ trình duyệt, MỘT công thức cho cả Home lẫn Result, mọi kích thước**:
  `.chart-container { height: calc(100dvh - var(--header-h) - var(--nav-h) - 6rem); min-height: 210px }`
  (dòng `100vh` đứng trước làm fallback). Trước đó là 4 giá trị cứng rời rạc (320 / 210 / 460 /
  `aspect-ratio 16/9`) nên desktop 1280×860 **thừa 256px trống** dưới chart. Hai token phải theo:
  `body.nonav` đặt **`--nav-h: 0px`** (chạy run thì nav ẩn, không trừ dải không còn tồn tại) và
  media landscape đặt **`--header-h: 56px`** (header thu nhỏ ở đó). `script.js` không đụng gì —
  Highcharts tự reflow, `makeChart()` không set `chart.height`. Chi tiết:
  [docs/history/2026-07-26-chart-height-viewport.md](docs/history/2026-07-26-chart-height-viewport.md).
- **Mobile + chart đang lên → ẩn bảng slot ở Home**: `@media (max-width: 819px), (max-height: 599px)`
  → `#screen-home.active:has(#homeChartCard:not(.hide)) #namingCard { display: none }`. Trên mobile
  bảng nằm **trên** chart và chiếm ~600px, phải cuộn qua nó mới thấy đường cong — trong khi lúc đó nó
  chỉ còn là legend (Highcharts đã tự in legend `#1`–`#10` dưới chart). **Chỉ ẩn khi chart lên, KHÔNG
  ẩn lúc đặt tên** — cùng card `#namingCard` phục vụ hai giai đoạn, ẩn cả hai là chặn luôn đường đặt
  tên bằng điện thoại quét QR. Media query là **phần bù chính xác** của breakpoint desktop (dấu phẩy,
  không phải `and`) để điện thoại xoay ngang cũng tính là mobile. Desktop giữ nguyên (bảng ở cột trái,
  không đè chart). Chi tiết:
  [docs/history/2026-07-26-mobile-hide-slot-table-under-chart.md](docs/history/2026-07-26-mobile-hide-slot-table-under-chart.md).
- **Breakpoint desktop phải xét CẢ HAI chiều**: `@media (min-width: 820px) and (min-height: 600px)`.
  Điện thoại **xoay ngang** rộng 844–915px nhưng chỉ cao ~390px — nếu chỉ gác `min-width` thì nó
  ăn layout desktop (lưới 2 cột + chart cao) dù chỉ cao ~390px. *(Bẫy sidebar-cụt-giữa-trang đã
  biến mất cùng sidebar, nhưng breakpoint vẫn phải gác cả hai chiều.)* Có media riêng cho **rộng-mà-thấp**
  (`min-width: 700px` + `max-height: 599px`): giữ bottom nav, `--maxw` 700px, header 56px
  (nhớ hạ luôn `--header-h`, chart tính theo nó). `min-width` **không** đồng nghĩa "màn hình lớn".

**Bảng slot = 3 cột** (`Sample | CT | Result`, trước là `Show|Slot|Disease|CT|Result`): cột Sample
gộp **chấm màu + số slot + select bệnh**. Chấm chính là **checkbox thật** (giữ bàn phím/screen
reader/`onToggle`) được style thành **màu series của slot đó trên chart** (`--series` set theo hàng;
`SERIES_COLORS` tách khỏi `buildSeries()`). Hai bẫy: **không** đặt `display:flex` thẳng lên `<td>`
(mất vai trò table-cell → viền hàng lệch; flex phải ở `div.sample-cell` bên trong), và cột CT/Result
để **3.2rem trên mobile** (5.5rem khiến cột Sample còn 110px → select 35px → **vùng chữ 2px**, mất
hẳn nội dung; desktop mới trả về 5.5rem). Hàng có `P`/`S` được `tr.hit` (nền `#fff7f5`, CT đậm) —
**sắc nền chỉ dẫn mắt**, nghĩa vẫn nằm ở chữ cái badge.

Chi tiết: [docs/history/2026-07-23-ui-contrast-a11y-review.md](docs/history/2026-07-23-ui-contrast-a11y-review.md).

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
g++ -O2 -std=c++17 tools/test_curve_length.cpp -o t && ./t      # /reviewlast quét đúng độ dài run (không tin amplification_time)
g++ -O2 -std=c++17 tools/test_wifi_store.cpp -o t && ./t        # saved-WiFi list: newest-to-front, no dup, cap 5, remove
node tools/test_wifi_e2e.js                 # E2E WiFi qua browser: list/pick/connect/forget/sai-mật-khẩu (tự bật mock)
python tools/test_no_method_branch.py       # guard: KHÔNG handler nào so req->method() (GOTCHA 3 làm POST rơi nhánh GET)
node tools/test_full_run.js                # E2E full quy trình (chạy với --full)
node tools/test_review_reboot.js           # E2E xem lại run sau reboot (tự bật mock --reboot)
node tools/ui_screenshot.js <outDir>       # chụp 9 trạng thái UI (mobile/landscape/desktop) để soát thiết kế
node tools/test_chart_ticks.js             # guard: trục Y chart LUÔN đúng 10 nấc, sàn 50
```

**`test_chart_ticks.js`** khoá bất biến trục tung: **luôn đúng 10 nấc**, sàn **50**, và **giãn
theo dữ liệu chứ không cắt**. Chạy trên **chart thật** (đẩy data vào `Highcharts.charts` rồi đọc
`yAxis[0].tickPositions`) chứ không chép lại công thức — bản chép sẽ pass trong khi bản chạy thật
đã hỏng. 11 mức dữ liệu: rỗng → 0.4 → 49.9 → 50 → 57.3 → … → 1234.

**`ui_screenshot.js`**: cờ `--screenshot` của Edge **treo vĩnh viễn** ở đây (trang giữ SSE mở nên
`--virtual-time-budget` không bao giờ hết) → phải qua CDP. Script **tắt cache**
(`Network.setCacheDisabled`), thiếu bước này browser phục vụ lại `style.css` cũ và ta soát nhầm
bản trước khi sửa. Chuyển tab bằng cách **click nav thật** (`show()` là hàm module-scope).

**`test_no_runtime_wifi_begin.py`** khoá bất biến chống bug "Up Data treo dashboard"
(GOTCHA 8/11): `WiFi.begin()` **chỉ** được phép ở `main.cpp` (boot). Bất kỳ caller runtime
nào (screen_Result, task khác) → deadlock `async_tcp`. Cũng assert `setAutoReconnect(true)`
còn đó (core tự reconnect STA nền → không cần app gọi begin). Thêm lại begin ngoài setup →
test đỏ ngay (host-side, không cần máy). Chi tiết:
[docs/history/2026-07-20-updata-wifi-reconnect-hang.md](docs/history/2026-07-20-updata-wifi-reconnect-hang.md).

**Lưu nhiều WiFi** (`wifiStore.cpp`, **NVS/Preferences** namespace `wifinets` — KHÔNG LittleFS:
`uploadfs` reflash cả partition `spiffs` từ `data/` → file runtime bị XÓA mỗi lần nạp UI. NVS ở
partition riêng `uploadfs` không đụng → **sống sót qua nạp**. Cũng không dùng buffer-4096 EEPROM). `setup()`
→ `connectSavedNetworks()` thử **ưu tiên (EEPROM) → từng mạng đã lưu**, thoát ngay khi nối được;
**mọi `WiFi.begin()` vẫn ở main.cpp** (bất biến trên). Đổi mạng **CHỈ lúc boot** (không begin
runtime, không tự reboot): mất mạng đang dùng thì `setAutoReconnect` nối lại đúng SSID cũ; sang
mạng khác phải power-cycle. Routes `GET /wifilist` (list, chỉ SSID), `POST /wifilist?remove=|connect=` (xóa / chuyển sang mạng
đã lưu — `connect` tra pass trên máy qua `wifiStoreGetPass` rồi `postWifiCreds`+reboot, KHÔNG lộ mật
khẩu). **Sai mật khẩu KHÔNG ghi đè mạng cũ** (trial-then-commit): Save/Connect `wifiStoreSetTrial` rồi
reboot; `setup()` thử trial trước, **nối được mới cam kết** (EEPROM + list), thất bại thì giữ mạng
cũ + `/wifilist` trả `trial:failed` → web báo "wrong password, re-enter". Kiểm mật khẩu buộc phải
reboot (begin runtime treo async_tcp). Web chặn SSID-không-có-trong-scan trước reboot. Forget dùng
`fetch cache:"no-store"` (tránh GET cache cũ làm mạng "xóa rồi" hiện lại). TFT
start screen: `refreshStartWifiLine()` hiện `Scanning...`/`0.0.0.0`/IP theo trạng thái.
Guards: `g++ tools/test_wifi_store.cpp` (contract list), `test_no_runtime_wifi_begin.py` (begin ở
boot). Chi tiết: [docs/history/2026-07-24-saved-wifi-networks.md](docs/history/2026-07-24-saved-wifi-networks.md).

**Đồng bộ web↔máy** (bug "máy đổi WiFi mà web không đổi"): hai gốc rễ. (1) **GOTCHA 3 routing** —
`/wifilist` + `/ota` phải là **`HTTP_ANY` một handler, dispatch theo `hasParam(...)` KHÔNG theo
`req->method()`**; trong build này symbol `HTTP_POST` (WebServer.h, tuần tự) ≠ `request->method()`
(AsyncWebServer, bit-flag) nên so **luôn sai** → mọi POST rơi nhánh GET → forget/connect/OTA
**không chạy** (guard `test_no_method_branch.py`). (2) **Field mạng live** — `buildHomeJson` thêm
`net{ap,ssid,ip}` đẩy qua SSE `home` 1s/lần; `wifiStoreListJson` thêm `current` + per-net `active`
→ panel gắn nhãn "connected" đúng mạng máy đang nối. Đổi subnet thì browser mất tầm với — không
tránh được. Xác nhận máy thật: `POST /wifilist remove=X` → `{ok:true}`, X biến mất. Chi tiết:
[docs/history/2026-07-24-wifi-web-machine-sync.md](docs/history/2026-07-24-wifi-web-machine-sync.md).

**Panel mạng đã lưu — CHỈ nhãn "connected", và nút Connect gate theo nó** (2026-07-27): badge
`preferred` cũ suy từ **vị trí list**, không phải cặp EEPROM, nên sau fallback boot
(`main.cpp:255-263` nối `nets[i]` mà **không** ghi EEPROM/đổi thứ tự) nó dán nhãn lên hàng máy
không hề nối. Nút Connect cũng từng gate bằng `i !== 0` → hàng 0 là preferred-nhưng-không-connected
mà **mất luôn nút để quay về**. Nay: bỏ badge `preferred`, ẩn Connect ở **đúng hàng đang nối**.
Hai bẫy kèm theo: (1) **một nguồn sự thật mỗi lần render** — `live = d.current` (tươi),
`curNet` (SSE, trễ 1s) chỉ fallback; quyết định từng hàng bằng `active || curNet khớp` làm **hai
hàng cùng "connected"**; (2) `.wifi-badge-on` phải nằm **SAU** `.wifi-badge` trong CSS (cùng 1
class = cùng độ đặc hiệu, rule sau thắng) — xếp trước thì badge **không bao giờ xanh**. Cơ chế
EEPROM preferred **giữ nguyên** (portal `Wifi_Connect` chỉ ghi EEPROM, không ghi list NVS). Mock
nay có `_wifi_live()` dùng chung cho `/home` + `/wifilist` và hook `POST /wifilist?current=` để
tái hiện trạng thái fallback. Chi tiết:
[docs/history/2026-07-27-wifi-connected-badge-and-connect-gate.md](docs/history/2026-07-27-wifi-connected-badge-and-connect-gate.md).

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
pio test -e esp32dev_test -f test_endrun_upload -v  # KHÂU CUỐI: run 40' + dashboard + upload
```

**`test_endrun_upload`** khoá đúng chỗ máy chết ngoài thực địa: hết run 40 phút, có WiFi, có
người xem dashboard, giờ phải upload. Không cần mạng — dựng payload **y như**
`postData_GoogleSheet` (10 slot × 120 vòng) rồi đo bộ nhớ. **Free heap không phải con số quyết
định**: mbedTLS xin buffer bằng **một** `malloc` liền mạch. Đo thật 2026-07-27: free ~68KB mà
upload vẫn chết `-0x0010 BIGNUM`; `intLargest` 65 524 lúc rảnh → **49 140** suốt cửa sổ upload,
chênh **đúng 16 384 = 2^14**, giữ từ trước handshake đầu tới sau handshake cuối. Handshake từng
**chạy được ở 63 476** và **chết ở 49 140** → nhu cầu thật nằm giữa hai mốc đó.

**AI GIỮ 16 384 B THÌ VẪN CHƯA BIẾT.** Nghi phạm đầu là payload (tưởng `String` nối dần sẽ
nhân đôi lên 16KB) — **chính test này bác bỏ**: payload tốn **8 656 B** cho chuỗi 8 626 B,
**có hay không có `reserve` đều thế**, tức `String` của ESP32 không nhân đôi. Chỗ tiếp theo cần
soi là pha tính kết quả `bResultPutToGoogleSheet`.

Vì vậy test chỉ khoá **phần đã chắc chắn**: payload luôn đúng cỡ của nó, `JsonDocument` đã được
giải phóng **trước** TLS, và phần liền mạch còn lại vẫn trên mốc 49 140. **Lưu ý phương pháp**:
test đo **kích thước cấp phát** chứ không đo khối liền mạch, vì firmware test không link `src/`
nên không có WiFi/AsyncWebServer — nó boot với ~110KB liền mạch và ~313KB free rải nhiều vùng,
payload rơi vào vùng khác và khối lớn **không nhúc nhích** (đo được `cost = 0`, mọi assertion
xanh trong khi máy thật chết). Cấp ballast để giả lập heap thiết bị cũng hỏng: nó bỏ đói
ArduinoJson và **cắt cụt payload** (8 626 B → 6 386 B).

Test **self-contained** (`test_build_src = no`, không link `src/`) — tái tạo pattern thay
vì gọi thẳng. `test_webcurve` dựng dữ liệu 120 vòng rồi **đo heap** của đường serialize
`/curve`, vì nó chạy **cùng lúc** với `postData_GoogleSheet` (TLS cần 32-40KB liền mạch,
GOTCHA 2) ở cuối run → nghi phạm gây reboot lúc 40 phút.
**Lưu ý: `pio test` nạp firmware test đè lên máy** → nạp lại firmware thật sau khi test.
