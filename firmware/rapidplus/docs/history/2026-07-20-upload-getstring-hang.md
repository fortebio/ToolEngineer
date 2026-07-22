# 2026-07-20 — Upload treo vĩnh viễn: `http.getString()` không có timeout

## Triệu chứng

Cuối **mỗi** upload (cả auto ở `escreenFinished` lẫn manual **"Up Data"**), máy **đơ hẳn**:
TFT đứng ở "Uploading results", web dashboard câm, **chỉ tắt/bật nguồn mới cứu** (nút trắng
soft-restart không cứu). WiFi STA **đang nối** (192.168.1.10). Reproduce bằng "Up Data"
(giữ đỏ ≥3s → menu → đỏ) nhanh hơn chờ hết run 40 phút — cùng đường `postData_GoogleSheet`.

## Chẩn đoán (5 agent soi song song từng nghi phạm)

Loại trừ: thuật toán `bResultPutToGoogleSheet` (mọi loop `for` có cận, retry cap 12 vòng),
tiền-xử-lý `screen_Result` (EEPROM read không loop, CSV dump ≤130×10), `releaseBluetoothStack`
(idempotent, đã release từ lúc boot). Deadlock `dashboardSuspend`→async_tcp chỉ là **race
hẹp** (core-locking OFF nên đường thường trả về).

**Gốc rễ — `http.getString()` tại `Bluetooth.cpp:636`** (đọc body của ingest, **vô điều
kiện**, chỉ để log):

```
getString() -> writeToStreamDataBlock():
  while (connected() && (len > 0 || len == -1)) {
    if (available()) { ... }
    else { delay(1); }        // <-- KHÔNG có timeout ở nhánh này
  }
```

- Response ingest: `Connection: close` + `http.useHTTP10(true)` → **không có Content-Length**
  → `_size = -1`.
- Endpoint ingest (`fbt.basa-luma.ts.net/ingest`, Tailscale-funnel reverse-proxy) **giữ
  socket TLS mở, không gửi FIN** sau khi trả body.
- ⇒ `connected() = available()>0 || _client->connected()` **mãi true**, `available()` **mãi
  0** → DisplayTask kẹt `delay(1)` **vô tận**. Ba timeout đã set (`setTimeout(60)`,
  `setTimeout(60000)` = header-phase `_tcpTimeout`, `setHandshakeTimeout(30)`) **không phủ**
  vòng đọc body này (connect/handshake/header đều có cận và hồi được, chỉ body-read là không).

Deterministic (gọi vô điều kiện) → khớp "cả manual lẫn auto đều treo".

## Fix (`src/Bluetooth.cpp`)

Thêm helper đọc body **có deadline tự quản** (HTTPClient không expose timeout cho body):

```cpp
static String readBodyDeadlined(HTTPClient &http, uint32_t idleMs)
{
  String body;
  WiFiClient *s = http.getStreamPtr();
  if (!s) return body;
  uint32_t last = millis();
  while (http.connected() && millis() - last < idleMs) {
    while (s->available()) { body += (char)s->read(); last = millis(); }
    delay(1);
  }
  return body;
}
```

Thay `http.getString()` bằng `readBodyDeadlined(http, 5000)` ở **cả 2 chỗ**: ingest (:636)
và nhánh lỗi GAS (:600, cùng bug tiềm ẩn). Body chỉ dùng để log nên cắt ở 5s idle là an toàn.

## Kiểm chứng (COM11 STA, capture serial)

- `pio run -e esp32dev` → SUCCESS (RAM 22.9%, Flash 68.9%). Nạp `-t upload --upload-port COM11`.
- Bấm "Up Data" → serial đi **trọn chuỗi** (trước fix đứng ngay sau `[up] ingest POST begin`):
  ```
  [up] GAS POST begin ... -> POST OK 6702ms code=302
  [up] ingest POST begin ... -> server feedback: {"ok":true,"file":"...","db":true,"id":20975}
  POST OK in 3414 ms, code=200
  [dash] dashboard on http://192.168.1.10/   <- resume, máy chạy tiếp
  ```
- `server feedback:` in ra (dòng trước đây KHÔNG BAO GIỜ tới) → xác nhận đúng chỗ kẹt, đã hết.

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx` (pin cổng: thường 2
board CH340 cắm cùng). `data/` không đổi.
