# 2026-07-30 — Đổi Device ID ở SoftAP: QR không vào lại được hotspot

## Triệu chứng

Đang chạy ở SoftAP fallback, đổi Device ID qua web → quét QR **không join lại được**, trừ khi
tắt/bật máy.

## Gốc rễ: gộp công thức không sửa được trạng thái phần cứng

Hôm 2026-07-29 tôi gộp cả ba nơi dựng SSID về `dashboardApName()` để hết lệch prefix
([chi tiết](2026-07-29-qr-reset-ssid-drift.md)). Chưa đủ — vì **ba consumer đó không cùng loại**:

| Consumer | Kiểu | Sau khi ID đổi |
|---|---|---|
| `dashboardStartAP()` → `WiFi.softAP()` | **latch vào radio, chạy 1 lần** | vẫn phát tên **CŨ** |
| `buildHomeJson()` → `net.ssid` | đọc **live** | báo tên **MỚI** |
| `screen_QR()` | đọc **live** | mã hoá tên **MỚI** |

`WiFi.softAP()` xuất hiện **đúng 1 lần** trong `src/`, nằm trong nhánh `if (!started)` đã chết sau
`dashboardBegin()` đầu tiên; `apActive` set 1 lần và **không bao giờ clear**. Không có đường nào
raise lại AP. `PEND_ID` thì chỉ ghi EEPROM — không reboot, không đụng AP.

**Bài học:** "một nguồn sự thật" cho một *công thức* khác với "một nguồn sự thật" cho một *trạng
thái*. Khi một consumer **chốt** giá trị vào phần cứng còn các consumer khác **tính lại**, gộp
công thức chỉ làm chúng lệch **đồng bộ hơn**, không hết lệch. Câu hỏi "tên này *nên* là gì" và
"tên nào *đang trên sóng*" là hai câu hỏi khác nhau.

## Sửa: hai phần, cả hai đều cần

### (A) Báo đúng sự thật — hỏi radio

`WiFi.softAPSSID()` (core 2.0.11, `WiFiAP.cpp:181`) bọc `esp_wifi_get_config(WIFI_IF_AP)` → trả
**tên driver đang giữ**, không thể cũ.

- `screen_QR()` nhánh AP: `String ap = WiFi.softAPSSID();` — rỗng thì in "No network", không mã hoá.
- `buildHomeJson()`: `net["ssid"] = ap ? WiFi.softAPSSID() : ...`
- `dashboardApName()` giữ nguyên, nay **chỉ còn một consumer**: `dashboardStartAP()`.

**Không thêm hàm accessor, không fallback về builder.** `softAPSSID()` *chính là* accessor; bọc nó
rồi fallback về `dashboardApName()` là tái tạo đúng lời nói dối đang gỡ, ngay ở tình huống nó sai.
Fallback là **chuỗi rỗng**: cả hai caller đã có nhánh "không biết" (TFT in "No network", web gửi
`""` → client render `SoftAP (hotspot)`). Builder-fallback sẽ in một câu trả lời **tự tin và sai**
đúng lúc ta không có sự thật.

### (B) Áp dụng thật — reboot hoãn, gate theo idle

ID bị latch ở **ba** chỗ lúc boot: SSID (`WiFi.softAP`), tên DHCP (`WiFi.setHostname`) và mDNS
(`MDNS.begin`). Core **không có API** đổi tại chỗ cái nào. Nên: reboot để re-latch cả ba qua đúng
đường đã chạy tốt.

`PEND_ID` và `JsonDataConfig()` nay đều: chụp giá trị cũ → `strlcpy` → **`sanitiseDeviceId()`** →
ghi EEPROM → nếu **thực sự đổi** thì `dashboardRequestRestart(1500)`.

- **Gate theo "giá trị có đổi không", không theo mode.** Thẻ Setting pre-fill ID hiện tại, nên bấm
  Save mà không sửa sẽ post lại chính nó — reboot ở đó là vô cớ.
