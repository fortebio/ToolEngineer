# CLAUDE.md

Hướng dẫn cho Claude khi làm việc trong repo này. (Docs tiếng Việt; code/UI/comment tiếng Anh.)

**Kiến trúc chi tiết + sơ đồ:** [docs/architecture/](docs/architecture/) (state machine, nhiệt/sensor, dashboard, mạng/upload).

## Quy tắc làm việc (BẮT BUỘC — đọc trước khi sửa bất cứ thứ gì)

Yêu cầu của Kane (FBT Engineer), 21/08/2026, sau khi nhận v2.4.3AT: *"source được giao mà
không có log, không có lịch sử sửa đổi"*. Nguyên văn cách anh muốn làm việc:

> *"Giúp tôi giữ lịch sử và ghi tài liệu cho từng tính năng trong thư mục docs mỗi khi
> phát triển một chức năng mới."*

Đây không phải chuyện gọn gàng. Firmware này gọi kết quả y tế; sáu tháng nữa phải trả lời
được *tại sao giếng này ra Positive* mà không cần hỏi người viết. Và người merge phải
review được từng phần, không phải một khối 20 file.

1. **Mỗi chức năng mới / mỗi thay đổi hành vi → một file
   `docs/history/YYYY-MM-DD-slug.md`, viết NGAY trong cùng lần thay đổi đó** — không dồn
   tới lúc bàn giao. Nội dung: trước thế nào, nay thế nào, **bằng chứng nào** dẫn tới, đã
   loại bỏ phương án nào. Tiếng Việt (code/comment tiếng Anh). **Rồi link nó vào CLAUDE.md**
   — một tài liệu không có đường dẫn tới thì coi như không tồn tại; đúng lỗi đã xảy ra với
   4 tài liệu của chính bản v2.4.3AT.

2. **Một commit cho một thay đổi.** Không gộp nhiều ngày, không gộp nhiều chủ đề.
   `7243cff` gộp 20 file / 2 242 dòng: nay không tách được phần di trú EEPROM khỏi phần
   thuật toán, không bisect được cả hai.

3. **Subject commit trả lời *tại sao*; diff đã nói *cái gì*.** Không `up`, `final`, `fix`,
   `wip`.

4. **Không bàn giao source ngoài git.** Bàn giao = tên branch + commit hash + sha256 của
   `.bin`, kèm `.elf`. Gửi file zip hoặc `.bin` rời là cách đã tạo ra chính vấn đề này —
   và `.elf` bị ghi đè là lý do backtrace của máy reset ở Vietnam (17/08) không giải mã được.

5. **Trước khi giao cho ai: `python tools/check.py` phải xanh.**

Bàn giao: **v2.4.5AT so với v2.4.4** → [docs/BAN_GIAO_v2.4.5AT.md](docs/BAN_GIAO_v2.4.5AT.md)
(nhánh `v2.4.5at`, HEAD `d7775b1`; bản đồ 10 commit / 26 file code, 7 việc còn nợ — trong đó
`v2.4.5at` **thiếu 10 commit guard của `v2.4.5`**). Bản trước: [docs/BAN_GIAO_v2.4.3AT.md](docs/BAN_GIAO_v2.4.3AT.md).

⚠ **Đo 2026-09-07: 12/40 guard và 10/52 link tài liệu viện dẫn trong chính file này KHÔNG TỒN TẠI** (chưa
từng có trong git history, mọi nhánh) — tức ~25% lớp bảo vệ mà tài liệu tuyên bố là thật sự chạy, và
`check.py` phát hiện được 8 cái nhưng vẫn **exit 0**. Danh sách đầy đủ + kế hoạch đóng khoảng cách (CI
GitHub Actions, `.claude/skills/`, harness gtest host-side):
[docs/plan/2026-09-07-rang-buoc-phat-trien-firmware-voi-claude.md](docs/plan/2026-09-07-rang-buoc-phat-trien-firmware-voi-claude.md).

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
pio run -e esp32dev -t uploadfs     # chỉ data/ vào LittleFS (KHÔNG còn cần cho UI - xem GOTCHA 4)
pio device monitor -b 115200        # Serial
pio test -e esp32dev_test -v        # unit test on-device (FreeRTOS)
```

Board `esp32dev`, framework arduino, filesystem **littlefs**, flash 8MB.

**Lần đầu / máy mới**: `src/secrets.h` (endpoint upload + API token) **được commit trong repo**
(dòng `src/secrets.h` trong `.gitignore` đang cố ý bị comment) → clone về là build được ngay,
**không** phải copy `secrets.example.h`. Đây là **quyết định có chủ ý, chốt 2026-08-07**, đánh đổi
được vì repo firmware `wuanpham/FBT-DXD` là **private**. `Bluetooth.cpp:22-27` lấy endpoint/token
**từ macro `SECRET_*`** (trước đó hardcode literal, `secrets.h` là code chết).

⚠ **Repo private KHÔNG gỡ được yêu cầu rotate trước khi phát hành OTA.** Đường rò là đường khác:
`firmware.bin` mang ERP key @offset **3144** và ingest Bearer @**3251** — nằm trong 4 KB đầu, `strings`
là ra — mà bản OTA phải đẩy lên **`FBTRapidplusOTA`, repo PUBLIC**. Token rò qua **file `.bin` trên
repo công khai**, không qua repo firmware. Rotate + redeploy GAS vẫn là chặn cứng của đợt OTA. Xem
[docs/history/2026-07-22-secrets-out-of-source.md](docs/history/2026-07-22-secrets-out-of-source.md)
và [docs/plan/2026-07-28-ota-fleet-upgrade-243.md](docs/plan/2026-07-28-ota-fleet-upgrade-243.md).

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

- **Chuỗi `result` upload = `"<bệnh> | <CT> | <kết luận>"`, buffer cỡ theo CAP CỦA `/rename`
  (32), không theo danh sách bệnh hiện có.** `char resultConfig[20]` vừa khít hồi `DISEASES` còn
  là mã 4 chữ; danh sách lên 12 mục thì `"ASF I177L | 04.7 | P"` **đúng 20 ký tự** → `snprintf`
  giữ 19 + NUL và **nuốt mất chữ kết luận**, im lặng (TFT vẫn đúng, `nameSlot` trong payload đang
  bị comment nên không có trường nào để đối chiếu). Nay `[64]` + cả 4 format dùng **`%.32s`** cho
  tên — lỡ có nhãn dài hơn thì cắt TÊN (nhìn thấy) chứ không cắt kết luận. Guard
  `python tools/test_result_string_fits.py`. Chi tiết:
  [docs/history/2026-08-20-result-string-mat-chu-ket-luan.md](docs/history/2026-08-20-result-string-mat-chu-ket-luan.md).
- `Bluetooth.cpp` — BLE config (dead — nhả BT), EEPROM settings,
  upload TLS: **`postJsonToAllTargets(payload, what)` là NƠI DUY NHẤT biết danh sách 3 đích**
  (GAS/Google Sheet · ingest `fbt.basa-luma` Bearer · ERP `api.fortebio` **X-API-Key**), gọi
  `postJsonRetry(url, payload, label, bearer, apiKey, outBody)` cho từng đích. **Cả kết quả
  (`postData_GoogleSheet`) lẫn lỗi (`postError_*`) đều đi qua nó** — trước 2026-08-05 đường lỗi
  có bản sao riêng và **chỉ tới GAS**, nên máy hỏng kênh thì báo bảng tính mà không báo hai hệ
  thống người ta thật sự theo dõi. Guard `python tools/test_upload_targets.py`. Release BT
  (`releaseBluetoothStack`, gọi sớm ở main.cpp — GOTCHA 1).
- `displayCLD/displayLCD.cpp` — máy trạng thái UI: `type_infor` kiểu `e_statuslcd`.
- `PIDControl.cpp` — nhiệt độ: `getBottomTemperature()` = {lysis, ampLeft, ampRight},
  `getHotlidTemperature()` = {topLeft, topRight, ambient}. **Hai PID nắp trên bị chặn duty ở
  `HOTLID_PWM_MAX` (128)** bằng `SetOutputLimits` — **không** kẹp sau `Compute()` (kẹp sau thì
  tích phân vẫn dồn tới 255 rồi phải xả mới hạ xung = vọt nhiệt). **Đừng hạ dưới 100**: nắp
  không vào nổi 3 °C quanh mục tiêu thì `heatNewLid23()` không bật cờ, máy **treo ở màn chờ**
  chứ không báo lỗi. Guard `python tools/test_hotlid_pwm_cap.py`. Chi tiết:
  [docs/history/2026-08-07-hotlid-pwm-cap.md](docs/history/2026-08-07-hotlid-pwm-cap.md).
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

Routes: `/` (UI nhúng trong firmware, `kWebAssets[]` — xem GOTCHA 4), `/events` (SSE), `/control?btn=red|green|white` (bấm nút →
`_buttonManager.postShortPress`; **green** = nút vật lý `B_BLUE`), `/home` (snapshot),
`/slots` (bảng kết quả: `{name, sample, ct, result}` ×10),
`/errors` (bảng lỗi cảm biến: `{code, text}` ×10 — xem dưới), `/rename?slot=N&name=<bệnh>&sample=<mẫu>`
(`name`→NVS key `names`, `sample`→NVS key `samples`, namespace `slotlabels`; **hai trường độc
lập**, gửi cái nào áp cái đó, cap 32 ký tự; xem [docs/history/2026-07-24-slot-sample-name.md](docs/history/2026-07-24-slot-sample-name.md)),
`/curve` (toàn bộ đường cong từ đầu run → backfill),
`POST /reviewlast?go=1` (nạp lại run cuối từ EEPROM để xem sau reboot — **`?go=1` bắt buộc**, xem dưới),
`GET /ota` + `POST /ota?action=check|update` (cập nhật firmware — xem dưới).
Kết quả cache qua `dashboardSetResults()` gọi từ `screen_Result()`.

**Xem lại run sau reboot (`POST /reviewlast?go=1`)**: **`?go=1` bắt buộc** — route đăng ký
`HTTP_POST` của WebServer.h (**=3**) mà AsyncWebServer khớp **bitwise** (GET=1) → `3 & 1 != 0`
nên **GET trần cũng vào đúng handler này**, và nó là route POST duy nhất **không có tham số nào
để từ chối**; link-prefetch của browser hay scanner sẽ kích một lượt đọc EEPROM ~8 s đè lên
`sensor67Value` đang sống. Kiểm bằng query param, **không** so `req->method()` (GOTCHA 3).
E2E khoá: `node tools/test_review_reboot.js`. cache RAM (`gResultsReady`, `gCT`,
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
- **Nhãn slot thuộc về MỘT run — `dashboardLoop()` xoá `names` + `samples` (RAM **và** NVS)
  khi `isBusy()` lên sườn** (idle/finished/review/QR/setting → run). Trước 2026-08-20 không ai
  xoá: `slotNames[]` nạp từ NVS lúc boot, `/rename` ghi vào, `postData_GoogleSheet` đọc thẳng ra
  `nameSlot`, nên **run không đặt tên vẫn upload tên bệnh của run trước** — kết quả nộp dưới nhãn
  một xét nghiệm khác, im lặng. Bảng naming điền sẵn từ `/slots` càng che lỗi. Là **điều kiện**
  chứ không hook vào nút: chu kỳ bắt đầu từ 4 đường (RED vật lý, BLUE lysis, `/control?btn=ampname`,
  lệnh Serial `ForteSetting.cpp:148`). Xoá lúc **VÀO** run, không phải lúc ra — Result phải còn đọc
  được nhãn của run vừa xong. Trống là fallback có sẵn (`"N/A"` khi upload, `#N` ở legend).
  Đánh đổi: chạy lại cùng panel phải chọn lại dropdown. Guard `python tools/test_slot_label_reset.py`.
  Chi tiết: [docs/history/2026-08-20-slot-label-reset-per-run.md](docs/history/2026-08-20-slot-label-reset-per-run.md).
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
(nay là `phase "restart"`, **không** nằm trong `chartMode`). Trạng thái máy là nguồn duy nhất
điều khiển việc này — client không giữ cờ riêng.

**`fillStatus` phủ MỌI state người vận hành ngồi được, và `phase` không phải nhãn tuỳ ý** (2026-08-05).
Trước đó nó xử lý **12/38 state**, phần còn lại rơi vào `default` = *"Idle / Waiting for a run to
start."* — trong đó có `ewaitphase2`, lúc máy đang chờ người **rút ống lysis đang nóng** ra. Cùng
một màn hình vừa nói "đang rảnh" vừa hiện chip xanh có nhãn "Amplification". Nay **34/38**, 4 state
còn lại nằm trong allowlist có ghi lý do (`escreenStart` là idle thật; `ewaitingReadsensor`,
`eheathotlid1`, `eprepare` **không nơi nào gán** — kiểm bằng `grep "type_infor = <tên>" src/`).

Hai luật khi đặt `phase`:

- **KHÔNG bao giờ trả `"idle"` cho state mà nút ĐỎ mang nghĩa khác** — `script.js` viết lại cú bấm
  đỏ thành cổng đặt tên **khi và chỉ khi** `phase === "idle"`. Đây là lý do `ewaitLysisTube` có
  phase riêng từ trước, và mọi state mới thêm cũng vậy. `escreenStart` là **ca cơ sở**, không phải
  ngoại lệ: ở đó đỏ đúng nghĩa "mở cổng đặt tên".
- **`"finished"` cũng không miễn phí** — `chartMode` của client chứa nó, nên phát nhãn đó là kéo
  chart lên Home. State chỉ *đi sau* một run thì cho phase riêng (`review`, `error`, `restart`).

Phase lạ thì **an toàn**: client coi mọi tên nó không biết là màn hình thường.

Nội dung đã đồng bộ với TFT: có **nhiệt độ hiện tại / mục tiêu** ở các pha sấy, **đếm ngược làm
tròn LÊN phút** giống hệt `waitLysis10min` in ra, **số vòng** ở pha đo (đồng hồ và bộ đếm vòng là
hai thứ độc lập — đồng hồ về 0 mà vòng chưa hết thì nói "finishing the last rounds", không bịa
phút), và `escreenFinished` **tách hai giai đoạn** theo `gResultsReady` (~30-90 s tính kết quả +
upload TLS trước đó báo sẵn "Results ready." nên reload vào thấy bảng trống).

Guard: `python tools/test_status_coverage.py` — phủ state, **`fillActions` và `fillStatus` phải
biết cùng một tập state** (hai bảng tra trên một enum, lệch nhau chính là bug này), và không state
mang nghĩa-đỏ nào được báo `"idle"`.

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

**Baseline của chart (web-only, KHÔNG quyết định kết quả)**: `baseline = trung bình các vòng
trong cửa sổ **[BASELINE_START_MIN, +BASELINE_RANGE_MIN)` phút** (mặc định **2 / 4**, `script.js`,
đổi được — thời gian quang học ổn định là tính chất của máy, không phải của code). Chart vẽ
`giá trị − baseline`.

**Vì sao có điểm BẮT ĐẦU chứ không tính từ vòng 0**: quang học + dung dịch cần vài phút mới ổn,
những vòng đầu leo dốc từ trạng thái nguội (đo trên run thật: **311 → 427 trong 5 vòng**) rồi mới
phẳng. Trung bình từ vòng 0 kéo baseline **xuống dưới** mức phẳng đó → chính đoạn ổn định-hoá hiện
lên như tín hiệu đang lên: cả 8 kênh đều vọt trong 2 phút đầu (tới **21.7**), và **2 kênh không hề
khuếch đại lại "lift-off" ở phút 1.3** — tức âm tính trông như dương tính. Với 2/4: vọt 2 phút đầu
= **0** ở mọi kênh, 2 kênh phẳng **không bao giờ** lift-off, 2 kênh khuếch đại thật vẫn lên ở ~7 phút.

**`rawY` giữ giá trị RAW, trừ baseline lúc VẼ** — không trừ sẵn khi lưu. Baseline chỉ chốt sau khi
cửa sổ chạy xong, mà trừ sẵn thì mọi điểm sớm bị đóng băng theo một baseline còn đang dịch (bug cũ:
reload giữa chừng làm đoạn đầu nhích khác đi). Trước khi cửa sổ mở, `baseCount == 0` → vẽ **phẳng 0**,
đúng bằng thứ mấy vòng ổn định-hoá sẽ floor về sau đó, nên lúc baseline có thì không có cú nhảy nào.
Chi tiết: [docs/history/2026-08-02-chart-baseline-start-window.md](docs/history/2026-08-02-chart-baseline-start-window.md).

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

## Tab Setting (3 card đang bật / 8 định nghĩa)

**Đang hiện**: WiFi · **Profile Configuration** · **Firmware (OTA)**. **Đang bị comment trong
`CARDS` (`script.js`)**: **Device ID** (ẩn 2026-08-05) · LED · **Calib** · PID/heater · Other.
Master-detail: lưới card → bấm mở panel form.

**Ẩn là CỐ Ý, sẽ bật lại sau (chốt 2026-08-02) — comment chứ không xoá.** Renderer + route của
cả 5 card vẫn sống, nên bật lại một card = bỏ comment entry của nó. **Đừng "dọn" `renderCalib()`
(~110 dòng), `renderDeviceId()`, nhánh `openPanel` `custom === "calib"`/`"id"`, hook
`applySettingLock` `openCard === "calib"` hay route `/calib` + `POST /deviceid` của firmware** —
chúng trông như code chết nhưng là đường bật lại.

**Khoảng hở khi Device ID đang ẩn**: `POST /deviceid` là đường ghi ID **có busy gate** (409 khi
đang chạy run); `JsonDataConfig()` (Serial/BT) thì **không có gate nào**. Ẩn card đi là bỏ mất
đường *được gác*, để lại đúng đường *không được gác* — nên nếu cần đổi ID lúc này thì làm qua
Serial **khi máy rảnh**, đừng đổi giữa run. ID vẫn **đọc được** trên web ở thẻ `#setAbout`
("Device → ID") và trên header, chỉ là không sửa được.

**Khoảng hở đã biết khi Calib đang ẩn**: BLUE long-press trên máy **vẫn vào wizard**, mà
`/calib?action=cancel` là **đường huỷ sạch duy nhất** (Setting #7 — WHITE gọi `ESP.restart()` và
để cờ latch lại, kẹt luôn lần calib sau). Không có card thì **không có đường thoát trên web**:
calib bắt đầu ở máy phải kết thúc ở máy. Firmware/client vẫn cố tình giữ nav khi calib
(`applyRunNav(busy && !calib)`) nên vào được tab Setting — chỉ là chưa có gì trong đó.

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
   **`dashboardDeviceBusy()` = `otaState == OTA_UPDATING` HOẶC `isBusy(type_infor)`** — màn
   hình **không** phản ánh việc đang tải OTA (`waittingUpdate()` chỉ vẽ đè, `type_infor` giữ
   nguyên ~2 phút), mà trong cửa sổ đó `POST /wifi` reboot giữa chừng và `POST /otaupload` ghi
   vào **cùng singleton `Update`** từ AsyncTCP. Ngược lại `eUpdateOTA` (màn *mời* cập nhật)
   **là rảnh** — coi nó bận thì nút ĐỎ trên chính màn đó tự huỷ download của mình.
   **Gate reboot chặt hơn gate settings**: `!busy && !suspended && type_infor != escreenFinished`
   — `escreenFinished` "rảnh" nhưng là lúc `screen_Result('f')` tính kết quả + upload TLS
   30-90 s, reboot hoãn qua run sẽ nổ đúng vào đó.
