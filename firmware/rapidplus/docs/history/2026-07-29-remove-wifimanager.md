# 2026-07-29 — Gỡ WiFiManager (−96 160 B flash)

## Câu hỏi ban đầu

"WiFiManager có đang dùng vào việc nào không? Bỏ được để nhẹ flash hơn không?"

Câu trả lời: **có dùng**, nhưng đường đó đã bị dashboard thay thế hoàn toàn từ nhiều tháng
trước mà không ai gỡ thư viện.

## Nó đang được dùng ở đâu

Một đường duy nhất, qua nút vật lý:

```
escreenStart → (BLUE long-press) → eSettingMenu
             → (GREEN) button.cpp:627 → eSettingWifi
             → displayLCD.cpp case eSettingWifi → setting_Wifi()
             → Bluetooth.cpp Wifi_Connect()
             → wifiManager.autoConnect("FBT <id>")   ← captive portal
             → saveSettingDevice() → esp_restart()
```

`setting_Wifi()` chỉ là khung vẽ TFT quanh cái portal đó (vẽ ssid/pass/id trước, vẽ lại sau,
rồi `esp_restart()`). Không có mục đích nào khác.

## Vì sao bỏ được: dashboard đã làm đủ

| WiFiManager làm | Dashboard làm | Route |
|---|---|---|
| Portal AP nhập SSID/pass | SoftAP `RAPID-<id>` + captive portal → Setting tab → WiFi card | `POST /wifi`, `GET/POST /wifilist` |
| `WiFiManagerParameter custom_id_device` | Device ID card | `POST /deviceid` |
| menu `"update"` (ESP web OTA upload) | Firmware card | `GET/POST /ota`, `POST /otaupload` |

Quan trọng: **SoftAP fallback là tự động** (`dashboardStartAP()` khi STA fail lúc boot), còn
portal WiFiManager phải người vận hành đi tới máy bấm 2 nút mới có. Đường thay thế ngắn hơn
đường bị bỏ.

`wifiStore.cpp` (NVS namespace `wifinets`) không hề import WiFiManager — hai hệ thống độc lập,
gỡ một cái không đụng cái kia.

## Đo thật (build 3 lần)

| Bản | Flash | % |
|---|---|---|
| Trước (có WiFiManager) | 2 460 701 B | 73.6% |
| Bỏ lib + `Wifi_Connect` + `setting_Wifi` | 2 364 573 B | 70.7% |
| Bỏ thêm `dashboardEnd()` (dead) | **2 364 541 B** | **70.7%** |

**Tổng: −96 160 B (~94 KB), 73.6% → 70.7%.** RAM −136 B.

Con số này lớn hơn "chỉ là mấy chuỗi HTML" vì WiFiManager kéo theo `DNSServer` + toàn bộ
`WebServer` handler tree + các trang cấu hình dựng bằng String.

## Ba cái gai gỡ được kèm theo

Không phải phần thưởng phụ — đây mới là lý do đáng gỡ hơn cả 94 KB:

1. **`disableCore0WDT()` không bao giờ bật lại** (`Bluetooth.cpp:336` cũ). Comment gốc thừa nhận
   nó dựa vào `esp_restart()` ở cuối `setting_Wifi()` để "bật lại". Nghĩa là trong suốt thời
   gian người dùng đứng ở captive portal, Core 0 **không có watchdog** trong khi ControlTask
   (Core 1) vẫn đang giữ duty heater. Nếu người dùng bỏ đi giữa chừng, máy nằm ở trạng thái đó
   vô thời hạn.
2. **`wifiManager.resetSettings()`** xoá creds lưu trong NVS của core **mỗi lần vào màn hình**,
   trước cả khi người dùng kịp nhập gì. Vào nhầm màn = mất mạng đang chạy.
3. **Portal dựng SoftAP của riêng nó**, sau khi gọi `dashboardEnd()` + `WiFi.disconnect(true)`.
   Người vận hành nào đang xem dashboard trên điện thoại (kể cả qua AP fallback) bị đá ra ngay
   lúc đó, không báo trước.

## Thay đổi

| File | Việc |
|---|---|
| `platformio.ini` | bỏ `tzapu/WiFiManager@^2.0.17` |
| `src/Bluetooth.h` | bỏ `#include <WiFiManager.h>` + khai báo `Wifi_Connect` |
| `src/Bluetooth.cpp` | xoá `Wifi_Connect()` (~62 dòng) |
| `src/displayLCD.cpp` | xoá `setting_Wifi()` (~80 dòng) + `case eSettingWifi`; nhãn menu `"Wifi/Update"` → `"Wifi/Web QR"` |
| `src/displayCLD.h` | xoá enum `eSettingWifi` + khai báo `setting_Wifi` |
| `src/button.cpp` | GREEN ở `eSettingMenu` → **`eShowQR`** thay vì `eSettingWifi` |
| `src/webDashboard.cpp/.h` | xoá `dashboardEnd()` — `Wifi_Connect` là caller **duy nhất** |

### Nút GREEN giờ mở QR

Cùng một cử chỉ ("đưa điện thoại vào giao diện cấu hình"), ít hơn 1 thư viện, 1 lần reboot và
1 cái watchdog bị tắt. `eShowQR` vốn đã có sẵn (WHITE ở màn chính), đã nằm trong allowlist
`isBusy()`, và lối ra đã có: WHITE → `escreenStart` (`button.cpp:744`).

### Xoá enum `eSettingWifi` có an toàn không?

Có. `type_infor` **chưa bao giờ được serialize** — `webDashboard.cpp:247` ghi rõ "A name, not
the raw enum: the client must not hardcode type_infor's numbering", và `fillStatus()` map sang
chuỗi `phase`. Không có `EEPROM.put` nào chạm `e_statuslcd`. Đánh số dịch đi không ảnh hưởng gì.

## Cái KHÔNG đổi

- `dashboardSuspend()`/`dashboardResume()` giữ nguyên — đó là cặp *có thể đảo ngược* dùng cho
  cửa sổ TLS upload. Cái bị xoá là `dashboardEnd()` (hạ vĩnh viễn), thứ chỉ portal mới cần.
- `releaseBluetoothStack()` vẫn được gọi sớm ở `main.cpp` (GOTCHA 1) — nó không phụ thuộc
  WiFiManager.
- Cơ chế EEPROM preferred pair + `connectSavedNetworks()` giữ nguyên.
- Bất biến "không `WiFi.begin()` runtime" (GOTCHA 8) không bị đụng: `Wifi_Connect` không gọi
  `WiFi.begin()` trực tiếp, portal gọi bên trong lib.

## Kiểm

```
python tools/test_no_runtime_wifi_begin.py   PASS (1 call site, main.cpp:194 boot)
python tools/test_no_method_branch.py        PASS
python tools/test_phase0_guards.py           PASS
python tools/test_web_assets.py              PASS
python tools/test_ota_guards.py              PASS
pio run -e esp32dev                          SUCCESS, 2 364 541 B
```

## Cần thử trên máy thật

Build sạch không chứng minh được UX. Ba việc:

1. Setting menu → GREEN → phải ra màn QR (không phải màn trắng / reboot), WHITE thoát về start.
2. Máy chưa từng có WiFi (EEPROM trắng) → boot → phải tự lên SoftAP `RAPID-<id>`, quét QR vào
   được dashboard, nhập mạng qua Setting → WiFi.
3. Nhập sai mật khẩu → phải giữ mạng cũ + `/wifilist` trả `trial:failed` (đường trial-then-commit
   vẫn nguyên, nhưng giờ nó là đường **duy nhất** nên đáng xác nhận lại).
