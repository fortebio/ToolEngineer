# 2026-07-24 — Lưu nhiều WiFi + panel WiFi gọn lại

## Hai việc

1. **Panel WiFi**: thêm **ô tên mạng (SSID)** riêng; chọn một mạng trong danh sách quét thì
   **tự điền tên và nhảy con trỏ xuống ô mật khẩu**. Ô tên gõ tay được → nối được cả **mạng
   ẩn** (không hiện trong scan, trước đây không nhập nổi).
2. **Lưu nhiều WiFi**: máy nhớ tối đa 5 mạng, tự thử lần lượt nếu mạng ưu tiên ngoài tầm →
   chuyển phòng/địa điểm không phải cấu hình lại.

## Lưu ở đâu: NVS (Preferences), KHÔNG phải EEPROM, KHÔNG phải LittleFS

**Bản đầu (LittleFS `/wifi.json`) SAI** vì `uploadfs`/`uploadall` **flash đè cả partition
`spiffs` từ thư mục `data/`** (không có `wifi.json`) → mỗi lần nạp UI là **xóa sạch** danh sách.
Đây là gốc rễ bug "nạp lại thì mất WiFi cũ".

- **EEPROM**: chỉ 1 khe SSID + 1 khe pass, ép sát `ADDR_ID_BLE` → thêm mạng phải dời địa chỉ,
  nâng cấp field đọc rác.