4. **Firmware không validate khoảng nào** → `handleConfigPost` là **trust boundary của
   heater**: `amplification time` phải ≤ **130** (không thì tràn `sensor67Value[10][130]`
   giữa run), mật khẩu WiFi ≤ **54** ký tự (khe EEPROM, không phải 63 của WPA2),
   **cả 4** field `char[10]` (`device ID`, `units`, `PCB version`, `para version`) ≤ **9**,
   mảng phải **đúng độ dài** (loop không check bounds). `PCB version` từng lọt (comment ghi
   "checked below" mà không có check) → `strcpy` vào offset 14 đè `slopes`/`origins`/`kpid` rồi
   `EEPROM.commit()`. Nay `JsonDataConfig()` dùng **`strlcpy`** cho cả 4 (`ForteSetting.cpp:241,
   248,348,355`) vì Serial/BT vào thẳng hàm đó **không qua validate nào**. Guard:
   `python tools/test_phase0_guards.py`.
5. **`parastructure` = 400B / giới hạn 402B** (`sizeof(parameter) > 512-110`) → **không
   thêm field mới** vào struct.
   - Hệ quả trực tiếp: **card Profile nhập PHÚT nhưng thiết bị vẫn lưu giây/vòng** — không có
     chỗ cho một bản sao ở đơn vị khác, và validate ở mục 4 viết theo **đơn vị lưu**. Chuyển đổi
     nằm ở **rìa form**, qua hai móc `f.toUi`/`f.toDev` trong `renderFields`/`collectFields`
     (`data/script.js`), **không ở đâu khác**. `Lysis time`→`lysis duration` (giây),
     `Amplification time`→`amplification time` (**vòng**), `Opto preheat`→`opto preheat time` (giây).
   - **`time per loop` đã rời khỏi form** (gộp vào Amplification time). `collectFields` chỉ gửi key
     của card nên nó **không bị ghi**; và `perLoopMs()` **đọc giá trị của chính máy** từ `/config`
     (mặc định 20 000 ms) để quy đổi — hardcode 20000 sẽ làm máy đặt vòng khác đọc ra số phút sai.
   - `toDev` của Amplification time **kẹp 1..130** — cùng biên với mục 4, hai cổng chứ không phải
     một; form không được post một giá trị mà nó biết chắc firmware sẽ từ chối. Trần: **43 phút**.
   - Guard `node tools/test_profile_minutes.js` ghim round-trip ở mặc định (600 s↔10′,
     300 s↔5′, 120 vòng↔40′), clamp, và việc `perLoopMs()` hỏi thiết bị. **Negative test tìm ra
     một lỗ trong chính guard**: bản đầu chỉ regex thân `perLoopMs` xem có đọc `"time per loop"`
     không, nên gieo `return 20000;` (để lại lệnh đọc chết) vẫn xanh → nay guard **gọi hàm thật**.
6. **Device ID có ĐÚNG MỘT store: `parameter.device_id`** (EEPROM 512, đọc qua macro `protoID`).
   Global `id_device` + slot EEPROM 170 **đã xoá hẳn 2026-07-30** — trước đó hai store lệch nhau
   mọi hướng (header đọc cái này, thẻ Setting đọc cái kia, Serial/BT đổi một cái mà không đổi
   cái còn lại → web hiện một ID, upload gửi ID khác). **Đừng thêm lại bản sao global** — đó
   chính là bug, không phải fix. Slot `170..210` và cờ `ADDR_CHECK_ID_DEVICE` nay chết.
   - **THỨ TỰ BOOT là bất biến**: ID chỉ có sau `_ForteSetting.begin()`, mà
     `WiFi.setHostname(dashboardHostname())` **đọc ID** → `_displayCLD.begin()` +
     `_ForteSetting.begin()` phải nằm **trước khối WiFi** trong `main.cpp` (display trước vì
     `ForteSetting::begin()` vẽ `ErrorDisplay()` khi EEPROM trắng). Sai thứ tự → hostname DHCP
     dựng từ mặc định biên dịch còn mDNS dùng ID thật = **hai tên cho một máy**.
   - **`sanitiseDeviceId()` là trust boundary duy nhất**, gọi ở cuối `begin()` **và** trong
     `JsonDataConfig()` khi key `"device ID"` có mặt (Serial/BT vào thẳng hàm đó, không qua
     validate nào — Setting #4). Nó **NUL-terminate trước khi đọc** (`EEPROM.get()` thô có thể
     trả `char[10]` không kết thúc → `strlen` đi lố sang `slopes[]`) rồi loại byte không in được.
     Rác → `"UNSET"` (hiện trên cả TFT lẫn header web, không persist).
   - Thẻ Setting đọc `deviceIdNow` (SSE `home.device`), **KHÔNG** `cfgCache["device ID"]`. Nút là
     **"Save & reboot"**: đổi ID kéo theo reboot hoãn (xem mục SSID SoftAP dưới).
   - **Slot 170 KHÔNG chết — nó là nguồn ĐỌC của migration v2.4.2→v2.4.3.** v2.4.2 giữ ID *đang
     dùng thật* ở đó (`EEPROM.writeString(170)`), còn `parameter.device_id` chỉ mặc định
     `"proto 0"` → nâng cấp là mất ID (mà hiện `"proto 0"`, **không** phải `"UNSET"`, nên im lặng).
     `begin()` migrate **một lần**, đánh dấu bằng byte `0xA5` ở `ADDR_CHECK_ID_DEVICE` — **đọc
     bằng `EEPROM.read()`, KHÔNG `readBool()`** (EEPROM trắng = `0xFF` = true = bỏ qua migration).
     Dấu-một-lần chứ **không so giá trị**: so giá trị sẽ ghi đè ngược mọi lần đổi ID sau này.
     **Không ghi/xoá slot 170** (giữ đường downgrade). Đọc phải **chặn biên 40 byte**, không dùng
     `EEPROM.readString()`.
   - **`idIsPlaceholder()` = `{"RPL","proto 0","UNSET"}`, so bằng `strcmp` CHÍNH XÁC.** Portal
     v2.4.2 **điền sẵn `"RPL"`** vào ô ID (`WiFiManagerParameter(..., "RPL", 40)`) và lưu dù người
     dùng không đụng, nên **rất nhiều máy có `"RPL"` ở slot 170**. Nhận nó làm serial = cả fleet
     dùng **chung một định danh**, qua được sanitise nên không thể phát hiện hay sửa lại. Prefix
     match sẽ giết serial thật `"RPL03010"` → phải `strcmp`. Mặc định biên dịch của
     `device_id` là **rỗng** vì lý do tương tự.
   - **Đừng gate migration bằng `FirmwareVer`** — global đó đổi mỗi bản, máy nhảy 2.4.2→2.4.4 sẽ
     bỏ qua. Gate theo **dữ liệu**. (Seed `kpid3` từng dính đúng lỗi này, đã sửa cùng lúc.)
   - Guard: `python tools/test_device_id.py` — bất biến cốt lõi là **không còn định danh
     `id_device` nào trong `src/`**; guard lọc comment + raw string + string thường trước khi
     soi (`"id_device"` vẫn là **tên field của cloud** trong payload upload, và `src/index.h`
     có biến JS trùng tên trong raw string). **Nhưng check "còn literal `"RPL"` không" phải chỉ
     bóc comment** — `code_only()` bóc cả string literal sẽ làm guard tự mù. Chi tiết:
     [docs/history/2026-07-30-device-id-single-store.md](docs/history/2026-07-30-device-id-single-store.md) ·
     [docs/history/2026-07-30-device-id-migration-slot170.md](docs/history/2026-07-30-device-id-migration-slot170.md).
7. **Calib là wizard người-trong-vòng-lặp**, không phải routine: BLUE **long-press**
   (`postLongPress`, mới thêm) → RED sấy 55°C → chờ **5 phút** → chọn slot → **4 lần đo,
   mỗi lần thay ống thật** (300/200/100/0). Máy **không có đường cancel sạch** (WHITE gọi
   `ESP.restart()`) → `/calib?action=cancel` tự gỡ cờ (`flag_calib_done`, `type_calib`),
   thiếu bước này lần calib sau sẽ **chết cứng**. Calib **chỉ ghi `slopes`**, không ghi
   `origins`. Chọn slot bằng cách ghi thẳng `_displayCLD.slot` (bấm RED nhiều lần sẽ bị
   gộp: `pendingEvent` chỉ có **1 ô**).
8. **`.hide` phải `!important`** — utility 1 class sẽ thua rule component 1 class định
   nghĩa sau (`.set-grid{display:grid}`), panel sẽ render **dưới** menu thay vì thay thế nó.
9. **Nhãn phải `for=` (hoặc bọc ô)** — `<label>` không `for=` và không phải tổ tiên thì **không
   đặt tên cho gì cả**: screen reader đọc "edit, blank" và **bấm vào chữ không focus vào ô**.
   `renderFields` nối **một chỗ duy nhất** sau chuỗi if/else (`querySelectorAll(".f-in,.f-sel")`,
   chỉ khi **đúng 1** control) nên mọi nhánh tự thừa hưởng, còn hàng `f.arr`/`f.mat` nhiều ô thì
   tự rơi ra và dùng `aria-label` riêng từng ô.
10. **Disabled = rút màu, KHÔNG làm mờ.** `opacity` nhạt cả chữ lẫn nền → tương phản sập hai
   phía (`.6` đo được **2.26** cho mô tả card, **2.55** cho nút Save). Dùng `filter: grayscale(1)`
   — ma trận luminance nên tỷ lệ **không thể tụt**. **Nhưng grayscale một mình gần như vô hình**
   trên card trắng chữ đen (chỉ ô icon 40px đổi màu) và `cursor: not-allowed` **không tồn tại
   trên cảm ứng** → `.set-card[disabled]` phải kèm `background: var(--bg)` + `box-shadow: none`
   (card hết nổi = tín hiệu thật), và `.set-card[disabled]:hover` **cũng phải** `box-shadow: none`.
11. **Trả focus khi đóng panel nằm ở listener `#setBack`, KHÔNG nằm trong `showMenu()`.**
   `showMenu()` có **3 caller** và `openCard` **sống sót qua việc rời tab**: đặt trong đó thì vào
   lại tab Setting sẽ giật focus khỏi nút nav vừa bấm sang một card, rồi
   `loadConfig().then(renderSetMenu)` chạy `innerHTML = ""` **xoá đúng node đang giữ focus** →
   `<body>`, đúng lỗi mà tính năng này sinh ra để tránh. Back là caller duy nhất mà thứ bị ẩn
   *chính là* thứ đang giữ focus. Guard: `node tools/test_setting_a11y.js`.
12. **Nearby WiFi lọc SSID đã lưu lúc RENDER, không lọc `wifiScanNets`** — mảng đó chống lưng
   cho việc kiểm SSID gõ tay trước reboot; lọc nó thì gõ đúng tên một mạng đã lưu bị từ chối là
   "typo". Hai danh sách là **hai fetch độc lập thứ tự bất kỳ** → `loadSavedWifi()` set
   `wifiSavedSsids` rồi **render lại** Nearby, thiếu bước này thì Forget xong mạng đó không quay
   lại Nearby tới lần quét sau. Chi tiết:
   [docs/history/2026-08-02-setting-ui-a11y-va-wifi-trung-lap.md](docs/history/2026-08-02-setting-ui-a11y-va-wifi-trung-lap.md).

**Nguồn OTA = Engineer Server, KHÔNG còn GitHub (v2.4.4, 2026-08-17).** `checkFirmware()` gọi
`GET /ota/check?device=<id>` với **cùng Bearer `SECRET_INGEST_TOKEN`** đang dùng để POST kết quả
(không có credential thứ hai). Máy trạng thái, cổng busy, reboot hoãn, panel web, prompt TFT
**giữ nguyên** — đây là đổi đường truyền, không phải tính năng mới.

- **HAI host, thử theo thứ tự**: `SECRET_OTA_CHECK_URL` (`hub.fortebio.tech`, Cloudflare)
  rồi `SECRET_OTA_CHECK_URL_FALLBACK` (`fbt.basa-luma.ts.net`, Funnel). OTA là **đường duy nhất**
  tới máy ngoài hiện trường, nên firmware chỉ biết một host = một sự cố Cloudflare biến 109 máy
  thành không ai với tới được. **Stateless cố ý**: không nhớ host nào thắng — nhớ thì tốn một lần
  ghi NVS và đẻ ra trạng thái cũ có thể sai, đổi lại chỉ tiết kiệm **một request hỏng mỗi 6 h**.
  `WiFiClientSecure` dựng **mới từng lần thử** (dùng lại qua hai host để lại socket nửa-đóng —
  bẫy ở `Bluetooth.cpp:474`). `otaCheckFailed` **chỉ bật khi CẢ HAI cửa im**.
- **`fwUrl` PHẢI lấy từ `json["url"]`, đừng ghép tay.** Server dựng URL từ **chính request**, nên
  host nào trả lời `/ota/check` thì `.bin` tải về host đó — đó là thứ làm fallback tự đúng mà
  không cần hằng số `.bin` thứ hai. Ghép tay là tải về primary dù fallback mới là cửa đang sống.
- **Không ghim CA ở đâu trong firmware** (`setInsecure()` ở cả `Bluetooth.cpp:414` lẫn
  `updateOTA.cpp`) — có chủ ý vì ràng buộc heap mbedTLS (GOTCHA 2). Nhờ vậy đổi domain (ISRG Root
  X1 → Google Trust Services) **không đứt TLS**. Ghim CA vào là biến mọi lần đổi hạ tầng thành
  rủi ro chết cả fleet, mà đường sửa duy nhất lại chính là OTA.
- **`SECRET_INGEST_URL` cũng trỏ Cloudflare, nhưng KHÔNG có fallback** — cố ý: hỏng ingest là mất
  một kết quả (đã có retry + 3 đích), hỏng OTA là mất đường sửa cả fleet. Lưới đặt ở chỗ
  hỏng-không-cứu-được, không rải đều.
- ⛔ **Cache Rule bypass `/ota/*` trên Cloudflare là CHẶN CỨNG.** `.bin` nằm trong extension cache
  mặc định; upload trùng tên xong edge vẫn phát bản cũ, và **`x-MD5` không cứu** (header đi kèm
  chính file cũ đang cache → firmware kiểm thấy "khớp" rồi nạp nhầm bản, im lặng hoàn toàn).

- **Server trả TÊN FILE, không trả số build.** Luật so sánh là **KHỚP CHÍNH XÁC**:
  `name != "fbt_" + FirmwareVer + ".bin"`. Vì vậy **`FirmwareVer` (`define.h`) nay là một nửa của
  phép so sánh, không phải nhãn hiển thị**, và quy ước đặt tên là **ràng buộc chức năng**.
  `currentVersion`/`fwVersion` (`versionCode`) và `fwCont` (notes) **đã xoá hẳn**.
  ⚠️ **ĐỪNG quay lại `indexOf()`** (bản đầu dùng thế, soát đối kháng 2026-08-18 bắt được): mọi tên
  **mở rộng** version đang chạy — `fbt_v2.4.4_rc1.bin`, `fbt_v2.4.4AT.bin`, mà hộp thoại upload của
  app **gợi ý đúng những tên đó** — đều CHỨA version nên cả fleet **từ chối bản vá, im lặng, vĩnh
  viễn**. Khớp chính xác hỏng theo hướng ngược lại (mời lặp lại) và hỏng-nhìn-thấy-được thắng.
- **`200` CHƯA phải câu trả lời cho tới khi nó PARSE ĐƯỢC.** `otaCheckHost()` phải kiểm giá trị trả
  về của `deserializeJson()` **và** `json["update"].is<bool>()`, không thì trang login Cloudflare
  Access / WAF interstitial / captive portal (đều là 200 + HTML) rơi vào nhánh "up to date" **và
  `return true`** → vòng hai host **dừng ở host 1**, Funnel dự phòng không bao giờ được gọi. Cả
  fleet báo khoẻ trong khi đường sửa đã chết, không dấu vết ở đâu.
- **Check hoàn tất GIỮA LÚC ĐANG TẢI không được ghi gì.** `otaState == OTA_UPDATING` là cờ DUY NHẤT
  giữ `dashboardDeviceBusy()` true suốt ~2 phút ghi flash; xoá nó là mở lại `POST /wifi` và cổng
  reboot hoãn lên một partition đang ghi dở. Nặng hơn: `fwUrl` bị gán lại **trong khi `httpUpdate`
  giữ nó bằng `const String&`** = giải phóng buffer dưới chân người đọc. `checkFirmware()` chặn ở
  đầu, `otaCheckHost()` kiểm lại trước khi publish (TOCTOU).
- **Toàn vẹn ảnh nằm ở SERVER, không ở firmware**: `HTTPUpdate` tự đọc header **`x-MD5`** của
  response rồi gọi `Update.setMD5()` (`HTTPUpdate.cpp:223,344`), nên `/ota/{file}` gửi header đó
  là xong — **0 dòng firmware**. Đừng thay bằng vòng tải `Update.write()` tự viết để kiểm sha256:
  `httpUpdate.update()` là **lời gọi duy nhất đã đo stack thật** (NetworkTask 6144 B ôm mbedTLS +
  HTTPClient + Update; tràn là **panic**), và đó là go/no-go của cả đợt phát hành. Bearer gắn qua
  tham số thứ 4 `HTTPUpdateRequestCB` (chạy ngay trước `http.GET()`, `HTTPUpdate.cpp:219`).
  ⚠ md5 do chính server phục vụ file tính ra → bắt hỏng **đường truyền**, **KHÔNG** bắt được
  **upload hỏng** (server băm đúng file cụt và tự đồng ý). `sha256` ở `/ota/check` cũng vậy. Muốn
  bịt thì bịt ở đầu upload, đừng thêm hash ở đầu tải.
- **Máy TỰ BÁO "vừa nạp xong" — `&updated=1`** (v2.4.6, 2026-08-20). `HTTP_UPDATE_OK` chốt một
  cờ trong **NVS** (`otaMarkInstalled`), lượt `/ota/check` kế tiếp gửi kèm `&updated=1`, server ghi
  thẳng một mốc vào `fw_log.json` kể cả khi version KHÔNG đổi (đánh dấu `how: "update"`).
  Vì sao cần khi đã có `?ver=`: phép so "version đổi giữa hai lượt poll" **mù hẳn với lần nạp lại
  CÙNG một bản**, và không phân biệt được "vừa cập nhật" với "vừa mất điện bật lại".
  - **NVS chứ không RTC RAM**: reboot sau khi nạp là **hoãn** (`dashboardRequestRestart` chờ máy
    rảnh) nên mất điện xen vào giữa là chuyện thường — RTC RAM mất đúng sự kiện đang cần.
  - **Chốt cờ TRƯỚC khi xin reboot**: tới `HTTP_UPDATE_OK` thì ảnh đã nằm trong partition kia, máy
    **sẽ** boot vào nó bằng đường này hay đường khác. Ghi muộn hơn là bỏ sót.
  - **Xoá cờ CHỈ sau khi một host trả lời đúng hợp đồng** — 401 / trang login Cloudflare / tunnel
    chết đều `return` trước đó, nên boot không mạng vẫn báo được ở lượt poll sau.