- **Reboot cả ở STA**, không chỉ AP: ở STA `screen_QR()` in `http://<newid>.local/` mà mDNS đăng ký
  `<oldid>` → gõ vào không ra. Cùng lỗi, cùng nguyên nhân, khác chế độ.
- **`dashboardRequestRestart()`, không `ESP.restart()`**: nó hoãn tới khi
  `!dashboardDeviceBusy() && !suspended && type_infor != escreenFinished` → không bao giờ cắt ngang
  run. Cơ chế này **đã có sẵn** cho OTA, chỉ là `PEND_ID` chưa từng gọi.
- **1500 ms** (không phải mặc định 800): khớp tiền lệ `PEND_WIFI`, đủ cho `settleSave()` render
  trước khi SSE đứt.
- `sanitiseDeviceId()` chạy **trước** khi ghi EEPROM: `/deviceid` chặn *độ dài* 1..9 nhưng không
  chặn byte điều khiển, nên POST độc hại sẽ persist `"UNSET"` thay vì rác.

**`JsonDataConfig()` quan trọng hơn chứ không kém**: khác `POST /deviceid` (có `guardBusy` → 409
giữa run), đường Serial/BT **không có gate nào** và đổi được tên giữa run — chính
`dashboardRequestRestart()` là thứ giữ reboot lại tới khi run xong.

### Đã cân nhắc và LOẠI

- **Gọi `softAP()` lần hai để đổi tên sống.** Arduino không deauth, IDF làm gì với station đang kết
  nối **không tài liệu hoá**. Khả dĩ nhất: station **trụ được bây giờ** (association khoá theo
  BSSID) rồi **không join lại được sau** — lỗi tệ hơn lỗi đang sửa vì vô hình tới lúc người vận
  hành đã đi khỏi. `dashboardStartAP()` cũng **không idempotent** (kèm `releaseBluetoothStack`,
  `WiFi.mode`, `dnsServer.start`). GOTCHA 8 là luật nhà.
- **Poll so lệch trong `dashboardLoop()`** (`if (softAPSSID() != dashboardApName()) restart()`).
  Trigger kiểu *điều kiện thường trực* **sống sót qua chính cái reboot nó gây ra** — chỉ cần một
  khác biệt chuẩn hoá giữa chuỗi ta tính và chuỗi driver lưu là thành **vòng lặp reboot vô tận trên
  thiết bị y tế**. Dạng event-driven không lặp được vì trigger là một lần ghi.

## Hai lỗi phụ sửa kèm (cùng hàm)

1. **`line2` là code chết**: gán 3 lần trong `screen_QR()`, **không bao giờ được vẽ**. Đã xoá.
2. **Dòng "Wifi Name:" rỗng ở chế độ AP**: nó in `WiFi.SSID()` = SSID của **STA**, mà ở SoftAP thì
   STA không nối gì. Tức nó trống đúng ở chế độ mà đường thoát "đọc tên trên màn rồi join tay" cần
   nó nhất. Nay `dashboardIsAP() ? WiFi.softAPSSID() : WiFi.SSID()`.

Và một trùng lặp: `JsonDataConfig()` có **hai** khối `containsKey("device ID")` giống hệt nhau
trong cùng scope (khối sau ghi đè khối trước, cùng kết quả). Đã bỏ khối thừa.

## UI (`data/script.js` `renderDeviceId`)

Cảnh báo reboot đặt **sau ô input, ngay trên nút** — không phải trên input:
`test_device_id.py` cắt hàm này theo độ dài cố định và cần `deviceIdNow` nằm trong lát cắt; đặt
trên input là ăn hết ngân sách vô cớ, mà cảnh báo nằm ngay trên nút nó nói về cũng đúng UX hơn.
Nhãn nút `"Save"` → `"Save & reboot"`. Chuỗi **không** nhắc prefix `FBT-` (guard cấm literal đó
trong `script.js` — đúng loại bản sao thủ công sinh ra bug này).

