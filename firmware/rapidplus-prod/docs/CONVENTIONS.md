# Quy ước code — FBT-RapidPlus

> Bắt buộc. Xung đột với file khác → file này thắng, trừ `AGENTS.md` và `SAFETY.md`.

## Chung

- Comment giải thích **tại sao**, không giải thích **cái gì**.
- Khi tắt/hạ một tính năng, comment phải ghi **lý do + điều kiện bật lại**.
  Không có comment = lần sau có người bật lại và tái tạo bug.
- Tên biến/hàm tiếng Anh. Comment tiếng Việt được.
- Không để code chết. Xoá, git nhớ giùm.

## Đặt tên

| Loại | Quy ước | Ví dụ |
|---|---|---|
| File | `snake_case.cpp/.h` | `safety_monitor.cpp` |
| Namespace | `lowercase` | `safety`, `config`, `ota` |
| Kiểu | `PascalCase` | `Verdict`, `RunConfig` |
| Hàm | `camelCase` | `validateAndClamp()` |
| Hằng | `kPascalCase` | `kBottomHardLimitC` |
| Macro | `FBT_UPPER` | `FBT_FW_VERSION` |
| Biến static file | `s_` | `s_lastPidMs` |

Hằng nhiệt độ luôn có hậu tố đơn vị: `kBottomHardLimitC`, `kSensorTimeoutMs`.
Nhầm đơn vị trong code điều khiển nhiệt là loại lỗi tốn máu nhất.

## Module thuần vs code Arduino

**Module thuần** (`src/safety/`, `src/config/`, `src/ota/`):

- KHÔNG `#include <Arduino.h>`, không HAL, không I/O, không `millis()` bên trong.
- Thời gian truyền vào làm **tham số** (`nowMs`) để test được mà không phải chờ thật.
- Hàm thuần: cùng input → cùng output, không state ẩn.
- Mọi hàm public phải có test native.

**Code Arduino** (`main.cpp` và driver):

- Chỉ nối dây: đọc phần cứng → gọi module thuần → tác động.
- Không chứa `if` quyết định an toàn. Xem [ARCHITECTURE.md](ARCHITECTURE.md).

## Bộ nhớ — WROOM-32 không có PSRAM

1. Stack cục bộ > 256 byte phải giải trình bằng comment.
2. `new` trên ESP32 **không nothrow** — hết RAM là `abort()`, không trả `nullptr`.
   Dùng `new (std::nothrow)` cho cấp phát có thể thất bại.
3. `malloc` luôn kiểm tra `NULL`, log rồi return lỗi. Không `abort()`.
4. Cấp phát một lần lúc init, tái dùng. Không heap churn trong vòng lặp.
5. Bảng hằng lớn → `static const` (nằm flash, không tốn DRAM).
6. Tránh `String` Arduino trong hot path — dùng `char[]` + `snprintf`.
7. Reserve `std::vector` trước vòng push.

## Số học thời gian

**Luôn** so sánh bằng hiệu unsigned:

```cpp
if (nowMs - lastMs >= periodMs)   // ĐÚNG — đúng cả khi millis() tràn
if (nowMs >= lastMs + periodMs)   // SAI — hỏng khi tràn (~49.7 ngày)
```

Máy PCR chạy liên tục nhiều tuần, nên tràn `millis()` **không phải** trường hợp
lý thuyết.

## Số thực

- **NaN vượt qua mọi so sánh ngưỡng.** Kiểm tra "thuộc dải hợp lệ", không kiểm
  tra "vượt ngưỡng". Xem [SAFETY.md](SAFETY.md).
- Không so sánh `float` bằng `==`. Dùng ngưỡng epsilon.
- Kiểm `std::isfinite()` cho mọi giá trị đến từ ngoài (cloud, file, sensor).

## Concurrency

- ISR: `IRAM_ATTR`; dữ liệu ISR đọc phải ở DRAM.
- **Không bao giờ** `xSemaphoreTake()` trong ISR — dùng API `...FromISR`.
- CloudTask ở core riêng, đọc telemetry qua snapshot có khoá (portMUX).
  **Không đọc trực tiếp state vòng đo** (PRD D2-02).
- Không cast con trỏ `uint8_t*` chưa align sang kiểu rộng hơn — dùng `memcpy`.

## Lỗi

- Log **trước khi** return lỗi ở mọi đường cấp phát / file / parse / mạng / phần cứng.
- Không nuốt lỗi. Không `catch` rồi bỏ qua.
- `esp_restart()` chỉ cho luồng khôi phục có chủ đích (VD hoàn tất OTA).
- Với đường an toàn: log phải ghi **bền**, không chỉ ra Serial — cần dấu vết sau sự cố.

## Format

```bash
clang-format -i src/**/*.cpp src/**/*.h include/*.h
```

## Git

- Branch: `feat/`, `fix/`, `safety/`, `docs/`, `refactor/`, `test/`, `chore/`.
- Commit: `<type>: <tóm tắt ngắn>`. Thân giải thích **tại sao**.
- Dùng `safety:` cho mọi thay đổi chạm an toàn — để lọc ra được khi audit.
- Một commit = một ý. Không trộn refactor với sửa lỗi.