- ⚠️ **`fbt_v2.4.5.bin` trên server là bản build TRƯỚC khi có `?ver=`** — đúng cái bẫy "hai ảnh một
  version" mà `define.h` cảnh báo, và nó **vô hình từ mọi dashboard**. Đo được: 29 lượt `/ota/check`
  trong 3 ngày **không lượt nào** có `&ver=`, `fw_seen.json` chưa từng tồn tại. Vì luật so là khớp
  chính xác, bản vá **bắt buộc** mang version mới mới tới được máy. Chi tiết:
  [docs/history/2026-08-20-bao-cap-nhat-thanh-cong.md](docs/history/2026-08-20-bao-cap-nhat-thanh-cong.md).
- **Bản đang phát triển là `v2.4.5AT` / `v2.4.5a` (2026-09-09)** — hai chuỗi này chưa từng tới máy
  nào nên OTA tới được, còn **`"v2.4.5"` TRẦN thì cấm vĩnh viễn**: nó khớp đúng file cũ ở gạch đầu
  dòng trên, nên máy nạp bản đó đọc "đã mới nhất" và **không bao giờ nhận thêm bản vá nào**. Hậu tố
  `AT`/`a` ở bản này gánh **hai** việc — trục quy tắc hình dạng, và né chuỗi đã cháy; `#ifndef` cho
  phép override từ env PlatformIO nên luật phải được **phát biểu** chứ không chỉ được hiện thực.
  Khoảng hở còn lại nằm ở **SERVER**: máy v2.4.5 poll mỗi 6 h, server còn phục vụ `fbt_v2.4.5.bin`
  thì tên **không khớp** → máy báo có bản mới → người vận hành bấm ĐỎ là **cài đè bản cũ
  tiền-`?ver=`**. Đặt đúng `fbt_v2.4.5AT.bin` lên server, hoặc giữ máy thử **ngoài mạng**. Chi tiết:
  [docs/history/2026-09-09-bump-version-v2.4.5.md](docs/history/2026-09-09-bump-version-v2.4.5.md).
  **Nhánh `v2.4.5at` nhích thêm một nấc: `v2.4.5AT1` / `v2.4.5a1`** (`21f9331`, 11/09) — thuật toán đổi
  ở đó (early-rise → `F`), nên **không được** dùng lại chuỗi của nhánh `v2.4.5`; server cần đúng
  `fbt_v2.4.5AT1.bin`. Bàn giao: [docs/BAN_GIAO_v2.4.5AT.md](docs/BAN_GIAO_v2.4.5AT.md).
