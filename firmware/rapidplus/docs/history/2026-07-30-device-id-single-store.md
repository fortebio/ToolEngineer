# 2026-07-30 — Xoá hẳn `id_device`: ID thiết bị còn MỘT store

Tiếp nối [2026-07-29-device-id-two-stores-and-ios-zoom.md](2026-07-29-device-id-two-stores-and-ios-zoom.md).
Hôm qua vá triệu chứng (đồng bộ hai store); hôm nay **xoá một store** — hết cả lớp bug.

## Vì sao vá đồng bộ là chưa đủ

Bản hôm qua giữ hai store và thêm cơ chế mirror. Nó chạy được, nhưng:

- mỗi đường ghi mới phải **nhớ** mirror, nếu quên thì lệch lại;
- mirror phải nằm **ngoài `eepromLock()`** và **có guard `containsKey`** — hai cái bẫy chỉ tồn
  tại *vì* có hai store;
- guard phải khoá cả cơ chế mirror, tức khoá luôn cái phức tạp không cần thiết.

Một store thì không cần mirror, không cần guard mirror, không có gì để lệch.

## Store nào ở lại

`parameter.device_id` (EEPROM `PARAMETERPOS`, đọc qua macro `protoID`).

| | `id_device` (đã xoá) | `parameter.device_id` (giữ) |
|---|---|---|
| Kiểm tính hợp lệ | **không có** | có — `parameter.length == sizeof(parameter)` |
| Đọc bằng | `EEPROM.readString(170)` — quét NUL **tới hết buffer 4096 B** | `EEPROM.get()` struct có gác |
| Sửa từ Serial/BT | không | có (key `"device ID"`) |

Chọn cái **có sẵn cơ chế kiểm** và **là cái Serial/BT vẫn ghi**. Slot `170..210` nay **chết** —
`ADDR_ID_DEVICE_BASE` không còn ai ghi/đọc, `ADDR_CHECK_ID_DEVICE` đi theo (reader duy nhất là
portal WiFiManager, đã xoá 2026-07-29).

## Thay đổi

| File | Việc |
|---|---|
| `Bluetooth.cpp` | xoá global `String id_device`; `loadSettingDevice()` trở lại **chỉ** ssid+password; `saveSettingDevice()` bỏ ghi slot 170 + cờ `ADDR_CHECK_ID_DEVICE` |
| `Bluetooth.h` | xoá `extern String id_device` |
| `ForteSetting.cpp/.h` | thêm **`sanitiseDeviceId()`**; gọi ở cuối `begin()` và trong `JsonDataConfig()` khi key `"device ID"` có mặt |
| `webDashboard.cpp` | `doc["device"]`, `dashboardApName()`, `dashboardHostname()` → `protoID` |
| `main.cpp` | **chuyển `_displayCLD.begin()` + `_ForteSetting.begin()` lên trước khối WiFi** |

`errorCheck.cpp`, `Bluetooth.cpp` (payload upload), `sensor6035.cpp` đã dùng
`parameter.device_id`/`protoID` từ trước.

## Bẫy chính: THỨ TỰ BOOT

ID nay **chỉ** nằm trong `_ForteSetting.parameter`, mà struct đó do `_ForteSetting.begin()` nạp
từ EEPROM. Trong khi `WiFi.setHostname(dashboardHostname())` — **đọc ID** — đứng trước nó:

```
305 loadSettingDevice()
322 WiFi.setHostname(dashboardHostname())   <- đọc ID
341 _ForteSetting.begin()                    <- ID mới được nạp ở đây
```

Để nguyên thì hostname DHCP dựng từ **mặc định biên dịch**, còn mDNS (chạy sau, trong
`dashboardBegin()`) dùng ID thật → **hai tên khác nhau cho cùng một máy**.

Nên `_ForteSetting.begin()` chuyển lên trước khối WiFi, kéo theo `_displayCLD.begin()` vì
`ForteSetting::begin()` vẽ `ErrorDisplay()` khi EEPROM trắng. Cả hai là TFT/EEPROM thuần, không
phụ thuộc WiFi → an toàn, và splash hiện sớm hơn. `_PIDControl.begin()` **giữ nguyên chỗ cũ**
(không đụng heater).

Guard `test_device_id.py` khoá **cả hai thứ tự** này.

## `sanitiseDeviceId()` — một trust boundary

