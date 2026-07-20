# 06 — Mạng, boot, upload, OTA, heap

Nguồn: [src/main.cpp](../../src/main.cpp), [src/Bluetooth.cpp](../../src/Bluetooth.cpp),
[src/updateOTA.cpp](../../src/updateOTA.cpp), [src/webDashboard.cpp](../../src/webDashboard.cpp).

## Trình tự boot — `setup()`

```mermaid
flowchart TD
  A[Serial + I2C Wire.begin] --> B[Tạo gI2CMutex + gSPIMutex<br/>NULL → hard halt]
  B --> C[buttonStart]
  C --> D[loadSettingDevice: ssid/password/id_device từ EEPROM]
  D --> E[WiFi.begin STA, retry ≤20×50ms ~1s]
  E --> F{WL_CONNECTED?}
  F -- không --> G[dashboardStartAP: SoftAP RAPID-id]
  F -- có --> H[tiếp tục]
  G --> H
  H --> I[displayCLD/ForteSetting/PIDControl.begin + logo]
  I --> J[sensor6035.begin dưới mutex I2C]
  J --> K[Fan.begin + PIDControl.timeoutSetting]
  K --> L[checkFirmware — probe OTA]
  L --> M[Tạo 6 RTOS task]
  M --> N[loop idle vTaskDelay 1000]
```

Dashboard **không** start trong `setup()` (WiFi thường chưa kịp connect) mà lazy trong
`dashboardLoop()` khi `networkUp()`. Xem [05-web-dashboard.md](05-web-dashboard.md).

## WiFi — 3 đường loại trừ nhau

| Đường | Khi nào | Việc |
|---|---|---|
| **STA** (creds EEPROM) | mặc định lúc boot | `WiFi.begin(ssid, password)`; upload Google Sheet được |
| **SoftAP fallback** | STA fail lúc boot | `dashboardStartAP()`: **release BT (~60KB) trước** rồi `softAP("RAPID-<id>")` (mở, 192.168.4.1). Không release BT thì AP hết heap → client không lấy được DHCP IP. |
| **WiFiManager portal** | menu Setting → WiFi (không phải boot) | release BT → `dashboardEnd()` → `resetSettings` → captive portal nhập creds → lưu EEPROM → thường `ESP.restart()` |

## Upload kết quả — `postData_GoogleSheet()`

Gọi từ `screen_Result('f')` (tự động cuối test, hoặc manual). Trình tự tối ưu heap:

```mermaid
sequenceDiagram
  participant SR as screen_Result (DisplayTask)
  participant PG as postData_GoogleSheet
  participant Dash as webDashboard
  participant TLS as WiFiClientSecure
  SR->>PG: gọi (WiFi đã connect)
  PG->>Dash: dashboardSuspend (đóng SSE + end server)
  PG->>PG: releaseBluetoothStack (~60KB, 1 chiều)
  PG->>PG: build JSON trong scope rồi hủy → trả ~25-40KB
  PG->>TLS: POST #1 Google Apps Script (302 = thành công)
  PG->>TLS: POST #2 ForteBio ingest (+ Bearer token)
  PG->>Dash: dashboardResume (ở CẢ 3 điểm return)
  PG-->>SR: return 200 (hardcode)
```

- **POST #1 (GAS):** `setFollowRedirects(DISABLE)` — GAS trả **302 sau khi** append xong nên
  **302 = thành công**; host redirect từ chối re-POST. `setInsecure()` (bỏ verify cert → cấp phát
  nhỏ hơn). Timeout 60s (GAS append ~35-40s).
- **POST #2 (ingest):** cùng client, thêm `Authorization: Bearer <token>`.
- **JSON payload:** `method=append`, `id_device`, `version`, `kitId`, `type_Upload`
  (Manual/Auto/N/A), mảng `slopes/origins/LED_power/CT_value/result`, `record_out` (đỉnh + outcome
  từng slot), `amplification` (đường cong raw).

> Caveat: hàm luôn `return 200` (dòng trả code thật bị comment) → caller không biết upload thật
> thành/bại.

## OTA — `updateOTA.cpp`

`OtaState` (1 byte volatile atomic): `IDLE → AVAILABLE → USER_ACCEPTED → UPDATING → (restart|FAILED)`.

```mermaid
stateDiagram-v2
  OTA_IDLE --> OTA_AVAILABLE: checkFirmware thấy version mới (boot)
  OTA_AVAILABLE --> OTA_USER_ACCEPTED: RED trên eUpdateOTA
  OTA_AVAILABLE --> OTA_DISMISSED: BLUE
  OTA_USER_ACCEPTED --> OTA_UPDATING: NetworkTask nhận
  OTA_UPDATING --> [*]: HTTP_UPDATE_OK → ESP.restart()
  OTA_UPDATING --> OTA_FAILED: lỗi (không tự restart)
```

`checkFirmware()` GET metadata JSON từ GitHub raw; `updateFirmware()` (NetworkTask poll 10ms) chỉ
hành động khi `OTA_USER_ACCEPTED`, dùng `httpUpdate.update()`. Thành công → `ESP.restart()` (đường
restart duy nhất của OTA).

## Ràng buộc heap (lý do của mọi thứ tự trên)

- **Release BT 1 chiều** (`releaseBluetoothStack`): `esp_bt_mem_release` trả ~60KB **vĩnh viễn**,
  chỉ gọi 1 lần (gọi lần 2 hỏng heap; `SerialBT.*` sau đó → assert → reboot). Cờ `gBtReleased`
  idempotent qua 3 luồng (auto upload, manual upload, WiFiManager).
- **TLS cần ~40KB liền mạch** — chỉ đủ nhờ **bộ ba**: release BT + hủy JsonDocument trước handshake
  + `dashboardSuspend()` (đóng SSE + AsyncWebServer). Thiếu 1 → `-32512` (SSL alloc) / `-10368`
  (X509). Chế độ AP còn chật hơn → `dashboardLoop` log heap mỗi 10s để đo.
- **Fragmentation** (khối liền mạch lớn nhất `getMaxAllocHeap`), không phải tổng free, là thứ gây
  lỗi TLS.