- **Rollback tự động KHÔNG làm được** — `esp_ota_mark_app_valid_cancel_rollback()` cần bootloader
  build với `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (arduino-esp32 mặc định TẮT) mà bootloader
  **nằm ngoài đường OTA**. Gọi hàm đó chỉ tạo cảm giác có lưới an toàn. Đường cứu vẫn là
  `/otaupload` hoặc dây.
- ⚠ **Fleet ≤ v2.4.3 KHÔNG với tới được bằng đường mới — đợt chuyển giao vẫn phải đi qua GitHub,
  đúng một lần.** Firmware cũ chỉ biết `raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/` +
  **version nó đang chạy** + `/updateOTA.json`, và chỉ prompt khi `versionCode > currentVersion`
  (v2.4.0→16, v2.4.2→17/18, v2.4.3→**19**). Nên bản 2.4.4 phải nằm trên branch của các version
  **cũ hơn** — kể cả branch **`v2.4.3`**, cái mà đợt trước cố ý bỏ vì 2.4.3 chính là đích. Manifest
  soạn sẵn + quy trình: [tools/ota-release/README.md](tools/ota-release/README.md), guard
  `python tools/test_ota_release_manifest.py`.
- ⛔ **Nhưng publish `.bin` v2.4.4 lên repo PUBLIC là trao quyền nạp firmware cho cả 109 máy**:
  ảnh mang `SECRET_INGEST_TOKEN` ở 4 KB đầu, mà từ v2.4.4 token đó **cũng mở `/ota/*`**. Chặn này
  gỡ ở **SERVER, không phải firmware**: tách scope để endpoint *engineer upload* đòi credential
  không nằm trong firmware, còn Bearer của máy chỉ đọc `/ota/check` + tải `.bin` + POST kết quả —
  rò về đúng mức rủi ro đã chấp nhận từ trước, không phải chiếm fleet. Chưa tách được thì đợt
  chuyển giao đi bằng `/otaupload` (chỉ máy ≥ v2.4.3 có route, và UI 2.4.3 **không gửi `?md5=`**
  → phải dùng curl) hoặc dây.
- **Go/no-go của đợt này nằm ở firmware CŨ**: máy tự tải bằng code đang chạy, mà v2.4.3 có
  `NetworkTask` **6144 B** (v2.4.2/v2.4.0 còn 8192) — tràn là **panic**, không phải
  `HTTP_UPDATE_FAILED`. Đo `uxTaskGetStackHighWaterMark` trên một máy **v2.4.3** bench sau một lần
  OTA thật trước khi mở cho fleet.

**Cập nhật firmware từ web (`/ota`)**: `GET /ota` trả `{version, state, hasUpdate, busy, checked,
checkFailed, online, newVersion}`; `POST /ota?action=check|update`.

- **`check` phải qua `PEND_OTACHECK` (SettingTask)** — HTTPS **blocking** vài giây, chạy trên
  AsyncTCP là treo dashboard (GOTCHA 8/11). `drainPending()` vốn guard `dashboardDeviceBusy()`
  nên tự động không hỏi server giữa run.
- **Poll định kỳ 6 h** (`dashboardLoop()`): trước v2.4.4 `checkFirmware()` chạy **đúng 1 lần mỗi
  boot** ở ~2 s sau khi bật — sát mức associate + DHCP (1–3 s), trượt là đứng bản cũ tới khi có
  người tắt/bật. Đó là lý do lớn nhất OTA "im lặng" ngoài đồng. Poll đẩy vào **cùng hàng đợi
  `PEND_OTACHECK`** (không gọi thẳng `checkFirmware()` trên NetworkTask), giữ **deadline riêng**
  chứ không đọc `otaLastCheck` (biến đó chỉ nhích khi GET **hoàn tất** → máy mất mạng sẽ re-queue
  mỗi tick), so sánh có dấu để sống qua wrap 49 ngày. Chỉ poll khi `OTA_IDLE`/`OTA_FAILED` —
  `OTA_DISMISSED` cố ý im tới hết phiên.
- **Poll phải mang cổng cấp-REBOOT, không phải cổng cấp-settings**: `!suspended` **và**
  `type_infor != escreenFinished`. `dashboardDeviceBusy()` của `drainPending()` KHÔNG đủ —
  `escreenFinished` là "rảnh" với settings nhưng chính là 30-90 s tính kết quả + upload TLS. Poll ở
  đó mở phiên mbedTLS **thứ hai** (ngân sách 42 KB liền mạch, GOTCHA 2), và một check thành công ghi
  `type_infor = eUpdateOTA` — mà `eUpdateOTA` nằm trong allowlist rảnh → **mở khoá `otaRestartAt`
  ngay giữa `screen_Result()`**. `drainPending()` kiểm LẠI để đóng TOCTOU ~10 ms.
- **`postOtaCheck(bool promptOnDevice)` — hai caller muốn hai đáp án NGƯỢC nhau.** Nút web truyền
  `false` (bấm từ xa mà chiếm màn TFT là bỏ rơi người đang đứng ở máy); poll định kỳ truyền
  **`true`** vì không ai đang xem browser, và `eUpdateOTA` là **chỗ duy nhất nút ĐỎ mang nghĩa
  "cài"**. Để `false` cho poll thì máy tìm ra bản mới mà **không hiện gì** — chỉ ai tình cờ mở
  dashboard mới thấy, tức không còn là cơ chế cập nhật fleet. Bool là **payload**, phải ghi TRƯỚC
  `pendingKind` (cùng khuôn `pendingA`/`pendingB`). Chiếm màn an toàn vì `drainPending()` chỉ chạy
  khi máy rảnh.
- **`update` thì AsyncTCP ghi thẳng `otaState = OTA_USER_ACCEPTED`** — *một byte volatile*,
  NetworkTask tự nhặt; việc tải không đụng task web.
- `checkFirmware(bool promptOnDevice = true)`: web gọi **`false`** để **không cướp màn TFT**
  (bấm từ xa mà chiếm màn là bỏ rơi người đang đứng ở máy). `otaLastCheck` phân biệt
  **"chưa kiểm tra"** với **"đã kiểm tra, không có bản mới"** — `OTA_IDLE` gộp cả hai.
- Cả hai POST **409** khi busy hoặc mất internet: OTA kết thúc bằng `ESP.restart()`.
- **`updateFirmware()` kiểm busy LẠI** trước `httpUpdate.update()` (web chốt
  `OTA_USER_ACCEPTED` lúc bấm, NetworkTask hành động vài phút sau — người dùng kịp đi tới máy
  bấm chạy run) → busy thì `OTA_FAILED`, không tải. Và **không `ESP.restart()` ngay**:
  `httpUpdate.rebootOnUpdate(false)` (nếu không thư viện tự restart bên trong,
  `HTTPUpdate.cpp:353`) rồi `dashboardRequestRestart()` — ảnh đã nằm trong partition OTA kia
  nên chờ máy rảnh mới reboot, dùng chung khuôn deferred-reboot với `/otaupload`.
- Panel **không đợi "cài xong"**: thành công = reboot, trang chỉ mất kết nối rồi tự nối lại.

**Nạp bằng file .bin** (`POST /otaupload[?md5=<32 hex>]`, multipart field `firmware`) — cho máy
**không có internet** hoặc build chưa có trên server. `?md5=` **tuỳ chọn nhưng nên có**: không có
nó thì **file .bin đứt giữa chừng vẫn boot** (`Update.end(true)` đặt `_size = progress()` → "bao
nhiêu byte tới nơi" được tính là cả ảnh) — đây là **đường brick thật duy nhất** của hệ thống.

⚠️ **UI phải THẬT SỰ gửi `?md5=`, và phải TỰ TÍNH** (2026-08-18) — firmware nhận tham số này
từ 2026-07-24 nhưng `script.js` **chưa bao giờ gửi**, nên suốt thời gian đó mọi lần nạp qua trình duyệt
chạy **không kiểm gì cả** — trong khi guard vẫn xanh vì chỉ soi phía C++. **Xử lý một tham số không ai
gửi thì không phải một phép kiểm** — guard 7 nay soi cả `data/script.js`.

- **Trình duyệt tự tính digest, KHÔNG có ô nhập.** Bản đầu làm ô cho người dùng dán md5 vào — chủ dự
  án bác đúng: *không thể bắt khách hàng ở xa nhập chuỗi 32 ký tự*. Đó là đẩy việc của máy sang cho
  người, trên chính kịch bản tính năng phục vụ (khách ở xa, máy không internet). **Đừng thêm lại ô đó.**
- **`md5Hex()` tự viết là bắt buộc**: `Update` của ESP32 chỉ kiểm MD5, còn `crypto.subtle` của trình duyệt
  có SHA-1/SHA-256 và **không có MD5**. Không mượn được ở đâu.
- **Bảng `K` là LITERAL**, không phải `Math.floor(abs(sin(i+1)) * 2^32)`: cách suy ra chuẩn giáo khoa
  nhưng dựa vào những bit cuối của `sin` trong libm khớp nhau trên mọi trình duyệt/điện thoại. Một hằng
  số lệch = **từ chối mọi lần nạp trên MỘT SỐ máy chứ không phải tất cả**.
- **Đếm bit bằng phép chia float, không phải dịch bit**: ảnh 2.4 MB là ~19 triệu bit, và trong JS
  `bits >>> 32` là **no-op** (trả lại chính `bits`), không phải 0. Chỉ lộ ra trên file lớn.
- Test: **`node tools/test_ota_md5.js`** — **trích hàm TỪ `data/script.js`**, không chép (bản chép tự đồng ý
  với chính nó trong khi bản ship đã hỏng). 7 vector RFC 1321 · mọi độ dài 0..200 vs `node crypto`
  (biên padding 55/56, 63/64) · chính `firmware.bin` 2.4 MB vs `md5sum`.
- ⚠️ **Không cứu được đợt nạp ĐẦU TIÊN**: 109 máy đang chạy UI cũ không có hàm này. Máy v2.4.3 thì
  dùng `curl ... "?md5=<hash>"`; máy v2.4.2/v2.4.0 không có route `/otaupload` → cầm dây.

Chi tiết: [docs/history/2026-07-24-web-ota-update.md](docs/history/2026-07-24-web-ota-update.md).

**Mức sóng WiFi trên TFT** (`show_IconWifi`, `displayLCD.cpp`): 4 bitmap cùng một glyph —
`image_WIFI_Lv0/Lv1/Lv2` + `image_WIFI_Connect` (3 cung). **Ba mức yếu được CẮT RA từ chính
`image_WIFI_Connect`**, không vẽ lại: phân loại từng pixel theo bán kính elip quanh đỉnh quạt
(apex ≈ (8.9, 13.0), x-scale 1.15) thì nó rơi vào **4 băng đồng tâm sạch** — chấm, cung trong,
giữa, ngoài — rồi bỏ băng ngoài. Nhờ vậy nét vẽ vẫn là art gốc, và mức 3 **trùng byte-for-byte**
với `image_WIFI_Connect` nên không cần Lv3. Đỉnh quạt đứng yên, chỉ quạt co lại. Ngưỡng **-60/-70/-80** **phải khớp `rssiBars()` trong `data/script.js`**: điện thoại
và máy đang nói về cùng một đường truyền, lệch nhau còn tệ hơn không hiện.

- **VẼ HAI LƯỢT: quạt đầy màu `DARKGREY` trước, rồi mức đang sáng màu `WHITE` đè lên.** Cung chưa
  sáng phải là **viền**, không phải nền trống — "1 trên 3" mới đọc ra là yếu; mạnh/yếu là **SỐ
  cung**, không phải màu. `drawBitmap` chỉ tô pixel **bật**, nên vẽ một mình bitmap mức thì cung bị
  bỏ đi **chìm hẳn vào nền đen** và "1 cung sáng" trông y hệt "một cái icon nhỏ" — chính là lỗi bản
  bitmap đầu tiên mắc phải khi thay cách vẽ vạch bằng `fillRect` cũ (sửa 2026-08-04). `DARKGREY` đo
  được **5.0:1** so với nền đen (nhìn thấy được) và trắng đứng trên nó **4.2:1** (rõ là cung đang
  sáng) — hai phép so đều tự đứng vững. **Không vẽ lượt xám cho `image_WIFI_Disconnect`**: glyph đó
  vốn đã là viền rỗng có gạch chéo, đệm quạt đặc phía sau là đá nhau chứ không bổ sung.
  Tính **lồng nhau** của 4 bitmap chính là thứ khiến lượt trắng phủ **đúng** phần đang sáng.
- **Phải chống nhảy**: RSSI dao động vài dB mỗi lần đọc, so thẳng ngưỡng thì vạch nhấp nháy khi
  đường truyền nằm sát biên, mà **mỗi lần nhấp là một lần blit bitmap qua SPI** trên chính task vẽ
  màn hình lúc đang chạy run. Phải **hai mẫu liên tiếp giống nhau** mới đổi mức.
- **Throttle nằm TRONG hàm** (1.5 s): cả gate vẽ lại (100ms) lẫn chỗ vẽ đều gọi nó, đặt throttle ở
  ngoài thì mỗi tick advance debounce hai lần.
- **Gate vẽ lại phải theo MỨC, không phải theo connected/disconnected** — không thì vạch không bao
  giờ đổi theo sóng. `lastWifiState` khởi tạo **-2** vì -1 nay là giá trị thật ("mất kết nối").
- **`show_IconWifi` phải `fillRect` xoá ô icon trước khi vẽ**: các mức chỉ khác nhau ở phần TRÊN,
  nên vẽ quạt ngắn đè lên quạt dài sẽ để lại cung đã bỏ.
- Guard: `g++ tools/test_wifi_bars.cpp` — ngoài ngưỡng và chống nhảy, nó còn ghim **4 bitmap phải
  LỒNG NHAU** (`lv[k] & ~lv[k+1] == 0`, và số pixel tăng dần 21 < 45 < 81 < 123). Một pixel sót lại
  sau khi cắt sẽ thành lỗ hoặc mảnh bay lơ lửng **chỉ xuất hiện ở đúng một mức sóng** — thứ không ai
  thấy cho tới khi máy nằm trong phòng sóng yếu.

**QR vào dashboard**: nút **TRẮNG ở màn chính** (`escreenStart`, nút này vốn không làm gì) —
hoặc **XANH trong Setting menu** (chỗ portal WiFiManager cũ đứng, xem dưới) →
state `eShowQR` → `screen_QR()` (displayLCD.cpp) vẽ QR bằng `ricmoo/QRCode` (version 3, ECC_LOW,
**tối trên nền sáng + quiet zone 4 module** — thiếu là máy quét không đọc được).

**PAYLOAD PHẢI ≤ 53 BYTE, và đây là đường RESET chứ không phải QR xấu**: version 3/ECC_LOW chứa
53B byte-mode mà `ricmoo/QRCode` **không tự kiểm** — `encodeDataCodewords()` không so length với
capacity, `bb_appendBits()` không bounds-check, đích là `codewordBytes[71]` **VLA trên stack
DisplayTask**. Dài hơn → đè stack → panic **ngay lúc mở màn QR**. Nguồn dài: `id_device` lấy từ
`EEPROM.readString(170)` mà hàm đó **quét NUL tới hết buffer 4096B**, không dừng ở biên 40B của
slot → máy có byte rác từ layout cũ nhận về chuỗi vài trăm ký tự. Ba clamp giữ nó lại:
`loadSettingDevice()` cắt `id_device` ≤ 24, `dashboardApName()` clamp lần nữa, `screen_QR()` chặn
`payload.length() <= 53` + kiểm return `qrcode_initText()`. Worst case = 18+4+24 = **46B**. Guard:
`python tools/test_qr_payload.py`.

**SSID SoftAP: builder nuôi RADIO, ai BÁO CÁO thì HỎI RADIO.** `dashboardApName()` (`"FBT-" + id`)
nay có **đúng một** consumer — `dashboardStartAP()` → `WiFi.softAP()`. Mọi nơi *hiển thị* tên
(`screen_QR()`, `buildHomeJson()` → `net.ssid`) đọc **`WiFi.softAPSSID()`**.
**Lý do (2026-07-30)**: `WiFi.softAP()` **chốt** tên vào radio và core này **không có API đổi tại
chỗ**, nên đổi Device ID xong thì builder trả tên mới còn sóng vẫn tên cũ → QR chỉ vào mạng không
tồn tại, không join lại được tới khi tắt/bật. Gộp *công thức* (bản 2026-07-29) không sửa được việc
**một consumer chốt vào phần cứng còn hai consumer tính lại** — "tên *nên* là gì" và "tên nào *đang
trên sóng*" là hai câu hỏi khác nhau. Kèm theo: **đổi ID luôn `dashboardRequestRestart(1500)`** (chỉ
khi giá trị **thực sự đổi**) vì ID bị latch ở **ba** chỗ lúc boot — SSID, `WiFi.setHostname`,
`MDNS.begin`. **KHÔNG** gọi `softAP()` lần hai để đổi tên sống (IDF không tài liệu hoá việc gì xảy
ra với station đang kết nối; `dashboardStartAP()` không idempotent) và **KHÔNG** poll so lệch trong
`dashboardLoop()` (điều kiện thường trực sống sót qua chính reboot nó gây ra = **vòng lặp reboot**).
Guard `test_qr_payload.py` ghim call graph: `WiFi.softAP()` **đúng 1 lần** trong `src/`,
`dashboardApName()` **đúng 1 call site**, hai reporter phải có `softAPSSID()` và không có builder;
cộng lệnh **cấm literal `"RAPID-"`/`"FBT-"`** trong `webDashboard.cpp`/`displayLCD.cpp`/`script.js`.
Dòng "Wifi Name:" trên màn QR phải
theo mode (`WiFi.SSID()` là **STA**, rỗng khi đang ở AP). Chi tiết:
[docs/history/2026-07-30-device-id-ap-ssid-latch.md](docs/history/2026-07-30-device-id-ap-ssid-latch.md) ·
[docs/history/2026-07-29-qr-reset-ssid-drift.md](docs/history/2026-07-29-qr-reset-ssid-drift.md).

Nội dung theo chế độ: **STA** → `http://<hostname>.local/`; **SoftAP** → `WIFI:T:nopass;S:FBT-<id>;;`
(AP mở) rồi **captive portal** (`DNSServer` + `onNotFound` redirect, chỉ khi `apActive`) tự bật dashboard.
`dnsServer.processNextRequest()` phải nằm **trên throttle 1s** của `dashboardLoop`. `eShowQR`
nằm trong allowlist `isBusy()` (xem QR không phải "bận"). Chi tiết:
[docs/history/2026-07-22-qr-dashboard-access.md](docs/history/2026-07-22-qr-dashboard-access.md).

**Màn QR ở STA mang HAI dạng địa chỉ, và đó là bất biến** (2026-08-04): QR mã hoá
`http://<dashboardHostname()>.local/`, còn **dòng chữ dưới nó in IP**. Trước đó ngược lại. `.local`
sống qua đổi lease DHCP nên hợp làm mã quét, nhưng nó **chỉ phân giải nếu CLIENT nói mDNS** —
iOS/macOS/Windows 10+ có, Android đời cũ không — và **thiết bị không có cách nào biết điều đó**.
Nên IP không được biến mất theo: nó là đường vào duy nhất cho mấy máy đó, và phải là dạng **đọc để
gõ tay** vì lý do người ta đọc nó chính là quét không vào được. Gộp caption về `.local` nữa là bỏ
đường thoát duy nhất — nhìn thì giống dọn dẹp, nên `test_qr_payload.py` mục 1b ghim lại:
`screen_QR()` phải **vừa** có `dashboardHostname()...".local/"` trong payload **vừa** còn
`localIP()`. Mục đó cũng chặn biên payload STA (`7 + clamp 24 + 7 = 38 B ≤ 53`) — IP tự giới hạn ở
15 ký tự, hostname thì không, nên đường tràn stack của mục 1 vừa có thêm cửa thứ hai.
**`code_only()` của guard nay biết string literal**: regex `//[^\n]*` cũ cắt
`payload = "http://" + ...` còn `payload = "http:` — hai gạch chéo của chính scheme URL bị đọc là
comment, guard **tự mù** và báo "không tìm thấy payload". Chi tiết:
[docs/history/2026-08-04-qr-ma-hoa-mdns-local.md](docs/history/2026-08-04-qr-ma-hoa-mdns-local.md).

**WiFiManager đã bị XOÁ (2026-07-29)** — `Wifi_Connect()`, `setting_Wifi()`, enum `eSettingWifi`,
`dashboardEnd()` (caller duy nhất là portal) và `lib_deps` đều đi hết: **−96 160 B flash**
(73.6% → 70.7%). Nhập WiFi nay **chỉ có một đường**: SoftAP fallback + captive portal → dashboard
Setting → `POST /wifi` / `/wifilist` (trial-then-commit). Device ID → `POST /deviceid`; firmware →
`/ota` + `/otaupload`. Nút XANH trong Setting menu giờ mở `eShowQR`. **Đừng thêm lại**: portal cũ
`disableCore0WDT()` mà không bao giờ bật lại (trong khi ControlTask giữ duty heater),
`resetSettings()` xoá creds mỗi lần vào màn, và SoftAP riêng của nó đá văng người đang xem
dashboard. Chi tiết: [docs/history/2026-07-29-remove-wifimanager.md](docs/history/2026-07-29-remove-wifimanager.md).

**Bật SoftAP THEO YÊU CẦU (giữ ĐỎ → XANH → QR)**: menu này là nơi người vận hành tới khi máy
chưa có WiFi dùng được, nên nó raise luôn hotspot và QR thành mã join WiFi.

- **`WiFi.mode()` chạy ở `dashboardLoop()` (NetworkTask)**, không bao giờ từ InputTask (GOTCHA 8).
  Nút chỉ `dashboardRequestAP()` đặt một byte volatile — cùng khuôn `otaState`.
- **Tiêu thụ cờ PHẢI nằm TRÊN `if (suspended) return;`** và từ chối khi `suspended || busy`. Nằm
  dưới thì bấm lúc đang upload sẽ **treo cờ lại**, hotspot bật lên ở tick đầu sau khi upload xong —
  vài phút sau, không ai liên hệ được với nút đã bấm.
- **Ghi `type_infor = eShowQR` TRƯỚC rồi mới `dashboardRequestAP()`.** Hai task, hai core, loop
  ~10ms: đặt cờ trước để lộ khe cho NetworkTask tiêu thụ khi `type_infor` chưa đổi → không ai xin
  vẽ lại, QR giữ URL của mạng radio vừa rời.
- **Gỡ hotspot canh theo ĐIỀU KIỆN "không còn ở `eShowQR`" trong `dashboardLoop()`, KHÔNG hook vào
  từng nút.** `handleLongPress_Red/Blue/White` đều ghi đè `type_infor` từ mọi state, nên liệt kê
  đường thoát là để lỗ — vào QR rồi giữ ĐỎ là máy **kẹt trên hotspot**, run sau không upload được.
  Giữ deadline riêng (`apExitAt`) thay vì arm `dashboardRequestRestart()` ngay, để **quay lại QR
  thì đứng xuống**; `otaRestartAt` dùng chung nên huỷ nó là huỷ cả của OTA/đổi ID.
- Điều kiện đó **không sống sót qua chính reboot nó gây ra** (`apOnDemand` false sau boot, fallback
  không set nó) → không phải vòng lặp reboot.
- `apOnDemand` tách khỏi `apActive`: AP lên do **fallback boot** thì rời QR **không** reboot.
  `otaRestartAt` nay có **ba** nguồn (OTA, đổi Device ID, đường này). Chi tiết:
[docs/history/2026-08-02-softap-theo-yeu-cau-tu-man-QR.md](docs/history/2026-08-02-softap-theo-yeu-cau-tu-man-QR.md).

SoftAP fallback: STA fail → `dashboardStartAP()` phát `FBT-<id>` (`dashboardApName()`, 192.168.4.1).
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
Người bị từ chối **vẫn xem được trang** (static + `/home` + `/curve`), chỉ không có live.
Đo thật: 2 người → #3 nhận `503 Too many viewers`, sau khi 2 người rời thì #3 vào lại `200`.

**NHƯNG tab bị từ chối KHÔNG tự hồi — phải RELOAD** (sửa 2026-08-06; trước đây mục này ghi
"`EventSource` tự retry nên có người rời là vào được ngay", **sai**). Theo HTML spec, response
có status **khác 200** (hoặc sai content-type) làm UA **"fail the connection"**: bắn `error`
**một lần** rồi đặt `readyState = 2 (CLOSED)` **vĩnh viễn**. Auto-reconnect chỉ áp dụng cho
đứt giữa chừng/EOF, **không** cho lỗi HTTP. Đo bằng thực nghiệm (endpoint trả 503 trong 6 s
rồi chuyển sang 200): browser gửi **đúng 1 request**, `readyState=2` suốt 18 s sau khi
endpoint đã khoẻ; reload thì `OPENED after 0 errors` ngay. Hệ quả cho UI/tài liệu: người thứ
3 phải **tải lại trang** khi có suất trống — đừng hứa nó tự vào.

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

## Thuật toán gọi kết quả (`src/Alg/`)

Chữ kết quả: **P** Positive · **N** Negative · **S** Slight positive · **E** Error ·
**B** Break · **F** Flagged (**từ v2.4.3AT; trên v2.4.4A / v2.4.5AT là env mặc định `esp32dev`**).
Từ `v2.4.5at`, **`F` có HAI lý do** (`shape_flag` là mã, không phải bit): **1** = dải ngưỡng
(`removed_by_new_gate()`, bản `a` hạ xuống `N`) · **2** = tăng sớm (qua cả hai ngưỡng, hình dạng
nhận ra được, nhưng Ct < `MIN_CALLABLE_CT` 3.0 — **vẫn `F` ở cả hai build**, không phải âm tính).
Giếng "tăng quá sớm" **không còn ra `!`/`E`**: `E` trả về nghĩa "phân tích không chạy được".

Đường đi của một đường cong: hiệu chuẩn `(raw − origin) / slope` → trừ baseline (trung bình
các điểm trong khoảng `baseline_start` … `+baseline_range` phút) → làm mượt Savitzky-Golay
9 điểm bậc 2 → đạo hàm → `sharpness` là đỉnh đạo hàm sau mốc `detection_margin_time`, `Ct`
là lúc tốc độ tụt còn 40% của đỉnh khi đi ngược lại, `increase` là mức plateau trừ mức tại
điểm chuyển.

| Tham số | v2.4.3 / v2.4.4 | v2.4.3AT / v2.4.4A | v2.4.5AT (`v2.4.5at`) |
| --- | --- | --- | --- |
| Số vòng | 120 (40 phút) | **90 (30 phút)** | 90 |
| `min_increase` | 20 | **25** | 25 |
| `min_sharpness` | 5 | **8** | 8 |
| baseline | 3 + 4 | **2 + 2** | 2 + 2 |
| Chữ `F` | không có | **có** (1 lý do) | **2 lý do** |
| Giếng đã khuếch đại, Ct sớm | `< 4.0` → **`E`** (`detection_margin_time`) | `< 4.0` → `E` | `< 3.0` → **`F`** lý do 2, `≥ 3.0` → **`P`** (`MIN_CALLABLE_CT`, `Algo.h`) |
| Rising-scan màn hình vs upload | mẫu 4 vs 12 (lệch) | lệch | **cùng** `marginToSampleIndex()` |

⚠️ Sửa giá trị mặc định trong `define.h` **KHÔNG** tới được máy đã cấu hình — phải qua di
trú EEPROM, xem `ADDR_CONFIG_REV`. Máy nào `GET /selfcheck` báo 40 phút là chưa di trú.

Tài liệu (đọc theo thứ tự này):

1. [docs/history/2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md](docs/history/2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md) — thuật toán hoạt động thế nào
2. [docs/history/2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md](docs/history/2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md) — quy tắc hình dạng, chữ `F` từ đâu ra
3. [docs/history/2026-08-16-min-sharpness-8-tail-climb-window-rate.md](docs/history/2026-08-16-min-sharpness-8-tail-climb-window-rate.md) — vì sao ngưỡng 8.0, và TAIL climb repair
4. [docs/history/2026-08-15-di-tru-cau-hinh-va-tu-kiem-tra.md](docs/history/2026-08-15-di-tru-cau-hinh-va-tu-kiem-tra.md) — di trú cấu hình + tự kiểm tra
5. ⚠️ [docs/history/2026-08-21-asf-va-quy-tac-hinh-dang.md](docs/history/2026-08-21-asf-va-quy-tac-hinh-dang.md) — **KHÔNG cài bản này lên máy chạy ASF**
6. [docs/history/2026-09-11-dieu-kien-tra-error-tang-som.md](docs/history/2026-09-11-dieu-kien-tra-error-tang-som.md)
   — **chỉ `v2.4.5at`** (4 commit `21f9331..d7775b1`): `MIN_CALLABLE_CT` tách khỏi
   `detection_margin_time` rồi hạ 4.0 → **3.0**; phép thử Ct đưa xuống **SAU** bằng chứng hình dạng;
   `E` → `F` lý do 2; `shape_flag` thành mã lý do; TFT **bật lại** nhánh `F` + `| ?? |` (`cf6e66e` đã
   comment); hai đường `check_risingData` cùng đơn vị; payload mang **7 trường** (`shape_flag`,
   `rise_width`, `window_rate`, `arm_width`, `suspect_score`, `climbs_fixed`, `climb_first_i`).
   Bằng chứng: 45 lượt sửa ERP 28/08–10/09, **15/16** giếng dính cổng Ct-sớm bị người duyệt lật sang
   `P`, tất cả Ct 3.0–3.7, 12/15 là chứng dương. ⚠ **Chưa phát lại bộ nhãn** (tool vắng trên nhánh),
   **chưa có guard**. **Đã chạy máy thật 15/09 và sàn 3,0 KHÔNG tới được sigmoid thường**: tay trái vẫn
   bị kẹp ở `discard_index − 1` (3,67′, `Algo.cpp:774`) nên dương thường có Ct 3,0–3,33 (k ≥ 1,5) hay
   3,33 (k ≥ 2) → `E`, dưới 3,0 → `E` chứ không `F`; sàn hữu dụng là **3,33–3,67′**. Đường dương thật
   `slots.txt` dịch sớm 1,0′ đã `E`. Ứng viên (kẹp theo chỉ số `MIN_CALLABLE_CT`) trên mirror lật đúng
   8 giếng ý đồ, 0 giếng khác — **chờ số trên bộ nhãn**, chưa sửa. Xem
   [docs/history/2026-09-15-kich-ban-ct-duoi-4-va-kep-tay-trai.md](docs/history/2026-09-15-kich-ban-ct-duoi-4-va-kep-tay-trai.md).

**Kế hoạch đang mở (2026-09-10) — chờ số:** (kế hoạch *điều kiện trả `!`* đã landed → doc 6 ở trên;
file [docs/plan/2026-09-10-dieu-kien-tra-error-tang-som.md](docs/plan/2026-09-10-dieu-kien-tra-error-tang-som.md)
giữ lại lập luận trung tâm — **`baseline()` trừ một HẰNG SỐ** nên cửa sổ baseline không ảnh hưởng
một phép so nào trong `predict_outcome_core()`, bất biến `baseline_start + baseline_range ==
detection_margin_time` không ràng buộc gì lên sàn Ct.)

- [docs/plan/2026-09-10-auto-gain-auto-origin.md](docs/plan/2026-09-10-auto-gain-auto-origin.md)
  — chất lượng đọc cảm biến quang. **Bản sửa 2026-09-10 sau khi ĐO** (`python
  tools/probe_sensor_noise.py sheet/test.json` tái lập mọi con số): nhiễu là **CỘNG TÍNH**, không
  đổi theo mức tín hiệu (`σ = −0.0001·mean + 2.06`, r = −0.03 trên dải 1.8×) ⇒ gain `k` cho SNR
  **`k` lần chứ không phải `√k`**, và quan trọng hơn: nhiễu cộng tính là **mỗi LẦN CHUYỂN ĐỔI**.
  ⇒ **Đòn bẩy lớn nhất KHÔNG cần đụng gain**: giữ nguyên tích `N × IT` (8×100ms → **4×200ms** →
  2×400ms) thì **thang lưu không đổi một đơn vị nào** — `slopes`/`origins`/`min_increase`/
  `min_sharpness` giữ nguyên nghĩa, **không phải recalibrate** — mà nhiễu giảm `√N` (**×1.41 →
  ×2.0**). Ba điều bản đầu nói sai, nay đã sửa trong file: (1) *"auto-origin không đổi được
  verdict"* chỉ đúng với origin **hằng số**; đo nền tối **mỗi vòng** là chuỗi thời gian và nó kéo
  trung vị `sharpness` **4.41 → 2.30**; (2) trần gain **×4** có nửa là ảo vì `DG` là digital
  (nhân cả tín hiệu lẫn nhiễu lượng tử) — `SENS`/`GAIN` đã kịch trần, trục analog duy nhất còn lại
  là `ALS_IT`; (3) chênh mức **1.80×** giữa các khe trong khi `slopes` chỉ chênh **1.19×** ⇒ đó là
  **OFFSET (ánh sáng tạp)** chứ không phải độ nhạy, nên **không** có thiên lệch hệ thống giữa các
  khe. ⚠ **`min_sharpness = 8.0` an toàn** (`P(>8.0)` từ nhiễu thuần = **0.000%**, cực đại 6.11 trên
  4000 lượt) — **nhưng 4.0 thì chưa**: dương tính ASF qPCR-xác-nhận nằm ở **4.0–8.4**, mà ở mức nhiễu
  hôm nay `P(sharpness>4.0)` là **1.6%/kênh = 14.9%/run** — cứ **7 run thì 1 run có một giếng vượt
  4.0 chỉ bằng nhiễu**. Ở mức **nhẹ hơn 2×** con số đó về **0.00%** — và nhẹ hơn 2× đúng bằng thứ
  `2 × 400 ms` cho. (Điều kiện **cần**, không đủ: bộ nhãn `tools/algo_labels.tsv` trả lời câu "đường
  không đặc hiệu có vượt 4.0 không" **không tồn tại trong branch này**.) Chặn cứng: C1 trần **~8191 count/lần đọc** (tổng-8 trong `uint16_t`), C2 không
  có phép kiểm cận trên lúc chạy, C3 `filterOdds` ngưỡng **tuyệt đối 3 count** — và nó đang gánh
  **hai** việc, cổng "đã chiếu sáng đủ chưa" là việc thứ hai (xem C10), C4 reconfig giữa run ghi đè
  bằng `Config_*`, C5 `VEML6035_SET_ALS_IT()` chỉ ghi nửa trường 4 bit, **C6 khoảng cách hai lần
  đọc là hằng số 100 ms không suy từ `ALS_IT`** (nâng IT mà quên → sai thang 8 lần, im lặng),
  **C7 `LED_DELAY_TIME` phải ≥ 2×`ALS_IT`** (nay 200 vs 100 — đúng bằng biên, không ai ghi),
  **C8 `store()` là mệnh đề luôn đúng** (`value != 0 || value < 1000`) — đừng sửa thành `&&`,
  **C9 `fixValuesErrors()` deref NULL khi `filterOdds` xoá sạch vector**, **C10 `calib_sensor()`
  không chờ LED ổn định** nên `slopes` đang dựa vào tác dụng phụ của C3, **C11 đọc ngoài mảng
  `sensor67Value[i][COUNTER-1]` ở vòng đầu**. Ứng viên nguồn nhiễu chung: **heater ĐÁY không kiểm
  `bSensorReadingGet()`** trong khi hotlid thì có (`PIDControl.cpp:1657`, comment ghi thẳng *"stop
  heating hotlid, to make sure the power is stable"*).

## GOTCHAS (quan trọng)

0. **`-Wformat` PHẢI ở trong `build_flags`** (`platformio.ini`). `Print::printf` có sẵn
   `__attribute__((format(printf,2,3)))` nhưng build này **không bật cảnh báo đó theo mặc định** —
   và vì thế `printf("... %s ...", i + 1, ...)` đã ship vào đường upload: `printf` coi số nguyên 1
   là `char*`, deref `0x00000001` → **LoadProhibited ở cuối run 40 phút**. Bật cờ lên lộ thêm
   **14 lỗi format có sẵn**, trong đó `%d` áp lên `double` ở **hai thông báo quá nhiệt**. Đã sửa
   hết để build **0 cảnh báo** — cảnh báo lúc nào cũng hiện là cảnh báo bị bỏ qua. **Đừng lọc
   warning khỏi output khi build** (`pio run | Select-String "error:"` giấu mất đúng loại tín hiệu
   này). Guard: `python tools/test_phase0_guards.py`. Chi tiết:
   [docs/history/2026-07-30-printf-loadprohibited-wformat.md](docs/history/2026-07-30-printf-loadprohibited-wformat.md).

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
4. **Firmware KHÔNG mount filesystem nào nữa** (2026-07-29): UI nằm trong flash, nhãn slot
   nằm NVS (`slotlabels`), `LittleFS.begin()` **đã xoá** → `spiffs` trống/hỏng/chưa format
   không còn hậu quả gì. **Đừng bao giờ thay bằng `LittleFS.begin(true)`** — format
   1 572 864 B kèm `disableCore0WDT()` trong khi ControlTask giữ duty heater.
   **UI nhúng trong firmware — `uploadfs` KHÔNG còn cần để đổi giao diện.** `data/` được
   `tools/pio_gzip_data.py` sinh thành **`src/webAssets.h`** (5 mảng `const uint8_t` +
   `WEB_ASSETS_ETAG`, gitignored, sinh lại mỗi build) và `webDashboard.cpp` phục vụ thẳng từ
   `.rodata`. Đổi file trong `data/` → chỉ cần **`upload`**. `serveStatic` **đã xoá** (nội dung
   `spiffs` cũ không với tới được nữa) — nghĩa là **thêm file mới vào `data/` mà quên đăng ký
   trong `kWebAssets[]` = 404 trên máy thật**; guard `python tools/test_web_assets.py` bắt đúng
   chỗ đó. RAM tốn **0**, flash 70.5% → **74.8%**. Chi tiết:
   [docs/history/2026-07-29-embed-web-assets.md](docs/history/2026-07-29-embed-web-assets.md).
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
12. **Route asset đăng ký CUỐI CÙNG** (chỗ `serveStatic` từng đứng): handler thử theo thứ
   tự đăng ký, nên bất cứ thứ gì khớp `/` phải nằm **sau** API. Thời `serveStatic` đặt trước
   API tốn 4 lần mở file LittleFS hỏng cho mỗi `/curve`/`/config` (spam `vfs_api ... does not
   exist`) và file trong `data/` trùng tên API sẽ **che khuất** route. `/home` sau khi sửa:
   ~21ms. Nay chỉ có **6 đường dẫn cố định** (`/`, `/index.html`, `/style.css`, `/script.js`,
   `/highcharts.js`, `/logo.png`) nên không còn dò file, nhưng thứ tự vẫn giữ nguyên.
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
  → `.noact` = opacity .85 + grayscale (**.35** trong code hiện tại; đo 4.68:1 hồi còn .65 — hạ
  xuống .35 để xanh/đỏ/trắng còn phân biệt được, và vì grayscale bảo toàn luminance nên tỷ lệ không
  tụt theo). Đừng quay lại opacity.
- **Nút `.on` (sáng khi nhấn) không làm sáng nền** — nền sáng từng kéo trắng-trên-đỏ xuống 3.11
  và trắng-trên-xanh xuống **2.28**, tệ nhất đúng lúc người dùng nhìn nút vừa bấm. Báo hiệu bằng
  **ring + glow**.
- **Badge P/N/S/E/B**: giữ hue, chỉ đậm chữ (3.66–4.38 → **5.01–5.09**). Nghĩa các chữ cái lấy từ
  `src/Alg/AlgoData.h:40-43` + `sensor6035.cpp:329`: **S = Slight Positive** (không phải Suspect),
  **B = Break** (không phải Blank) — `result[i]` là **ký tự đầu của chuỗi outcome**. Legend đã có
  trong tab Result.
- **Input phải ≥ 16px trên thiết bị touch** — iOS Safari **zoom cả viewport** khi focus input có
  `font-size < 16px`, mà mọi control đều dưới ngưỡng (`.sample-name`/`.slot-name` 0.85rem = 13.6px,
  `.f-in`/`.f-sel` 0.9rem = 14.4px) → chạm ô sample là trang nhảy. Fix bằng **một** rule
  `@media (hover: none) and (pointer: coarse)` đặt 16px cho cả 5 selector (desktop giữ 13.6px).
  **KHÔNG dùng `maximum-scale=1`**: nó chặn zoom bằng cách tước pinch-zoom của người thị lực kém
  trên thiết bị y tế — 16px bỏ *lý do* zoom, không bỏ *khả năng* zoom. Guard `test_device_id.py`
  fail nếu `maximum-scale`/`user-scalable=no` xuất hiện.
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
- **"Vào được layout 2 cột" ≠ "đủ chỗ cho mọi thứ trong 2 cột".** `.temp-grid.four` từng dùng
  **chung breakpoint 820px** với lưới 2 cột, nhưng ở 820 cột phải chỉ ~370px → 4 ô nhiệt độ còn
  **53px lòng ô** cho giá trị rộng **78px**: nhiệt độ **in tràn ra ngoài ô trên MỌI iPad dựng dọc**,
  mà `scrollWidth` vẫn sạch nên `test_no_hscroll` không thấy. Đo: tràn **+26px @820**, +16 @900,
  +3 @1000, vừa từ ~1100; giá trị rộng nhất là `"105.3 C"` (nắp) ≈ 91px. Nay 4-ô-ngang gác riêng
  ở **`min-width: 1280px`**, dải 820–1279 dùng lưới **2×2** sẵn có của điện thoại. Bài học: mỗi
  rule *bên trong* media query desktop phải tự hỏi **ở mép dưới 820px có vừa không**, đừng thừa
  hưởng breakpoint của layout. Guard: `test_no_hscroll.js` quét 6 kích thước 2 cột, **tự nhét giá
  trị rộng nhất** rồi so bề rộng chữ với lòng ô (đo thật, không chép công thức).
- **`1fr` TRẦN trong grid là bug, luôn dùng `minmax(0, 1fr)`.** `1fr` = `minmax(auto, 1fr)`, và
  cái hỏng là **minimum `auto`**: track lấy base size = min-content của item và free space âm nên
  `1fr` không bao giờ được áp. Đo trên máy thật: `.set-grid` track **350.7px trong khung 288px** ở
  điện thoại 320px → trang tràn ngang → **Chrome mobile nới layout viewport** (`innerWidth` 367)
  → `.bottom-nav` (`fixed; width:100%`) giãn theo trong khi header vẫn 320 = người dùng thấy
  **"giao diện lệch sang phải"**. `width: 100%` trên item **vô dụng** (item cũng có
  `min-width: auto`), `overflow:hidden` không giảm min-content, `min-width: 0` trên con chỉ kẹp
  used size chứ không kẹp contribution đẩy ngược lên. Trên **desktop lỗi này ẩn dưới dạng LỆCH
  CỘT** chứ không tràn: ở 1280/font 150% đo được **496px vs 388px**, `scrollWidth` sạch nên không
  có gì lộ ra. Guard `test_no_hscroll.js` gác cả hai mặt.
- **`.card-head` phải `flex-wrap: wrap`** kể từ khi nó mang cả tiêu đề lẫn control. Hàng cứng làm
  nhãn "All slots" (`white-space: nowrap`) **đẩy cả trang trượt ngang** ở 320px/130%: đo được
  `scrollWidth 347 / 320`, control thò ra ngoài card 48px. `test_no_hscroll.js` **không bắt được**
  vì `#homeChartCard` luôn `.hide` ở mọi state guard đi qua — nay guard tự bỏ `.hide` **trong cùng
  một tick** với phép đo (`renderHome` gắn lại sau mỗi frame SSE).
- **Guard phải MỞ một panel Setting, không chỉ xem lưới card.** Lưới chỉ có 4 hàng ngắn và luôn vừa;
  nội dung rộng nằm trong panel. Một lỗi tràn thật ở panel WiFi (hàng mạng đã lưu, 366px trong 320)
  nằm đó không ai thấy vì guard chỉ nhìn lưới. Nay nó mở card `wifi` và **chờ quét xong** — không
  chờ thì danh sách chỉ là một dòng "Scanning..." và lại không đo được gì.
- **KHÔNG ghim px vào cột bảng bằng JS.** `fitNameColumn()` (đã xoá) ghim
  `th.style.width` với sàn cứng 210px; cộng 2 cột `3.2rem` là **312 trong card 254** ở máy 320px,
  và **344 trong 212** ở desktop 820px. `table-layout: fixed` + hai cột value cỡ rem **đã** trả
  đúng phần còn lại — đó chính là cái kẹp, miễn phí, ở mọi bề rộng và cỡ chữ. Đừng thêm lại.
- **Chiều cao chart nằm trong MỘT token `--chart-h`, khai báo trên `body` — KHÔNG trên `:root`.**
  `var()` trong custom property được thay **tại phần tử khai báo**: từ `:root` nó nướng cứng
  `--nav-h: 62px` của root, nên `body.nonav { --nav-h: 0px }` không bao giờ với tới và **mọi run
  mất 62px chiều cao chart**. Khai báo trên `body` thì nó giải theo `--nav-h` của chính body. Đo:
  606px → **668px** khi `.nonav`. Không viết lại công thức lần hai.
  Fallback `vh`/`dvh` giữ bằng **`@supports (height: 100dvh)`** chứ không bằng thứ tự khai báo:
  custom property nhận `100dvh` như token lạ trên trình duyệt cũ và **chỉ hỏng lúc DÙNG**, nên hai
  dòng `--chart-h` liền nhau sẽ không tự rơi về bản `vh`.
  ⚠ **`--chart-h` nay CHỈ là chiều cao của chart RỖNG** (chưa có run thì không có độ dài để suy
  chiều cao ra). Có dữ liệu là chiều cao **suy ra từ thang đọc**, `script.js` ghi inline và
  **không bao giờ nhả lại cho CSS** — xem mục dưới.
- **HÌNH DẠNG đường cong KHÔNG được đổi theo màn hình — một nấc lưới Y luôn = `CHART_MIN_PER_STEP`
  (2.5) phút của trục X** (2026-09-10). Trước đó cả hai trục kéo giãn cho vừa khung nên "một nấc
  đáng bao nhiêu phút" đi từ **0.64** (ngang 844×390, plot chỉ cao **93px**) tới **7.28** (dọc
  412×915) — **lệch 11.4 lần**, cùng một cú lift-off vẽ ra **38.1°** hay **83.6°**. Nay **1.00×**.
  - `pxMin = max(CHART_PX_PER_MIN_MIN, plotWidth/scaleLen)`; **`plotHeight` SUY RA từ `pxMin`**;
    `window = min(plotWidth/pxMin, scaleLen)` → không đủ chỗ thì hiện **thanh trượt** (`.chart-pan`,
    **TRÊN** chart vì landscape card cao hơn viewport). `tickPositioner` giữ đúng 10 nấc nên phát
    biểu theo *nấc* độc lập với biên độ dữ liệu.
  - ⛔ **`scaleLen` của run LIVE = độ dài run DỰ KIẾN (`plannedRunMin()` = `(amplification time − 1)
    × time per loop`, từ `/config`), KHÔNG phải số vòng đã về** (2026-09-11). Bản 10/09 lấy
    `runLengthMin(v)` cho cả hai → ở Home, vòng 3 chart cao **3 472 px**, vòng 6 **4 142 px**, co dần
    mỗi 20 s tới vòng ~79 mới về 383 px — mà `min/step` **vẫn đọc 2.5 suốt**, nên guard cũ xanh (bất
    biến phát biểu theo *nấc* không nói gì về việc px/phút có đứng yên). `homeView.live = true`,
    `resultView.live = false` (run lưu vẫn **đo**: config có thể đã đổi sau khi ghi). Hai độ dài
    cố ý tách: thang từ `scaleLen`, **thanh trượt + bám đuôi từ `dataLen`** (`maxStart = dataLen −
    win`) — trục hiện sẵn 0..window từ vòng đầu, đường cong điền dần, tới khi data vượt cửa sổ thì
    thanh trượt bật. `/config` đọc **lúc boot** (trước SSE) và **ở sườn lên của thẻ chart**; chưa có
    thì rơi về `dataLen`. Guard section 6 chạy run thật trên mock và so chart Home vòng 3/6 với run
    lưu cùng kích thước; mock có **`POST /__reset`** (test hook) để guard trả nó về idle. Chi tiết:
    [docs/history/2026-09-11-chart-scale-run-live-theo-do-dai-du-kien.md](docs/history/2026-09-11-chart-scale-run-live-theo-do-dai-du-kien.md).
  - ⛔ **ĐỪNG kẹp `plotHeight` theo chỗ trống của viewport** — đã thử, **phá bất biến**: rút ngắn
    plot không rút ngắn trục X, run lại giãn lấp hết bề rộng, `min/step` tụt **2.50 → 1.76** mà
    chart trông vẫn bình thường. Chiều cao suy ra từ scale, **không bao giờ ngược lại**.
  - **Thanh trượt: nấc phải CHIA HẾT dải.** `<input type=range>` chỉ nhận `min + n*step` và trần
    thật là `floor((max-min)/step)*step` → step = một vòng (0.333) với max 13.9 dừng ở **13.65**,
    hụt một vòng, và tail-follow (tái vũ trang khi "đang ở max") **không bao giờ** tái vũ trang.
    Chia dải cho `round(maxStart/minPerRound)` nấc, **`floor` cái nấc rồi suy `max` TỪ nó** —
    `toFixed` làm tròn lên là `notches*step > max` và lỗi tái hiện.
  - **Legend tắt ở Result, GIỮ ở Home**: trên điện thoại Home **ẩn bảng slot** khi chart lên, và
    legend `#1..#10` chính là thứ thay thế nó. `spacingTop` phải **10**, không phải 2 — nhãn trục
    trên cùng vẽ căn giữa đường lưới nên 2px cắt đôi chữ `200`.
  - ⛔ **KHÔNG có chế độ thứ hai, và đừng thêm lại.** Nút **`Fit run`** (kéo giãn cho vừa khung,
    `aria-pressed`) đã làm xong rồi **gỡ bỏ trong cùng ngày**: hai thang đọc nghĩa là độ dốc của
    một đường cong **chỉ có nghĩa sau khi đã kiểm chế độ nào đang bật** — đúng điều kiện mà tính
    năng này sinh ra để xoá; ảnh chụp gửi cho nhau lại **không mang theo** trạng thái nút. Cái
    "tổng quan" không mất: thanh trượt cho xem đúng run đó, từng cửa sổ một. Guard ghim **sự vắng
    mặt**: `querySelectorAll('.chart-fit, [id$=ChartFit]')` phải rỗng **và** chiều cao chart phải
    còn là inline `<số>px` (nhả về CSS = hành vi cũ đi vòng).
  - Cái giá đã chốt: điện thoại **ngang** phải cuộn (card ~530px trong 272px khả dụng; header bỏ
    `sticky` dưới `max-height: 599px` để lấy lại 56px). Guard `node tools/test_chart_scale.js`.
    Chi tiết + đường đổi ý:
    [docs/history/2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md](docs/history/2026-09-10-chart-thang-doc-bat-bien-va-thanh-truot.md).
- **Desktop: hai thẻ ở tab Result cao bằng nhau.** `#screen-result.active:not(:has(#resultChartCard.hide))
  > .card:not(#resultChartCard) { height: var(--chart-card-h, var(--chart-h)) }` — `--chart-card-h`
  do `applyChartScale` ghi từ chiều cao **thật** của thẻ chart. Trước 2026-09-10 cả hai thẻ ghim vào
  `--chart-h`; nay chiều cao chart là **suy ra** nên token đó không còn là sự thật. Vẫn **một nguồn
  duy nhất** (chart), không có công thức thứ hai để lệch — và thẻ chart **bị loại khỏi rule** (nó tự
  co theo nội dung), nếu không thì vòng tròn: bảng theo chart, chart theo bảng.
  `#resultChartCard` vẫn là flex column; thẻ bảng `overflow-y: auto` cuộn 10 hàng bên trong.
  **Phải gác bằng `:not(:has(.hide))`**: khi chưa bấm "View chart" thì layout co về một cột, mà bảng bị
  ghim theo chiều cao viewport sẽ thành một hộp cao lêu nghêu chứa một bảng ngắn.
- **Dải 481–819px phải nới `--maxw: 100%`** (media riêng, đặt **TRÊN** rule landscape). `--maxw`
  mặc định 480px viết cho điện thoại, mà **không có gì nới nó lại cho tới 820px** → tablet dọc hoặc
  cửa sổ trình duyệt nửa màn hình render một dải 480px với nền trang hai bên: đo được **170px mỗi
  bên ở 819px**. Người dùng đọc ra là **"hai viền trắng"** vì header xanh đậm là thứ duy nhất có
  màu mạnh và nó cũng dừng ở mép cột. **Thứ tự nguồn là bắt buộc**: 700–819px rộng **và** dưới
  600px cao khớp *cả hai* rule, và ở đó cap 700px của landscape mới là lựa chọn đúng (điện thoại
  xoay ngang không phải tablet). Đo lại sau khi sửa: gutter = 0 ở 481/600/736/768/819, còn
  736×390 / 844×390 vẫn giữ đúng 700px. Chi tiết:
  [docs/history/2026-08-02-maxw-gap-481-819.md](docs/history/2026-08-02-maxw-gap-481-819.md).

**Bảng lỗi cảm biến trên web (`GET /errors`, 2026-08-05)** — bản sao bảng máy vẽ ở
`screen_errorResult()` (nút ĐỎ trên màn finished/review). Nút **"Error table"** ở tab Result
**thay chỗ** chart chứ không xếp dưới: hai thứ trả lời cùng một câu hỏi về cùng một run từ hai
phía, và một card cao bằng viewport nữa nằm dưới là bắt người ta cuộn qua thứ họ không xem.

- **Là SNAPSHOT, không phải đọc sống, và đó là toàn bộ thiết kế.** `error.error` là
  `std::vector` mà **ControlTask `push_back()` từ ~28 chỗ** bất kỳ lúc nào; `push_back` cấp phát
  lại, nên duyệt nó từ task AsyncTCP là **use-after-free** chờ ngày nổ. Chụp trong
  `dashboardSetResults()` — chạy trên DisplayTask (`screen_Result`) hoặc SettingTask
  (`/reviewlast`), **đúng task đã đọc vector đó để vẽ TFT**, nên không thêm phơi nhiễm mới.
- **Cùng thời điểm với cache CT/outcome** → `/errors` và `/slots` **không thể** mô tả hai run khác
  nhau (bài học desync bảng-vs-chart 2026-07-21). `ready` dùng **cùng biểu thức**.
- **Mirror đúng truy vấn của TFT** (`errorLightSensor` + `errorNoData` + `eSensor1stReading`), và
  in **cùng mã 4 chữ số** (`module*1000 + type*100 + step*10 + slot`) để đọc chéo hai màn hình.
  Nới rộng truy vấn ở một bên là hai bảng bắt đầu bất đồng về cùng một run.
- **`ready=false` ≠ "không có lỗi"** — client nói "No stored run to report on yet.", vì cấp giấy
  chứng nhận sạch cho một máy chưa chạy gì là thứ tệ hơn im lặng.
- **Trên tab HOME cũng vậy**: hết run bấm **ĐỎ** (chip web hoặc nút máy) → máy sang
  `escreenErrorResult` → `phase "errortable"` → Home **đổi chart sang bảng lỗi**, giữ nguyên
  strip nhiệt gọn + bảng slot. Web **đi theo máy**, không tự bày view riêng: ai đứng ở máy và ai
  cầm điện thoại nhìn thấy cùng một thứ. Bấm **TRẮNG** rời màn → về idle, mất cả hai.
- **`escreenErrorResult` phải có phase RIÊNG, không dùng chung `"error"` với `errprocess`** — cái
  kia là máy hỏng thật, cái này là người dùng *xin xem*. Gộp lại là lỗi PID sẽ kéo bảng lỗi lên
  Home. Cũng không được là `"finished"` (mock từng làm thế và giấu mất cả tính năng).
- Guard: `node tools/test_error_table.js` (mock `--reboot`) · `node tools/test_home_error_table.js`
  (mock `--full`, chạy trọn một run).

**Bảng Result = 4 cột theo thứ tự `màu | Result | CT | Sample`** (2026-08-02). Kết luận đứng
trước vì đó là thứ người ta mở tab này để đọc; định danh mẫu đi sau vì đó là thứ đã biết sẵn.
Chấm màu tách thành **cột riêng dẫn đầu** (`td.vis`), nên `.sample-cell` giờ chỉ còn select bệnh +
ô tên mẫu.

- **Thứ tự ô = thứ tự DOM, KHÔNG đảo bằng CSS.** Đảo thứ tự ô của `<table>` bằng CSS làm hỏng thứ
  tự đọc của screen reader — nó sẽ đọc kết luận trước khi nói kết luận đó thuộc mẫu nào. Guard
  `verify` so `getBoundingClientRect().left` theo DOM với thứ tự đã sort để ghim điều này.
- **Chiều rộng cột keyed bằng CLASS (`col-vis`/`col-res`/`col-ct`/`col-name`), KHÔNG `:nth-child`.**
  Rule cũ gác `:nth-child(2)/(3)`; thêm cột chấm vào đầu là chúng **âm thầm trỏ sang cột khác**.
- **Bảng NAMING (Home) giữ nguyên một cột**: ở đó không có cột kết luận nào để xếp thứ tự, và chấm
  phải nằm cạnh `#N` mà nó gắn nhãn.

**Bảng slot (lịch sử) = 3 cột** (`Sample | CT | Result`, trước là `Show|Slot|Disease|CT|Result`): cột Sample
gộp **chấm màu + số slot + select bệnh**. Chấm chính là **checkbox thật** (giữ bàn phím/screen
reader/`onToggle`) được style thành **màu series của slot đó trên chart** (`--series` set theo hàng;
`SERIES_COLORS` tách khỏi `buildSeries()`). Hai bẫy: **không** đặt `display:flex` thẳng lên `<td>`
(mất vai trò table-cell → viền hàng lệch; flex phải ở `div.sample-cell` bên trong), và cột CT/Result
để **3.2rem trên mobile** (5.5rem khiến cột Sample còn 110px → select 35px → **vùng chữ 2px**, mất
hẳn nội dung; desktop mới trả về 5.5rem). Hàng có `P`/`S` được `tr.hit` (nền `#fff7f5`, CT đậm) —
**sắc nền chỉ dẫn mắt**, nghĩa vẫn nằm ở chữ cái badge.

**`#N` chỉ có ở bảng NAMING, không có ở bảng Result** (`if (!withResults)` trong `buildTable`).
Ở Home anh đang khớp ống thật với tên nên con số **là** việc chính; ở Result nó lặp lại đúng thứ
mà vị trí hàng đã nói, mà 27px nó chiếm là ranh giới giữa hàng một dòng và hai dòng trên máy 360px.
**Không mất định danh**: bảng luôn đủ 10 hàng theo thứ tự, chấm màu đúng màu series mà legend của
chart gắn nhãn `#1..#10`, và `aria-label` của chấm vẫn đọc "Show #N on the chart".

**Bảng Result trên điện thoại ép MỘT dòng để thấy đủ 10 slot** (media mobile, chỉ
`#screen-result` — bảng naming ở Home giữ ô rộng để đặt tên). Hàng **88px → 43px**, đủ 10 slot
không cuộn trang ở 390 và 412px. Cơ chế: select + ô mẫu trước đây có flex-basis `5rem + 6rem`,
cộng chấm + `#N` + 3 gap là ~243px trong ô ~240px — **thiếu vài pixel là xuống dòng**. Hạ basis
xuống `3.8rem`/`4rem` là đủ chung một dòng.

- **`disease-sel` sizing theo nhãn DÀI NHẤT trong `DISEASES`, và phải `flex: 1 1 …` (grow 1).**
  Ghim `0 1` thì nó không lấy được phần dư và cắt cụt. Danh sách đã từ 5 mã 4 chữ lên **12 mục**,
  dài nhất `ASF I177L` = **58px** ở cỡ chữ 16px cảm ứng; ở basis 3.8rem content box chỉ 53px (390)
  và 35px (360) nên **`ASF I177L` và `ASF MGF` cùng hiện thành "ASF …"** — hai xét nghiệm khác
  nhau đọc ra như một, trên đúng bảng kết quả. Nay `5rem` + thu máng chevron
  (`padding-right: .95rem`, `background-position: right .25rem`) → **61-87px**, đủ mọi nhãn mà vẫn
  một dòng và vẫn đủ 10 slot. **Cắt cụt chỉ xảy ra ở ca MỘT DÒNG**: chỗ nào cặp control xuống dòng
  (320px, hay 412px ở font 130%) thì select đã chiếm cả ô và thừa chỗ.
  **Thêm nhãn dài hơn thì phải đo lại** — `probe`: đặt mọi select về nhãn dài nhất rồi so bề rộng
  chữ với `clientWidth - padding`.
- **`.sample-name` `min-width` là 3rem trên Result, 4.5rem ở bảng naming.** Nó là sàn giữ ô còn
  đọc được và là thứ làm cặp control **xuống dòng thay vì co thành hai mảnh vô dụng**. Hạ xuống
  3rem chỉ ở Result vì ở đó việc chính là **phân biệt hai xét nghiệm**, còn gõ tên mẫu diễn ra ở
  Home nơi ô giữ nguyên bề rộng. Xuống dòng ở 320px là phương án dự phòng, không phải lỗi.

**Ẩn/hiện TOÀN BỘ slot nằm ở GÓC PHẢI TRÊN của thẻ chart** (`label.chart-vis` chứa
`input.vis-all`), có trong **cả hai** thẻ chart — Home và Result. Cũng là checkbox thật, và trạng
thái "một số đang ẩn" báo bằng **`indeterminate` gốc của checkbox** — không cần widget hay chữ
nghĩa thêm.

- **Nó ở cạnh thứ nó điều khiển, không ở tiêu đề bảng.** Bản trước đặt trong `<th>` cột Sample:
  ô tiêu đề vừa là nhãn cột vừa là nút, nên chạm nhầm vào chữ "Sample" là xoá sạch 10 đường đang
  đọc. Trong thẻ chart thì `<label>` bọc **chữ của chính nó** ("All slots") → không còn vùng bấm
  nhập nhằng, và tên khả truy cập khớp chữ nhìn thấy (WCAG 2.5.3).
- **Ở góc phải là do `.card-head` có `justify-content: space-between`** — nó chỉ cần là con cuối.
  Trên Home nó đi cùng `.head-right` bên cạnh dòng "Last update".

- **`setAllVis()` redraw MỘT lần mỗi chart**, không phải mỗi series: `setVisible(.., true)` ×10 là
  10 lần redraw Highcharts, trên đường cong 120 điểm thấy giật rõ.
- **`syncVisAll()` phải gọi lúc LOAD**, không chỉ trong `applyVisTo()` — hàm đó chỉ chạy khi vẽ
  chart, mà markup ship sẵn `checked`, nên reload lúc đang ẩn vài slot thì header nói dối là đang
  hiện đủ. `syncVisAll` đọc localStorage chứ không đọc DOM nên đúng cả khi chưa có hàng nào.
- **Vùng bấm chỉ là cái chấm, KHÔNG bọc `<label>` quanh chữ "Sample"**: bọc thì bấm vào tiêu đề cột
  là ẩn sạch 10 đường — một cú chạm nhầm xoá hết biểu đồ đang đọc. Checkbox có `aria-label` riêng
  nên không cần label nhìn thấy.

Chi tiết: [docs/history/2026-07-23-ui-contrast-a11y-review.md](docs/history/2026-07-23-ui-contrast-a11y-review.md).

## Quy ước

- **Code/UI/comment: tiếng Anh** (an toàn font thiết bị). **Docs (.md): tiếng Việt.**
- **Mỗi thay đổi → 1 file `.md` có ngày trong `docs/history/`**; giữ README/CLAUDE cập nhật.
- Tài liệu ở `docs/` (`docs/history/`, `docs/GUI_SSE/GUI.md`).
- Ưu tiên dùng `codebase-memory` MCP tools để khám phá code (nhanh hơn grep).

## Test

### Chạy TOÀN BỘ guard bằng một lệnh

```bash
python tools/check.py            # mọi guard không cần mock/phần cứng, ~2 s
python tools/check.py --list     # guard nào tồn tại, bảo vệ cái gì; không chạy
python tools/check.py --mock     # thêm các guard cần tools/sse_test_server.py
```

`check.py` **tự tìm** `tools/test_*` chứ không giữ danh sách cứng, và báo riêng guard nào được ghi ở đây nhưng **không có** trong repo — một guard mà cả team tin là có mà thực ra không tồn tại thì tệ hơn không có guard. Exit code khác 0 khi có guard fail, nên dùng được để chặn merge.

### Không cần phần cứng

```bash
python tools/sse_test_server.py            # mock ESP32 -> http://localhost:8000
python tools/sse_test_server.py --full     # scale THẬT: 120 vòng x 20s = run 40 phút
python tools/sse_test_server.py --reboot   # boot như vừa tắt/bật: run cũ ở EEPROM, RAM trống
python tools/sse_test_server.py --slots tools/slots.txt --reboot   # PHÁT LẠI run thật từ file
python tools/sse_test_server.py selftest   # tự kiểm các hàm thuần
python tools/test_no_runtime_wifi_begin.py # guard: KHÔNG WiFi.begin() runtime ngoài setup()
g++ -O2 -std=c++17 tools/test_readcmd_overflow.cpp -o t && ./t  # readCommand không tràn recvData[2048]
g++ -O2 -std=c++17 tools/test_curve_length.cpp -o t && ./t      # /reviewlast quét đúng độ dài run (không tin amplification_time)
g++ -O2 -std=c++17 tools/test_wifi_store.cpp -o t && ./t        # saved-WiFi list: newest-to-front, no dup, cap 5, remove
node tools/test_wifi_e2e.js                 # E2E WiFi qua browser: list/pick/connect/forget/sai-mật-khẩu (tự bật mock)
python tools/test_no_method_branch.py       # guard: KHÔNG handler nào so req->method() (GOTCHA 3 làm POST rơi nhánh GET)
python tools/test_phase0_guards.py          # guard: không strcpy(parameter.*), 4 field char[10] được validate, secrets không nằm trong source commit, **-Wformat còn bật**
python tools/test_web_assets.py             # guard: UI nhúng đủ + có route + .gz không cũ + không serveStatic + KHÔNG LittleFS (GOTCHA 4)
python tools/test_ota_release_manifest.py   # guard: manifest GitHub đủ branch + versionCode vượt currentVersion ngoài đồng
python tools/test_ota_guards.py             # guard: OTA -> server (Bearer, so tên file), UI nạp gửi ?md5=, eUpdateOTA không "busy"
node tools/test_ota_md5.js                  # md5Hex() trong script.js là MD5 ĐÚNG (RFC 1321 + mọi độ dài 0..200 + firmware.bin thật)
python tools/test_upload_targets.py         # guard: mọi upload đi đủ 3 đích, từ MỘT danh sách
python tools/test_result_string_fits.py     # guard: chuỗi result upload không bị cắt mất chữ kết luận (tên bệnh dài)
g++ -O2 -std=c++17 -I.pio/libdeps/esp32dev/ArduinoJson/src tools/test_json_key_present.cpp -o t && ./t   # guard: POST /config chỉ áp key CÓ MẶT (cần -I, thiếu nó không build được)
node tools/test_profile_minutes.js          # guard: card Profile nhập PHÚT nhưng lưu giây/vòng, clamp 130 giữ nguyên
python tools/test_status_coverage.py        # guard: web không báo "Idle" khi máy đang chờ người; fillStatus/fillActions cùng tập state
python tools/test_slot_label_reset.py       # guard: nhãn slot bị xoá khi vào run mới (không upload tên bệnh run trước)
python tools/test_sim_cases.py              # guard: bộ kịch bản mô phỏng đúng ý đồ ở 5 slope, file simcases/ không lệch `gen`, ngưỡng mirror đọc từ source, `uploadResult` còn gác busy/STA, --regrade tìm đúng section
node tools/test_error_table.js              # guard: Error table thay chỗ chart, đủ 10 slot, mã trùng máy (cần mock --reboot)
node tools/test_home_error_table.js         # guard: hết run bấm ĐỎ -> Home đổi chart sang bảng lỗi (cần mock --full)
g++ -O2 -std=c++17 tools/test_wifi_bars.cpp -o t && ./t          # vach song WiFi tren TFT: nguong khop web + chong nhay
python tools/test_qr_payload.py             # guard: payload QR ≤ 53B (encoder KHÔNG bounds-check → reset), SSID không lệch 2 nơi
python tools/test_hotlid_pwm_cap.py         # guard: trần duty nắp nhiệt còn nguyên ở CẢ HAI PID, không đường ghi nào lách
python tools/test_device_id.py               # guard: ID 1 giá trị qua 2 store + 4 giới hạn khớp, input touch 16px (iOS zoom)
sh tools/test_break_trim.sh                # guard: cửa sổ cắt theo bậc thang phải bắt đầu SAU cú nhảy (A1)
python tools/test_algo_accuracy.py         # guard: đồng thuận với 659 kênh người phán + verdict bất biến khi rescale quang học
python tools/test_outcome_reset.py         # guard: mọi field của các class trong AlgoData.h phải được clear() reset (rò dữ liệu giữa 10 slot)
python tools/audit_logs.py <thu-muc-log>   # kiểm định thuật toán chẩn đoán trên kho log thật -> docs/reports/algo-audit.md
python tools/probe_sensor_noise.py sheet/test.json   # chất lượng đọc quang của một run: luật nhiễu, thành phần chung, sàn sharpness
node tools/test_full_run.js                # E2E full quy trình (chạy với --full)
node tools/test_review_reboot.js           # E2E xem lại run sau reboot (tự bật mock --reboot)
node tools/ui_screenshot.js <outDir>       # chụp 9 trạng thái UI (mobile/landscape/desktop) để soát thiết kế
node tools/test_chart_ticks.js             # guard: trục Y chart LUÔN đúng 10 nấc, sàn 200 (cần mock chạy sẵn)
node tools/test_chart_scale.js             # guard: hình dạng đường cong KHÔNG đổi theo màn/hướng cầm, kể cả run LIVE từ vòng đầu (cần mock --slots --reboot; tự POST /__reset)
node tools/probe_chart_scale.js            # đo thang đọc trên 7 kích thước; --after áp thử bộ hằng số khác
node tools/test_setting_a11y.js            # guard: tab Setting - nhãn gắn với ô, focus vào/ra panel, Nearby lọc, disabled không dùng opacity (cần mock chạy sẵn)
node tools/test_no_hscroll.js              # guard: KHÔNG màn nào trượt ngang (320-412px × font 100-130%) + 2 cột Setting bằng nhau (cần mock)
```

**Cắt cửa sổ theo bậc thang — `+ breakIndex + JUMP_SETTLE_SKIP`, KHÔNG phải `+ breakIndex`.**
`check_breakData` đo `_array[i+1] - _array[i]` nên **`breakIndex` là mẫu ngay TRƯỚC cú nhảy**; cắt
từ chính nó thì bậc thang vẫn nằm gọn ở hai phần tử đầu của cửa sổ. `differentiate` cho phần tử 0
sai phân **tiến** (chia *một* khoảng, `Algo.cpp:233`) còn điểm giữa dùng sai phân trung tâm (*hai*
khoảng) → bậc thang thành đạo hàm lớn nhất run, `argmax` bám vào, đỉnh ở chỉ số 0 **không thể có
tay trái**, `detected_ea()` nổ, kết luận thành `Error`. Lưới cứu ở `sensor6035.cpp:492` chỉ bắt
`'N'` nên `'E'` lọt qua — bậc thang thuần tuý cũng ra `E` thay vì `B`.

- **`+3` chứ không `+1`**: `+1` rơi vào đúng vùng tín hiệu vọt lố sau bậc thang, cú vọt lại vào
  phần tử 0 và lỗi tái hiện. `JUMP_SETTLE_SKIP` (nay ở **`Algo.h`**, không chép số vào
  `sensor6035.cpp`) là hằng số `checkJump` vốn đã dùng để bỏ qua vùng đó.
- **Chặn cửa sổ tối thiểu `2*sg_window+2` là bắt buộc**: `sg_smooth` trả **mảng toàn 0** dưới
  ngưỡng đó và **dòng báo lỗi bị comment** (`sgsmooth.cpp:537-541`). Sau khi vá, cửa sổ ngắn nhất
  có thể xảy ra là **đúng bằng ngưỡng** — biên an toàn bằng 0, mà `sg_window` sửa được từ
  web/Serial không validate. Đặt chặn **sau** cả khối `if (breakIndex)` để che luôn nhánh end-trim.
- **Không đụng nhánh end-trim** (`timeEnd = begin() + breakIndex`) — đó là biên loại trừ, vốn đã
  dừng trước bậc thang.
- Guard `sh tools/test_break_trim.sh` có **hai phần và phần thứ hai bắt buộc**: khối trim không
  biên dịch được ngoài thiết bị nên phần kiểm hành vi tự mang bản sao của nó — revert
  `sensor6035.cpp` thì phần đó **vẫn xanh** (đã kiểm bằng negative test). Vì vậy guard phải grep
  thẳng `src/sensor6035.cpp`.

**Khối phân tích slot chỉ có MỘT bản: `analyseSlotCurve()`.** Trước 2026-08-11 nó tồn tại hai bản
55 dòng (`bResultGet` → TFT/dashboard, `bResultPutToGoogleSheet` → bản upload) và **chính việc chép
đó đẻ ra lỗi đơn vị**: v2.4.1 đổi đơn vị `risingIndex` ở đúng một bản, nên cùng một kênh ra hai kết
luận tuỳ nhìn ở đâu. Kèm theo, phép quy đổi phút→mẫu gom về **`marginSamples()`** (tính bằng
`double` — `60000 / OPTO_INTERVAL` là chia số nguyên, ở vòng 25 s ra 2 thay vì 2.4, trên 60 s ra
**0** — và có chặn chia-0 vì `timePerLoop` đặt được từ web lẫn Serial/BT). Guard ghim: **đúng 2**
nơi gọi `analyseSlotCurve`, **đúng 1** nơi gọi `check_risingData`, **đúng 1** chỗ quy đổi.

- **`mean()` và `find_crossing_lower_than_reversed()` nay chặn chỉ số âm.** `baseline()` đưa thẳng
  `-1` vào `mean()` khi cửa sổ không chạm `baseline_start`, và `find_sigmoidal_feature` đưa
  `discard_index - 1 = -1` vào phép quét ngược. **Lưu ý kiểu**: `find_crossing_higher_than(-1)`
  **không** đọc ngoài mảng (so `int` với `size_t` biến −1 thành số khổng lồ → vòng lặp không chạy);
  hai hàm kia thì so `int` với `int` nên **có** chạy. Kẹp biên chưa đủ cho `mean()` — dải rỗng phải
  trả 0, không thì mẫu số bằng 0.
- **`audit_logs.py` KHÔNG kiểm được thay đổi trong `sensor6035.cpp`** — nó chỉ link `Alg/Algo.cpp`
  và tự mang bản sao khối trim, nên sửa `sensor6035.cpp` ra số y hệt dù đúng hay sai. Chứng minh
  bằng so văn bản + guard đọc mã nguồn.
- **Đừng thay khối code bằng regex quét toàn file** khi mẫu tìm cũng xuất hiện trong đoạn vừa thêm:
  lần gộp đầu tiên regex khớp vào chính thân hàm mới và nuốt mất nó. Thay theo **dải dòng có neo
  kiểm hai đầu**.

Chi tiết: [docs/history/2026-08-11-break-trim-off-by-one.md](docs/history/2026-08-11-break-trim-off-by-one.md).

**Ngưỡng break đo trên ĐƠN VỊ CẢM BIẾN THÔ, có chủ ý** — `BREAK_MIN_INCREASE_RAW` (`Alg/Algo.h`).
Chỗ gọi chia nó cho slope, và phép chia đó **triệt tiêu** phép calibrate có sẵn trong `raw_data`
(`raw[i+1]-raw[i] = (sensor[i+1]-sensor[i])/slope`), nên phép so cuối cùng nằm trên **đếm thô**.
Break là hiện tượng **của máy** (ống bị va, bọt khí) nên độ lớn thuộc quang học chứ không thuộc
sinh học; ép về thang calibrate đo được **647/659** so với **653/659**, và 7 kênh mất đều là Break
thật **ở rìa phân bố slope**. **Đừng "sửa" phép chia này tưởng là lỗi** — nó từng bị báo là "chia
hai lần" và không phải. Hằng số này **tách khỏi `min_increase`** từ 2026-08-07: trước đó một field
gánh hai phép thử ở hai đơn vị, nên chỉnh độ nhạy khuếch đại từ web là âm thầm dời cổng break trên
mọi máy. Cái giá đã đo và **chưa giải quyết**: 143/726 kênh đổi verdict khi rescale quang học, tức
hai máy hiệu chuẩn đúng vẫn có thể bất đồng về cùng một đường cong. Chi tiết:
[docs/history/2026-08-07-tach-nguong-break-khoi-min-increase.md](docs/history/2026-08-07-tach-nguong-break-khoi-min-increase.md).

**Kế hoạch nâng độ chính xác đang mở**:
[docs/plan/2026-08-07-nang-do-chinh-xac-thuat-toan.md](docs/plan/2026-08-07-nang-do-chinh-xac-thuat-toan.md)
— Phase 0.2/0.3/1 đã xong; **Phase 1b (chuyển break sang thang calibrate) chờ quyết định lâm sàng**,
không phải chờ code — sau bản tách hằng số nó chỉ còn là một dòng.

**Đổi thuật toán chẩn đoán thì PHẢI có số, không được lập luận suông.** `tools/algo_labels.tsv`
giữ **726 kênh quang do kỹ sư đọc từng đường cong và ghi kết luận ĐÚNG** (trích từ 3922 lần chạy,
phủ mọi kênh mà logic bậc thang chạm vào). `python tools/test_algo_accuracy.py` phát lại chúng qua
**chính `src/Alg/Algo.cpp`** và đỏ khi mức đồng thuận tụt.

Ghim bốn thứ, mỗi thứ có lý do riêng: **tổng số kênh đúng** (≥ **653**/659 = 99.09%) để không đánh
đổi nhiều thắng nhỏ lấy một thua lớn · **dương tính giả** (≤ 2) vì đó là con số người vận hành hành
động theo · **bỏ sót dương tính** (≤ **3**) vì phép kiểm một-mẫu mua được việc giảm dương tính giả
**bằng đúng một** ca này · **`RPL03008-2026-08-12#9`** đích danh — kênh kỹ sư phán tận tay sau khi
máy báo Dương tính cho một bậc thang, và là lý do hệ số break là 2.2 chứ không phải 2.5.

