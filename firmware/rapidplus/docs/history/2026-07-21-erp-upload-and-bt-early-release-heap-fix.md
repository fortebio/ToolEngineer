# 2026-07-21 — Upload thêm ERP + FIX GỐC RỄ `-32512` bằng nhả BT sớm

## Phần 1 — Thêm đích upload thứ 3: ERP (`api.fortebio.tech`)

Trước có 2 đích: GAS (Google Sheet) + ingest (`fbt.basa-luma.ts.net`). Thêm **ERP**
(`https://api.fortebio.tech/api/v1/results/ingest`) — DB cloud chính thức.

**Khác biệt auth:** ingest dùng `Authorization: Bearer <token>`, **ERP dùng `X-API-Key: <token>`**
(curl mẫu: `-H "X-API-Key: rapidplus_..."`). `--ssl-no-revoke` của curl = `client.setInsecure()`
đã có sẵn trên ESP32.

`postJsonRetry()` (Bluetooth.cpp) thêm tham số `apiKey` → gắn header `X-API-Key` khi non-null.
3 call: GAS `(nullptr, nullptr)`, ingest `(server_engineerToken, nullptr)`, **ERP
`(nullptr, server_erpToken)`**. Cùng `jsonPost` (cả 3 server đều cần mảng `amplification`).

ERP trả `{"status":"ok","result_id":"...","device_matched":true}` → xác nhận chạy đúng.

## Phần 2 — Điều tra & FIX `-32512 SSL memory allocation failed`

### Triệu chứng
Upload (auto + "Up Data") **chập chờn `-32512`** (mbedTLS xin không đủ block liền mạch —
GOTCHA 2). Có board OK, có board fail. Ban đầu tưởng regression code / SSE fragment heap.

### Đo bằng `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` (thêm vào `[dash]` log)
Con số quyết định `-32512` **KHÔNG** phải `ESP.getMaxAllocHeap()` chung mà là **block liền
mạch trong INTERNAL RAM** (mbedTLS dùng). Đo được:
- mbedTLS thực cần **~42KB liền mạch** (fail ở `intLargest=40948`, chỉ lọt ở `42996`).
- 2 board **giống hệt firmware** nhưng lệch: 02013 = **47KB**, 03010 = **40-43KB** → 03010 sát
  ngưỡng → chập chờn. (Ban đầu tưởng "khác board".)

### GỐC RỄ: BT release nhả MUỘN
`releaseBluetoothStack()` (`esp_bt_mem_release(ESP_BT_MODE_BTDM)`, ~60KB) trước đây chạy
**trong `dashboardBegin()`** — tức **SAU** `WiFi.begin()` của `setup()`. Vùng BT được trả về
heap **muộn**, sau khi WiFi/lwIP đã cắm allocation → block BT nằm chỗ xấu → `intLargest` chỉ
40-47KB.

**BT là code chết trong build này**: `setup()` không gọi `SerialBT.begin()`; đường init duy
nhất (`connectBLE()` trong `eSettingBluetooth`) đã hỏng (gặp `if(gBtReleased) return`); web
Setting tab thay cấu hình BT; log đã sang USB. Nên nhả nó càng sớm càng tốt.

### Fix: nhả BT **NGAY ĐẦU `setup()`, TRƯỚC `WiFi.begin()`** (main.cpp)
```cpp
loadSettingDevice();
error.readErrorFromEEPROM();
releaseBluetoothStack();   // <-- nhả ~60KB TRƯỚC khi WiFi/lwIP cấp phát
WiFi.mode(WIFI_STA);
WiFi.begin(...);
```
Allocator giờ xếp WiFi/AsyncWebServer/TLS quanh **vùng đầy** → block liền mạch lớn hơn hẳn.
`releaseBluetoothStack()` trong `dashboardBegin` trở thành no-op (guard `gBtReleased`).

### Kết quả (đo trực tiếp, cùng firmware)
| Board | `intLargest` idle TRƯỚC | SAU (nhả sớm) | Upload SAU |
| --- | --- | --- | --- |
| 02013 | 47.092 B | **69.620 B** | GAS/ingest/ERP OK try 1 |
| 03010 | 40.948-42.996 B | **69.620 B** | **GAS/ingest/ERP OK try 1**, hết `-32512` |

**+22-26KB.** Cả 2 board giờ **68KB liền mạch** — thừa xa ngưỡng ~42KB (upload còn ~55-60KB).
"Khác biệt board" thật ra chỉ là vùng BT-nhả-muộn nằm khác chỗ; nhả sớm → cả 2 giống hệt.
**Không cần** giảm payload, không cần heap-gate, không cần reorder POST.

## Ghi chú các fix phụ trong buổi (đã có/đã revert)
- `dashboardSuspend()`: **giữ skip-close** (bỏ `dashEvents.close()` — nó self-deadlock đệ quy,
  xem GOTCHA 14). Rủi ro `-32512` do giữ SSE socket **nay hết** vì heap dư 68KB.
- Còn để lại (thừa nhưng vô hại, sẽ dọn sau): heap-gate `TLS_MIN` + `intLargest` log trong
  `postJsonRetry`; retry 4 lần; code BT chết (`connectBLE`/`eSettingBluetooth`).

## Kiểm chứng
- `pio run -e esp32dev` → SUCCESS. Nạp cả 2 board (`--upload-port COMxx`, 2 board CH340 hay
  flap/wedge → pin cổng, cần replug khi "port doesn't exist / access denied").
- Serial: `[up] GAS POST OK 302 → ingest OK 200 → ERP OK 200` (đều try 1), `intLargest` ~58KB
  lúc handshake.

## Nạp
Đổi `src/` (main.cpp + Bluetooth.cpp) → `pio run -e esp32dev -t upload --upload-port COMxx`.
