# 2026-07-20 — Upload lần 2 treo: deadlock `dashEvents.close()` khi web đang mở

> **Đính chính (2026-07-21):** cơ chế deadlock ghi dưới đây (ABBA queue-đầy) là **giả thuyết
> ban đầu chưa đúng**. Điều tra sau xác nhận là **self-deadlock ĐỆ QUY**: `AsyncClient::_close()`
> gọi `_discard_cb` đồng bộ → `_handleDisconnect` khóa LẠI đúng `_client_queue_lock` trên cùng
> task (`std::mutex` không đệ quy) → `delay()`/drain VÔ DỤNG. Kết luận cuối vẫn giống: **giữ
> skip-close** (bỏ `dashEvents.close()`). Ngoài ra, rủi ro `-32512` do giữ SSE socket **đã hết**
> nhờ nhả BT sớm (block liền mạch 68KB) — xem
> [2026-07-21-erp-upload-and-bt-early-release-heap-fix.md](2026-07-21-erp-upload-and-bt-early-release-heap-fix.md)
> và CLAUDE.md GOTCHA 14.

## Triệu chứng (sau khi đã fix `getString()`)

Fix `readBodyDeadlined` (xem [2026-07-20-upload-getstring-hang.md](2026-07-20-upload-getstring-hang.md))
làm upload **lần 1** chạy trọn. Nhưng bấm **"Up Data" lần 2** lại **treo vĩnh viễn** (tắt/bật
nguồn mới cứu). Khác lần trước: lần này serial in `[up] begin` rồi **câm** — **chưa tới**
`[up] GAS POST begin`. Các task khác vẫn sống ("finish one round maintenance" đều đặn) → chỉ
**DisplayTask** kẹt trong `postData_GoogleSheet`, ở đoạn **giữa `[up] begin` và `[up] GAS POST begin`**.

Điểm phân biệt then chốt: **lần 1 chưa mở web** (0 SSE client), **lần 2 đã mở web** (≥1 SSE client).

## Chẩn đoán

Bước duy nhất đáng kể giữa 2 marker là `dashboardSuspend()` → `dashEvents.close()`. Đọc thẳng
lib đã pin (`ESP32Async/AsyncTCP@3.3.2`, `ESPAsyncWebServer@3.6.0`):

- `AsyncEventSource::close()` (AsyncEventSource.cpp:371) **giữ `_client_queue_lock`** (std::mutex,
  không timeout) suốt vòng lặp `c->close()`.
- `c->close()` → `AsyncClient::close()` → `_tcp_clear_events()` → `_prepend_async_event(&e)`
  với **`portMAX_DELAY`** (AsyncTCP.cpp:152, 368) → **chặn** nếu `_async_queue` đầy.
- Chính việc đóng client làm async_tcp chạy disconnect callback → `_handleDisconnect()`
  (AsyncEventSource.cpp:357) **cần đúng `_client_queue_lock` đó**.

⇒ **ABBA self-induced**: DisplayTask giữ lock, chờ chỗ trống trong queue; async_tcp ngừng rút
queue vì chờ lock → deadlock. `CONFIG_ASYNC_TCP_USE_WDT=0` (GOTCHA 11) nên **không reboot** ra
được → treo vĩnh viễn. `close()` chỉ đụng client khi có SSE client → khớp "lần 1 (0 client) qua,
lần 2 (web mở) treo".

(5 agent soi song song đã xếp đây là runner-up ngay từ đầu; marker `[up] begin` là dòng cuối
đúng như dự đoán.)

## Fix (`src/webDashboard.cpp`, `dashboardSuspend`)

**Bỏ `dashEvents.close()`**, chỉ giữ `dashServer.end()`:

- `AsyncServer::end()` (AsyncTCP.cpp:1601) **chỉ `tcp_close` listen pcb**, không đụng client
  đã accept → không lấy `_client_queue_lock`, không enqueue chặn → **hết deadlock**.
- SSE socket idle vẫn mở nhưng cờ `suspended` đã chặn mọi `dashEvents.send` nên nó không tốn
  gì thêm. BT nhả từ boot → ~78KB heap rảnh lúc upload, thừa cho TLS 40KB dù còn 1 SSE socket
  (maxAlloc tụt 47→41KB, vẫn trên ngưỡng).
- Thêm marker `[up] suspended` / `[up] result computed` để định vị nếu còn kẹt.

Vì sao không đóng SSE cho an toàn hơn? Mọi task **khác async_tcp** gọi `dashEvents.close()` đều
dính ABBA này (lỗi concurrency của lib). Đóng listen socket là đủ cho heap; nhả BT lúc boot đã
lo phần lớn.

## Kiểm chứng (COM11 STA, web mở suốt, bấm "Up Data" 3 lần liên tiếp)

Cả 3 lần **đều qua `[up] suspended` → `[up] result computed`** rồi **resume dashboard** — không
lần nào treo (trước fix treo ngay lần 2 tại `[up] begin`):

```
[up] begin -> [up] suspended -> [up] result computed -> [up] GAS POST begin
   -> POST OK 302 -> [up] ingest POST begin -> server feedback: {"ok":true,...} -> POST OK 200
   -> [dash] dashboard on http://192.168.1.10/   (resume)
```

Ghi chú: bấm dồn dập thỉnh thoảng gặp lỗi mạng transient (`-3 send payload failed`, `-1
connection refused`) — nhưng là **thất bại có kiểm soát** (in lỗi → `dashboardResume` → chạy
tiếp), **không phải treo**. Lần giữa upload thành công trọn (`id:20976`).

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
