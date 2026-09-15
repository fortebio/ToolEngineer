# 2026-07-24 — Đồng bộ WiFi giữa web và máy

## Bug người dùng báo

> "WiFi đang bất đồng bộ giữa web và trên máy. Nếu máy kết nối với WiFi khác nhưng web vẫn
> không thay đổi."

Và trước đó: **"bấm Forget trên web mà WiFi vẫn còn"**, **"nhấn Connect không thấy chuyển"**.

Ba triệu chứng, **hai gốc rễ độc lập** cộng lại:

1. **Web không *đổi được* trạng thái máy** — mọi `POST` (forget/connect) **âm thầm rơi nhánh
   `GET`** vì lỗi định tuyến (GOTCHA 3). Bấm Forget/Connect như bấm vào khoảng không.
2. **Web không *thấy được* trạng thái thật của máy** — kể cả khi máy tự đổi mạng
   (autoReconnect, hoặc reboot sau Connect), dashboard **không có field nào phản ánh SSID máy
   đang thực nối** → màn hình đứng im, trông như "không đồng bộ".

Sửa cả hai thì "đồng bộ" mới thật.

## Gốc rễ 1 — POST rơi nhánh GET (GOTCHA 3)

`handleWifiList` cũ phân nhánh bằng `req->method() == HTTP_POST`. Trong build này phép so đó
**luôn sai**:

- Header dự án kéo `WebServer.h` (đặt `WEBSERVER_H`) **trước** `<ESPAsyncWebServer.h>`, nên
  symbol `HTTP_POST` phân giải thành **giá trị tuần tự của `http_parser`** (WebServer.h) — còn
  `request->method()` trả **bit-flag của AsyncWebServer**. Hai biểu diễn **khác nhau** → so
  `req->method() == HTTP_POST` không bao giờ đúng.
- Hệ quả: **mọi POST rơi xuống nhánh GET** → `wifiStoreRemove()` / `connect` **không bao giờ
  chạy** (serial cũng không có log). `POST /wifi` (save) thoát nạn vì nó đọc thẳng
  `getParam(..., true)`, **không** so `method()`.

**Fix — phân nhánh theo *sự hiện diện của param*, không theo method** (giống `/rename`):

- `/wifilist` đăng ký **`HTTP_ANY` một handler** (`webDashboard.cpp:1409`) →
  `handleWifiList` (`webDashboard.cpp:1148`) nhánh:
  - `hasParam("remove", true)` → `wifiStoreRemove` (`:1151`)
  - `hasParam("connect", true)` → busy-guard + `wifiStoreGetPass` + `postWifiCreds` → PEND_WIFI
    trial+reboot (`:1162`)
  - còn lại → `wifiStoreListJson()` (`:1183`)
- `/ota` cùng bệnh, cùng thuốc: `HTTP_ANY` một handler `handleOta` (`:1415`) dispatch theo
  `hasParam("action")` (`:849`) → `handleOtaAction` / `handleOtaStatus`.
- Lưu ý đối số thứ hai của `hasParam` khác nhau theo route: `/wifilist` dùng
  `hasParam("remove"|"connect", true)` (param trong **body** POST); `/ota` dùng
  `hasParam("action")` **không** đối số thứ hai (param **query-string**).

Guard host-side khoá bất biến: **`tools/test_no_method_branch.py`** (`:46`) quét mọi
`->method()` so sánh trong `src/**` → đỏ ngay nếu ai đó thêm lại. Đây là lớp chặn thật — mock
Python định tuyến bằng `startswith` nên **KHÔNG** tái hiện được GOTCHA 3.

## Gốc rễ 2 — web không có field trạng thái mạng live

Trước đây payload `home` không mang thông tin mạng, nên **không gì trên web thay đổi** khi máy
đổi SSID hay rơi SoftAP.

**Fix — đẩy trạng thái mạng thật vào SSE `home` (1 giây/lần):**

- `buildHomeJson()` (`webDashboard.cpp:299`) thêm object **`net{ap, ssid, ip}`**:
  - `ap` = `dashboardIsAP()`
  - `ssid` = AP ? `"RAPID-<id_device>"` : (nối được ? `WiFi.SSID()` : `""`)
  - `ip` = (AP ? `softAPIP()` : `localIP()`)`.toString()`
- Đẩy qua event **`home`** (`:1532`), **throttle 1 s** (`:1505`); cũng phục vụ ở `GET /home`
  (`:1397`).

**Fix — panel Saved networks đánh dấu đúng mạng máy đang nối:**