**Vì sao guard này tồn tại:** ba thay đổi trông hiển nhiên đúng đã bị chính bộ nhãn này bác —
khử trôi khi ước lượng nhiễu (+1 ròng), lấy nhiễu phía yên hơn (0 ròng), và đưa phép thử pha lag
về theo cửa sổ đã cắt (**−6 dương tính thật**). Suy luận bằng hình dạng đường cong **không đáng
tin** ở đây.

- **Kênh nhiễu (`Z`) chấm riêng, chỉ báo cáo không assert** — máy không có nhãn "nhiễu" nên `B` là
  câu trả lời hợp lý duy nhất.
- **Điểm đồng thuận KHÔNG thấy được lỗi phụ thuộc slope** — mỗi kênh phát lại bằng đúng slope của
  nó nên thứ gì scale theo slope bị nướng vào cả hai vế và triệt tiêu. Vì vậy có phép kiểm thứ 5:
  nhân **cả** mẫu thô **và** slope với `k` (chỉ dùng **lũy thừa của 2** để phép nhân `double` chính
  xác tuyệt đối) → đường cong đã calibrate không đổi một bit, verdict phải không đổi theo. Hiện
  **143/726 kênh (19.7%) vẫn đổi** vì `min_increase` bị chia cho slope **lần thứ hai** ở
  `sensor6035.cpp:269` trong khi `raw_data` đã chia rồi (`:415`) và `Algo.cpp:656` so **không**
  chia. `MAX_SLOPE_FLIPS` là **chốt bánh cóc**, phải về 0 chứ không được nâng lên. Chi tiết + bảng
  quét ngưỡng: [docs/history/2026-08-07-calibration-invariance-check.md](docs/history/2026-08-07-calibration-invariance-check.md).
