# 2026-07-24 — Cập nhật firmware (OTA) từ web Setting

## Yêu cầu

Web → Setting → thêm chỗ **kiểm tra bản firmware mới** và **cài đặt** nó, thay vì chỉ có
đường qua nút vật lý.

## OTA vốn đã có — chỉ thiếu đường vào từ web

`src/updateOTA.cpp` đã đầy đủ và **không đổi cơ chế**:

- `checkFirmware()` tải `updateOTA.json` từ
  `raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/<FirmwareVer>/`, so `versionCode` với
  `currentVersion` (hằng trong updateOTA.cpp).
- Có bản mới → `otaState = OTA_AVAILABLE`.
- `updateFirmware()` chạy trên **NetworkTask** (poll mỗi 10 ms), chỉ hành động khi
  `OTA_USER_ACCEPTED`, tải bằng `httpUpdate.update()` rồi `ESP.restart()`.

Trước đây chỉ boot mới gọi `checkFirmware()` (main.cpp) và chỉ nút RED trên màn `eUpdateOTA`
mới chấp nhận cài.

## Ràng buộc kiến trúc phải tôn trọng

| Việc | Chạy ở đâu | Vì sao |
| --- | --- | --- |
| `checkFirmware()` | **SettingTask** (`PEND_OTACHECK`) | HTTPS **blocking** vài giây — chạy trên AsyncTCP là treo web (GOTCHA 8/11) |
| Chấp nhận cài | AsyncTCP ghi `otaState` | **một byte volatile**, NetworkTask tự nhặt; bản thân việc tải không đụng task web |
| Đọc trạng thái | AsyncTCP | thuần đọc biến, không mạng/EEPROM |

`drainPending()` vốn đã guard `dashboardDeviceBusy()` ở đầu, nên `PEND_OTACHECK` **được guard
miễn phí** — không bao giờ đi hỏi GitHub giữa lúc máy đang chạy mẫu.

## Thay đổi

**`checkFirmware(bool promptOnDevice = true)`** — mặc định giữ nguyên hành vi cũ (boot check
chiếm màn TFT bằng `eUpdateOTA`). Web gọi **`false`**: người bấm đang nhìn trình duyệt, cướp màn
máy từ xa sẽ bỏ rơi người đang đứng ở thiết bị.

Thêm `otaLastCheck` (millis của lần check xong, 0 = chưa bao giờ) để web phân biệt
**"chưa kiểm tra"** với **"đã kiểm tra, không có bản mới"** — hai trạng thái rất khác nhau mà
`otaState == OTA_IDLE` gộp chung.

**Routes** (`webDashboard.cpp`) — **một handler `HTTP_ANY`** `handleOta` (`:1415`) dispatch theo
`hasParam("action")` (`:849`), **KHÔNG** phân nhánh theo `req->method()` (dính GOTCHA 3, POST sẽ
rơi nhánh GET — xem
[docs/history/2026-07-24-wifi-web-machine-sync.md](docs/history/2026-07-24-wifi-web-machine-sync.md)):

- `GET /ota` (không `action`) → **luôn có**: `version, versionCode, state, hasUpdate, busy,
  checked, checkFailed, online`; **chỉ khi `fwVersion > 0`**: `newVersion, newVersionCode, notes`.
  `state` ∈ `{idle, available, accepted, updating, failed, dismissed}`.
- `POST /ota?action=check` → xếp hàng `PEND_OTACHECK` (`503 busy,retry` nếu queue bận;
  `409 device busy` / `409 no internet`).
- `POST /ota?action=update` → chỉ từ `OTA_AVAILABLE`, đặt `OTA_USER_ACCEPTED` (`409 no update
  available` nếu không có bản mới).

Cả hai POST **từ chối 409** khi máy bận hoặc mất internet: OTA kết thúc bằng `ESP.restart()`,
làm giữa run là mất cả mẫu lẫn bản ghi của nó.

**UI**: thẻ **Firmware** trong Setting (thẻ thứ 8) → panel hiện bản đang cài / bản có sẵn /
trạng thái, nút **Check for updates** và **Install update** (chỉ hiện khi thật sự có bản mới,
kèm `confirm()` cảnh báo máy sẽ khởi động lại).

Panel **không chờ "cài xong"**: đường thành công kết thúc bằng reboot, nên trang chỉ mất kết nối
rồi tự nối lại — không có phản hồi nào để đợi.

**Mock** (`tools/sse_test_server.py`): `/ota` GET/POST mô phỏng, để thử UI không cần phần cứng.

