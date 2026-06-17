# FBT_RAPID — Spec & Kiến trúc App người dùng

Tài liệu thiết kế cho app đồng hành của thiết bị **Forte Rapid+ / FBT_RAPID**.
Liên quan: Mục 13 của [CLAUDE.md](../CLAUDE.md).

## 1. Mục tiêu & phạm vi

App đa nền tảng (iOS / Android / Windows / macOS) giúp người dùng:

- **Lịch sử xét nghiệm:** xem kết quả bệnh từng lần chạy + **đồ thị CT** (đường cong khuếch đại 10 slot).
- **Cài đặt:** cấu hình WiFi cho máy + thiết lập thông tin người dùng.

> **Giai đoạn hiện tại: build cho Windows trước**, framework **Flutter**, kết nối qua **WiFi/HTTP**.

## 2. Ràng buộc quan trọng (đã quyết)

| Vấn đề | Kết luận |
|---|---|
| Bluetooth của máy là **Bluetooth Classic (SPP)** (`BluetoothSerial` trong `Bluetooth.h`) | iOS không dùng được → **không dùng BLE/Classic cho app**; chỉ dùng **WiFi/HTTP + cloud** |
| Web server của máy chỉ chạy **khi có WiFi** và **tắt Bluetooth** khi bật (`SerialBT.end()` trong `postData_Chart`) | App giao tiếp qua HTTP khi máy đã ở cùng mạng WiFi |
| Apps Script chỉ có `doPost` (ghi), **chưa có `doGet`** | Chưa đọc lịch sử từ cloud được → **v1 lưu lịch sử cục bộ trên PC** |
| Máy chỉ lưu **lần chạy cuối** trong EEPROM | `/getdata` trả về kết quả mới nhất; lịch sử do app tích lũy |

## 3. Kiến trúc tổng thể

```
┌──────────────────────────┐        WiFi/LAN (HTTP GET)        ┌───────────────────────────┐
│   App Flutter (Windows)   │  ───────────────────────────────▶ │   ESP32 (FBT_RAPID)       │
│                           │      GET http://<ip>/getdata       │   WebServer cổng 80       │
│  - History (local store)  │  ◀───────────────────────────────  │   /  ,  /getdata          │
│  - Đồ thị CT (fl_chart)   │        JSON kết quả + đường cong     │   (chỉ chạy khi có WiFi)  │
│  - Settings (local)       │                                     └─────────────┬─────────────┘
└──────────────────────────┘                                                   │ doPost (HTTPS)
            ▲                                                                   ▼
            │ (roadmap) doGet đọc lịch sử                              ┌───────────────────┐
            └──────────────────────────────────────────────────────── │  Google Sheets    │
                                                                       │  (Apps Script)    │
                                                                       └───────────────────┘
```

- **Nguồn dữ liệu chính (v1):** `GET /getdata` trực tiếp tới máy (cùng mạng WiFi).
- **Lịch sử:** app lưu mỗi lần lấy thành 1 bản ghi trong storage cục bộ (`shared_preferences`).
- **Roadmap:** thêm `doGet` cho Apps Script để app đọc lịch sử nhiều máy từ cloud.

## 4. API thiết bị (hiện có)

Định nghĩa trong `src/Bluetooth.cpp` (`postData_Chart`, `getData_toChart`).

### 4.1 `GET /getdata` → `application/json`

Trả về kết quả **lần chạy gần nhất** đang lưu trong EEPROM:

```jsonc
{
  "id_device": "proto 0",
  "CT_value": ["N/A", "12.3", "...", /* 10 phần tử, chuỗi; "N/A" nếu âm tính */],
  "result":   ["Negative", "Positive", "Slide Positive", "E", /* 10 phần tử */],
  "#1":  ["-12.3", "0.0", "5.1", /* loops điểm — đường cong đã xử lý của slot 1 */],
  "#2":  ["..."],
  // ...
  "#10": ["..."]
}
```

