# 2026-07-22 — LCD luôn báo "Upload Success" dù upload thất bại

## Lỗi

`postData_GoogleSheet()` **luôn `return 200`** bất kể kết quả thật. `screen_Result` tính
`ok = httpCode >= 200 && httpCode <= 302` rồi in `"Upload Success"` (xanh) / `"Upload Failed"`
(đỏ). Vì luôn 200 → **luôn "Upload Success"**, kể cả khi:
- WiFi rớt giữa chừng → cả 3 đích (GAS/ingest/ERP) fail (-1/-3),
- token sai (401), server 5xx…

Kỹ thuật viên tin kết quả đã lên cloud trong khi **không có gì được ghi** — nguy hiểm với thiết bị y tế.

Bug phụ: `uint16_t tmpHttpCode = postJsonRetry(...)` ép mã lỗi âm mất dấu (`-11` → `65525`),
nên kể cả muốn xét mã cũng sai.

## Fix (Bluetooth.cpp `postData_GoogleSheet`)

Verdict LCD **theo GAS** — đích chính lưu kết quả (đây là `postData_GoogleSheet`, ghi Google Sheet);
ingest/ERP là bản sao best-effort, không quyết định verdict.

```cpp
int gasCode = postJsonRetry(serverName, ...);   // int, giữ dấu (-1/-3/-11)
...
// 2xx/302 = đã ghi. -11 (read timeout) cũng tính: GAS chỉ trả 302 SAU khi doPost() append
// xong, nên reply timeout vẫn nghĩa dòng rất có thể đã landed (postJsonRetry cố ý không retry
// -11). Mã khác = chưa ghi -> return 0 -> caller báo "Upload Failed".
bool gasOk = (gasCode >= 200 && gasCode <= 302) || gasCode == -11;
return gasOk ? 200 : 0;
```

Caller không đổi: `200`→"Upload Success", `0`→"Upload Failed".

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).
- Verify runtime cần **upload thất bại thật** (rút WiFi/AP giữa lúc "Up Data"): LCD phải hiện
  **"Upload Failed"** (đỏ) + serial `[up] GAS not confirmed (code=…) -> Upload Failed`; khi GAS
  2xx/302/-11 vẫn hiện "Upload Success". (Chưa tự động hoá được vì cần điều khiển mạng đúng lúc.)

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