## Kiểm

`tools/test_qr_payload.py` mục 2 **viết lại**, mạnh hơn chứ không nới:

> Builder nuôi **radio**, và chỉ radio. Ai **báo cáo** tên thì **hỏi radio**.

- `dashboardStartAP()` vẫn gọi `dashboardApName()`;
- **`WiFi.softAP()` đúng 1 lần trong toàn `src/`, và ở trong hàm đó** — đây là thứ giữ số học ≤53 B
  của mục 1 còn đúng khi QR đọc driver thay vì builder (chuỗi duy nhất từng lên sóng là output đã
  clamp), đồng thời chốt quyết định không raise AP sống;
- `dashboardApName()` có **đúng 1 call site** ngoài thân nó;
- `screen_QR()` + `buildHomeJson()` có `softAPSSID()` và **không** có `dashboardApName()`.

Bản cũ cho phép **bao nhiêu** `softAP()` cũng được và bao nhiêu consumer cũng được, miễn hai hàm
có tên mỗi cái chứa một lời gọi. Bản mới ghim cả call graph.

**Phải sửa `code_only()` kèm theo**: nó chỉ bóc `//`, không bóc `/* */`, nên doc-header của
`screen_QR()` (có nhắc `dashboardApName()` khi *giải thích vì sao không còn gọi nó*) bị đếm thành
call site thứ hai. Guard trip vì đọc phải văn xuôi là guard sẽ bị xoá.

Negative test 3/3, mỗi lỗi trúng ≥2 assertion:

| Gieo lỗi | Guard báo |
|---|---|
| `screen_QR()` quay lại `dashboardApName()` | 2 call site + "that is exactly the stale-SSID bug" |
| thêm `WiFi.softAP()` thứ hai | `must be called exactly once ... found [x2]` |
| `buildHomeJson()` bỏ `softAPSSID()` | mất `softAPSSID` + gọi lại builder |

Build `pio run -e esp32dev` SUCCESS, **2 355 889 B (70.5%)** — giảm ~10 KB so với trước
(2 365 861 B) nhờ bỏ `line2` + khối `containsKey` trùng. 7/7 guard xanh.

## Cần làm trên máy thật

Mock **không tái hiện được** bug này: `/home` hardcode `"ap": False`, không mô hình `apActive` lẫn
tên AP. Thêm mô hình AP sẽ phải viết literal `"FBT-"` trong `tools/` — tái tạo đúng bản sao thủ
công mà guard sinh ra để chặn.

1. Bật máy ngoài tầm mọi mạng đã lưu → `[dash] SoftAP 'FBT-<old>'`.
2. WHITE → QR → quét → join. **Dòng "Wifi Name:" trên TFT giờ phải có chữ** (trước đây rỗng).
3. Setting → Device ID → đổi → **phải thấy cảnh báo reboot** → Save & reboot.
4. **Bước bắt regression**: bấm GREEN vào preheat ngay sau khi Save để giữ reboot lại, rồi WHITE →
   QR. Payload, dòng "Wifi Name:" và `SoftAP (...)` trên web **phải vẫn là `FBT-<old>`**. Trước fix
   cả ba lật sang tên mới tức thì. Quét bằng máy thứ hai: **vẫn join được**.
5. Serial: `[cfg] device id changed - reboot queued to re-announce it` → `[dash] deferred restart`.
6. Sau reboot: `[dash] SoftAP 'FBT-<new>'`; QR mã hoá tên mới.
7. **Save không đổi gì** → có `[cfg] device id:` nhưng **không** có dòng reboot.
8. **Hoãn**: chạy run → `POST /deviceid` phải **409**; đổi qua Serial thì áp dụng được, arm reboot,
   **máy phải chạy hết run**, restart chỉ nổ sau khi WHITE rời `escreenFinished`.
9. **STA**: `ping <oldid>.local` ra → đổi ID → reboot → `ping <newid>.local` ra, tên cũ tắt.
