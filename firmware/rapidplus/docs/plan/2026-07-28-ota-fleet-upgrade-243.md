# Kế hoạch nâng fleet lên v2.4.3 **không dùng dây** (2026-07-28)

Trạng thái (cập nhật **2026-07-29**): **Pha 0, 1, 2 đã code xong và đo trên máy thật** (board
RPL00001 qua COM6 + LAN). Mọi số liệu dưới đây đọc từ source/artifact thật (file:line ghi kèm),
không suy đoán.

| Pha | Trạng thái | Nhật ký |
| --- | --- | --- |
| Pha 0 (mục 1-5) | xong — trừ **rotate token** (việc phía server) | [2026-07-29-phase0](../history/2026-07-29-phase0-ota-fleet-upgrade.md) |
| Pha 1 (mục 6-9) | xong — flash 70.5% → 74.8%, RAM +0 | [2026-07-29-embed-web-assets](../history/2026-07-29-embed-web-assets.md) |
| Pha 2 (mục 10) | xong — flash về **73.6%** (bỏ LittleFS −40 628 B) | [2026-07-29-slot-labels-nvs](../history/2026-07-29-slot-labels-nvs-no-filesystem.md) |
| Pha 2 (mục 11) | **chưa đẩy** — file soạn sẵn ở [tools/ota-release/](../../tools/ota-release/), chặn bởi rotate token | — |

**4 vòng soát đối kháng** đã chạy trên diff: **9 lỗi thật**, trong đó **8 do chính đợt sửa này
tạo ra** (nặng nhất: upload trùng kết quả xét nghiệm, `GET /otaupload` reboot máy, OTA tự huỷ
download của mình). Vòng 4 sạch. Chi tiết trong 3 nhật ký trên.

**Còn chặn rollout**: rotate ingest Bearer + ERP X-API-Key + redeploy GAS · và gate go/no-go
`uxTaskGetStackHighWaterMark(NetworkTask)` sau một OTA thật — **chưa đo được** vì `baseUrl` trỏ
cứng vào `raw.githubusercontent.com/<version>/`, phải publish `updateOTA.json` mới kích được
(`/otaupload` không thay thế được: nó chạy trên AsyncTCP, không đụng mbedTLS).