- **Ngưỡng trong phép kiểm một-mẫu KHÔNG phải tham số dò trúng**: mọi giá trị từ **0.25 đến 0.70**
  cho điểm y hệt trên 659 kênh. Nếu ai đó phải chỉnh nó để thay đổi của mình lọt qua, thay đổi đó
  sai chứ không phải ngưỡng sai.
- **Chỉ áp cho cửa sổ đã bị cắt** (`time_data.front() > 0`). Áp cho mọi kênh thì điểm tụt xuống
  96.51% và xáo trộn ~1200 kênh thay vì 27.

**Ba phép sửa của B2 — cả ba đều nhắm vào việc CHỌN NHÁNH CẮT, không nhắm vào ngưỡng kết luận**
(2026-08-14, 98.03% → **99.09%**). Cùng một gốc: `analyseSlotCurve` chọn nhánh bằng cách so
`risingIndex` với `breakIndex`, nên **một `risingIndex` sai làm cả run bị vứt** dù mọi ngưỡng P/S/N
đều đúng.

1. **`is_rising_trend` phải so độ tăng ròng với dao động** (`Algo.cpp`, tổng tăng ≥ ⅓ tổng quãng
   đường đi). Trước đó nó chỉ đòi "5 trên 6 sai phân dương và tổng > 0" — **không có yêu cầu biên độ
   nào**, nên một đoạn phẳng có nhiễu ±4 trên nền 830 báo "đang tăng" ở vòng 20, hai mươi vòng
   trước khi có gì xảy ra. Vì `risingIndex` khi đó **nhỏ hơn** bậc thang thật, `breakIndex >
   risingIndex` thành đúng → nhánh cắt-cuối giữ đoạn phẳng đầu và **vứt sạch phần khuếch đại**:
   5 kênh báo Break chỉ vì phân tích 13-32% đường cong. So tỷ lệ thì **không cần ngưỡng theo đơn vị
   máy**: đường lên sạch có `tổng tăng == tổng dao động`, nhiễu tình cờ kết thúc cao hơn thì tỷ lệ
   rất thấp. Mọi tỷ lệ từ **⅛ đến ⅖** cho kết quả y hệt (+5/−0, đúng 5 kênh đổi trên **cả kho
   39 220 kênh**) → ⅓ là **tính chất hình dạng**, không phải hằng số dò trúng. Đòi hơn cả tổng dao
   động thì loại gần hết đoạn tăng thật và kho sập còn 72.99%.
