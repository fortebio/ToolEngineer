# 2026-07-23 — Giới hạn 2 người xem dashboard (SoftAP + STA)

## Yêu cầu

Chỉ cho **2 người** xem dashboard cùng lúc. Ban đầu chỉ đặt ở SoftAP, nhưng `max_connection`
**không có tác dụng ở STA** (máy nối WiFi thật thì cả LAN vào được) → phải chặn thêm ở tầng SSE.

## Trước hết: đây KHÔNG phải vấn đề heap

Đo thật trên board RPL (STA), mở dần SSE client và đọc `[dash] heap ...` từ serial:

| clients | free | **intLargest** | `/home` |
| --- | --- | --- | --- |
| 0 | 87 528 | **51 188** | 19 ms |
| 2 | 86 156 | **51 188** | 27 ms |
| 4 | 84 832 | **51 188** | 22 ms |
| 8 | 82 184 | **51 188** | 34 ms |
| 0 (sau khi đóng) | 87 528 | 51 188 | — |

→ **~660 B/client**, và **`intLargest` không nhúc nhích** → client **không phân mảnh** block
liền mạch, nên ngưỡng TLS 42KB (GOTCHA 2) không bị đe doạ dù đông người xem. Đóng hết thì heap
về đúng số cũ (**không rò rỉ**). Vậy giới hạn này thuần là **chính sách "ai được xem"**, không
phải để cứu bộ nhớ — muốn nâng lên 4/8 sau này đều an toàn.

## Hiện thực: ép ở hai tầng

`static constexpr size_t MAX_VIEWERS = 2;` (webDashboard.cpp) dùng cho cả hai:

1. **SoftAP** — `WiFi.softAP(ap.c_str(), NULL, 1, 0, MAX_VIEWERS)`; mặc định của core là 4.
   Thiết bị thứ 3 bị từ chối **lúc associate**, không hề lấy được IP.
2. **STA** — `ViewerCapHandler`, một `AsyncWebHandler` tự viết, `addHandler` **TRƯỚC**
   `dashEvents`:

   ```cpp
   bool canHandle(AsyncWebServerRequest *request) const override {
     return request->isSSE() && request->url() == "/events" &&
            dashEvents.count() >= MAX_VIEWERS;   // chỉ nhận KHI ĐÃ ĐỦ NGƯỜI
   }
   void handleRequest(AsyncWebServerRequest *request) override {
     request->send(503, "text/plain", "Too many viewers");
   }
   ```

   Chưa đủ người → `canHandle` false → `WebServer.cpp:151`
   (`if (h->filter(request) && h->canHandle(request))`) bỏ qua, request **rơi xuống SSE handler
   thật**. Đủ người → 503.

Người bị từ chối **vẫn xem được trang** (static + `/home` + `/curve`), chỉ mất live; và
`EventSource` của browser **tự retry**, nên có người rời là tự vào được — cũng chính nhờ retry
mà **reload trang không kẹt slot**.

## Hai đường CỤT đã thử (đừng lặp lại)

**1. `dashEvents.onConnect(...)` + `client->close()`** — idiom "chuẩn" của thư viện, nhưng ở
đây là **self-deadlock treo máy**. Đọc lib trước khi viết mới thấy:

```cpp
void AsyncEventSource::_addClient(AsyncEventSourceClient* client) {
  std::lock_guard<std::mutex> lock(_client_queue_lock);   // giữ lock...
  _clients.emplace_back(client);
  if (_connectcb) _connectcb(client);                     // ...rồi gọi callback TRONG lock
```

mà `client->close()` → `AsyncClient::_close()` → `_onDisconnect` → `_handleDisconnect()` →
`std::lock_guard lock(_client_queue_lock)` — **cùng mutex, không đệ quy** → hang. Đúng hình
dạng **GOTCHA 14**. `AsyncEventSource::count()` **cũng lấy lock đó**, nên `count()` chỉ được gọi
**ngoài** callback (trong `canHandle` thì an toàn — lúc đó chưa có client nào được tạo).

**2. `dashServer.on("/events", HTTP_GET, ...).setFilter(...)`** — biên dịch được, chạy thì
**không chặn gì** (đo: client #3 vẫn `200`). Lý do ở `WebHandlers.cpp:267`:

```cpp
if (!_onRequest || !request->isHTTP() || !(_method & request->method())) return false;
```

Request SSE là `RCT_EVENT`, **không phải** `isHTTP()` → `AsyncCallbackWebHandler` **không bao
giờ thấy** `/events`, filter chạy vô ích. Và `AsyncEventSource::canHandle` khai báo `final` nên
cũng **không subclass** được event source → chỉ còn đường tự viết `AsyncWebHandler`.

## Kiểm chứng (board .23, sau khi nạp)

| Tình huống | Kết quả |
| --- | --- |
| Chưa ai xem → người #1 | `200` |
| Đã 2 người → #3 mở `/events` | **`503 Too many viewers`** |
| Đã 2 người → #3 mở `/home`, `/curve` | `200` (trang vẫn dùng được, chỉ không live) |
| 2 người rời → người mới | `200` (tự vào lại, không kẹt slot) |

Chi phí: RAM +40 B, Flash +388 B.

## Bẫy chẩn đoán kèm theo

`curl http://ip/events` trả **"Not found"** (404 từ `onNotFound` của captive portal) — trông như
SSE hỏng, thực ra `AsyncEventSource::canHandle` đòi `request->isSSE()` = header
`Accept: text/event-stream`. Browser luôn gửi; curl phải thêm tay:
`curl -H "Accept: text/event-stream"` → `200` + stream.

## Còn hở

Giới hạn này chỉ chặn **xem live**, **không phải bảo mật**: người thứ 3 vẫn gọi được
`/control`, `/config`, `/rename`… Muốn chặn thật thì cần xác thực, không phải đếm client.