Phần chưa verify được đánh dấu rõ ở mục [Chưa đo được](#chưa-đo-được).

Liên quan: [CLAUDE.md](../../CLAUDE.md) · [docs/architecture/06-mang-va-upload.md](../architecture/06-mang-va-upload.md) ·
[docs/history/2026-07-24-web-ota-update.md](../history/2026-07-24-web-ota-update.md) ·
[docs/history/2026-07-22-secrets-out-of-source.md](../history/2026-07-22-secrets-out-of-source.md)

---

## 0. Quyết định trung tâm

> **Nhúng 5 file `.gz` vào firmware, bỏ hẳn `serveStatic`, không bao giờ ghi partition `spiffs` nữa.**

Nó biến câu hỏi *"trong `spiffs` của máy ngoài đồng đang có gì"* — câu **không ai trả lời được** —
thành câu **không cần trả lời**. Chỉ còn **1 artifact** duy nhất là `firmware.bin`, đi qua đúng
đường OTA mà mọi phiên bản cũ đã biết sẵn (`httpUpdate.update()`, `src/updateOTA.cpp:115`).

**Ngân sách (số đo thật, không ước lượng):**

| Mục | Byte | Nguồn |
| --- | ---: | --- |
| `firmware.bin` hiện tại | 2 361 728 | `.pio/build/esp32dev/firmware.bin` |
| 5 file `.gz` sẽ nhúng | 147 739 | `index.html.gz` 2 999 + `style.css.gz` 12 919 + `script.js.gz` 25 955 + `highcharts.js.gz` 98 969 + `logo.png` 6 897 |
| Sau khi nhúng | ~2 509 467 | cộng |
| App slot (`app0`) | 3 342 336 | `default_8MB.csv:4` (0x330000) |
| **Mức dùng** | **70.7% → 75.1%** | còn dư 832 869 B |

RAM tốn **0** — nằm `.rodata`, `beginResponse(int, const char*, const uint8_t*, size_t)` stream
thẳng từ flash (có sẵn, không deprecated, `ESPAsyncWebServer.h:363`, lib 3.6.0).

### Vì sao **không** chọn tải `littlefs.bin` (self-heal lúc boot)

Đường `U_SPIFFS` **không có bảo vệ toàn vẹn nào**:

- `_verifyHeader()` luôn `return true` cho U_SPIFFS (`Updater.cpp:243-245`)
- Trick "giữ lại 16 byte đầu để ảnh cụt không boot được" **chỉ áp dụng cho `U_FLASH`** (`Updater.cpp:185-202`)
- `raw.githubusercontent.com` không gửi header `x-MD5`
- Partition `spiffs` **không có A/B, không rollback** (khác `app0`/`app1`)

⇒ Bất kỳ body HTTP 200 nào — kể cả trang HTML rate-limit của GitHub — sẽ được ghi thẳng lên
partition, tạo ra một filesystem **vẫn mount được** và **vẫn phục vụ file cụt**. Đổi một vấn đề
hiển thị lấy một đường ghi flash mới, trên máy điều nhiệt: không đáng.

Phụ: `updateSpiffs()` **cố ý không reboot** (`HTTPUpdate.cpp:353` `if(_rebootOnUpdate && !spiffs)`),
nên mọi call site bắt buộc phải là `LittleFS.end(); updateSpiffs(...); ESP.restart();` — quên bước
cuối là biến một lần cập nhật thành công thành dashboard hỏng.

### Vì sao "OTA app trước, FS tính sau" là lựa chọn **tệ nhất**

Hai nhánh, đều xấu, nhánh thứ hai xấu hơn nhiều:

- **`spiffs` trống** (nhiều khả năng — 2.4.2 *không hề* mount LittleFS, web của nó là PROGMEM
  `src/index.h` phục vụ bởi `WebServer` sync, `FBT-DXD242/src/Bluetooth.cpp:726-727`) →
  `LittleFS.begin()` fail (`formatOnFail=false`, `LittleFS.h:26`) → `serveStatic.canHandle()`
  false (`WebHandlers.cpp:114`) → rơi `onNotFound` (`webDashboard.cpp:1549-1552`).
  **STA: 404 trắng. SoftAP: `ERR_TOO_MANY_REDIRECTS`** ngay sau khi khách quét QR
  (redirect `/` → `/`, mà `/` cũng 404).
- **`spiffs` có ảnh 4 file cũ** → mount **im lặng, không log gì** (chỉ log khi *fail*,
  `webDashboard.cpp:1499-1500`), browser nhận trang demo 2020 "ESP WEBSERVER". Trang đó
  **vẫn subscribe được `/events`** của 2.4.3 và vẽ chart từ `new_readings` thật — nhưng
  **lệch một series**: `doc["i"]` (số vòng đo) là key đầu tiên trong `buildReadingsJson`
  (`webDashboard.cpp:383-397`) nên bị vẽ thành `#1`, slot 1 thành `#2`, **slot 10 biến mất**.
  → Một biểu đồ khuếch đại **trông hợp lý mà sai nhãn**, trên máy chẩn đoán.

Cả 4 cây firmware cũ ship `data/` **byte-identical** (md5 `index.html=bcd6c2b8…`,
`script.js=ffe6c925…`), nên nhánh này giống nhau ở mọi version.

---

## 1. Ma trận publish

Repo `github.com/wuanpham/FBTRapidplusOTA` (public) hiện chỉ có branch `main`, `v2.3.6`…`v2.4.0`.
**Không có branch `v2.4.2`** → `raw.githubusercontent.com/.../v2.4.2/updateOTA.json` trả **404**.
Máy 2.4.2 ngoài đồng đang nhận 404 mỗi lần boot. Đó là lý do OTA "im lặng" lâu nay.

`baseUrl` gắn version **đang chạy** (`src/updateOTA.cpp:9`), nên mỗi bản phát hành phải publish
lên branch của các version **cũ hơn**, không phải branch của chính nó.

| Firmware ngoài đồng | `currentVersion` | Branch cần | `versionCode` publish | Trạng thái |
| --- | ---: | --- | ---: | --- |
| v2.4.0 (`RPLv2.4.0/FBT-DXD`) | 16 | `v2.4.0` | 19 | đã có |
| v2.4.0.x (`FBT-DXD_HotlidDisable`) | 16 | `v2.4.0.x` | 19 | **thiếu → tạo** |
| v2.4.2 (`Now/FBT-DXD`) | **17** | `v2.4.2` | 19 | **thiếu → tạo** |
| v2.4.2 (`v2.4.3/FBT-DXD242`) | **18** | `v2.4.2` | 19 | như trên |
| v2.4.3 (sau khi lên) | 19 *(sau bump)* | `v2.4.3` | ≤ 19 hoặc **không tạo** | tạo sau |

⚠ Có **hai bản 2.4.2 khác nhau** đang tồn tại (`currentVersion` 17 và 18). `versionCode 19` phủ cả hai.

⚠ **Bắt buộc bump `currentVersion` 18 → 19** (`src/updateOTA.cpp:3`). Không bump thì mọi máy sau
khi lên 2.4.3 thấy `19 > 18` (`updateOTA.cpp:45`) → prompt mỗi lần boot → tải lại 2.36 MB mỗi lần
người dùng bấm ĐỎ. Màn OTA nằm trong `isBusy()` nên mọi POST `/config` và `/ota` bị **409** suốt.
`OTA_DISMISSED` (nút XANH) chỉ sống trong RAM → reboot là hỏi lại.

Một artifact, bốn file JSON.

---

## 2. EEPROM: an toàn, kể cả từ v2.4.0

- `parastructure` cùng `sizeof = 400`, mọi `ADDR_*` và `_EEPROM_SIZE 4096` **giống nhau qua cả 4 cây**
  (hash các dòng `#define ADDR_*` = `30ba52722079` ở v2.4.0, v2.4.2, v2.4.3).
  → calib `slopes`/`origins`, PID, error log, device ID, bản ghi run cuối ở `RECORDPOS` **giữ nguyên**.
- Cổng hợp lệ duy nhất là `paraEEPROM.length == sizeof(parameter)` (`ForteSetting.cpp:747`) → 400 == 400 → pass.
- v2.4.0 khác **đúng một chỗ**: `double empty[5]` → `double kpid3[3] + double empty[2]` — **cùng 40 byte**.
  `kpid3` đọc ra `{0,0,0}` → khối seed ở `ForteSetting.cpp:755-765` tự nạp `{60, 0.1, 40}` và ghi lại.
  **Khối đó không phải trang trí — nó chính là migration hook v2.4.0 → v2.4.3.**
- Arduino EEPROM là **NVS-backed** (namespace `"eeprom"`, `EEPROM.cpp:36,68`) nằm ở partition `nvs`
  @0x9000 — **không nằm trong `spiffs`**. Đây là lý do một lần ghi FS không thể phá calib nhiệt.
- NVS namespace mới của 2.4.3 (`wifinets`, `wifitrial`) vắng mặt được xử lý sạch:
  `Preferences.begin(NS, true)` trả false → list rỗng (`wifiStore.cpp:32-33`). Boot đầu vẫn nối bằng
  cặp SSID/pass trong EEPROM y như 2.4.2.

⇒ **Không cần factory reset. Không cần bump `"para version"`** (chuỗi đó không được so sánh ở đâu cả).

---

## 3. Assay không đổi

| Thành phần | Kết quả diff |
| --- | --- |
| `src/Alg/*` (Algo, AlgoData, sgsmooth) | **md5 giống hệt** |
| `src/VEML6035/*` | **md5 giống hệt** |
| `acquisition.cpp`, `thermometer.cpp`, `Fan.cpp`, `LED.cpp`, `buzzer.cpp`, `errorCheck.cpp` | diff 0 dòng |
| `PIDControl.cpp` | xoá 6 dòng `RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0`, xoá 2 hàm không ai gọi, thêm `eepromLock()` |
| `parastructure` defaults (`define.h:109-175`) | `diff -w` = rỗng |

⇒ **CT và kết luận P/N/S/E/B tính y hệt.** Chart *trông* mượt hơn vì Savitzky-Golay chạy phía
browser (`data/script.js`, client-only) — **phải nói rõ điều này**, kẻo người dùng lâm sàng so
screenshot lại tưởng assay đổi.

---

## 4. Rủi ro #1 chưa đo được: cửa sổ OTA lúc boot

`FBT-DXD242/src/main.cpp:218-225` chờ WiFi tối đa `20 × 50ms = 1 s`, rồi chạy
`_displayCLD.begin()` → `_ForteSetting.begin()` → `_PIDControl.begin()` →
`logoFortebiotech()` (`LOGODISPLAYTIME = 1000`, `define.h:274`) → sensor/fan init,
**rồi mới** `checkFirmware()` ở dòng 244.

Cửa sổ thật ≈ **2 s+**, không phải 1 s — nhưng vẫn sát mức associate + DHCP điển hình của
ESP32 (1–3 s). Và `checkFirmware()` **không bao giờ chạy lại**: NetworkTask chỉ poll
`updateFirmware()`, hàm này no-op trừ khi `otaState == OTA_USER_ACCEPTED`.

**Không sửa được từ xa** (sửa 2.4.2 thì phải cầm dây — đúng cái đang tránh). Chỉ có:
đo trên máy thật → máy nào trượt hoài thì tắt/bật lại vài lần, hoặc đành cầm dây.

Phụ: khi tải thất bại, máy park ở `OTA_FAILED` **không retry, không hỏi lại** → muốn thử lại
phải power-cycle, tức lại đánh cược với cửa sổ trên.

---

## 5. Các pha

### Pha 0 — chặn cứng, làm trước mọi thứ

1. **`currentVersion` 18 → 19** (`src/updateOTA.cpp:3`).
2. **Không publish `.bin` build từ cây hiện tại lên repo public.**
   `strings` trên artifact thật cho ra ERP key @offset **3144**, ingest Bearer @**3251** — nằm trong
   4 KB đầu, không cần dịch ngược. Bản đã publish (v2.4.0, 2 187 216 B) **không** chứa 2 token này
   ⇒ **chưa lộ, đây sẽ là lần đầu**.
   → **Rotate 2 token trước khi build.**
   GAS `/exec` URL thì **đã lộ sẵn** trong bin v2.4.0 (offset 2789) và vẫn y nguyên trong cây hiện tại
   → **redeploy deployment ID mới bất kể**. Đây đúng là trigger mà chính team đã pre-agree ở
   `docs/history/2026-07-22-secrets-out-of-source.md:30-34` ("nếu repo chuyển public thì nên rotate").
3. **Sửa cả cụm secrets trong một lần**: `src/Bluetooth.cpp:22-27` dùng macro `SECRET_*` (hiện đang
   hardcode literal, và `SECRET_*` **không được dùng ở đâu cả** → cả cơ chế `secrets.h` là code chết),
   **đồng thời** scrub `src/secrets.example.h:8,10,12` (file này **được commit** và chứa token thật).
   ⚠ Sửa mỗi file example là làm repo *trông như* đã vá trong khi `.bin` vẫn mang token thật.
4. **Vá `"PCB version"`** ở `src/webDashboard.cpp:680` (thêm `strlen > 9 → reject`, giống nhánh
   `device ID`/`units` ngay bên dưới ở :682-685). Comment hiện tại ghi "checked below" — **không có
   check nào**, `validateConfig` kết thúc ở :827. Đường đi: `POST /config` → `drainPending` →
   `strcpy(parameter.PCB_version, ...)` (`ForteSetting.cpp:245`) vào `char[10]` ở offset 14 → đè
   `slopes@24`, `origins@64`, `amplification_time@212`, `lysisTemp@216`, `kpid@232`, `kpid2@256`,
   `kpid3@360`, chạy tiếp ~1 384 byte quá struct vào heap → rồi `EEPROM.commit()`.
   Bounded bởi `CFG_BODY_MAX = 1800`, chỉ chạy khi máy **idle** (`guardBusy` :1065), và UI shipped
   không bao giờ gửi key này ⇒ **chỉ khai thác được có chủ đích**, nhưng nó là mới trong 2.4.3
   (2.4.2 chỉ có Serial/BT tới được).
   Chắc tay hơn: đổi `strcpy` → `strlcpy` ở `ForteSetting.cpp:238` và `:245`.
5. **`rm -rf .pio/libdeps` rồi build lại.** `.pio/libdeps/esp32dev/ESPAsyncWebServer/src/` đang có
   **24 file `* - Copy.cpp/.h`** (`AsyncEventSource - Copy.cpp`, `WebHandlers - Copy.cpp`, …) bị biên
   dịch vào archive. Build hiện tại link được là **may**. Không publish `.bin` dựng từ cây libdeps hỏng.

### Pha 1 — nhúng asset (bản chất của fix)

6. Mở rộng **`tools/pio_gzip_data.py`** (đừng thêm script mới) để sinh `src/webAssets.h`:
   5 mảng `const uint8_t[]` + 1 `#define WEB_ASSETS_ETAG "<hex>"` = hash nội dung.
   Giữ nguyên `GZIP_ME` và trick `mtime=0`. **Chuyển việc ra module scope** — hiện nó gắn vào
   `AddPreAction("$BUILD_DIR/littlefs.bin")`, mà target đó sẽ không còn được build nữa.
   `platformio.ini` **không cần đổi** (script đã đăng ký `post:tools/pio_gzip_data.py`).
7. Thay `src/webDashboard.cpp:1540-1542` bằng bảng `{path, ptr, len, mime, gz}` + vòng đăng ký,
   **giữ nguyên vị trí** (sau API routes, trước `onNotFound` — GOTCHA 12 vẫn đúng).
   Mỗi route: `beginResponse(200, mime, ptr, len)` → `Content-Encoding: gzip` (khi gz) →
   `Cache-Control: no-cache` → `ETag: WEB_ASSETS_ETAG`, và trả **304** khi `If-None-Match` khớp.
   Đăng ký `/` và `/index.html` về cùng một entry. **Xoá hẳn `serveStatic`** để nội dung FS cũ
   không thể với tới được nữa.
   ⚠ **Tuyệt đối không truyền template callback** — `_fillBufferAndProcessTemplates`
   (`WebResponses.cpp:494-496`) quét ký tự `%` và sẽ sửa bytes gzip tại chỗ.
   ⚠ ETag **không được bỏ**: bỏ `serveStatic` là bỏ luôn ETag/304 của thư viện
   (`WebHandlers.cpp:210-245`); thiếu nó thì mỗi lần load trang gửi lại 147 739 B, và browser
   còn cache `script.js` 2.4.2 cũ → **đúng lớp lỗi chart-sai-nhãn**, chỉ khác đường vào.
8. **`/otaupload` nhận `?md5=`** → `Update.setMD5()` ngay sau `Update.begin`.
   Hiện `Update.end(true)` đặt `_size = progress()`, nên **một file `.bin` đứt giữa chừng vẫn boot được**.
   Đây là **đường brick thật duy nhất** trong hệ thống, và nó tồn tại ngay hôm nay.
9. **Re-check `dashboardDeviceBusy()`** ngay trước `httpUpdate.update()` (`updateOTA.cpp:115`)
   **và** trước `ESP.restart()` (:121). Hiện web check busy lúc bấm (`webDashboard.cpp:910`),
   latch `OTA_USER_ACCEPTED` (:942), NetworkTask hành động vài phút sau **không check lại**
   → người dùng bấm Update, đi qua máy, bấm chạy run, rồi mất mẫu.
   Khuôn deferred-reboot đã có sẵn ở `webDashboard.cpp:1592`.

### Pha 2 — tách riêng, ship sau khi Pha 1 đã ổn ngoài đồng

10. Chuyển `/slotnames.json` + `/slotsamples.json` sang **NVS** (`Preferences`, đúng khuôn
    `wifiStore.cpp:31,57`), mỗi cái **1 chuỗi JSON** (2 entry NVS, không phải 20). Rồi xoá
    `LittleFS.begin()` (`webDashboard.cpp:1499`) và 4 hàm load/save (:462-512). **Net âm dòng code.**
    Sau bước này `spiffs` trống/hỏng/chưa format **không còn hậu quả gì**, và hết luôn phiền
    "`uploadfs` xoá nhãn của người dùng" mà CLAUDE.md đã ghi.
    ⚠ **Đừng thay bằng `LittleFS.begin(true)`** — nó format 1 572 864 B kèm `disableCore0WDT()`
    (`LittleFS.cpp:114-124`) trong khi ControlTask đang giữ duty heater trên core 1.
11. Publish `updateOTA.json` lên 4 branch theo [ma trận](#1-ma-trận-publish).

---

## 6. Gate trước khi chạm fleet

| Gate | Cách đo | Sai thì sao |
| --- | --- | --- |
| **OTA app chạy được trên stack đã cắt** | 1 lần OTA thật, in `uxTaskGetStackHighWaterMark(NetworkTask)` sau khi xong | NetworkTask bị hạ 8192→6144 (`main.cpp:390`) mà phải chứa mbedTLS + HTTPClient + Update. Overflow = **panic**, không phải `HTTP_UPDATE_FAILED`. Cả kế hoạch sụp. **Go/no-go, không phải nice-to-have** |
| **Dashboard render với `spiffs` đã xoá sạch** | Xoá partition có chủ ý rồi mở web | đó là trạng thái ngoài đồng khả dĩ nhất |
| **Cửa sổ OTA lúc boot** | Bật/tắt 10 lần, đếm số lần hiện prompt | tỉ lệ thấp = phải cầm dây, xem [mục 4](#4-rủi-ro-1-chưa-đo-được-cửa-sổ-ota-lúc-boot) |
| **ControlTask 4096 dưới tải thật** | 1 run 40 phút + upload, **giữ** census `[stack]` ở `main.cpp:436-461` | ControlTask bị **giảm một nửa** (8192→4096, `main.cpp:363`), số liệu lấy từ high-water lúc **idle** — chính docs của dự án 2 lần ghi là cách đo không tin được (`2026-07-27-task-stacks-fix-upload.md`, `2026-07-27-waitlysis-phase.md`) |
| **`fbt.basa-luma.ts.net` thông từ mạng khách** | `curl` tại site | 2.4.3 đổi đích ingest sang host Tailscale (`Bluetooth.cpp:22-26`); bị chặn = kết quả **im lặng** không lên |
| **ERP đã đăng ký `id_device`** | 1 upload thật | `2026-07-27-task-stacks-fix-upload.md` ghi ERP từng trả `device_matched:false` |

**Rollout:** 1 máy bench (`uploadall`, `spiffs` đã xoá) → 1 máy **2.4.2 thật** → 1 máy **2.4.0 thật**
→ mới tới fleet.

**Checklist sau mỗi máy:** serial phải in `there is para in the EEPROM with length 400`
(`ForteSetting.cpp:749`). Nếu thấy `there is no para in the EEPROM` → máy đang chạy **default
compiled** (slopes = 1, PID mặc định) sau 3 giây báo trên TFT → **không được giao lại cho người dùng**.

---

## 7. Ngoài phạm vi (cố ý)

- TLS cert validation / signed image / secure boot — ràng buộc heap (`Bluetooth.cpp:459` TLS_MIN,
  `:475` chọn `setInsecure()` để bớt cấp phát mbedTLS), eFuse một chiều. Không làm giữa rollout.
- Token per-device trong NVS — kẹt bootstrap đúng lúc upgrade (máy 2.4.2 chưa có chỗ nhập token).
- Auth cho toàn dashboard — nếu làm thì chỉ `/otaupload`.
- Mọi bug tiềm ẩn **có sẵn từ trước**: clamp `amplification_time` (≤130), guard `slopes[i] == 0`,
  `clear()` quét đủ 130 cột, `/rename` trả bool thay vì luôn `ok:true`. Mỗi cái ép build lại và
  test lại một ảnh vừa mới kiểm xong.
- Viết lại git history (7 commit, đã push) — **rotate server-side thay thế**.
- Menu TFT **Settings ▸ Bluetooth thành reboot câm** trong 2.4.3 (`main.cpp:315`
  `releaseBluetoothStack()` chạy sớm → `connectBLE()` return ngay → `displayLCD.cpp:1631-1637`
  vẫn `ESP.restart()`) → **ghi release note, không sửa code**.

---

## 8. Chưa đo được

| Chưa biết | Cái giá nếu đoán sai |
| --- | --- |
| NetworkTask stack high-water lúc OTA | **Toàn phần** — cơ chế phát hành tự nó hỏng, fleet phải đi tận nơi. 30 phút bench, giá trị cao nhất trong cả kế hoạch |
| Trong `spiffs` máy thật đang có gì | **Bằng 0 dưới phương án này** — chính là lý do nhúng asset thắng |
| Tỉ lệ trúng cửa sổ OTA lúc boot | Trượt hoài = cầm dây, không sửa được từ xa |
| `raw.githubusercontent.com` có trả 200 kèm `Content-Length` cho `.bin` | Bằng 0 — hành vi này không đổi so với hiện tại (`httpUpdate` **tắt follow-redirect** mặc định, `HTTPUpdate.cpp:38`, nên URL phải trả 200 trực tiếp) |
| Kích thước 2 509 467 B sau khi nhúng | Thấp và ồn — PlatformIO fail build khi quá `app0` (`main.py:189-192` ghi đè `upload.maximum_size`; con số `8388608` trong `platformio.ini:59` là **số chết**) |
| NVS còn bao nhiêu chỗ trống | Trung bình — ghi WiFi mới có thể fail, `WIFI_STORE_MAX` là nút để hạ |

---

## 9. Cần quyết (không phải việc kỹ thuật)

1. **Rotate** ingest Bearer + ERP X-API-Key + redeploy GAS — ai làm phía server, khi nào?
   *Đây là gate của toàn bộ rollout.*
2. Fleet **bao nhiêu máy, đang ở version nào?** Repo OTA cho thấy từng phát hành tới `v2.4.0`;
   `v2.4.2` **chưa từng đi qua OTA lần nào**.
3. Có máy nào **không có internet / chỉ SoftAP** không? Những máy đó không dùng được đường này —
   `checkFirmware()` đòi `WL_CONNECTED` ở STA (`updateOTA.cpp:25-28`).
4. Còn ai đang dùng **TFT Settings ▸ Bluetooth** để cấu hình hay đọc kết quả không? Nếu có thì
   việc nó chết là **gỡ tính năng**, cần khách xác nhận, không phải chỉ ghi release note.

---

## 10. Release note cho người vận hành (nháp)

1. Dashboard web qua WiFi: chart trực tiếp, bảng kết quả từng slot, đặt tên bệnh/mẫu, tab cài đặt.
2. Màn **QR** trên TFT (nút TRẮNG ở màn chính) → quét là vào dashboard.
3. Máy có **tên cố định** (`http://<id>.local/`) thay vì IP đổi liên tục.
4. **Lưu nhiều mạng WiFi**, thử lần lượt lúc khởi động; nhập sai mật khẩu **không** làm mất mạng cũ.
5. Cập nhật firmware **từ trình duyệt** (`/otaupload`) và nút "kiểm tra bản mới" trong tab Setting.
6. Vào **calib** bằng giữ lâu nút XANH.
7. Kết quả nay lên **3 đích** thay vì 2.
8. ⚠ **TFT Settings ▸ Bluetooth không còn hoạt động** (chọn vào là máy khởi động lại).
9. ⚠ Boot lần đầu có thể lâu hơn (**tới ~9 s** nếu không có mạng nào đã lưu) — không phải lỗi.
10. **Assay, nhiệt độ, calib và kết quả: không đổi.** Đường cong trên web trông mượt hơn do làm
    mượt phía trình duyệt; CT và kết luận vẫn do máy tính y như cũ.