- **LittleFS (spiffs)**: bị `uploadfs` reflash → **mất khi nạp**.
- **NVS (Preferences, namespace `wifinets`)**: partition riêng (`nvs` 0x9000) mà `uploadfs`
  **KHÔNG đụng** → **sống sót qua mọi lần nạp firmware/UI**. Có chỗ cho nhiều mạng, bỏ luôn
  giới hạn pass 54 ký tự. **Không** dùng buffer 4096B của EEPROM (né luật CLAUDE.md Setting #2);
  NVS thread-safe sẵn nên ghi từ AsyncTCP/SettingTask/setup đều an toàn không cần mutex.

`wifiStore.cpp` ghi **cả list** mỗi lần (`p.clear()` rồi ghi lại `n` + `s0..s4`/`p0..p4`) — list
tối đa 5 record ngắn, rewrite giữ nhất quán. Cặp ưu tiên vẫn ở EEPROM (cho `WiFi.begin` đầu tiên
của `setup`), nó cũng ở partition khác `spiffs` nên vốn đã sống sót qua `uploadfs`.

## Cách nối nhiều mạng — MỌI `WiFi.begin()` ở `setup()`

**Bản đầu (đã bỏ)** đặt `dashboardTryNextSaved()` trong `dashboardLoop` gọi `WiFi.begin()` — vi
phạm bất biến được test khoá: `WiFi.begin()` **chỉ** được ở `main.cpp`, vì begin() runtime từ
task **treo `async_tcp` vĩnh viễn** (bug "Up Data", `test_no_runtime_wifi_begin.py` +
[docs/history/2026-07-20-updata-wifi-reconnect-hang.md](docs/history/2026-07-20-updata-wifi-reconnect-hang.md)).
`python tools/test_no_runtime_wifi_begin.py` bắt đỏ ngay.

**Bản đúng**: dồn TẤT CẢ vào `setup()` — nơi duy nhất được chứng minh an toàn.

- `connectSavedNetworks()` (main.cpp): thử **mạng ưu tiên** (EEPROM, 4s) rồi từng mạng đã lưu
  (3s/mạng, cap tổng 9s). **Thoát ngay khi một mạng nối được** → mạng có sẵn không bị chậm;
  chỉ mất outage thật mới chờ hết timeout. `delay(100)` trong vòng chờ để nuôi watchdog.
- Không mạng nào lên → `dashboardLoop` grace + SoftAP như cũ (không còn `dashboardTryNextSaved`).

`connectSavedNetworks` đọc danh sách từ **NVS** (không cần mount gì trước `WiFi.begin`).

## Failover lúc đang chạy: KHÔNG tự động chuyển mạng (có chủ ý)

Bug người dùng báo: nạp 3 WiFi, **tắt 1 router thì máy không nhảy sang mạng khác**.

Bản đầu thử **tự reboot** khi mất WiFi 60s để `setup()` chọn lại — người dùng phản đối đúng: tự
reset cả máy chỉ vì rớt WiFi là quá tay. **Đã bỏ.**

Sự thật kỹ thuật: **không có cách nào chuyển sang SSID khác lúc runtime mà an toàn**.
`WiFi.begin()` runtime treo `async_tcp` vĩnh viễn, và tài liệu ghi rõ **`dashboardSuspend()`
trước begin cũng KHÔNG đủ** (GOTCHA 8 Option C — vẫn treo residual). Nên chỉ còn hai đường:
reboot (bỏ), hoặc **không tự động gì**. Chọn cái sau:

- `setAutoReconnect(true)` (core) tự nối lại **đúng SSID cũ** ở nền → router bật lại là **tự
  liền**, không reboot. Đây là "self-heal" cho outage tạm.
- Đổi **sang mạng khác** chỉ xảy ra **lúc boot** (`connectSavedNetworks`) → **power-cycle** để
  chọn lại. Danh sách lưu vẫn có ích: không phải gõ lại mật khẩu, và boot tự chọn mạng còn sóng.
- Mất WiFi máy **vẫn chạy đủ offline** (đo, hiện kết quả, lưu EEPROM); chỉ mất dashboard từ xa.
- TFT hiện `Scanning...` để người dùng **thấy** đang mất mạng, không đoán mò.

## Dòng WiFi trên màn TFT (start screen)

Trước: IP vẽ **một lần** lúc vào màn → đóng băng ở `0.0.0.0` (STA nối sau đó 1-2s). Thêm
`refreshStartWifiLine()` chạy **mỗi tick DisplayTask**, ngoài cổng `changeScreen`, chỉ vẽ lại
dòng IP nhỏ (không nhấp nháy toàn màn):

- đang dò STA → **`Scanning...`** (chấm chạy, màu vàng)
- SoftAP (không có router) → **`0.0.0.0`**
- nối được → **IP thật**

Cùng DisplayTask nên không cần lock SPI thêm (giống `screen_Start`).

## Sai mật khẩu KHÔNG ghi đè mạng đang chạy: trial-then-commit

**Bug (người dùng báo, gấp)**: nhập sai mật khẩu vẫn Save + reboot → ghi đè mạng tốt cũ trong
EEPROM → máy không nối được → rơi SoftAP, mất mạng cũ. Web và máy "không đồng bộ" cũng từ đây:
cam kết một mạng chưa chắc nối được.

**Sự thật**: kiểm mật khẩu = phải thử associate = phải `WiFi.begin()`; mà begin runtime treo
async_tcp → **bắt buộc reboot để thử**. Không có cách kiểm mật khẩu tại chỗ không reboot.

**Fix — thử trước, cam kết sau (kiểu router)**:

1. Save/Connect **KHÔNG cam kết**: `PEND_WIFI` chỉ `wifiStoreSetTrial(ssid, pass)` (NVS namespace
   `wifitrial`) rồi reboot. EEPROM + list **giữ nguyên mạng cũ**.
2. `setup()` → `connectSavedNetworks` thử **trial trước** (`wifiTryOne`, 8s):
   - **nối được** → cam kết: ghi EEPROM ưu tiên + `wifiStoreAdd` lên đầu list, xoá trial,
     `result = OK`.
   - **thất bại** → xoá trial, `result = FAILED(ssid)`, **rơi xuống nối mạng cũ** (EEPROM chưa
     đổi). Máy **không bao giờ mất kết nối** vì một mật khẩu sai.
3. `GET /wifilist` kèm `trial:{result:"failed", ssid}` → panel hiện **"Could not join <ssid> —
   wrong password? Re-enter below."** ngay chỗ sửa. Kết quả OK không cần báo (máy đã sang mạng
   mới, browser cũ không với tới — chuẩn).
4. **Chặn lỗi SSID TRƯỚC reboot** (phía web, không tốn reboot): bấm Save mà SSID không có trong
   scan → `confirm("...không thấy trong scan, mạng ẩn?")`. Mạng ẩn thì OK, còn typo thì hủy tại
   chỗ. Mật khẩu vẫn phải thử qua reboot.

Vì máy giờ chỉ cam kết mạng **đã nối được**, "web và máy không đồng bộ" tự hết: trạng thái cam
kết = mạng máy nối = thứ `/wifilist` báo.

## Ghi khi lưu (sau khi cam kết)

`PEND_WIFI` (SettingTask) sau `saveSettingDevice()` gọi thêm `wifiStoreAdd(ssid, password)` —
mạng vừa lưu thành **đầu danh sách** (ưu tiên). EEPROM vẫn giữ cặp ưu tiên cho `WiFi.begin()`
đầu tiên của `setup()`; danh sách là **tập fallback**.