Ghi chú:
- Tất cả giá trị là **chuỗi** (firmware serialize bằng `String()`).
- `result[i]` ∈ `"Positive"` | `"Negative"` | `"Slide Positive"` | `"E"` (lỗi). (Lưu ý firmware ghi `"Slide Positive"` — app chấp nhận cả `"Slight Positive"`.)
- `CT_value[i]` = `"N/A"` (âm tính) hoặc số (chuỗi).
- `#1`..`#10` = đường cong **đã baseline/smooth** (processed_data), mỗi mảng `loops` điểm (`amplification_time`, mặc định 120).
- `/getdata` **không** trả khoảng thời gian giữa các điểm → app dùng `readingIntervalSec` trong Settings (mặc định 20s) để quy đổi trục X sang phút.

### 4.2 `GET /` → trang HTML chart (dùng nội bộ, app không cần)

> Lưu ý: web UI cũ `data/script.js` gọi `/readings` + `/events` — **firmware không có** 2 endpoint này (đã lỗi thời). Dùng `/getdata`.

## 5. Mô hình dữ liệu app

```
TestResult
  id            : String        // = millis của thời điểm lấy, làm khóa lịch sử
  deviceId      : String        // từ id_device
  timestamp     : DateTime      // app gán lúc fetch
  slots         : List<SlotResult> (10)

SlotResult
  index          : int          // 1..10
  classification : Classification  // positive | negative | slightPositive | error | unknown
  ct             : double?      // null nếu N/A
  curve          : List<double> // đường cong processed_data
```

Lưu lịch sử: list `TestResult` serialize JSON trong `shared_preferences` (key `history_v1`).

## 6. Màn hình & chức năng (v1)

### 6.1 Lịch sử xét nghiệm (`HistoryScreen`)
- Nút **"Lấy kết quả từ máy"** → `GET /getdata` → tạo `TestResult` → lưu vào lịch sử → báo thành công.
- Danh sách bản ghi: thời gian, mã máy, tóm tắt số slot Dương/Âm/Lỗi. Bấm vào → chi tiết.
- Xóa 1 bản ghi / xóa tất cả.

### 6.2 Chi tiết kết quả (`ResultDetailScreen`)
- **Kết quả bệnh:** lưới 10 slot, mỗi slot hiện phân loại (màu) + CT.
- **Đồ thị CT:** `fl_chart` vẽ 10 đường cong, trục X = phút (quy đổi từ `readingIntervalSec`), trục Y = huỳnh quang. Bật/tắt từng slot.

### 6.3 Cài đặt (`SettingsScreen`)
- **Kết nối máy:** nhập IP máy + **"Kiểm tra kết nối"** (ping `/getdata`). Lưu cục bộ.
- **Khoảng đọc (giây):** để quy đổi trục thời gian (mặc định 20).
- **WiFi cho máy:** form SSID/mật khẩu. v1: hướng dẫn dùng **captive portal của WiFiManager** (kết nối PC vào AP của máy). Khi có endpoint firmware `/setwifi` (roadmap) thì gửi trực tiếp trong app.
- **Thông tin người dùng:** tên, đơn vị/phòng khám… lưu cục bộ.

## 7. Tech stack & cấu trúc thư mục

- **Flutter** (Dart). Deps tối thiểu để build Windows ổn định: `http`, `fl_chart`, `shared_preferences`.
- Quản lý trạng thái: `StatefulWidget` + service singleton (không thêm lib state-management để giảm rủi ro build).

```
app/
  pubspec.yaml
  README.md
  lib/
    main.dart
    models/test_result.dart
    services/device_api.dart
    services/history_store.dart
    services/app_settings.dart
    screens/home_shell.dart
    screens/history_screen.dart
    screens/result_detail_screen.dart
    screens/settings_screen.dart
    widgets/ct_chart.dart
    widgets/result_badge.dart
    util/format.dart
```

## 8. Khoảng trống & roadmap (cần thêm ở firmware/cloud)

Các tính năng sau cần bổ sung phía thiết bị/cloud; app đã chừa chỗ:

