# 2026-07-20 — "Up Data" khi WiFi rớt: dashboard treo câm (không reboot)

## Triệu chứng

Bấm **"Up Data"** trên máy (giữ nút đỏ ≥3s → menu Settings → nhấn đỏ) khi WiFi STA vừa
rớt → LCD hiện **"Upload Failed"**, và **dashboard web chết hẳn**: máy vẫn ping được, port
80 vẫn nhận TCP, nhưng **mọi request (`/`, `/home`, `/events`) không bao giờ có hồi âm**.
Chỉ **tắt/bật nguồn** mới cứu (nhấn nút trắng chỉ soft-restart UI, không cứu được).

## Gốc rễ — GOTCHA 8 + GOTCHA 11 tương tác thành chế độ hỏng mới

"Up Data" → `eUpLoadData` → `screen_Result('f')` (displayLCD.cpp). Trong đó, **trước** khi
gọi `postData_GoogleSheet`, có 1 khối reconnect STA:

```cpp
if (!dashboardIsAP() && ssid.length() > 0 && WiFi.status() != WL_CONNECTED) {
  WiFi.begin(ssid.c_str(), password.c_str());   // <-- thrash WiFi/lwIP stack
  for (...) delay(100);
}
```

`WiFi.begin()` giằng xé WiFi+lwIP stack → task **`async_tcp`** (phục vụ dashboard) kẹt chờ
tcpip core lock (đúng GOTCHA 8). **Trước đây** watchdog sẽ abort→reboot; nhưng sau khi ta gỡ
watchdog khỏi `async_tcp` (`-DCONFIG_ASYNC_TCP_USE_WDT=0`, GOTCHA 11) để nó sống sót qua TLS
upload, cú thrash này **không còn reboot** — thay vào đó `async_tcp` **treo câm vĩnh viễn**.

`postData_GoogleSheet` **có** `dashboardSuspend()` (hạ server để async_tcp không có gì để bị
bỏ đói), **nhưng** nó chạy **SAU** khối reconnect. Lúc thrash xảy ra thì server vẫn đang lên
→ hỏng đã rồi. Và khi WiFi rớt luôn (không lên lại), luồng đi nhánh `else` (`bResultGet`,
tính kết quả cục bộ) — **không** đi qua `postData` → không có suspend/resume nào chạy.

## Fix (displayLCD.cpp, `screen_Result`)

Hạ dashboard **trước** khi thrash, và cân bằng resume trên **mọi** đường ra:

- `dashboardSuspend()` **ngay đầu** khối `WiFi.begin()` retry (đúng điều kiện thrash sắp xảy
  ra: `!AP && ssid && !CONNECTED`). Server xuống trước → `async_tcp` không còn gì để bị đói.
- Nhánh **`else`** (WiFi vẫn rớt → `bResultGet`, không upload): thêm `dashboardResume()` —
  vì nhánh này không đi qua `postData` nên nếu không resume, dashboard kẹt `suspended`.
- Nhánh upload (`postData_GoogleSheet`) tự `dashboardSuspend()` (idempotent) + `dashboardResume()`
  ở cuối → không cần đụng.

Cân bằng suspend/resume trên mọi đường:

| WiFi | key | Đường đi | Ai suspend | Ai resume |
| --- | --- | --- | --- | --- |
| lên | `'f'` | retry skip → `postData` | postData | postData |
| rớt→lên | `'f'` | retry (suspend) → `postData` | retry | postData |
| rớt | `'f'` | retry (suspend) → `else` | retry | else |
| rớt | khác | retry (suspend) → `else` | retry | else |
| lên | khác | retry skip → `else` | — | else (thừa, vô hại) |

Không đường nào kẹt `suspended`.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.9%).
- Nạp `pio run -e esp32dev -t upload --upload-port COM11` → SUCCESS; board boot sạch, 10
  sensor init OK, dashboard lên lại **HTTP 200** tại `http://192.168.1.10/home` (phase idle,
  heap free≈89KB).
- Tái hiện đầy đủ cần **nút vật lý** (giữ đỏ→đỏ) + làm STA rớt đúng lúc → thao tác trên máy:
  bấm "Up Data" trong khi WiFi rớt, dashboard phải **tự hồi** sau ~10–15s thay vì treo.

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx` (pin cổng: thường có 2
board CH340 cắm cùng lúc). `data/` không đổi nên không cần `uploadfs`.
