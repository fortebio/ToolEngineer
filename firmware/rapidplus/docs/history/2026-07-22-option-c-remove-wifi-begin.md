# 2026-07-22 — Đóng dứt điểm treo dashboard: xóa `WiFi.begin()` khỏi `screen_Result` (Option C)

## Triệu chứng (tái diễn)

Máy đang xài, **bật bình thường, ping được, nhưng không vào được web** (dashboard câm). Serial
vẫn in `[dash] heap...` đều đặn (NetworkTask sống) nhưng HTTP không bao giờ trả lời. Chỉ **reset**
mới cứu. = **`async_tcp` treo** (GOTCHA 8/11).

## Gốc rễ

`screen_Result` (đường upload/review cuối run) còn 1 lần `WiFi.begin()` để reconnect STA khi rớt.
Cú đó re-vào `esp_wifi_set_mode()`/connect, giằng xé WiFi/lwIP stack → task `async_tcp` (phục vụ
web) kẹt tcpip core lock. Vì `CONFIG_ASYNC_TCP_USE_WDT=0` (GOTCHA 11) nó **không reboot** mà **treo
vĩnh viễn**. Band-aid trước (hạ server trước `WiFi.begin`) **chưa đủ** — còn khe hở residual → treo
lại. (Điều tra gốc: docs/history/2026-07-20-updata-wifi-reconnect-hang.md + workflow đầu session.)

## Fix (Option C — displayLCD.cpp `screen_Result`)

**Xóa hẳn `WiFi.begin()`** (và band-aid `dashboardSuspend`/`dashboardResume` quanh nó). STA reconnect
đã do **core lo** (`WiFi.setAutoReconnect(true)`, main.cpp) — core tự re-associate STA nền, không cần
app gọi `begin()`. Thay khối cũ bằng **chờ thụ động** có giới hạn (không `begin`, không suspend,
dashboard vẫn phục vụ suốt):

```cpp
for (int i = 0; i < 50 && key == 'f' && !dashboardIsAP() && WiFi.status() != WL_CONNECTED; i++)
  delay(100); // ~5s, nhường CPU, không đụng gì
```

- Không còn `WiFi.begin()` runtime → không thrash → **async_tcp không bao giờ treo vì đường này**.
- Nếu STA rớt: chờ core reconnect tối đa ~5s rồi upload nếu kịp, không thì báo "Upload Failed".

## Kiểm chứng

- `tools/test_no_runtime_wifi_begin.py` → **PASS** (guard này ĐỎ với band-aid; giờ XANH: `WiFi.begin`
  chỉ còn ở `main.cpp` boot, `setAutoReconnect(true)` còn đó).
- `pio run -e esp32dev` → SUCCESS. Nạp COM18, boot OK, dashboard phục vụ (`/home` 200, idle).

## Nạp

Đổi `src/` (+ `data/` cho fix web re-arm cùng lần) → `pio run -e esp32dev -t uploadall`.