2. **Lưới cứu break bắt cả `'E'`, không chỉ `'N'`** (`sensor6035.cpp`). Lưới có đó vì **bậc thang
   làm kết luận mất tin cậy**, mà `Error` còn kém tin cậy hơn `Negative` — nó nghĩa là phân tích bỏ
   cuộc. Báo thứ ta **đã đo được** (một bậc thang) hơn là báo rằng không đo được gì. Khoảng hở này
   đã ghi từ 2026-08-11 ("bậc thang thuần tuý cũng ra `E` thay vì `B`"). 3 kênh đổi toàn kho.
   **Không liên quan** tới override `/E` lỗi cảm biến ở `Bluetooth.cpp`.
3. **Nhánh cắt-cuối chỉ chạy khi `breakIndex > risingIndex + RISING_WINDOW`** (`sensor6035.cpp`).
   `check_risingData` trả về chỗ **BẮT ĐẦU** một cửa sổ dài `RISING_WINDOW` đang lên, nên bậc thang
   rơi **trong chính cửa sổ đó** là cùng một sự kiện — không phải một đợt khuếch đại xảy ra trước
   nó. Lấy nhánh cắt-cuối ở đó là **lật ngược tiền đề của chính nó** ("khuếch đại rồi mới gãy").
   Không thêm hằng số mới: dùng lại đúng cửa sổ ở dòng trên. Mọi offset từ 2 đến 12 cho kết quả y
   hệt; từ 20 trở lên bắt đầu mất kênh.

**5 kênh còn sai KHÔNG nên đuổi tiếp — dữ liệu tự mâu thuẫn.** Xếp theo "độ tăng sau bậc thang chia
cho chính bậc thang": **0.98 (đúng là P) · 2.25 (đúng là B) · 2.47 (P) · 9.03 (P)** — ca `B` nằm
**giữa hai ca `P`**, nên không ngưỡng nào trên trục này tách được chúng. Kỹ sư phán bằng thông tin
không có trong đường cong (nạp mẫu gì, nhìn thấy gì ở máy). Ca thứ 5 khác loại: `RPL02014#4` là
khuếch đại sạch mà `find_sigmoidal_feature` trả `-1` — lỗi **dò đỉnh**, không phải lỗi cắt cửa sổ.

Chi tiết: [docs/history/2026-08-14-b2-chon-nhanh-cat-cua-so.md](docs/history/2026-08-14-b2-chon-nhanh-cat-cua-so.md).

**`audit_logs.py` — kiểm định thuật toán chẩn đoán trên kho log thật.** Đọc thư mục payload đã
upload, phát lại từng run qua **chính `src/Alg/Algo.cpp`** rồi xuất
`docs/reports/algo-audit.md`: bảng những kênh có vấn đề (lỗi cắt cửa sổ ở bậc thang, đọc ngoài
mảng, màn hình lệch bản upload, kết luận sát vách ngưỡng, replay không tái hiện được).

- **Liên kết thẳng `Algo.cpp`, KHÔNG chép công thức sang Python.** Bản chép sẽ tự đồng ý với chính
  nó trong khi thuật toán đang hỏng — đúng thứ tool này sinh ra để bắt. Vì `Algo.cpp` include
  `"../ForteSetting.h"` (kéo Arduino vào, không build được trên PC), driver **copy `src/Alg/` sang
  `.audit-build/`** cạnh một stub, rồi **hash các bản copy và in vào báo cáo** để người đọc biết
  đã chạy thuật toán nào.
- **Tham số đọc thẳng từ `src/define.h`**, không hardcode trong tool — hardcode là báo cáo âm thầm
  trôi khỏi firmware sau mỗi lần chỉnh tham số.
- **Khối cổng break/rise + cắt cửa sổ của `sensor6035.cpp` là bản SAO** (đặt một chỗ duy nhất trong
  `tools/replay_algo.cpp`, có đánh dấu) vì `bResultPutToGoogleSheet` cần cả firmware. Sửa
  `sensor6035.cpp` mà quên chỗ này thì báo cáo sai.
- Mỗi run phát lại **3 lượt** để tách ba câu hỏi khác nhau: máy đã **upload** gì
  (`sensor6035.cpp:441`), máy đã **hiện trên TFT** gì (`:288` — đơn vị khác), và **sau khi vá** thì
  ra gì.
- **Log bản cũ lệch là dự kiến** (thuật toán đã đổi qua các version) — báo cáo tách tỷ lệ tái hiện
  **theo từng phiên bản**, chỉ dòng v2.4.x mới dùng để kết luận đúng/sai. Giới hạn lớn nhất:
  payload **không mang tham số thuật toán** của máy (chỉ có `slopes`/`origins`/`LED_power`), nên
  máy nào đã chỉnh EEPROM sẽ lệch mà tool không thể biết.
- Bảng dài bị cắt theo `--cap` nhưng **phần thống kê phân bố tính trên TOÀN BỘ** hàng — cắt rồi
  thống kê sẽ mô tả 60 hàng đầu mà đọc như mô tả tất cả.
- **`shape_of()` là ý kiến thứ hai, cố ý KHÔNG dùng gì của `Algo.cpp`** (chỉ median + sai phân
  bậc một). Suy từ chính thuật toán thì nó luôn đồng ý với thuật toán và không bao giờ bắt được
  kết luận sai. Gán nhãn `amp`/`step`/`staircase`/`drift`/`flat`/`descending`/`dead` rồi đối
  chiếu với kết luận của máy.
  - **Chiều tin được: `P`/`S` mà đường cong là `step`/`flat`.** Một cú nhảy đơn lẻ hoặc một đường
    phẳng **không thể** là phản ứng — đó là sự thật về hình dạng, không phải phán đoán ngưỡng.
  - **Chiều KHÔNG tin được: `N` mà đường cong là `amp`.** Bộ mô tả không tách được trôi quang chậm
    khỏi khuếch đại chậm-nhưng-thật. Đã thử 5 bộ ngưỡng: số ca nhảy **22 → 97 → 36 → 744 → 136**,
    và phép thử "phải phẳng trước khi tăng" loại nhầm cả dương tính đã biết chắc (khuếch đại sớm
    thì cửa sổ đầu đã nằm trên đoạn dốc). Mục này đã siết chặt và chỉ là **danh sách cần người
    xem lại**. Đừng nới ngưỡng để "tìm được nhiều hơn".
  - Sửa `shape_of` thì **phải chấm điểm lại trên 4 run đã biết đáp án** trước khi tin số liệu.

**Wokwi (mô phỏng TFT, không cần máy)**: `wokwi.toml` + `diagram.json` ở repo root — build
`pio run -e esp32dev` rồi mở bằng extension "Wokwi for VS Code" (F1 → *Wokwi: Start Simulator*;
cần license key, bản community free). Chỉ để **soát màn TFT tĩnh** (boot / idle / QR / Setting):
VEML6035, TCA9548A, MCP23017, DS18B20 **không có part** → lỗi cảm biến trên màn là **kỳ vọng**,
và nhiệt độ không bao giờ lên nên các pha chờ nhiệt đứng yên. Nút web mock vẫn là đường test
chính cho dashboard. Chi tiết + giới hạn: [docs/history/2026-08-07-wokwi-tft-simulation.md](docs/history/2026-08-07-wokwi-tft-simulation.md).

**`test_chart_ticks.js`** khoá bất biến trục tung: **luôn đúng 10 nấc**, sàn **200** (sàn 50 cũ
đổi 2026-07-29 — giá trị VEML đã calibrate nằm ở hàng trăm, trục cao 50 biến nhiễu nền thành thứ
trông như tín hiệu), và **giãn theo dữ liệu chứ không cắt**. **Phải bật mock trước**
(`python tools/sse_test_server.py`) — test không tự bật, thiếu nó nó chỉ báo timeout. Chạy trên **chart thật** (đẩy data vào `Highcharts.charts` rồi đọc
`yAxis[0].tickPositions`) chứ không chép lại công thức — bản chép sẽ pass trong khi bản chạy thật
đã hỏng. 11 mức dữ liệu: rỗng → 0.4 → 49.9 → 50 → 57.3 → … → 1234.

**`test_setting_a11y.js`** khoá Setting #9-#12: hỏi **trình duyệt** `element.labels` chứ không
regex `script.js` — regex sẽ pass ngon trên markup mà trình duyệt từ chối liên kết, đúng loại lỗi
guard này sinh ra để bắt. Danh sách panel lấy từ **chính bảng `CARDS` của trang** nên card khôi
phục sau này (Calib, PID) được phủ ngay. **Cần mock chạy sẵn.** Ba bẫy đã dẫm phải khi viết, đừng
lặp lại: (1) bấm card sau **delay cố định** thì panel chưa tồn tại (card chỉ có sau khi
`loadConfig()` resolve) → phải **poll**; (2) "không control nào thiếu tên" đúng một cách **vô
nghĩa** trên panel rỗng → phải assert `n > 0`; (3) bản gieo lỗi để thử guard mà **anchor sai** thì
guard xanh và trông như đang hoạt động — phải grep lại file sau khi gieo. Negative test 5/5 đỏ đúng
check.

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

