# 2026-07-21 — Tab Result: bảng có kết quả nhưng chart trống (desync cache)

## Triệu chứng

Vào tab **Result**: **bảng** hiện đầy kết quả (tên bệnh, P/N/S, CT) nhưng **chart trống**.
Tái hiện trực tiếp trên máy thật (192.168.1.25):

```
GET /slots  -> {"ready":true, ...}          # bảng có data
GET /curve  -> {"count":0,"series":[[],...]} # chart TRỐNG
```

Kết quả RAW của run **vẫn còn nguyên trong EEPROM** (RECORDPOS) — bug không ở EEPROM,
mà ở chỗ `/curve` không được trỏ tới nó.

## Gốc rễ — bảng và chart dùng HAI cache khác nhau, lệch lifecycle

| | Bảng (`/slots`) | Chart (`/curve`) |
| --- | --- | --- |
| Nguồn | `gResultsReady` + `gCT`/`gResult` (webDashboard.cpp) | `getLastRunLoops()` + `sensor67Value` (sensor6035) |
| Set | `dashboardSetResults()` (screen_Result / reviewlast) | `lastRunLoops = COUNTER` khi finished (sensor6035.cpp:1744) |
| Bị xóa | **không gì** khi bắt đầu run mới | `clear()` đầu run mới (button.cpp:481) + reboot → `=0` |

`handleCurve`: `n = getCurrentLoop()`; nếu `0` và không đang amp → `n = getLastRunLoops()`.
Khi một run được **Start** rồi **ngắt trước khi finished** (hoặc reboot giữa chừng),
`clear()` (button.cpp:481) đã đưa `lastRunLoops`/`sensor67Value` về 0 **nhưng
`gResultsReady` vẫn giữ** kết quả run trước → `/slots ready=true`, `/curve count=0`.

**Và không tự chữa được:** client chỉ nạp lại từ EEPROM khi bảng **cũng** trống
(`script.js`: `if (d.ready === false && curPhase !== "amplification") reviewStoredRun()`).
Vì `ready=true`, client **không bao giờ** gọi `POST /reviewlast` — đường duy nhất nạp lại
`sensor67Value` + set `lastRunLoops` — nên chart trống **vĩnh viễn** tới khi chạy run mới.

## Fix — đồng bộ hai cache: xóa bảng cùng lúc xóa chart

Cho `dashboardClearResults()` (mới) chạy **ngay cạnh** `_sensor6035.clear()` ở đầu run mới,
đặt `gResultsReady = false`. Bảng và chart giờ cùng một lifecycle: cùng trống → lần vào
Result kế tiếp `ready=false` → client tự `POST /reviewlast` → nạp lại **cả hai** từ EEPROM,
đồng bộ. Hết cảnh bảng-không-có-chart.

- `src/webDashboard.h` / `webDashboard.cpp`: thêm `dashboardClearResults()` (đặt
  `gResultsReady = false`), song đôi với `dashboardSetResults()`.
- `src/button.cpp`: `#include "webDashboard.h"`; gọi `dashboardClearResults()` ngay sau
  `_sensor6035.clear()` tại `ewaitampTube → eoptoreading` (button.cpp:481).

Không đụng `postData_GoogleSheet`, `/reviewlast`, hay heuristic EEPROM.

## Vì sao không phải các nghi phạm đoán ban đầu

Trace ban đầu nghi (①) độ dài `/curve = amplification_time`, (②) heuristic một-điểm
`sensor67Value[0][0]`. Cả hai chỉ cắn ở đường **reviewlast sau reboot**. Query máy thật cho
thấy bug thực xảy ra **không cần reboot**: desync `gResultsReady` (dai) vs `lastRunLoops`
(bị `clear()` reset). Mock (`sse_test_server.py`) che khuất vì nó không tách hai cache này.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).
- Verify E2E trên máy thật (khi máy rảnh, chưa nạp lúc viết — máy đang `waitamp`): chạy 1
  run tới finished (bảng + chart hiện) → **Start run mới rồi ngắt** → vào Result: với fix
  `ready=false` → client `POST /reviewlast` → chart hiện lại từ EEPROM. Không fix: `ready=true`,
  chart trống.

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx` (pin cổng; máy đang
busy thì chờ về idle vì nạp = reboot, hủy run đang chuẩn bị).