## Routes

- `GET /wifilist` → `{max, nets:[{ssid, saved}]}` — **CHỈ SSID**, không bao giờ trả mật khẩu
  (dashboard không xác thực; ai vào được cũng đọc được hết mật khẩu WiFi phòng lab).
- `POST /wifilist?remove=<ssid>` → xóa một mạng (NVS, thread-safe, không cần busy-guard).
- `POST /wifilist?connect=<ssid>` → **chuyển máy sang mạng đã lưu**: tra mật khẩu **trên máy**
  (`wifiStoreGetPass`, không gửi lên web) rồi đi qua **`postWifiCreds` → PEND_WIFI → reboot** y
  như `/wifi` save. Busy-guard (reboot giữa run là mất mẫu). Chuyển mạng runtime bằng begin sẽ
  treo async_tcp → phải reboot.

## Bug "Forget không xóa được" (người dùng báo)

Nguyên nhân khả dĩ nhất phía web: `loadSavedWifi()` re-fetch `/wifilist` sau khi remove, browser
phục vụ lại **bản GET cũ trong cache** → mạng "xóa rồi" hiện lại. Fix: `fetch(..., {cache:
"no-store"})`. E2E (browser thật) xác nhận forget xóa cả server lẫn UI, không cache cũ.

Nếu trên **máy thật** vẫn không xóa: serial log `[wifi] forget '<ssid>': removed / not in saved
list / WRITE FAILED` chỉ đúng chỗ hỏng — ghi NVS fail, hay SSID không khớp (khoảng trắng/hoa-thường).

**Lưu ý quan trọng**: bug gốc thật sự có thể là **NVS bị xóa cùng lúc nạp firmware** nếu lần nạp đó
erase cả flash — nhưng `uploadfs`/`upload` thường KHÔNG đụng partition `nvs`, nên sau khi chuyển
sang NVS, danh sách WiFi sẽ **không mất khi nạp UI** nữa (đó chính là lý do chuyển).

## QR "một quét làm cả hai" (nối WiFi + mở dashboard)

Một QR chuẩn chỉ **một** hành động: `WIFI:...` (nối) HOẶC URL (mở). Không vừa nối vừa mở.

- **SoftAP** (máy không có router): **ĐÃ làm cả hai bằng một quét** — QR là mã join AP, rồi
  **captive portal** (DNSServer + `onNotFound` redirect) tự bật dashboard. Khả thi vì máy **là**
  mạng, chặn được thăm dò của điện thoại.
- **STA** (máy nối router): **không thể** — máy chỉ là client, không redirect được traffic điện
  thoại. Giữ QR = URL (điện thoại thường đã ở cùng router → quét mở dashboard ngay).

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS**, +5,7 KB flash (69.3%).
- `g++ -O2 -std=c++17 tools/test_wifi_store.cpp -o t && ./t` → **all contract checks passed**:
  newest-to-front, cập nhật mật khẩu không tạo bản trùng, cap 5 (rớt cái cũ nhất), remove
  present/absent, ssid rỗng/>32 bị chặn. *(Test host-side tái tạo thuật toán, không link .cpp —
  cùng khuôn các test tự-chứa khác trong `tools/`.)*
- Mock `/wifilist` GET/POST: liệt kê 2 mạng → `POST remove=Lab-2G` → còn 1.
- UI: chọn "FBT-Office" trong scan → ô SSID tự điền, con trỏ vào ô mật khẩu (đo bằng CDP:
  `document.activeElement === wifiPass`). Panel: **Saved networks** (nhãn "preferred" + nút
  Forget) → **Nearby networks** → SSID/Password.

## Đánh giá tính khả thi (yêu cầu là "test thử")

**Khả thi, chi phí thấp**: ~+8 KB flash, không đụng EEPROM, lưu ở **NVS** nên sống sót qua nạp,
tái dùng đúng cơ chế grace→SoftAP. Rủi ro chính đã xử: partition đúng (NVS vs LittleFS), không
thrash `async_tcp`, mật khẩu không lộ qua API.

**Còn hở**:

- **Mật khẩu nằm chữ thường trong NVS** — như EEPROM hiện tại, không tệ hơn, nhưng ai đọc
  được flash sẽ thấy hết.
- **Chưa test trên phần cứng thật**: cần 2 router. Xác nhận (a) tắt cái đang nối rồi bật lại →
  autoReconnect tự liền (không reboot); (b) power-cycle với chỉ cái thứ hai còn sóng → boot bắt
  đúng nó; (c) dòng TFT chuyển `IP → Scanning...`. Mock không mô phỏng tầng RF.