| Tính năng | Cần thêm | Ghi chú |
|---|---|---|
| Đọc lịch sử từ cloud | ✅ **Đã có backend** — `doGet` trong [sheet/getData.js](../sheet/getData.js) trả JSON các lần chạy theo từng ID máy (xem §10) | App đọc nhiều máy, nhiều lần, từ xa. Còn lại: phần app (§11) |
| Cài WiFi trong app (không cần captive portal) | Endpoint `GET /setwifi?ssid=..&pass=..` (lưu EEPROM qua `saveSettingDevice`) | Tránh phải đổi mạng PC |
| Thông tin người dùng đẩy xuống máy | Endpoint `GET/POST /config` hoặc tái dùng `JsonDataConfig` qua HTTP | Hiện chỉ đặt qua JSON trên BT/Serial |
| Lấy metadata máy qua HTTP | Endpoint trả nội dung lệnh `M` (slopes/origins/units/interval) | Giúp app tự biết `readingIntervalSec`, đơn vị |

## 9. Chạy app trên Windows

Xem [app/README.md](../app/README.md). Tóm tắt:
```powershell
# Cần: Flutter SDK + Visual Studio (workload "Desktop development with C++")
cd app
flutter create --platforms=windows .   # sinh runner Windows, giữ nguyên lib/ & pubspec
flutter pub get
flutter run -d windows
```

## 10. API đọc lịch sử từ Drive (`doGet`)

Cài đặt trong [sheet/getData.js](../sheet/getData.js), **cùng project Apps Script** với `doPost`
(dùng chung `folderId = 1nAQT5LBkFJZ1OXIVOHqSRL-ZhkzcWwgH`). Đây là nơi firmware đã đổ log mỗi lần
chạy thành file `Log_<id_device>-<time>.txt` (qua `saveAmplificationToFolder`).

### 10.1 Bối cảnh nguồn dữ liệu

- Mỗi **file** = một **lần chạy**. Folder lưu **phẳng** (không có thư mục con theo ID), nhưng `doGet`
  vẫn quét đệ quy phòng trường hợp về sau gom theo ID. Quy mô thực tế đã xác nhận: **~2765 file**.
- Firmware **không gửi `time`** (dòng bị comment trong `Bluetooth.cpp`) → Apps Script tự đặt tên file
  bằng `new Date().toISOString()`. Vì vậy **mốc thời gian lấy thẳng từ tên file**
  (`Log_<id>-<ISO>.txt`), tránh gọi `file.getDateCreated()` cho từng file (chậm ở quy mô nghìn file).
- **Hiệu năng:** `runs` dùng `folder.searchFiles("title contains 'Log_<id>-'")` để chỉ quét file của
  đúng máy đó; chỉ `ids`/`peek` mới duyệt cả folder (chặn ở `LOG_SCAN_LIMIT = 5000`). `ids` còn đọc
  **1 file/máy** (file mới nhất) để lấy `version` firmware → chậm hơn chút (mỗi máy +1 lần đọc Drive).
- Nội dung file là **JSON đầy đủ** (bản `getData.js`); parser vẫn chịu được **định dạng text cũ**
  (bản `src/AppScript.js`) — tự dò bằng `JSON.parse`, fallback sang parse text.
- `result[i]` trong log có dạng `"<CT> | <chữ>"`, chữ ∈ `P` (Positive) / `S` (Slide Positive) /
  `N` (Negative) / `E` (Error) — xem `Bluetooth.cpp::postData_GoogleSheet`. `doGet` chuẩn hoá về
  một chữ cái + `resultLabels`.

### 10.2 Endpoint (query string trên URL `/exec`)

| `action` | Tham số | Trả về |
|---|---|---|
| `ids` (mặc định) | — | Danh sách **ID máy** + số lần chạy + lần mới nhất |
| `runs` | `id` (bắt buộc), `limit` (mđ 50), `offset` | **Tóm tắt** các lần chạy của 1 máy, mới nhất trước, **không kèm đường cong** |
| `run` | `fileId` (bắt buộc) | **Chi tiết** 1 lần chạy: kèm `curves` (10 mảng số) + `outcome`/`peak` |
| `peek` | — | (debug) 1 file mẫu: tên, định dạng dò được, 600 ký tự đầu |

Tuỳ chọn `&callback=fn` cho JSONP (chỉ cần cho Flutter Web; desktop/mobile không cần).

### 10.3 Ví dụ response

`?action=ids`:

