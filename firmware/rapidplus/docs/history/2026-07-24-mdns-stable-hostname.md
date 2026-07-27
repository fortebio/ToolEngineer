# 2026-07-24 — Tên miền cố định cho dashboard (mDNS + setHostname)

## Vấn đề

DHCP cấp IP động → mỗi lần vào lại web phải dò IP mới (TFT có hiện IP, nhưng bất tiện).
Cần **một cái tên ổn định** trỏ vào máy bất kể IP đổi.

## Giải pháp (leo thang, chọn cái nhẹ nhất)

Không dựng DNS server, không xin IP tĩnh (đụng cấu hình router của khách). Dùng **hai cơ
chế có sẵn trong ESP32 core** — 0 lib_deps mới:

1. **mDNS** (`ESPmDNS`, `MDNS.begin(hostname)` + `MDNS.addService("http","tcp",80)`):
   quảng bá `http://<hostname>.local/` trên LAN. Windows 10+/macOS/iOS/Android phân giải
   `.local` sẵn, không cần cài gì.
2. **`WiFi.setHostname()`**: đăng ký hostname với DHCP → router nào phân giải hostname sẽ
   cho vào bằng `http://<hostname>/` luôn (không cần `.local`). Cùng một tên với mDNS.

## Hostname lấy từ đâu

`dashboardHostname()` (webDashboard.cpp) sinh **DNS-safe label** từ `id_device`:
lowercase, chỉ giữ `[a-z0-9-]`, cắt `-` ở đầu (label DNS không mở đầu bằng `-`), tối đa
24 ký tự, rỗng thì fallback `"rapid"`. VD `id_device = "RPL"` → `http://rpl.local/`,
`"RPL03010"` → `http://rpl03010.local/`.

## Ba điểm sửa

- **main.cpp** `setup()`: `WiFi.setHostname(dashboardHostname().c_str())` — **sau
  `WiFi.mode(WIFI_STA)`, trước `WiFi.begin()`** (thứ tự bắt buộc; gọi sau begin không ăn).
- **webDashboard.cpp** `dashboardBegin()`: `MDNS.begin(...)` + `addService` **sau
  `started = true`**, guard `static bool mdnsUp` (dashboardBegin chạy lại sau mỗi
  suspend/resume upload — chỉ announce **một lần**) và **chỉ khi STA** (`!apActive`): ở
  SoftAP đã có 192.168.4.1 + captive portal lo.
- **webDashboard.h**: khai báo `String dashboardHostname()` + `#include <Arduino.h>`
  (header trước đó không dùng `String` nên chưa include — thiếu nó build fail
  `'String' does not name a type`).

## Đo trên máy thật (board RPL, STA)

```
[wifi] connected to 'Engineer-FBT_2.4GHz' -> 192.168.0.103
[dash] mDNS up -> http://rpl.local/
[dash] dashboard on http://192.168.0.103/ (STA) | free=79868 maxAlloc=63476
```

Windows phân giải `http://rpl.local/home` → dashboard trả JSON `/home` bình thường.
Heap không đổi đáng kể (mDNS responder nhẹ).

## Lưu ý

- **`.local` chỉ trong cùng LAN** (mDNS là multicast link-local) — không vào được từ
  ngoài mạng. Đúng nhu cầu (dashboard vốn chỉ phục vụ LAN/AP).
- **SoftAP không announce mDNS** (đã có IP cố định + captive portal). QR/URL ở
  [2026-07-22-qr-dashboard-access.md](2026-07-22-qr-dashboard-access.md) vẫn nguyên.
- Đổi `device ID` (tab Setting) → **hostname đổi theo sau reboot** (id_device là nguồn).