**Panel WiFi có HAI view** (`.wifi-seg`): **Connect** (quét + SSID + mật khẩu + Save & reboot) và
**Saved** (danh sách đã lưu, Connect/Forget). Gộp chung một cột làm panel dài tới mức nút Save nằm
dưới màn trên điện thoại. **Dùng `<input type=radio>` trong `role="radiogroup"`, KHÔNG phải hai nút
tự chế**: radiogroup cho sẵn phím mũi tên, **một tab stop** và trạng thái checked cho screen reader;
mỗi input bọc trong `<label>` riêng nên chữ nhìn thấy **chính là** accessible name.
Mặc định là **Connect** — đó là lý do người ta mở panel. Segment đang chọn báo bằng **chip trắng
nổi (box-shadow)**, không chỉ bằng màu. **`loadSavedWifi()` vẫn chạy dù view Saved đang ẩn** vì nó
cũng nuôi `wifiSavedSsids` mà danh sách Nearby bên Connect lọc theo.

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
reboot (begin runtime treo async_tcp). Web chặn SSID-không-có-trong-scan trước reboot. Có nút **Scan again** (`.wifi-rescan`): **chỉ icon**, đặt ở **góc phải trên** của khối quét bằng
`position: absolute` trong `.wifi-scan { position: relative }` — **KHÔNG đưa vào `<summary>`**, vì
nút lồng trong summary là nested interactive content và cú bấm còn gập/mở luôn khối trừ khi chặn tay
từng event. Bỏ chữ thì **tên khả truy cập biến mất cùng nó** → phải có `aria-label` + `title`
("Scan again"). Ô vuông **30px** vì icon 14px một mình chỉ cho vùng chạm 20px — và vì thế **hàng `summary` phải
CAO bằng nút**: line box của nó chỉ 16px (22px ở font 130%), nên nút neo `top: 0` thò **9px** xuống
và **đè lên hàng mạng đầu tiên**. `min-height: 30px` giữ chỗ, `line-height: 30px` canh giữa chữ;
`display` vẫn phải là `list-item` (đổi khác là mất tam giác disclosure). Thêm
`padding-right: 2.4rem` để chữ không chui xuống dưới nút khi font phóng to. Bấm → disable +
`pollWifiScan(0)`; **`renderWifiList()` là chỗ duy nhất bật lại** vì mọi đường thoát của poll
(có list / hết lượt / lỗi) đều đi qua đó, nên nút không thể chết cứng.

**`.wifi-item` phải `flex-wrap: wrap`**: hàng mạng đã lưu mang SSID + badge "connected" + tối đa hai
nút, để cứng thì ở 320px/130% nó dài **366px trong client 320** và kéo cả trang trượt ngang.

Hàng quét hiện **biểu tượng sóng + ổ khoá** thay cho chữ `lock  -73 dBm`: 3 cung sáng dần (`rssiBars()`, ngưỡng `-60/-70/-80`) cộng padlock khi mạng có mật khẩu. **dBm KHÔNG mất** — nó nằm ở `title`/`aria-label` ("Signal good (-61 dBm), password required") để screen reader và kỹ sư dò sóng yếu vẫn đọc được. Mạnh/yếu mã hoá bằng **SỐ CUNG SÁNG, không bằng màu** (cả glyph là `currentColor`); cung chưa sáng để `opacity .22` làm nền đếm, còn **chấm luôn đậm hết cỡ kể cả `sig-0`** — làm mờ nó thì cả glyph xuống ~1.9:1 và trông như lỗi render chứ không phải "không vạch". Danh sách
"Nearby networks" nằm trong **`<details id="wifiScan">`**, mở sẵn và **tự gập khi chọn một mạng**
(ô SSID/Password lên trên, không phải cuộn qua chính danh sách vừa dùng xong; đo được 521→377px).
Hàng chỉ ẩn chứ không xoá, bấm `<summary>` mở lại. Dùng `<details>` để lấy sẵn vùng bấm, phím
Enter/Space và trạng thái cho screen reader; **`summary.f-lbl` phải `display: list-item`** — `.f-lbl` đặt
`block`, mà `<summary>` block thì **mất tam giác disclosure** ở Blink/WebKit, tức mất đúng tín hiệu
"khối này gập được". Forget dùng
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
EEPROM preferred **giữ nguyên** (portal `Wifi_Connect` cũ chỉ ghi EEPROM, không ghi list NVS —
portal đã xoá 2026-07-29 nhưng cặp EEPROM vẫn là "preferred" mà `connectSavedNetworks()` thử trước). Mock
nay có `_wifi_live()` dùng chung cho `/home` + `/wifilist` và hook `POST /wifilist?current=` để
tái hiện trạng thái fallback. Chi tiết:
[docs/history/2026-07-27-wifi-connected-badge-and-connect-gate.md](docs/history/2026-07-27-wifi-connected-badge-and-connect-gate.md).

### Đo chất lượng đọc quang từ payload thật (`probe_sensor_noise.py`)

```bash
python tools/probe_sensor_noise.py sheet/test.json              # payload đã upload
python tools/probe_sensor_noise.py tools/slots.txt --calibrated # file đã calibrate
```

Bảy mục: mức + biên tràn · **luật nhiễu** (hồi quy σ theo mức → cộng tính / bắn / nhân) ·
**bậc thang đồng bộ** (và nó cộng tính hay nhân — quyết định đo nền tối có bắt được không) ·
**thành phần chung giữa 10 kênh** · `sharpness` trước/sau khi bỏ thành phần chung ·
**sàn `sharpness` của đường PHẲNG + nhiễu** (thứ `min_sharpness` phải vượt qua) · bảng ngân sách
`N × IT`.

- **Nó KHÔNG chép công thức từ firmware** — nó mô tả *dữ liệu firmware đã sinh ra*. Chỗ duy nhất
  soi gương `Alg/Algo.cpp` là chuỗi baseline → SG → đạo hàm → đỉnh, **đánh dấu `MIRROR`**, vì một
  con số nhiễu tính bằng count thô thì tự nó không nói gì; `min_sharpness` so với `sharpness`, nên
  phải quy về đúng đại lượng đó. Đó cũng là chỗ duy nhất có thể trôi khỏi firmware.
- **Stdlib thuần** (tự dựng hệ số Savitzky-Golay kể cả hàng biên bất đối xứng) vì nó phải chạy
  được trên máy nào đang giữ log, không phải máy có numpy.
- **Chạy trên NHIỀU run trước khi tin một con số**: thành phần chung đo được **64%** trên
  RPL250701 nhưng chỉ **9%** trên `tools/slots.txt`. Đó là **một tình trạng của máy**, không phải
  hằng số của thiết kế.

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

### Debug `http://<id>.local/` vào không được (bán tự động)

```bash
python tools/probe_mdns.py rpl03003                       # chạy tới khi Ctrl+C
python tools/probe_mdns.py rpl03003 30                    # dừng sau 30 vòng
python tools/probe_mdns.py rpl03003 --ip 192.168.0.103    # biết IP: test HTTP cả khi mDNS câm
python tools/probe_mdns.py rpl03003 --iface 192.168.0.50  # PC nhiều NIC: chỉ định card WiFi
```

**Một triệu chứng (trang trắng + spinner), BỐN nguyên nhân** — probe đo ba thứ độc lập mỗi
vòng để tách chúng: truy vấn **mDNS thô** (máy có đáp không), **`getaddrinfo()` của OS**
(đường trình duyệt đi — hỏng được trong khi truy vấn thô vẫn chạy), và **`GET /home` theo IP**
(tách "tên hỏng" khỏi "máy hỏng"). Verdict gọi thẳng tên ca: **1** máy không trên LAN (rơi về
SoftAP — chế độ đó **cố ý không announce mDNS**, nên `.local` không tồn tại) · **2/2b** mDNS
câm hoặc chập chờn trong khi HTTP vẫn tốt (responder chết, hoặc AP chặn multicast) · **3**
tên phân giải ra **nhiều IP** = DHCP đổi IP + client cache bản ghi cũ → treo trên IP chết ·
**4** PC xanh hết = lỗi ở điện thoại. Chạy trên PC **cùng WiFi**, **tắt VPN** (mDNS là
link-local, khác subnet thì luôn ra ca 1 và sai).

⚠ **`MDNS.begin()` chạy ĐÚNG MỘT LẦN trong đời máy, không retry, không quan sát được từ
ngoài.** `dashboardBegin()` mở đầu bằng `if (started) return;` và `started` **không bao giờ**
về false (từ 2026-07-27 `dashboardSuspend()` chỉ đặt `suspended`, không hạ server) → khối
mDNS nằm sau `started = true` chỉ được đánh giá một lần. `MDNS.begin()` trả `false` lần đó là
`.local` **chết tới khi tắt/bật nguồn**, dấu hiệu duy nhất là **thiếu** dòng serial
`[dash] mDNS up ->`. Comment ở `webDashboard.cpp:2114` ("dashboardBegin re-runs after every
suspend/resume") **đã sai từ 2026-07-27**. Đường thoát có sẵn: màn **QR** in **IP** ngay dưới
mã — đó chính là lý do 2026-08-04 giữ cả hai dạng địa chỉ. Chẩn đoán + đề xuất sửa:
[docs/history/2026-09-07-mdns-local-vao-khong-duoc.md](docs/history/2026-09-07-mdns-local-vao-khong-duoc.md).

Mock **được web điều khiển** như máy thật: `waitamp` **đứng chờ** tới khi bấm Start (đỏ),
`finished` đứng chờ tới khi bấm White. Nhờ vậy gate đặt tên + chart sau run mới test được.
`--full` giữ **đúng khối lượng dữ liệu và trục X thật** (120 vòng, 20s/vòng → 39.67 phút,
`/curve` ≈ 7.7KB) nhưng nén đồng hồ còn ~20s.

**`--slots <file>` phát lại run THẬT** thay cho đường sigmoid tổng hợp — để soát chart (baseline,
làm mượt, trục) trên số liệu thật mà không cần máy. File: **mỗi dòng một slot**, các giá trị
**đã calibrate** ngăn bằng dấu phẩy (đúng dạng `/curve` phục vụ); số vòng và `AMP_ROUNDS` lấy
theo file, `REPORT_INTERVAL_MS` tự thành 20 s. Dòng thiếu → slot phẳng 0; dòng ngắn hơn run →
giữ giá trị cuối (bỏ trống sẽ thành **đứt đường**, không phải "hết dữ liệu"). Cắm vào **`reading_at()`**
— nguồn duy nhất của cả SSE `new_readings` lẫn `/curve` — nên phát lại đi đúng hai đường mà máy
thật đi. Kèm `--reboot` thì Result có chart ngay (không thì phải chạy hết một run).
Mẫu sẵn có: `tools/slots.txt` (10 slot × 120 vòng).

`tools/test_full_run.js` — E2E qua Edge headless + DevTools Protocol (chỉ dùng node
stdlib, poll DOM nên không race SSE). Đi hết: heater → waitamp (Start khoá, đặt tên) →
Confirm → **bấm Start** → amplification đủ 120 vòng → finished (chart ở lại) → Result
(đọc lại) → **bấm White** (chart mới mất).

### Kịch bản mô phỏng đánh giá KẾT QUẢ trên máy thật (COM) — `tools/sim_cases.py` + `tools/run_sim_cases.py`

```bash
python tools/sim_cases.py list                    # 14 kịch bản × 10 giếng, ý đồ từng giếng
python tools/sim_cases.py screen -v               # MIRROR pre-screen: mọi giếng đúng nhánh ở CẢ 5 slope đội máy
python tools/sim_cases.py gen                     # sinh lại tools/simcases/ (.cal.txt / .raw.txt / .expect.json / README.md)
python tools/run_sim_cases.py COM7                # ~6 phút: ParaRead → nạp 10 slot/kịch bản → getResult → chấm → docs/reports/simcases/
python tools/run_sim_cases.py COM7 --only S06     # một kịch bản; --only S10,S11,R03 = nhiều
python tools/run_sim_cases.py COM7 --regrade docs/reports/simcases/<log>.serial.log   # chấm lại log cũ, không cần máy
python tools/run_sim_cases.py COM7 --restore-from docs/reports/simcases/<log>.serial.log   # trả lại record run thật từ log
python tools/run_sim_cases.py COM7 --upload      # chấm xong mỗi kịch bản thì `uploadResult` → GAS + ingest + ERP THẬT (~15 s/kịch bản)
```

**Máy là oracle, mirror chỉ để chọn recipe có biên.** Máy này không có g++ nên không link được `Algo.cpp`
host-side; `sim_cases.py` chép lại thuật toán (đánh dấu MIRROR) **chỉ** để pre-screen — máy và mirror lệch
thì máy là phép đo. Ba trường kỳ vọng: `expect` (ý đồ) · `accept` (giếng cố ý sát biên) · `known` (điểm yếu
đã biết, máy hôm nay trả gì; không tính đậu/rớt). Dữ liệu sát thực tế theo số đo (nền 145–560, nhiễu cộng
tính σ≈1,5 count, warm-up leo từ dưới, bậc đồng bộ vòng 6, trôi ≤25/30′, dương A·k/4 = 20–60/phút, ASF 4–8).

- **Runner sinh lại dữ liệu theo đúng slope/origin/số vòng/ms-vòng của máy đang cắm** (`ParaRead`), không gửi
  `.raw.txt` đã commit (file đó ở slope danh nghĩa 1,4, dành cho `send_slots.py`). Máy phải **rảnh** ở màn
  chính; `getResult` đi qua `escreenReview` → **không upload**; xong bấm TRẮNG (reboot).
- **`--upload` là gửi THẬT, lên ba đích thật, dưới id của máy đang cắm** (`type_Upload "Manual"`, tên bệnh
  `N/A`) — 11 run mô phỏng nằm cạnh run thật của máy đó trên Sheet/ERP; report ghi giờ gửi, mã HTTP từng đích và
  `id`/`result_id` server trả về để tìm lại. Lệnh Serial **`uploadResult`** (v2.4.5at, 15/09) = ĐỎ trong menu
  Setting: `eUpLoadData` → `screen_Result('f')`; **từ chối** khi `dashboardDeviceBusy()` (kể cả khi lượt trước
  chưa xong — `eUpLoadData` là busy) hoặc không có STA (`'f'` không STA thì im lặng bỏ qua post). Ghi
  `type_infor` **trước** `changeScreen` — ngược lại DisplayTask vẽ lại màn cũ và không upload. Record trả lại
  cuối run **không** upload. **Đo 15/09 16:11: 11/11 kịch bản → 200 ở cả 3 đích, không retry, ~14 s/kịch bản,
  payload ~7,9 KB; ingest id 21760–21770, ERP `device_matched true`; chữ đường upload = `getResult` 110/110** —
  tức payload v2.4.5AT (7 trường mới + chữ `F`) đã qua server thật. Mỗi thân phản hồi server kết thúc bằng một
  byte `0xFF` (`readBodyDeadlined()` nối `(char)read()` = −1 ở EOF của TLS) — vô hại, chưa sửa. Chi tiết:
  [docs/history/2026-09-15-gui-bo-kich-ban-mo-phong-len-server.md](docs/history/2026-09-15-gui-bo-kich-ban-mo-phong-len-server.md).
- ⚠ **"Up Data" trên máy gửi tên bệnh `N/A`** dù run vừa xong có tên: `eUpLoadData` là busy → sườn lên của
  `isBusy()` trong `dashboardLoop()` xoá nhãn slot trong ~10 ms, trước khi `postData_GoogleSheet` dựng payload
  (~1 s sau). Có từ 20/08 (reset nhãn theo run), **chưa sửa** — xem "Còn nợ" trong tài liệu trên.
- **`--regrade` từng đặt kịch bản 0 lệch một section** khi log có cả backup read lẫn restore read (`offset` =
  tổng getResult − số kịch bản) → **110/110 STALE** mà report chỉ nói "re-run on the unit". Nay **khớp section
  với kịch bản bằng echo** (máy in lại nguyên văn message nạp; section nào không trùng kịch bản nào thì bỏ qua,
  kịch bản vắng trong log được liệt kê) — log cũ vẫn chấm được sau khi catalogue thêm/đảo thứ tự; guard chấm lại
  log 12:01 phải ≥ 90 PASS.
- **Vùng Ct < 4 (S10 · S11 · R03, 15/09 16:48: 20 PASS · 0 FAIL · 10 KNOWN-WEAK, máy = mirror 30/30)**: sàn
  `MIN_CALLABLE_CT` 3,0 **chết với sigmoid thường** vì kẹp tay trái 3,67′ — Ct 3,00 → `E` (k 1,5 và 2,0, và
  đường dương thật `slots.txt` dịch sớm), Ct 3,33 → `E` khi k ≥ 2; `F` lý do 2 chỉ với tới bằng hai pha, bậc 3,0′
  không vá + sườn sớm (Ct 1,67 giả), hay sườn k ≤ 0,7. **Bậc 3,0′ của RPL01015 được vá đẩy Ct 3,3 → 4,00 và biến
  `E` thành `P`**. Nhiễu σ3, warm-up 250, creep, xung hai vòng không dịch biên. `known="E"` giữ ý đồ trong
  catalogue, sửa xong cột tự đổi. Chi tiết + ứng viên sửa:
  [docs/history/2026-09-15-kich-ban-ct-duoi-4-va-kep-tay-trai.md](docs/history/2026-09-15-kich-ban-ct-duoi-4-va-kep-tay-trai.md).
- **Nạp là ghi đè record run cuối trong EEPROM.** Runner đọc record cũ bằng `getResult` trước và nạp trả lại
  sau (`raw_data` in ra là **sau** `neutralise_climbs` → run cũ có climb thì trả lại bản đã vá, báo cáo ghi rõ).
  Không dùng `EEPROMRead` để backup: `sprintf(tmp[4], "%02X", (char)c)` tràn với byte ≥ 0x80.
- **Mirror phải nhìn dữ liệu như máy nhìn**: raw nguyên → `(float(raw) − origin)/slope` **float32**
  (`sensor6035.cpp:293`), và quét 5 slope (1,0…2,2). Bậc +15 từng **bằng đúng** `4×range` tới bit cuối
  float32 trên RPL01015 → máy không vá, mirror double thì vá. Recipe không có biên, không phải lỗi máy.
- **JSON Serial của `bResultGet()` luôn in `climbs_fixed 0`** — `toJSON()` (`sensor6035.cpp:418`) chạy
  **trước** hai phép gán (`:427-428`); đường upload gán trước (`:613`) nên payload đúng. Runner đếm climb từ
  dòng `Slot N: neutralised K vertical climb(s)`. Sửa = dời hai dòng gán lên trên `toJSON()` (chưa làm).
- **Serial máy đan xen ở mức BYTE**: `[stack]` là 8 lần `Serial.print` riêng (`main.cpp:471-480`), cùng
  `[len]`, `finish one round maintenance`, `[dash]` rơi vào giữa hai chữ số của JSON (9/120 record lần đầu).
  Parser gỡ mảnh **biết trước** (không có xuống dòng) rồi quét tiền tố JSON theo schema → 120/120.
- **Đo được trên RPL01015 (15/09, 12:01): 106 PASS · 0 FAIL · 1 KNOWN-WEAK · 3 INFO / 110 giếng**, cùng
  chữ với mirror 110/110, Ct lệch ≤ 1 vòng. `MIN_CALLABLE_CT` 3,0 sống (Ct 3,33 → P); **F lý do 2 đã kích**
  (hai giếng hai pha Ct 1,67 / 2,67 → F, flag 2); **bậc +32 sạch → N đã vá, KHÔNG phải B** — `B` chỉ còn
  khi `2,5×range ≤ jump < 4×range` (nền nhiễu, +32 σ3 → B); **+40 σ4,5 → P** (`known`, họ lỗi RPL01004
  trên giếng nhiễu); **rise xong trước 4′ → N vô hình**; Ct thuật toán ≈ chân sườn (`slots.txt` slot 6 =
  4,33, không phải ~6,7). **Run thật của chính máy đó có bậc đồng bộ +45…58 ở phút 3,0 trên cả 10 kênh
  (kênh 1–3 trễ một vòng — đọc tuần tự) và `neutralise_climbs` vá ×10**; cùng bậc mà có đuôi warm-up
  trong 8 điểm nhìn lại thì thành **B** — S09 chép đúng mẫu đó. Record trả lại là bản **đã vá** (report
  ghi rõ). Chi tiết:
  [docs/history/2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md](docs/history/2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md).

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