```jsonc
{
  "ok": true,
  "folderId": "1nAQT5LBkFJZ1OXIVOHqSRL-ZhkzcWwgH",
  "count": 2,
  "devices": [
    { "id": "RPL250701", "runCount": 12, "latest": "2025-07-30T11:18:59.123Z", "version": "v2.4.0" },
    { "id": "RAPIDPlus", "runCount": 3,  "latest": "2025-06-01T09:10:00.000Z", "version": "v2.3.9" }
  ]
}
```

`?action=runs&id=RPL250701&limit=50`:

```jsonc
{
  "ok": true, "id": "RPL250701", "total": 12, "offset": 0, "limit": 50, "count": 12,
  "runs": [
    {
      "fileId": "1AbC...",                 // dùng để gọi action=run lấy chi tiết
      "fileName": "Log_RPL250701-2025-07-30T11:18:59.123Z.txt",
      "id_device": "RPL250701",
      "version": "V2.2.9",
      "time": "2025-07-30T11:18:59.123Z",  // = created nếu firmware không gửi time
      "created": "2025-07-30T11:18:59.123Z",
      "ct": [3, 31, 25.3, /* …10 */],
      "result": ["N", "P", /* …10: P|N|S|E|? */],
      "resultLabels": ["Negative", "Positive", /* … */],
      "counts": { "positive": 1, "negative": 9, "slightPositive": 0, "error": 0, "unknown": 0 }
    }
    // …
  ]
}
```

`?action=run&fileId=1AbC...` — như một phần tử `runs` ở trên, **cộng thêm**:

```jsonc
{
  "ok": true,
  "run": {
    /* …các trường tóm tắt… */
    "curves": [[212, 212, 214, /* …loops điểm */], /* …10 mảng */],
    "loops": 120,
    "outcomeDetail": [ /* transition_time/plateau_point/increase mỗi slot */ ],
    "peakFeatures": [ /* main_peak/left_arm/right_arm mỗi slot */ ]
  }
}
```

### 10.4 Triển khai & kiểm thử

1. Mở project Apps Script chứa `doPost` (gắn với folder Drive trên).
2. Dán/đồng bộ nội dung [sheet/getData.js](../sheet/getData.js) (đã có thêm `doGet` + helper).
3. **Deploy → Manage deployments → Edit → New version** (nếu chưa deploy: *Deploy → New deployment →
   Web app*, **Execute as: Me**, **Who has access: Anyone**).
4. Test nhanh trong editor: chạy `test_doGet_peek` / `test_doGet_ids` (xem log) để xác nhận định
   dạng file thực tế.
5. Test qua URL (mở trình duyệt):
   `https://script.google.com/macros/s/<DEPLOY_ID>/exec?action=peek`
   rồi `...&action=ids`, `...?action=runs&id=<ID>`.

## 11. Kế hoạch phía app (lịch sử theo từng ID — giai đoạn sau)

Chưa code ở giai đoạn này. Khi backend §10 đã test xong, phần app dự kiến:

- **`services/cloud_history_api.dart`** (mới): HTTP client gọi `doGet`, base URL = Apps Script
  `/exec` (lưu trong Settings). Hàm: `listDeviceIds()`, `listRuns(id)`, `fetchRun(fileId)`.
- **`models/test_result.dart`**: thêm factory `TestResult.fromCloudRun(json)` map `result` letter
  (P/N/S/E) → `Classification`, `curves` → `SlotResult.curve`, `time` → `timestamp`,
  `fileId` → `id` (khoá ổn định, chống trùng khi đồng bộ).
- **`services/app_settings.dart`**: thêm `cloudApiUrl` (URL `/exec`).
- **UI**: tab Lịch sử thêm bước **chọn ID máy** (từ `listDeviceIds`) → danh sách lần chạy của máy đó
  (`listRuns`) → bấm vào 1 lần chạy mới gọi `fetchRun` lấy `curves` để vẽ đồ thị CT (lazy, tránh tải
  toàn bộ đường cong). Có nút **Đồng bộ** để merge vào `HistoryStore` cục bộ (dedupe theo `fileId`).

> Lưu ý hiệu năng: `runs` cố ý **không** trả `curves`; chỉ `run` (1 file) mới trả — để danh sách
> nhẹ, đồ thị tải theo nhu cầu.
