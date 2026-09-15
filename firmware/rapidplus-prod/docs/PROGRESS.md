# Tiến độ — FBT-RapidPlus Production

> **Luật:** chỉ ghi những gì **đã kiểm chứng** bằng build / nạp / đo thật.
> Không ghi phỏng đoán, không ghi kế hoạch. Kế hoạch thuộc về issue.

## Trạng thái

**Giai đoạn: scaffold.** Khung dự án + lớp an toàn dạng module thuần + test native.
Chưa nạp lên máy thật lần nào.

## Đã xong

- Khung repo: PlatformIO 3 env (`esp32dev`, `native`, `esp32dev_test`).
- Lớp an toàn nhiệt `src/safety/` — C++ thuần, 10 test native.
- Validate + clamp config `src/config/` — C++ thuần.
- Verify OTA `src/ota/` — C++ thuần.
- Bộ tài liệu: `AGENTS.md`, `SCOPE.md`, `docs/*`.
- CI: build firmware + chạy test native trên mọi PR.

> ⚠️ **Chưa build lần nào trên máy có toolchain.** Scaffold viết trên máy không
> cài PlatformIO. Lần chạy đầu có thể còn lỗi vặt — sửa và ghi lại ở đây.

## Đang chặn

| Việc | Chặn bởi |
|---|---|
| Ngưỡng hard-limit chính thức | Team nhiệt/sinh học chốt (PRD Q3) |
| Pin map | Chưa trích từ `instrument-wroom32` sang [HARDWARE.md](HARDWARE.md) |
| Ngân sách RAM | Chưa có máy để đo |
| Lớp 0 (cầu chì nhiệt) | Chưa rõ phần cứng có hay không |

## Việc tiếp theo

1. Điền [HARDWARE.md](HARDWARE.md) từ `instrument-wroom32` — pin map, sensor, PCB V1.2/V1.3.
2. Chốt ngưỡng hard-limit bằng văn bản → cập nhật `include/safety_limits.h`.
3. Port driver sensor nhiệt → nối vào `readTemperatures()`.
4. Port driver heater → nối vào `stopAllHeating()`.
5. **Test trên máy thật: rút sensor → phải cắt gia nhiệt ≤ 2 s.** Đây là mốc
   xác nhận toàn bộ chuỗi an toàn thật sự hoạt động, không chỉ đúng trên host.
6. Đo `ESP.getFreeHeap()` cơ sở → ghi vào [HARDWARE.md](HARDWARE.md).
7. Port PID + thuật toán.
8. Lớp cloud (OTA + WiFi + WebSocket) — **chỉ sau khi P1.5 xanh** (xem [SAFETY.md](SAFETY.md)).

## Cách build / test

```bash
pio run -e esp32dev              # build firmware
pio test -e native               # test logic an toàn (host, không cần máy)
pio test -e esp32dev_test -v     # test trên ESP32 thật
pio run -e esp32dev -t upload    # nạp
pio device monitor               # xem log
```

## Môi trường dev

| | |
|---|---|
| PlatformIO | `TODO` — điền phiên bản |
| Cổng nạp | `TODO` — VD `COM5` |
| Máy test | `TODO` — số serial máy dùng để test |

---

## Bài học đã verify

> Mỗi lần mất > 1 giờ vì một vấn đề, ghi vào đây. Ghi **cơ chế**, không chỉ triệu
> chứng. Đây là phần không tái tạo được từ git log.

_(chưa có — dự án mới)_

Mẫu:

> **YYYY-MM-DD — <tiêu đề ngắn>.** Triệu chứng: … Nguyên nhân gốc: … Cách xử lý: …
> Cách phát hiện lại: … (commit `abc1234`)
