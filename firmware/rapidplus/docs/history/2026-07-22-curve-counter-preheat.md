# 2026-07-22 — `/curve` báo số vòng preheat ở idle & cắt cụt run review

## Triệu chứng

Máy vừa boot, **UI báo `idle`** (chưa bấm gì), nhưng `GET /curve` trả `count` **tăng dần**
`4 → 10 → 14 → 15` rồi dừng ở **15**, series là baseline phẳng ~232 (không phải đường
amplification). Sau `POST /reviewlast` (nạp run cũ 120 vòng từ EEPROM), chart chỉ hiện **15
điểm đầu** thay vì toàn 120 vòng.

## Gốc rễ — `COUNTER` bị dùng chung cho preheat lẫn amplification

`COUNTER` (chỉ số vòng) được **tái sử dụng cho 2 mục đích**:
- Đếm vòng **amplification** khi đo (`eSensor1stReadingFunc`, sensor6035.cpp:1710).
- Đếm vòng **preheat optics** (`eSensorPreheat`, sensor6035.cpp:1328) — làm nóng cảm biến
  sau boot, log "finish N rounds preheating".

Sau mỗi lần **boot**, constructor đặt `sensorStep = eSensorpreheat` → SensorTask **tự động
preheat** (không cần bấm), đọc opto mỗi `OPTO_INTERVAL` (20s) và `COUNTER++`, tới
`PREHEATLOOPS = opto_preheat_time*1000 / OPTO_INTERVAL` (config: 300s/20s = **15**). Việc này
**độc lập với `type_infor`** → UI báo idle nhưng `COUNTER` vẫn chạy. `eSensorMaintain()` (sau
preheat) **không đụng `COUNTER`** → nó parked ở 15 vĩnh viễn.

`getCurrentLoop()` trả chính `COUNTER` này. `handleCurve` cũ dùng:
```cpp
uint8_t n = getCurrentLoop();              // = 15 (preheat), KHÔNG phải độ dài run
if (n == 0 && type_infor != eoptoreading)  // n≠0 → fallback KHÔNG bao giờ chạy ở idle
    n = getLastRunLoops();
```
Vì `COUNTER > 0` ở idle/maintain, nhánh `getLastRunLoops()` không bao giờ chạy → `/curve`
dùng `COUNTER=15`: báo 15 vòng lúc idle, và cắt cụt run review (dù `reviewlast` đã
`setLastRunLoops(120)`) còn 15 điểm đầu. (Preheat không ghi `sensor67Value`, nên 15 điểm đó là
15 điểm đầu của run EEPROM mà reviewlast vừa nạp.)

## Bằng chứng máy thật

- Config: `opto preheat time = 300`, `time per loop = 20000` → `PREHEATLOOPS = 15`.
- `/curve count` đo được: 4 → 10 → 14 → **15** → 15 (dừng đúng ở `PREHEATLOOPS`, nhịp 20s).

## Fix (webDashboard.cpp `handleCurve`)

Chỉ tin `COUNTER` khi **thực sự đang đo**; mọi lúc khác dùng độ dài run đã lưu:
```cpp
uint8_t n = (_displayCLD.type_infor == eoptoreading)
                ? _sensor6035.getCurrentLoop()   // measuring: COUNTER = completed rounds
                : _sensor6035.getLastRunLoops();  // else: stored run length (0 if never run)
```
- Đang đo: `COUNTER` (=0 ở ~20s đầu → chart mở trống, đúng — không kế thừa run trước).
- Idle/preheat/maintain: `getLastRunLoops()` = 0 sau boot (chưa chạy) → `/curve count=0`,
  không còn báo 15 vòng preheat.
- Review: `getLastRunLoops()` = `amplification_time` (do `reviewlast` set) → chart đủ run.

`dashboardLoop` streaming `new_readings` đã guard `type_infor==eoptoreading` sẵn nên không đổi.
Mock (`sse_test_server.py curve_count`) vốn theo phase (amplification→rounds, else→retained)
đã khớp logic mới, không cần sửa.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).
- Verify trên máy sau nạp: `/curve count` ở idle sau boot = **0** (không còn 15); sau
  `POST /reviewlast` = **120** (đủ run cũ).

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
