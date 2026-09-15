# FBT-RapidPlus Production

Firmware production máy phân tích **PCR/qPCR RapidPlus** (Forte Biotech).
ESP32-WROOM-32 · Arduino · PlatformIO.

> ⚠️ **Thiết bị chẩn đoán.** Điều khiển gia nhiệt tiếp xúc mẫu sinh học.
> Đọc [AGENTS.md](AGENTS.md) §0 và [docs/SAFETY.md](docs/SAFETY.md) **trước khi
> sửa bất cứ thứ gì**. Bối cảnh chuẩn: IEC 61010, IEC 62304.

> Trạng thái: **scaffold** — khung + lớp an toàn + test. Chưa nạp máy thật.

---

## Chạy nhanh

```bash
pio test -e native               # test logic an toàn (host, ~vài giây, không cần máy)
pio run  -e esp32dev             # build firmware
pio run  -e esp32dev -t upload   # nạp
pio device monitor               # log
```

## Cấu trúc

```
include/
  safety_limits.h    NGƯỠNG AN TOÀN TUYỆT ĐỐI — compile-time, không sửa lúc chạy
  version.h          FBT_FW_VERSION, FBT_PCB_VERSION
src/
  safety/            C++ THUẦN — nhiệt độ → hành động (test trên host)
  config/            C++ THUẦN — config   → clamp/từ chối
  ota/               C++ THUẦN — manifest → nhận/từ chối
  main.cpp           Arduino — CHỈ nối dây, không quyết định
test/
  test_safety/       10 test hard-limit, NaN, timeout, tràn millis
  test_config/       16 test validate/clamp + verify OTA
partitions/          8 MB, 2 app slot để OTA rollback
```

## Quyết định kiến trúc cốt lõi

**Logic quyết định an toàn là C++ thuần, không phụ thuộc Arduino** → chạy được
bằng `pio test -e native` trên host, **trong CI, ở mọi commit**.

Lý do: logic an toàn mà chỉ kiểm chứng được bằng cách cắm máy thật thì thực tế là
không ai kiểm chứng — nó bị bỏ qua đúng vào lúc gấp, tức lúc dễ sai nhất.

Chi tiết: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Phân lớp phòng vệ

```
Lớp 4  Cloud gửi config       ← không tin
Lớp 3  validateAndClamp()     ← clamp/từ chối trước khi áp
Lớp 2  PID bám target         ← có thể sai, chấp nhận
Lớp 1  safety::evaluate()     ← HARD-LIMIT, độc lập, luôn thắng
Lớp 0  Bảo vệ phần cứng       ← cầu chì nhiệt (TODO(hw))
```

| Ngưỡng | Giá trị |
|---|---|
| Bottom heater | > 99 °C → cắt toàn bộ gia nhiệt |
| Hot-lid | > 85 °C → cắt toàn bộ gia nhiệt |
| Sensor ngoài −40…200 °C, NaN, hoặc timeout > 2 s | → cắt |

## Tài liệu

| File | Khi nào đọc |
|---|---|
| [AGENTS.md](AGENTS.md) | **Trước mọi thứ** — quy tắc làm việc |
| [docs/SAFETY.md](docs/SAFETY.md) | Trước khi chạm gia nhiệt |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Trước khi thêm module |
| [docs/CONVENTIONS.md](docs/CONVENTIONS.md) | Khi viết code |
| [docs/TESTING.md](docs/TESTING.md) | Khi thêm test |
| [docs/OTA.md](docs/OTA.md) | Khi chạm OTA |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Pin map, sensor, ngân sách RAM |
| [docs/RELEASE.md](docs/RELEASE.md) | Trước khi phát hành |
| [docs/PROGRESS.md](docs/PROGRESS.md) | Việc đang dở + bài học |
| [SCOPE.md](SCOPE.md) | Khi phân vân "có nên làm X" |

## Quan hệ với repo anh em

| Repo | Vai trò |
|---|---|
| `FBT/firmware/instrument-wroom32` | Bản v2.4.2 đang bán — nguồn tham chiếu driver/thuật toán |
| `FBT/backend/cloud` | Cloud FastAPI: OTA, activation, WebSocket |
| `FBT/docs/FBT-DXD_PRD.md` | **Nguồn sự thật sản phẩm** |

## Ràng buộc quan trọng

- **Không PSRAM.** WROOM-32 — mọi cấp phát mới phải giải trình, đo `heap_free`.
- **Không block vòng PID 100 ms / vòng đo 20 s.**
- **Đổi partition table** = máy đã bán không OTA sang được, phải nạp USB tay.
- **Không commit** credential, cert, build artifact.