- **Đổi mạng cần power-cycle** (không tự động): mất WiFi đang dùng thì autoReconnect chỉ nối lại
  đúng mạng cũ; muốn sang mạng đã lưu khác phải tắt/bật máy. Đây là ràng buộc an toàn, không
  phải thiếu tính năng (xem phần failover).
- `connectSavedNetworks` **chặn boot tối đa ~13s** khi KHÔNG mạng nào có sóng (4s ưu tiên + cap
  9s fallback) → màn hình đen lâu hơn ở lần bật máy không có WiFi. Có mạng thì nối 1-3s.

## Đánh giá tính khả thi (cập nhật)

**Khả thi và an toàn**. Runtime-switching mạng bị chặn bởi bất biến "không `WiFi.begin()`
runtime" (**cứng**, bug treo vĩnh viễn đã có tiền sử) → multi-network **chỉ tác dụng lúc boot**,
đổi mạng bằng power-cycle. autoReconnect lo outage tạm của mạng đang dùng. Không có máy tự
reboot. Đây là bản dùng được thật cho việc chuyển phòng/nhiều mạng, chỉ không "hot-switch".

## Sửa loạt bug sau code-review (2026-07-24)

**Bug gốc "forget/connect không chạy trên máy" = GOTCHA 3, KHÔNG phải cache/NVS.**
`handleWifiList` phân nhánh theo `req->method() == HTTP_POST`, mà trong build này symbol
`HTTP_POST` (giá trị http_parser tuần tự) **≠** `request->method()` (bit-flag AsyncWebServer),
nên so **luôn sai** → mọi POST rơi nhánh GET → `wifiStoreRemove`/`connect` **không bao giờ chạy**
(serial cũng không có log). `/wifi` thoát nạn vì đọc thẳng param, không so method. Fix: **bỏ so
`req->method()`**, phân nhánh theo **param** (`remove`/`connect`/`action`), đăng ký **`HTTP_ANY`
một handler** (như `/rename`). Cùng bệnh ở `/ota` → gộp `handleOta` dispatch theo `?action`.
Guard mới `tools/test_no_method_branch.py` khoá bất biến "không handler nào so `->method()`".
**Xác nhận trên máy thật**: `POST /wifilist remove=hehe` → `{"ok":true}`, `hehe` biến mất.

Các fix kèm (từ review):

- **Trial result kẹt mãi**: `wifiStoreClearTrial` không xoá `res/rs` → panel báo "wrong password"
  mỗi lần mở. Fix: xoá result **ngay sau khi báo** (clear-on-read trong `wifiStoreListJson`).
- **Connect ngoài tầm bị báo "sai mật khẩu"**: thêm reason `auth` vs `range`
  (`WiFi.status()==WL_NO_SSID_AVAIL`) → web nói đúng.
- **Boot chặn ~13-21s**: rút budget fallback 9→5s, per-net 3→2.5s → xấu nhất ~9s.
- **`otaLastCheck` chỉ set khi 200**: giờ set mọi lần GET xong + cờ `otaCheckFailed` → web hiện
  "Check failed" thay vì kẹt "Not checked yet".
- **Reboot sau upload .bin**: thêm `!dashboardDeviceBusy()` — run bắt đầu trong 800ms không bị
  reboot phá (firmware đã staged, kích hoạt ở lần reboot idle sau).
- **`gasCode` trả qua `uint16_t`**: `-11` (timeout-nhưng-row-ghi) bị wrap thành 65525 → "Upload
  Failed" sai. Fix: `return gasOk ? 200 : 0` (gasOk gồm 2xx/302/-11).
- **Web/máy không đồng bộ mạng**: `/home` thêm `net{ssid,ip,ap}` (SSE cập nhật mỗi 1s) → card
  Device hiện Network + IP live; `/wifilist` thêm `current`/`active` → panel gắn nhãn "connected"
  đúng mạng máy đang nối. (Đổi sang subnet khác thì browser mất kết nối là điều không tránh được.)
- **Comment sai** (LittleFS/wifi.json → NVS) đã sửa để không dẫn người sau "sửa nhầm" về LittleFS.

**Còn hở của test**: `test_wifi_e2e.js` chạy trên mock (Python định tuyến khác AsyncWebServer) nên
KHÔNG bắt được bug GOTCHA 3 — guard host-side `test_no_method_branch.py` mới là lớp chặn thật.