## Đường thứ hai: nạp bằng file .bin

Cần khi máy **không có internet** (đang ở SoftAP của chính nó) hoặc bản build **chưa đẩy lên**
update server. Trình duyệt POST thẳng file, thiết bị ghi vào phân vùng OTA.

`POST /otaupload` (multipart, field `firmware`) — `dashServer.on(url, HTTP_POST, onRequest,
onUpload)`:

- **`onUpload` nhận body theo chunk ~1-4 KB** → `Update.write()` từng khúc. Không bao giờ giữ
  cả 2,3 MB ở đâu, nên chạy được từ task web.
- Chunk **đầu tiên** (`index == 0`) là chỗ duy nhất từ chối được: kiểm `dashboardDeviceBusy()`
  và đuôi `.bin`, rồi `Update.begin(UPDATE_SIZE_UNKNOWN)` (thân multipart không mang độ dài
  đáng tin, để thư viện tự đo theo phân vùng OTA còn trống).
- Bị từ chối → cờ `otaUpFail`, các chunk sau **vẫn được đọc nhưng không ghi**: phải rút cạn
  socket, không thì browser thấy connection reset thay vì thông báo lỗi.
- **Reboot phải HOÃN**: `ESP.restart()` ngay trong handler làm mất luôn cái reply 200 → browser
  báo lỗi mạng cho một bản cập nhật hoàn toàn thành công. `handleOtaUploadDone` (`:962`) đặt
  `otaRestartAt = millis() + 800`, `dashboardLoop()` (`:1463`) restart khi qua mốc đó **và**
  `!dashboardDeviceBusy()` — nếu một run vừa bắt đầu trong 800ms đó thì hoãn tiếp, không cắt
  ngang mẫu (firmware đã staged xong, chỉ chờ lần idle để reboot vào).

**UI dùng `XMLHttpRequest`, không phải `fetch`**: chỉ XHR có sự kiện **upload progress**, mà
ảnh ~2,3 MB qua WiFi đủ lâu để một màn hình im lặng trông như treo. Panel hiện `% + KB/KB`.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS**, +2,4 KB flash (69.1%).
- Mock: `GET /ota` (chưa check) → `state idle, checked false, hasUpdate false` →
  `POST ?action=check` → `GET /ota` → `available / checked true / newVersion v2.4.4` →
  `POST ?action=update` → `{"ok":true,"started":true}`.
- Ảnh panel: `node tools/ui_screenshot.js <out> http://localhost:8080/` (shot `setting-ota`).
- Upload file thật qua mock: `curl -F "firmware=@.pio/build/esp32dev/firmware.bin" .../otaupload`
  với ảnh **2 317 104 byte** → `{"ok":true,"restarting":true}`.

## Sửa sau code-review + verify máy thật (2026-07-24)

- **Phân biệt "check xong nhưng lỗi" với "chưa check"**: `otaLastCheck` giờ set sau **MỌI** GET
  hoàn tất (không chỉ HTTP 200) — `updateOTA.cpp:76`; thêm cờ `otaCheckFailed` (`:7`) = `false`
  khi 200 (`:64`), `true` khi non-200 (`:72`). `GET /ota` trả `checkFailed` (`webDashboard.cpp:822`).
  Client `renderOta` (`script.js:1556`) hiện **"Check failed - try again"** thay vì kẹt mãi
  ở "Not checked yet" (rate-limit GitHub / DNS / 404 giờ có phản hồi rõ).
- **Dispatch GOTCHA 3**: `/ota` gộp về `HTTP_ANY` một handler (xem phần Routes) — trước khi sửa,
  `POST ?action=…` rơi nhánh GET nên **check/update không chạy**.
- **Reboot sau `.bin` busy-guard**: `!dashboardDeviceBusy()` (xem phần nạp file .bin).

**Verify trên máy thật** (COM18 → 192.168.0.103): `GET /ota` → `"checked":true,
"checkFailed":true` — check GitHub thất bại (chưa có `updateOTA.json` phiên bản mới / rate-limit)
và cờ **hiện đúng** thay vì kẹt "chưa check". Đường tải-và-flash thật vẫn cần `versionCode` remote
> `currentVersion` (18) mới chạy trọn (xem dưới).

## Chưa kiểm được ở đây

Đường OTA **thật** (tải .bin từ GitHub rồi flash) cần máy thật có internet và một
`updateOTA.json` có `versionCode` **lớn hơn** `currentVersion` (hiện 18). Trên mock chỉ giả lập
được luồng trạng thái.
