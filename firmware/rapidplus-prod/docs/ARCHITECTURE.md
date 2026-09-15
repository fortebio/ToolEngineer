# Kiến trúc — FBT-RapidPlus Production

> Đọc cùng [SAFETY.md](SAFETY.md) và [CONVENTIONS.md](CONVENTIONS.md).

## Quyết định cốt lõi: logic quyết định phải test được trên host

```
src/
  safety/    safety_monitor   C++ THUẦN — nhiệt độ  → hành động
  config/    config_validate  C++ THUẦN — config    → clamp/từ chối
  ota/       ota_verify       C++ THUẦN — manifest  → nhận/từ chối
  main.cpp   Arduino — CHỈ nối dây: đọc HW → gọi module thuần → tác động
```

**Không module thuần nào được `#include <Arduino.h>`.**

Lý do: logic an toàn mà chỉ kiểm chứng được bằng cách cắm máy thật thì **thực tế
là không ai kiểm chứng**. Cắm máy tốn thời gian, cần mẫu, cần người — nên nó bị
bỏ qua đúng vào lúc gấp, tức lúc dễ sai nhất.

Tách ra thì `pio test -e native` chạy **trong CI trên mọi commit**, không cần
phần cứng, xong trong vài giây.

Đây cũng là lý do `platformio.ini` env `native` có `build_src_filter = +<*> -<main.cpp>`:
main.cpp là thứ duy nhất không biên dịch được trên host, và nó **không được phép**
chứa quyết định nào.

### Luật kiểm tra nhanh

> Nếu bạn viết một `if` mà kết quả sai của nó có thể làm nóng máy, hỏng mẫu, hay
> ghi nhầm flash — nó **không được** nằm trong `main.cpp`.

## Phân lớp phòng vệ

Xem [SAFETY.md](SAFETY.md). Tóm tắt: hard-limit (lớp 1) độc lập hoàn toàn với
PID (lớp 2) và với remote config (lớp 3).

## Timing — hợp đồng không được phá

Nguồn: PRD NFR-07, D2.

| Vòng | Chu kỳ | Ràng buộc |
|---|---|---|
| PID | 100 ms | Không gì được block. `safetyGate()` chạy TRƯỚC mỗi bước PID. |
| Đo | 20 s | Không gì được block. |
| CloudTask | tuỳ | **Core riêng.** Đọc telemetry qua snapshot có khoá (portMUX), không đọc trực tiếp state vòng đo. |

Thêm bất cứ thứ gì vào core đo phải chứng minh không block — bằng số đo, không
bằng lập luận.

## RAM — ràng buộc cứng

**ESP32-WROOM-32 không có PSRAM.** Đây là rủi ro số 1 của dự án (PRD NFR-01).

- Trước/sau thay đổi lớn: ghi lại `ESP.getFreeHeap()` và stack high-water mark.
- Thêm thư viện là thêm RAM. Thêm **từng cái một**, đo sau mỗi lần.
- TLS + MQTT + AsyncWebServer cùng lúc là điểm chật nhất đã biết. Nếu không đủ:
  cân nhắc bỏ AsyncWebServer khi cloud active, hoặc đẩy mTLS ra edge/VPN.
- Ghi số đo vào [PROGRESS.md](PROGRESS.md) — nếu không ghi thì lần sau lại đo lại.

## Flash

Xem [`partitions/default_8MB.csv`](../partitions/default_8MB.csv). Hai app slot
3 MiB để rollback được.

`ota::kMaxImageBytes` trong [`src/ota/ota_verify.h`](../src/ota/ota_verify.h)
**phải khớp** kích thước slot. Lệch = ảnh quá lớn ghi tràn sang partition kế bên.

## Tương thích ngược

PRD NFR-08: hỗ trợ đồng thời PCB V1.2 và V1.3; `heater_duty` khác độ dài theo PCB.

- `FBT_PCB_VERSION` trong [`include/version.h`](../include/version.h) quyết định
  bản build này dành cho PCB nào.
- OTA từ chối nếu không khớp (`RejectPcbMismatch`) — nạp nhầm = sai pin map = hỏng
  phần cứng.

## Quan hệ với các repo anh em

| Repo | Vai trò |
|---|---|
| `FBT/firmware/instrument-wroom32` | Bản v2.4.2 đang bán — **nguồn tham chiếu** cho driver và thuật toán |
| `FBT/backend/cloud` | Cloud FastAPI: OTA, activation, WebSocket |
| `FBT/docs/FBT-DXD_PRD.md` | **Nguồn sự thật sản phẩm** |
| `FBTRapidplusOTA`, `plus239`, `FBT-DXD` | Bản thử nghiệm/cũ |

Port code từ `instrument-wroom32` sang đây thì **tách logic quyết định ra module
thuần trước**, đừng bê nguyên khối.