- `wifiStoreListJson()` (`wifiStore.cpp:230`) thêm:
  - top-level **`current`** = SSID máy đang nối (`""` nếu chưa nối; có thể là mạng **không** có
    trong danh sách lưu).
  - mỗi net đã lưu: **`active: true`** *chỉ khi* khớp SSID đang nối — **bỏ hẳn key** nếu không
    khớp (không phải `false`), client dựa vào truthiness. **Không bao giờ** kèm mật khẩu.
- Client: `renderHome` (`script.js:113`) đọc `d.net` → cập nhật `setNet`/`setIp` + lưu global
  **`curNet`**; `loadSavedWifi` (`:1932`) fetch `/wifilist` **`cache:"no-store"`**; badge
  **"connected"** (`:1971`) hiện khi `n.active || (curNet && !curNet.ap && curNet.ssid ===
  n.ssid)` — **hai nguồn tín hiệu**: cờ `active` từ `/wifilist` *và* SSE `curNet` live (khi
  không ở AP).

Vì máy giờ chỉ cam kết mạng **đã nối được** (trial-then-commit — xem
[docs/history/2026-07-24-saved-wifi-networks.md](docs/history/2026-07-24-saved-wifi-networks.md)),
cộng field live này, trạng thái web = trạng thái máy trong ~1 giây.

## Hình dạng JSON (sau fix)

```
GET /home  (event "home", buildHomeJson):
  { device, company, net:{ap:bool, ssid, ip}, temps{…}, status{…}, notify{…} }

GET /wifilist  (wifiStoreListJson):
  { max, current:"<liveSSID|>", nets:[ {ssid, saved:true, active:true?} ],
    trial:{result:"failed", ssid, reason:"auth|range"}?  }
  # active: chỉ có ở net đang nối
  # trial: chỉ có 1 lần ngay sau lần thử thất bại, rồi clear-on-read
```

## Ràng buộc không tránh được: đổi subnet thì mất kết nối

Đã ghi rõ trong comment (`webDashboard.cpp:296`): nếu đổi mạng làm máy sang **subnet khác**,
browser **vốn không với tới máy được nữa** — không field live nào cứu được. Chỉ khi **cùng
subnet** thì `net` mới cập nhật trong ~1 s. Đây là giới hạn vật lý của mạng, không phải thiếu
sót. (Cũng vì vậy máy **không** hot-switch mạng lúc runtime — đổi mạng chỉ ở boot; xem
failover trong doc saved-wifi.)

## Kiểm chứng trên máy thật (COM18 → 192.168.0.103)

**Bẫy trước khi verify được**: bản `uploadall` trước đó in **SUCCESS** nhưng máy vẫn chạy
**firmware cũ** (`/home` không có `net`, `/wifilist` không có `current`). Nguyên nhân: rối cổng
hai board CH340 — Windows liệt kê *cùng một board* dưới COM17/COM19/COM18 (chung parent
`7&C2DF9A6`), chỉ **COM18** có Status `OK`. **Bài học: verify bằng field chỉ có ở build mới,
đừng tin dòng SUCCESS của esptool.** Re-flash thẳng cổng đang active mới ăn.

Sau re-flash, đo trực tiếp:

| Kiểm | Lệnh | Kết quả |
| --- | --- | --- |
| net live | `GET /home` | `"net":{"ap":false,"ssid":"Engineer-FBT_2.4GHz","ip":"192.168.0.103"}` ✓ |
| current + active | `GET /wifilist` | `"current":"Engineer-FBT_2.4GHz"`, `"active":true` đúng net ✓ |
| forget (GOTCHA 3) | `POST /wifilist remove=hehe` | `{"ok":true}` → `hehe` biến mất khỏi list ✓ |

Forget chạy đúng trên máy thật = xác nhận GOTCHA 3 đã dứt điểm.

## Còn hở

- **Comment header `/wifilist`** (`webDashboard.cpp:1137`) còn ghi shape cũ
  `{max, nets:[{ssid, saved}]}` — **lỗi thời** so với emitter thật (đã thêm `current` / `active`
  / `trial`). Không sai logic, chỉ là comment cần cập nhật để người sau khỏi nhầm.
- **Connect qua reboot**: verify live "connect sang mạng đã lưu" chưa chạy (sẽ đẩy máy sang
  subnet khác, mất tầm với browser giữa lúc test). Nhưng nó **dùng chung cơ chế dispatch với
  forget** (đã chứng minh chạy) + `postWifiCreds` trial-then-commit (đã có host test + boot
  log), nên coi như đã phủ.
