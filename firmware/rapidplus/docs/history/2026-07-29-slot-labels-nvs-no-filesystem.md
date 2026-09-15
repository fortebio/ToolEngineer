# Pha 2: nhãn slot sang NVS, firmware không còn cần filesystem (2026-07-29)

Thực hiện [docs/plan/2026-07-28-ota-fleet-upgrade-243.md](../plan/2026-07-28-ota-fleet-upgrade-243.md)
**Pha 2 (mục 10-11)**, nối tiếp [Pha 1](2026-07-29-embed-web-assets.md).
Kèm **2 lỗi của Pha 1 bị soát ra và đã sửa** (mục cuối).

## Mục 10 — `/slotnames.json` + `/slotsamples.json` → NVS

Hai file LittleFS thành **2 entry NVS** (namespace `slotlabels`, key `names`/`samples`), mỗi
cái **một chuỗi JSON** — không phải 20 key, vì cả bộ 10 nhãn đều được ghi lại mỗi lần đổi.
4 hàm `loadSlotNames`/`saveSlotNames`/`loadSlotSamples`/`saveSlotSamples` gộp còn **2 hàm**
`loadSlotLabels(key, dst)` / `saveSlotLabels(key, src)`. **Net âm dòng code.**

Cùng lý do đã đưa danh sách WiFi sang NVS (`wifiStore.cpp`): `uploadfs`/`uploadall` reflash
**toàn bộ** partition `spiffs` từ `data/`, nên mọi file ghi lúc chạy ở đó **bị xoá mỗi lần nạp
UI** — kể cả nhãn người vận hành vừa đặt. NVS nằm partition riêng (`0x9000`), `uploadfs` không
đụng. NVS cũng **thread-safe** và **không có buffer 4096 B dùng chung** như thư viện EEPROM
(CLAUDE.md Setting #2), nên `/rename` ghi thẳng từ AsyncTCP được, không cần mutex.

Sau bước này **`LittleFS.begin()` bị xoá hẳn** (kèm `#include <LittleFS.h>` ở `webDashboard.cpp`
và một include thừa ở `Bluetooth.h`). Firmware **không mount filesystem nào nữa** ⇒ `spiffs`
trống / hỏng / chưa format — trạng thái **nhiều khả năng nhất** của máy đến từ 2.4.2, vốn không
hề mount LittleFS — **không còn hậu quả gì**.

⚠ **Đừng bao giờ "sửa" bằng `LittleFS.begin(true)`**: nó format **1 572 864 B** kèm
`disableCore0WDT()` (`LittleFS.cpp:114-124`) trong khi ControlTask đang giữ duty heater trên
core 1.

**Đo thật**: flash 74.8% → **73.6%** (2 500 089 → 2 459 461 B, bỏ thư viện LittleFS **−40 628 B**),
RAM 77 204 → 77 172 B.

**Không migrate nhãn cũ** (`ponytail`): máy 2.4.2 không có file đó, còn máy 2.4.3 bench thì nhãn
là thứ người vận hành đặt lại trước **mỗi** run (card đặt tên ở `waitname`/`waitamp`). Giữ
`LittleFS.begin()` chỉ để đọc một lần rồi bỏ là trả đúng cái giá mà cả pha này đang gỡ.

## Mục 11 — `updateOTA.json` cho 4 branch

Soạn sẵn trong [tools/ota-release/](../../tools/ota-release/) (`v2.4.0`, `v2.4.0.x`, `v2.4.2`,
`versionCode 19`) + README ghi quy trình đẩy và lý do **không tạo branch `v2.4.3`**.
**Chưa đẩy gì** — việc này đẩy lên repo **public** bên ngoài và bị chặn bởi *rotate token*
(mục 9.1 của kế hoạch) cùng gate stack NetworkTask.

## Hai lỗi của Pha 1 bị soát ra (đều do đợt sửa hôm nay tạo ra)

**1. `eUpdateOTA` bị tính là "busy" ⇒ mục 9 tự giết đường OTA trên máy.**
`isBusy()` là allowlist, `eUpdateOTA` không có trong đó → `default: return true`. Mà mục 9 vừa
thêm `dashboardDeviceBusy()` **ngay trước** `httpUpdate.update()` ⇒ bấm ĐỎ trên chính màn
"có bản mới" → `OTA_FAILED`, **không tải một byte nào**. Nút ĐỎ lại không đổi `type_infor` nên
máy **kẹt luôn** ở màn đó: mọi `POST /config`, `/ota`, `/otaupload` **409** cho tới khi reboot.
Và ngay cả khi bỏ được guard đó, `dashboardRequestRestart()` cũng vô hiệu vì `dashboardLoop()`
chỉ reboot khi `!dashboardDeviceBusy()` — ảnh đã tải xong **nằm im vĩnh viễn**.

Sửa **một dòng ở gốc**: thêm `case eUpdateOTA:` vào allowlist. Đúng ngữ nghĩa của allowlist đó
(`eShowQR`, `errprocess` đã ở trong: "chỉ đang hiện gì đó, không chạy gì cả"), và màn này
**chỉ tới được từ `checkFirmware()` lúc boot** (đường web truyền `promptOnDevice=false`), nên
sau lưng nó chắc chắn không có run nào.

**2. `?md5=` hoa bị nhận rồi mới từ chối sau 2,3 MB.** `isxdigit` nhận `A-F`, nhưng
`Update.end()` so **case-sensitive** với digest của `MD5Builder` vốn **luôn thường**
(`sprintf("%02x")`). PowerShell `Get-FileHash` in **HOA** — đúng thứ người dùng Windows dán vào.
Sửa: `md5.toLowerCase()` trước `setMD5()`.

## Đo được gate go/no-go: một dòng log trong `updateFirmware()`

Gate lớn nhất của kế hoạch — *NetworkTask (stack 6144) có chứa nổi mbedTLS + HTTPClient +
Update không* — trước đây **không đo được**: census `[stack]` ở `main.cpp` in **10 giây/lần**,
mà reboot rơi **800 ms** sau khi tải xong, nên gần như chắc chắn trượt cửa sổ. Nay
`updateFirmware()` in ngay sau `httpUpdate.update()`:

```text
[ota] NetworkTask stack headroom after update(): <N> B free of 6144
```

In cả khi tải **thất bại** (dữ liệu vẫn có giá trị). `uxTaskGetStackHighWaterMark(NULL)` =
task đang gọi = chính NetworkTask, không cần `extern` handle. Tràn stack là **panic**, không
phải `HTTP_UPDATE_FAILED` — nên con số này phải đo, không được đoán.

## Release note bổ sung (cho người vận hành)

- **Nhãn bệnh/mẫu của từng slot bị xoá một lần** khi lên bản này (chúng chuyển từ filesystem
  sang NVS). Đặt lại như thường trước run kế tiếp — từ nay chúng **sống sót qua mọi lần nạp
  firmware/UI**, khác trước đây bị `uploadfs` xoá.

## Vòng soát thứ hai: 4 lỗi nữa, **tất cả** đều từ cơ chế hoãn-reboot của mục 9

Mục 9 ("đừng reboot vào giữa run") tự nó đẻ ra một họ lỗi. Gốc chung: **`type_infor` không đổi
suốt ~2 phút `httpUpdate.update()`** — `waittingUpdate()` chỉ vẽ đè.

1. **Máy báo "rảnh" suốt cả cửa sổ tải.** ⇒ `POST /wifi` được nhận → reboot giữa lúc tải;
   `POST /otaupload` được nhận → **AsyncTCP ghi vào cùng singleton `Update`** mà NetworkTask
   đang stream vào partition. Với đường web thì lỗ này **có sẵn từ trước** (màn `escreenStart`
   vốn "rảnh"); thêm `eUpdateOTA` vào allowlist chỉ mở rộng nó sang đường TFT.
   Sửa gốc: `dashboardDeviceBusy()` = **`otaState == OTA_UPDATING` || `isBusy(type_infor)`** —
   đang tải là bận, bất kể màn hình nói gì.
2. **Tải xong lại hiện đúng lời mời tải tiếp.** `HTTP_UPDATE_OK` để nguyên `type_infor ==
   eUpdateOTA` và `otaState` chưa terminal, mà `changeScreen = true` của tôi vẽ lại **prompt
   "Press red button: Update"**. Bấm ĐỎ (hoặc chip "Update" trên web — `fillActions` gắn nhãn
   đó suốt thời gian tải) → tải lại từ đầu, đẩy lùi reboot thêm 2 phút, **lặp vô hạn**, và
   xoá lại đúng partition mà `esp_ota_set_boot_partition` vừa trỏ vào.
   Sửa: `otaState = OTA_IDLE` (đóng latch đã bấm giữa chừng) + đưa màn về `escreenStart`
   **chỉ khi** nó còn là `eUpdateOTA` (đường web có thể đang chạy run phía sau).
3. **Reboot hoãn 40 phút rồi nổ đúng lúc upload kết quả.** `escreenFinished` nằm trong
   allowlist "rảnh", nhưng đó chính là state mà `screen_Result('f')` chạy: đọc lại run từ
   EEPROM, tính CT/kết luận, rồi **chặn 30-90 s trong mbedTLS** upload lên GAS + ingest + ERP.
   Reboot hoãn qua cả run sẽ bắn trong ~10 ms sau khi run kết thúc — tức **mất đúng kết quả
   mà nó được hoãn để bảo vệ**. Gate reboot nay thêm `!suspended` (cửa sổ upload) và
   `type_infor != escreenFinished` (đoạn tính toán + dump CSV trước đó); reboot rơi vào lúc
   người dùng bấm TRẮNG rời màn kết quả.
4. **`GET /otaupload` reboot được máy, không cần body.** Route đăng ký bằng `HTTP_POST` của
   `WebServer.h` (**= 3**) trong khi AsyncWebServer khớp **bitwise** với cờ riêng của nó
   (GET = 1) → `3 & 1 != 0` → GET rơi vào handler POST, `otaUpFail=false` + `!Update.hasError()`
   ⇒ **200 + hẹn reboot**. Đúng số học của GOTCHA 3, chỉ khác đường vào (đăng ký route, không
   phải so `req->method()`, nên `test_no_method_branch.py` không thấy). Lỗi **có sẵn từ
   trước**. Sửa **không đụng tới method**: cờ `otaUpSeen` — không có body thì **400**.

Kèm theo: hai đường thất bại của `updateFirmware()` **ép `type_infor = escreenStart` vô điều
kiện**. `type_infor` **là state machine**, không phải chỉ là màn hình — mà nút vật lý không hề
bị gate, nên người dùng có thể bấm chạy run giữa lúc tải; tải hỏng là **derail luôn run đó**.
Nay cả hai đi qua `showStartScreenIfIdle()`.

## Phụ: sàn trục Y của chart = **200** (không phải 50)

`data/script.js:370` đặt `Math.max(dataMax, 200)` từ commit `85a05b1` ("tmp"), trong khi comment
**cùng dòng**, CLAUDE.md, [2026-07-23](2026-07-23-ui-contrast-a11y-review.md) và guard
`test_chart_ticks.js` đều nói **50** → guard **đỏ 8/11 mức** từ trước đợt này. Chốt: **giữ 200**
(giá trị VEML đã calibrate nằm ở hàng trăm; trục cao 50 biến nhiễu nền thành thứ trông như tín
hiệu), sửa comment + guard + CLAUDE.md cho khớp. Guard nay có thêm mức `199.9`, `200`, `213`
quanh sàn mới → **all checks passed**.

`test_chart_ticks.js` cũng bỏ `sleep(2200)` cứng sau `Page.navigate` → **poll DOM** như các test
khác: mock phục vụ `highcharts.js` **634 KB chưa nén**, máy chậm hơn 2,2 s là `querySelector` trả
null, lỗi đọc như "UI hỏng" trong khi chỉ là đồng hồ. Docstring nay ghi rõ test **không tự bật
mock**.

## Bộ test e2e: 3 chỗ **stale từ trước**, không phải lỗi sản phẩm

Chạy lại toàn bộ suite sau đợt sửa. Ba assertion đỏ, **tất cả** do UI đổi ở commit `85a05b1`
("tmp") mà test không đổi theo — không liên quan code firmware đợt này:

1. `test_full_run.js` gõ chuỗi tự do `SampleC` vào ô bệnh, nhưng ô đó **nay là `<select>` danh
   sách cố định** `{PC,EHP,EMS,WSSV,TPD}`. Gán giá trị không có trong options → select về `""`
   → **cả chuỗi đặt-tên coi như không kiểm gì**, rồi đỏ ở bước Result. Đổi sang `EHP`.
2. Cùng file, cột CT đọc `children[3]` theo layout **5 cột cũ** (`Show|Slot|Disease|CT|Result`);
   bảng nay **3 cột** (`Sample|CT|Result`) → `undefined.textContent`, lỗi đọc **như trang bị
   crash**. Đổi sang `children[1]`.
3. `test_chart_ticks.js`: sàn 50 vs code 200 (mục trên) + `sleep(2200)` cứng.

**Đã kiểm chứng không phải lỗi sản phẩm**: probe CDP riêng trên bảng Result cho thấy chọn `EHP`
→ `/slots` nhận `"EHP"` → DOM giữ `"EHP"`. Chuỗi đặt tên chạy đúng; chỉ test là cũ.

Sau khi vá: `test_full_run.js` **ALL PASSED (0 failure)**, `test_review_reboot.js` ALL PASSED,
`test_wifi_e2e.js` all passed, `test_chart_ticks.js` all checks passed.

## `GET /reviewlast` — route POST duy nhất không có gì để từ chối

Cùng lớp với `GET /otaupload` ở trên. Soát **cả 5 route POST-only**: `/wifi`, `/deviceid` trả
**400** (thiếu param body — `hasParam(name, true)` luôn false trên GET không body), `/calib` trả
**400** (thiếu `action`), `/config` rơi vào handler **GET đăng ký trước đó**. Chỉ `/reviewlast`
**không có tham số nào để từ chối**: GET trần → `PEND_REVIEW` → SettingTask đọc 4 KB EEPROM,
**đè `sensor67Value` đang sống**, chạy lại `bResultGet` (~8 s). Không ghi EEPROM, không reboot,
và bị chặn khi máy đang chạy — nhưng **link-prefetch của browser hoặc scanner cũng kích được**.

Sửa: đòi **`?go=1`** (query param — **không** so `req->method()`, GOTCHA 3). Client
`data/script.js` gửi `POST /reviewlast?go=1`. **Không sợ lệch phiên bản**: từ Pha 1, UI nằm
trong chính firmware đó, nên không còn trang cũ nào ngoài kia gọi dạng cũ.

Mock cũng phải sửa: `do_GET` của nó **không** route `/reviewlast` nên GET rơi xuống static và
trả về trang HTML — tức **mock đang che đúng cái lỗ này**. Nay mock mirror hành vi bitwise của
thiết bị, và `test_review_reboot.js` có thêm 2 assertion: GET trần bị từ chối, và nó **không**
làm `/slots` chuyển sang ready.

Phụ (không sửa): `dashServer.on("/calib", HTTP_GET, ...)` là **dead code** — GET đã khớp dòng
`HTTP_POST` đăng ký ngay trước. Vô hại vì cùng trỏ một hàm; nếu hai bên phân kỳ thì dòng GET
**không bao giờ chạy**.

## Vòng soát thứ ba: 3 lỗi, đều nằm trong chính các bản vá của vòng hai

1. **`changeScreen = true` vô điều kiện ⇒ upload kết quả LẦN HAI.** `changeScreen` **không phải
   cờ vẽ lại**: với vài state, `switch` trong `displayLCD.cpp` chạy lại **việc thật**. Ở
   `escreenFinished` nó gọi `screen_Result('f')` = **cả pipeline cuối run** — đọc EEPROM, dump
   CSV, `postData_GoogleSheet()` — nên bật lại cờ đó sau khi tải xong sẽ **đẩy cùng một xét
   nghiệm lên GAS + ingest + ERP lần thứ hai** (kèm `postError_fullGoogleSheet` lần hai).
   `escreenReview` thì đọc lại EEPROM ~8 s. Mà `escreenFinished` **nằm trong allowlist "rảnh"**
   nên OTA hoàn toàn có thể bắt đầu ngay tại đó: run xong → chưa bấm TRẮNG → vào web bấm Update.
   Sửa: `changeScreen` chỉ đặt **bên trong** nhánh `type_infor == eUpdateOTA`. Giá phải trả:
   đường web giữ màn "Waiting..." tới khi người dùng bấm nút — đúng giá của việc **không** chạy
   lại pipeline sau lưng họ.
2. **Cờ `otaUpSeen` tĩnh rò sang request sau.** Upload đứt giữa chừng (đóng tab, WiFi chớp giữa
   2,3 MB) **không bao giờ tới** `handleOtaUploadDone`, nên cờ ở lại `true`; `GET /otaupload`
   kế tiếp lọt qua guard → **200 `{"ok":true,"restarting":true}` + hẹn reboot** trong khi
   **không nạp gì cả**. Đúng cái lỗ mà bản vá ấy sinh ra để bịt. Sửa: hỏi **chính request** —
   `req->hasParam("firmware", true, true)`; parser multipart ghi field file vào request
   (`WebRequest.cpp:540`, `isPost=true, isFile=true`). **Bỏ hẳn biến static.**
3. **Guard tự bịt mắt mình.** `test_ota_guards.py` kiểm "chuỗi `dashboardDeviceBusy()` có trong
   `updateOTA.cpp` không" — mà `showStartScreenIfIdle()` (thêm ở vòng hai) **cũng gọi hàm đó**,
   nên xoá đúng cái re-check thật thì guard **vẫn xanh**. Sửa: cắt thân `updateFirmware()` và
   đòi lời gọi **đứng trước** `httpUpdate.update(`. Kiểm ngược: xoá đúng re-check thật (giữ
   helper) → guard đỏ.

Bài học lặp lại ba vòng liền: **mỗi bản vá là code mới, và code mới cần được soát như code mới.**
Vòng ba tìm lỗi *trong các bản vá của vòng hai*, không phải trong code gốc.

## Vòng soát thứ tư: **0 lỗi** (11 ứng viên bị bác)

Vòng đầu tiên sạch, sau ba vòng liên tiếp mỗi vòng đều tìm ra lỗi thật trong bản vá của vòng
trước. Soát đúng 3 bản vá vòng ba: `changeScreen` thu vào nhánh · kiểm upload theo **request**
thay cho cờ static · guard cắt thân `updateFirmware()`.

Hai thứ **chưa** nằm trong vòng đó vì tôi sửa trong lúc nó đang chạy:

- **Kiểm "có part file nào không" thay vì `hasParam("firmware")`.** `onUpload` kích cho **bất kỳ**
  field file nào, nên `curl -F "file=@fw.bin"` **thật sự được nạp** (`Update.end()` đã chuyển
  boot partition) rồi nhận về "no firmware in request" — một câu trả lời mà **lần reboot kế tiếp
  bác bỏ**. Nay duyệt `req->params()`, nhận mọi part có `isFile()`.
- **Từ chối upload mới khi đã có ảnh chờ boot** (`otaRestartAt != 0`). Trong cửa sổ hoãn reboot,
  `Update.begin()` của upload thứ hai **xoá đúng partition mà `esp_ota_set_boot_partition()` đang
  trỏ vào**; upload đó fail giữa chừng thì reboot đang hẹn boot vào **ảnh viết dở**.
  Hệ quả chấp nhận được: máy đậu mãi ở `escreenFinished` với reboot đang hoãn sẽ **từ chối mọi
  upload** cho tới khi rời màn đó — nhưng lúc ấy đã có firmware mới nằm sẵn chờ boot rồi.

Còn `otaUpFail` (biến static duy nhất còn lại) **không** cùng lỗi với `otaUpSeen`: nó chỉ được
đọc sau khi đã có part file, và `index == 0` của upload kế tiếp luôn dọn nó.

## Đo trên MÁY THẬT (COM6, board RPL00001, STA `192.168.1.23`)

Nạp firmware của cây này rồi kiểm trực tiếp. Đây là lần đầu các thay đổi Pha 1/Pha 2 chạy trên
phần cứng — trước đó mọi khẳng định đều chỉ dựa vào đọc code.

**Boot:**

| Dấu hiệu | Kết quả |
| --- | --- |
| `there is para in the EEPROM with length 400` | **có** → calib/PID/EEPROM giữ nguyên qua bản mới (checklist mục 6 của kế hoạch) |
| `[heap] after BT release` | free 225 280 · `intLargest` **110 580** |
| `[heap] after dashServer.begin` | free 106 980 · `intLargest` **86 004** → thừa xa ngưỡng TLS 42 KB (GOTCHA 2) |
| `[stack] Network=2032/6144` (idle) | còn 4 KB dư — **chưa phải** con số gate, gate cần đo **sau một OTA thật** |
| `[dash] mDNS up -> http://rpl00001.local/` | mDNS chạy |
| `[ota] check failed: HTTP 404` | **đúng như kế hoạch dự đoán**: branch `v2.4.3` không tồn tại trên repo OTA |

**Pha 1 — asset nhúng, phục vụ từ flash:**

| Đường dẫn | Mã | Byte | Khớp bảng nhúng |
| --- | ---: | ---: | --- |
| `/` và `/index.html` | 200 | 2 999 | ✔ |
| `/style.css` | 200 | 12 919 | ✔ |
| `/script.js` | 200 | 26 088 | ✔ |
| `/highcharts.js` | 200 | 98 969 | ✔ |
| `/logo.png` | 200 | 6 897 | ✔ |

Header của `/`: `Content-Encoding: gzip` · `Cache-Control: no-cache` · `ETag: f981cec974d2ef39`.
`If-None-Match` đúng → **304, 0 byte**; ETag sai → **200, 2 999 byte**. Tức 147 KB chỉ đi qua dây
**một lần**, đúng mục tiêu khi bỏ ETag của `serveStatic`.

*Lưu ý*: `HEAD /` trả **404** — route đăng ký `HTTP_GET` (=1) mà cờ HEAD của AsyncWebServer là bit
khác, `1 & 4 == 0`. `serveStatic` cũ cũng chỉ nhận GET nên **không phải hồi quy**; browser không
dùng HEAD để tải trang. `curl -I` thì thấy 404 — không phải lỗi.

**Ba bản vá bảo mật — kiểm bằng curl:**

- `GET /reviewlast` → **400** `use POST /reviewlast?go=1` (trước: chạy nguyên pipeline ~8 s)
- `GET /otaupload` → **400** `no firmware in request` (trước: **200 + hẹn reboot** dù không nạp gì)
- `POST /reviewlast?go=1` → **200**, và `/slots ready=true` sau **185 ms**, `/curve count=120`

**Pha 2 — nhãn slot trong NVS:** `POST /rename?slot=2&name=EHP&sample=probe-01` → `/slots` đúng →
**hard reset (esptool RTS)** → vẫn `{"name":"EHP","sample":"probe-01"}`; `ready` về `false` đúng
như thiết kế (cache RAM mất, nhãn thì không).

**`/otaupload` toàn trình, 2,4 MB qua WiFi:**

| Thử | Kết quả |
| --- | --- |
| md5 **sai** (32 hex hợp lệ) | **500** `upload failed` — `Update.end()` từ chối, **không** reboot |
| md5 **sai định dạng** (31 ký tự) | **500** — bị chặn ngay ở chunk đầu |
| md5 **CHỮ HOA** đúng (kiểu `Get-FileHash`) | **200** `{"ok":true,"restarting":true}` → máy tự reboot vào ảnh vừa nạp |

Chữ hoa chạy được **chính là bằng chứng của bản vá `toLowerCase()`**: trước đó nó sẽ nuốt trọn
2,4 MB rồi mới từ chối. Sau reboot: `/ota` báo `version v2.4.3, versionCode 19`, và nhãn NVS
**sống qua cả hai lần reboot** (hard reset + reboot do OTA).

**Vẫn chưa đo được**: `[ota] NetworkTask stack headroom after update()` — dòng log đã có sẵn nhưng
chỉ in khi chạy `httpUpdate.update()`, mà `baseUrl` trỏ cứng vào `raw.githubusercontent.com/<version>`
nên **phải publish `updateOTA.json` lên branch tương ứng mới kích được**. Đường `/otaupload` không
thay thế được: nó chạy trên AsyncTCP, không phải NetworkTask, và không đụng mbedTLS.

## Soát hai bản vá làm trong lúc vòng 4 đang chạy

Hai thứ này không nằm trong vòng soát nào nên soát tay, đọc thẳng nguồn thư viện.

**Kiểm "có part file nào không" — ĐÚNG, và đúng vì một lý do mạnh hơn tôi nghĩ.**
`WebRequest.cpp:533-541`: `_params.emplace_back(..., isFile=true, ...)` nằm **trong cùng khối
`if (_itemSize)`** với lời gọi `handleUpload(...)`. Nên bất biến **"`onUpload` đã chạy ⟺ tồn tại
file param"** đúng tuyệt đối, không phải suy đoán. Kéo theo:

- part file **rỗng** (bấm Upload mà chưa chọn file) → không param → **400**, và đó là câu trả lời
  *thật*: `onUpload` chưa từng chạy, không byte nào được ghi.
- upload **bị từ chối** ở chunk đầu (busy / không phải `.bin` / md5 sai) → parser **vẫn** ghi param
  → `gotFile` true → **500 `upload failed`**, không phải 400. Đúng: có file, ta từ chối nó.
- `handleRequest` chạy tại `_parsedLength == _contentLength` (`WebRequest.cpp:157`), tức **sau khi**
  boundary cuối đã được parse → param luôn có mặt kịp.

**Từ chối upload khi đã có ảnh chờ boot — đúng, nhưng câm.**
Cả **bốn** lý do từ chối đều trả về đúng một câu `{"ok":false,"error":"upload failed"}`; lý do chỉ
nằm trên **cáp serial mà ngoài đồng không ai cắm**. Chính phiên làm việc này vừa chứng minh cái giá
đó: mất COM6 là mù hoàn toàn. Nay mỗi lý do có câu riêng (`device busy…`, `a firmware is already
staged…`, `not a .bin file`, `md5 must be 32 hex characters`), dùng chung vòng đời với `otaUpFail`
(cùng reset ở `index == 0`) nên **không rò sang request khác** — đúng lỗi mà `otaUpSeen` đã mắc.

Lưu ý còn lại (không sửa): `otaRestartAt` chỉ mất khi reboot, nên máy đậu mãi ở `escreenFinished`
với reboot đang hoãn sẽ **từ chối mọi upload** cho tới khi rời màn đó. Tự giải quyết bằng bất kỳ
tương tác nào, và nay người dùng **đọc được lý do** thay vì đoán.

## Guard mới

```bash
python tools/test_ota_guards.py    # 3 bất biến OTA
python tools/test_web_assets.py    # thêm check 6: không LittleFS trong src/
```

`test_ota_guards.py` khoá đúng 3 thứ **im lặng khi hỏng** (máy chỉ đơn giản là không update
được, không ai biết cho tới lúc cần vá gấp): `eUpdateOTA` phải nằm trong allowlist **chừng nào**
`updateFirmware()` còn gate theo busy · `rebootOnUpdate(false)` phải đứng **trước**
`httpUpdate.update()` · `?md5=` phải được hạ về chữ thường trước `setMD5()`.

Kiểm ngược: tái tạo cả 3 regression → 3 dòng FAIL; thêm `LittleFS.begin()` vào `dashboardBegin`
→ `test_web_assets.py` bắt.
