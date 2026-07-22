# 2026-07-22 — Tràn buffer trong `ForteSetting::readCommand()`

## Lỗi

`readCommand()` (đọc lệnh JSON khung từ Serial/SerialBT vào `recvData[2048]`) có **3 khiếm
khuyết**, nặng nhất là **ghi tràn buffer**:

```cpp
uint16_t len = port.readBytes(recvData + recvLen, 1024 * 2); // count CỐ ĐỊNH 2048
...
recvLen += len;
if (recvLen >= 1024 * 2) { ... return false; } // guard chạy SAU khi đã ghi
```

1. **Tràn buffer**: count truyền cho `readBytes` luôn là `1024*2`, **bỏ qua `recvLen`** đã
   tích lũy. Với message nhiều mảnh (đường `moreMsg`, JSON dài chia nhiều lần đọc UART),
   `recvLen > 0` khi vào lần đọc kế → `readBytes(recvData + recvLen, 2048)` ghi tới
   `recvData[recvLen + 2047]`, **vượt `recvData[2047]`**, đè các member ForteSetting kế bên.
   Guard `recvLen >= 2048` chỉ chạy **sau** khi đã ghi tràn. Kích hoạt được qua Serial config
   JSON > ~2040 byte.
2. **OOB read**: `char last = recvData[len + recvLen - 1];` → `recvData[-1]` nếu `len == 0`.
3. **`moreMsg` không reset**: `moreMsg` là member; một message dở dang (bắt đầu JSON dài,
   hết window trước khi có ký tự kết `@`/`#`) để lại `moreMsg = true`. Lần gọi sau `recvLen`
   bị xóa về 0 nhưng `moreMsg` vẫn true → lệnh mới bị hiểu nhầm là phần tiếp của message cũ.

## Sửa (src/ForteSetting.cpp, `readCommand`)

- **Giới hạn đọc theo chỗ CÒN TRỐNG**: `port.readBytes(recvData + recvLen, sizeof(recvData) - recvLen)`
  → không lần đọc nào ghi quá `recvData[2047]`. `\0` cuối vẫn an toàn vì guard chặn `recvLen < sizeof`.
- **`if (len == 0) break;`** ngay sau `readBytes` → chặn `recvData[-1]` và bỏ vòng khi không có byte thật.
- **`moreMsg = false;`** ở đầu hàm (cạnh `recvLen = 0`) → mỗi lần gọi bắt đầu sạch, khớp với
  việc `recvLen` cũng reset (message phải hoàn tất trong 1 lần gọi, window tự kéo dài tới 10s).
- Đổi `1024 * 2` → `sizeof(recvData)` ở guard + flush cho nhất quán.

Không đổi hành vi với input hợp lệ (≤ buffer). Chỉ chặn tràn/leak ở biên.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).
- `g++ -O2 -std=c++17 tools/test_readcmd_overflow.cpp -o t && ./t` → **PASS**. Test tái tạo
  đúng vòng đọc với fake Stream kiểu UART + canary sau `recvData[2048]`, nạp message 3000 byte
  chia mảnh 250 byte. Chứng minh **hai chiều**: bản fix (bound = chỗ trống) không tràn và
  chạm nhánh "too-long flush"; bản cũ (count `1024*2`) **ghi đè canary** → test có tính phân
  biệt (không phải luôn xanh).

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx` (pin cổng; chờ máy về
idle vì nạp = reboot).
