# 2026-07-21 — Gộp copy-paste trong ForteSetting (nhóm 2, phần #1 + #2)

Từ review toàn dự án. Refactor **giữ-nguyên-hành-vi** (chỉ gộp trùng lặp), build-verify.

**Cùng đợt nhóm 2 (đã nạp + xác nhận trên máy):**

- `displayLCD.cpp` — `drawWarnFrame(uint16_t color)`: gom **9 site** khung cảnh báo giống hệt
  (box `30,140,272,80` + dải hazard `i<=310` + circle `55,180`) thành 1 helper. Biến thể
  `ErrorProcessatBegin` (y=150/190) và nhánh VN chết của `prepare` (i<=300) để riêng.
- `sensor6035.cpp/.h` — xóa `bResultPutToChart` (166+1 dòng): thành **dead** sau khi nhóm 1
  xóa `getData_toChart` (caller duy nhất).

## #1 — `loadJsonArr<T,N>()` template (`JsonDataConfig`)

11 khối copy mảng JSON→struct giống hệt (slopes, origins, LED power, bottom/top sensor
seq, PID/PID2/PID3, bottom/top overheat, temperature offset) — mỗi khối là
`containsKey + for + cast + store + log` ~10 dòng — gộp thành 1 template:

```cpp
template <typename T, size_t N>
static void loadJsonArr(JsonArray src, T (&dst)[N], const char *label)
{
    info_displayln(label);
    for (size_t i = 0; i < src.size() && i < N; i++)
    {
        dst[i] = src[i].as<T>();
        info_displayln(dst[i]);
    }
}
```

- Mảng nhận **bằng tham chiếu `T (&dst)[N]`** → tự suy **kiểu T** và **sức chứa N** từ chính
  `parameter.*`. Mỗi khối còn 1 dòng: `loadJsonArr(json_document["LED power"].as<JsonArray>(), parameter.led_power, "LED Power:");`
- **Cải thiện an toàn (không đổi hành vi với input hợp lệ)**: `i < N` **clamp** theo sức chứa
  → mảng JSON dài quá **không còn ghi tràn** sang field kế (các mảng heater/setpoint nằm sát
  nhau — GOTCHA 4). Đường Serial không đi qua validate của `handleConfigPost` nên clamp này
  là lớp chặn tràn thật.
- Khối `top heater PWM` (mảng 2 chiều) và các scalar (`parameters{}`, units, device ID...)
  **để nguyên** (không phải copy mảng 1 chiều).

## #2 — `readCommand(Stream&, window)` (gộp vòng nhận Serial + SerialBT)

Vòng framing byte của Serial (window 30ms) và SerialBT (100ms) **trùng nhau** — chỉ khác
stream, window, và Serial xử lý cả `@`/`#` còn BT chỉ `@`. Cả `HardwareSerial` lẫn
`BluetoothSerial` đều kế thừa `Stream` → gộp:

```cpp
if (Serial.available() > 0) { ...; if (!readCommand(Serial, 30)) return; }
else if (!gBtReleased && SerialBT.available() > 0) { ...; if (!readCommand(SerialBT, 100)) return; }
```

- `readCommand` trả `false` khi buffer tràn (đã flush) → caller `return` (giữ đúng hành vi cũ).
- **BT nay nhận cả `@` và `#`** (superset vô hại — sender BT vẫn dùng `@`; ponytail-review ghi
  nhánh `#` là "harmless to unify").
- Giữ guard `!gBtReleased` (BT nhả sau boot → không dùng SerialBT).

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.8%, Flash 2.299.589 B — giảm tiếp so với các
  bước trước; tổng phiên đã giảm ~4KB).
- **Cần khi có điều kiện**: POST 1 config qua dashboard/mock (kiểm giá trị áp đúng), gửi 1
  lệnh JSON qua Serial (kiểm framing).

## Còn lại nhóm 2/3 (chưa làm)

- `bResultPutToChart` giờ **0 caller** (dead sau khi nhóm 1 xoá `getData_toChart`) → xoá được.
- #3 gộp 5 `*Process_loop` (sensor6035, I2C) — **cần phần cứng test**.
- Nhóm 3: gộp `bResultGet`↔`bResultPutToGoogleSheet` (có nghi vấn scaling — xác minh trước),
  gộp per-heater PID — **rủi ro cao, cần phần cứng**.

## Nạp

Đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
