# 2026-07-27 — "Up Data không được": màn eUpLoadData là ngõ cụt (không phải lỗi mạng)

## Kết luận ngược với chẩn đoán ban đầu

Nghi ban đầu là mạng/RAM. Đo trên máy thật thì **mạng của thiết bị hoàn toàn tốt**:

```
[ota] web-requested check
[dash] heap free=30204 maxAlloc=19444 ...   <- TLS đang chạy
[ota] check failed: HTTP 404
```

Thiết bị **phân giải DNS → bắt tay TLS → gửi HTTPS → nhận về HTTP 404**. Cả ngăn xếp mạng chạy
đúng. `checkFailed:true` của `/ota` chỉ có nghĩa "check hỏng" — ở đây hỏng vì **nội dung**
(`updateOTA.json` 404), không phải vì mạng. Đừng dùng cờ đó làm bằng chứng mất mạng nữa.

Cấu hình L3 cũng chuẩn (thêm tạm vào `/home`): `ip 192.168.1.23, gw 192.168.1.1,
mask 255.255.255.0, dns1 192.168.1.1, rssi -61` — giống hệt PC cùng LAN.

## Gốc rễ: `eUpLoadData` không bao giờ thoát

```cpp
case eUpLoadData:
{
  displayWaitingUpData();
  this->screen_Result('f');      // đây mới là chỗ thật sự upload
  // this->type_infor = escreenStart;   <-- BỊ COMMENT
  // this->changeScreen = true;         <-- BỊ COMMENT
  break;
}
```

`screen_Result()` đặt `changeScreen = false` (displayLCD.cpp:1351), mà `displayCLD::loop()` chỉ
vào switch khi `changeScreen` **true**. Nên sau một lần bấm Up Data, `type_infor` **kẹt ở
`eUpLoadData` vĩnh viễn**. Hệ quả dây chuyền:

1. Máy ngồi lì trên màn upload — nhìn như "bấm mà không ăn gì".
2. `isBusy()` là **allowlist** và không có `eUpLoadData` → `dashboardDeviceBusy()` **true mãi mãi**
   → mọi ghi cấu hình và OTA check bị **409**. Đo được đúng như vậy:
   `POST /ota?action=check` → `{"ok":false,"error":"device busy"}`, dù máy chẳng chạy gì.
3. `fillStatus`/`fillActions` cũng không có case này → rơi `default` → `/home` báo
   `phase "idle"`, `title "Idle"`, `busy true`, `white "Return"`. Đây chính là dấu vân tay quan sát
   được lúc chẩn đoán.
4. **Nút TRẮNG không có nhánh cho `eUpLoadData`** trong `handleShortPress_White` → rơi xuống
   `_displayCLD.type_infor = ebuttonrestart` cuối hàm, tức **REBOOT** — trong khi cả TFT lẫn chip
   web đều ghi "Return".

## Đã sửa

- `displayLCD.cpp` `case eUpLoadData`: **cố ý không** tự quay về (phải để người vận hành đọc được
  kết quả), ghi rõ lối ra là nút TRẮNG.
- `button.cpp` `handleShortPress_White`: thêm nhánh `eUpLoadData → escreenStart` (cạnh nhánh
  `eShowQR`). Giờ "Return" đúng nghĩa Return, và đó cũng là lối duy nhất đưa máy về trạng thái
  **không bận**.

Không đụng `isBusy()`: lúc đang upload thật thì **phải** bận, thêm vào allowlist sẽ mở cửa cho web
ghi cấu hình giữa lúc upload.

Đã nạp và xác nhận trên máy: `busy:false`, `actions {green:"Lysis", red:"Amplification",
white:"QR / Web"}`.

## Vấn đề CÒN LẠI (chưa sửa)

**TLS cần 44 032 B liền mạch — đo trực tiếp**: `intLargest` 63 476 → **19 444** trong lúc bắt tay,
rồi hồi đủ về 63 476. Boot sạch có 63 476 nên thừa. Nhưng lần upload hỏng ở cuối run chỉ có
**42 996** → **thiếu 1 036 byte**. Đó là lời giải cho `(-16) BIGNUM - Memory allocation failed`.

Ngưỡng gác trong `postJsonRetry` đang là `TLS_MIN = 33 * 1024` — **thấp hơn nhu cầu thật ~11KB**,
nên nó thả qua đúng dải 33–44KB mà máy hay rơi vào, rồi handshake chết. Nâng lên ~45KB (và cho
cổng **thật sự từ chối** thay vì chỉ chờ 2,5 giây) là hướng sửa.

Còn phải tìm: vì sao `intLargest` tụt 63 476 → 42 996 trong một run (đo được 63 476 → 57 332 chỉ
sau vài phút). mDNS và client SSE **đã được đo là vô can**. Nghi phạm còn lại là chu kỳ
`dashboardSuspend()/dashboardBegin()` — `dashServer.begin()` tốn 14 336 B liền mạch lúc boot, và
`63 476 − 6 144 − 14 336 = 42 996` khớp chính xác con số quan sát.

**OTA check trả HTTP 404** — `updateOTA.json` không có ở URL đang gọi. Lỗi thật, riêng biệt, không
phải mạng.

## Ghi chú

Còn để lại trên máy: `dashHeapProbe()` (webDashboard.cpp/.h + 3 lời gọi ở main.cpp) và các trường
`gw/mask/dns1/dns2/rssi` trong `/home` `net`. Cả hai đánh dấu `TEMPORARY (2026-07-27)`, xoá sau khi
chốt xong chuyện phân mảnh heap.