Đặt ở đúng nơi store được nạp. Hai thứ có thể sai:

1. **Không NUL-terminated.** `begin()` đổ struct bằng `EEPROM.get()` thô; block từ layout cũ có
   thể để cả 10 byte khác 0 → mọi `strlen`/`String` sau đó **đi lố sang `slopes[]`**.
2. **Rác.** Mọi writer sinh ra 1..9 ký tự in được (`/deviceid` validate 1..9, `handleConfigPost`
   từ chối > 9, field là `char[10]`) → khác thế là **chứng minh được** không phải bản ghi thật.

Rác → `"UNSET"`, hiện trên **cả** TFT start screen lẫn header web. Không persist: lần Save thật
mới ghi, tới lúc đó thì cứ nhắc.

Cũng gọi trong `JsonDataConfig()` vì **Serial/BT vào thẳng hàm đó, không qua validate nào**
(CLAUDE.md Setting #4) — `strlcpy` chặn *độ dài* nhưng ký tự điều khiển vẫn lọt.

## Kiểm

`tools/test_device_id.py` viết lại cho kiến trúc mới, 7 bất biến:

1. **Không còn định danh `id_device` ở bất kỳ file nào trong `src/`** — bất biến cốt lõi.
2. `saveSettingDevice()` không ghi lại `ADDR_ID_DEVICE_BASE`.
3. `sanitiseDeviceId()` còn NUL-terminate trước khi đọc + còn loại byte không in được.
4. `begin()` **và** `JsonDataConfig()` đều gọi nó (cái sau có guard `containsKey`).
5. **Thứ tự boot**: `_displayCLD.begin()` → `_ForteSetting.begin()` → `WiFi.setHostname()`.
6. Ba giới hạn độ dài khớp nhau.
7. Thẻ Setting đọc `deviceIdNow`; input touch 16px; viewport không chặn zoom.

**Lọc chuỗi trước khi soi:** `code_only()` bỏ comment, **raw string** `R"(...)"` **và** string
thường. Cần cả ba — `dataPostGoogleSheet["id_device"]` là **tên field của cloud** (giữ nguyên,
không phải biến C++), và `src/index.h` chứa trang HTML cũ trong raw string với biến JS tên
`id_device`.

Negative test từng cái:

| Gieo lỗi | Guard báo |
|---|---|
| `doc["device"] = id_device` | `` `id_device` is back (~line 318) `` |
| trả `_ForteSetting.begin()` về sau WiFi | `WiFi.setHostname() BEFORE _ForteSetting.begin()` |
| bỏ `sanitiseDeviceId()` trong `begin()` | `EEPROM noise reaches every reader` |
| `saveSettingDevice()` ghi lại slot 170 | `that is the second store coming back` |

Build `pio run -e esp32dev` SUCCESS, **2 365 861 B** (70.8%), RAM 77 020 B (−16 B: mất một
`String` global). 7/7 guard xanh.

### Sửa kèm: bỏ trùng lặp giữa hai guard

`test_qr_payload.py` check #4 từng assert bound của ID trong `loadSettingDevice()`. ID chuyển
sang `sanitiseDeviceId()` → check đó **đỏ vì một bug không còn tồn tại**. Đã bỏ hẳn: file này sở
hữu số học payload QR, `test_device_id.py` sở hữu bound của ID. **Hai guard cùng khoá một bất
biến thì luôn có một cái lỗi thời.**

## Cần làm trên máy thật

1. Nạp `upload`. Serial lúc boot: `[id] no usable device ID in EEPROM -> "UNSET"` nếu EEPROM
   chưa có ID hợp lệ.
2. Setting → Device ID → nhập ID thật → Save. Sau đó **header, thẻ Setting, QR, tên SoftAP,
   `<id>.local` và upload** phải cùng một giá trị — giờ chúng đọc chung một biến, không còn
   đường nào để lệch.
3. Kiểm hostname: `[dash] mDNS up -> http://<id>.local/` phải khớp tên máy thấy trên router.

## Ghi chú: `index_html` là code chết

Phát hiện lúc viết guard: `src/index.h` khai báo `const char* index_html = R"rawliteral(...)"`
(trang chart cũ của sync WebServer). Được `Bluetooth.cpp` include nhưng **`index_html` không
được tham chiếu ở đâu cả** — linkage ngoài nên vẫn tốn flash. Chưa xoá vì ngoài phạm vi lần
sửa này.
