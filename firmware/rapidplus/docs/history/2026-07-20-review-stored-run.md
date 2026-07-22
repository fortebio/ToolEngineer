# 2026-07-20 — Tab Result: xem lại run đã đo, kể cả sau khi tắt/bật máy

## Vấn đề

Tab **Result** chỉ xem được run vừa chạy **trong cùng phiên** (cache RAM:
`gResultsReady`, `gCT/gResult`, `lastRunLoops`). Tắt/bật máy → cache RAM mất sạch →
Result trống, dù đường cong RAW của run cuối **vẫn còn** trong EEPROM tại `RECORDPOS`
(sensor6035 ghi khi run kết thúc, `sensor6035.cpp:1892`).

Yêu cầu: mở Result **sau khi reboot** vẫn xem lại được run cuối (bảng kết quả + chart).

## Hướng xử lý

**Thiết bị là nguồn sự thật.** Web không tự giữ lịch sử — nó *yêu cầu* thiết bị nạp lại
run từ EEPROM rồi tính lại kết quả vào đúng cache mà `/slots` + `/curve` đang phục vụ.

Đã có sẵn `resultOutput()` (lệnh Serial `getResult`) làm gần y hệt, **nhưng nó đổi
`type_infor = escreenReview`** → cướp màn TFT. Không dùng được cho web. Nên làm bản
**không đụng màn hình**: một pending mới trên SettingTask.

### 1. Hàng đợi `PEND_REVIEW` (`ForteSetting`)

- `postReviewLast()` — AsyncTCP chỉ **enqueue** (không đụng EEPROM/parameter, GOTCHA
  Setting #2), publish cờ **cuối cùng** sau `__sync_synchronize()`.
- `drainPending()` (SettingTask, đã chặn busy ở đầu hàm) xử lý:
  - `getDataAmplificationEEPROM()` → nạp `RECORDPOS` (10×130 Word RAW) vào
    `sensor67Value`. **Guard idle** ở đầu drain đảm bảo SensorTask không đang ghi buffer.
  - Heuristic bỏ record rỗng: EEPROM chưa ghi đọc ra `0xFFFF`; baseline run thật ~150–260.
    `probe = sensor67Value[0][0]`, chỉ nạp khi `10 < probe < 60000` → máy mới tinh hiện
    **trống**, không hiện rác.
  - `bResultGet(ct, res)` tính lại CT / P-N-S → `dashboardSetResults(ct, res)`
    (bật `gResultsReady`, `/slots` `ready=true`).
  - `setLastRunLoops(parameter.amplification_time)` — record không tự mang độ dài, và
    sau reboot `lastRunLoops` RAM = 0, nên phải set để `/curve` phục vụ đúng số vòng
    (GOTCHA 7: `COUNTER=0` sau run, `/curve` fallback `getLastRunLoops()`).

### 2. `POST /reviewlast` (firmware — `webDashboard.cpp`)

- Chặn **409** khi `dashboardDeviceBusy()` (nạp lại sẽ đè `sensor67Value` mà SensorTask
  đang sở hữu giữa run), **503** nếu đã có pending khác. Idle → `postReviewLast()` → 200.
- Đăng ký **trước** `serveStatic` (GOTCHA 12).

### 3. Client (`data/script.js`)

- `loadResultSlots()`: nếu `/slots` trả `ready === false` **và** `curPhase !== "amplification"`
  → gọi `reviewStoredRun()`. (Không làm khi đang amp: `ready=false` lúc đó là **cố ý giấu**
  cache run cũ, không được dựng lại đè lên.)
- `reviewStoredRun()`: `POST /reviewlast` → poll `/slots` (tối đa 15 lần × 200ms, thừa cho
  drain ~10ms + đọc flash) tới khi `ready` → dựng lại bảng + `loadCurve(resultView)` nếu
  chart đang mở. Cờ `reviewing` chặn gọi chồng.

### 4. Mock (`tools/sse_test_server.py`)

- Cờ `--reboot`: boot như vừa tắt/bật máy — `_stored=True` (EEPROM còn run), `_reviewed=False`
  (RAM trống). `/slots` `ready` giờ gác thêm `available = _ran_before or _reviewed or not _stored`;
  `/curve` phục vụ đường cong khi `_ran_before or _reviewed`.
- `POST /reviewlast` (chặn busy) → `_reviewed=True`. Mặc định (không `--reboot`) hành vi
  **không đổi** → E2E cũ (`test_full_run.js`) không bị ảnh hưởng.

## Kiểm chứng

- `pio run -e esp32dev` → SUCCESS (RAM 22.9%, Flash 68.9%).
- `node tools/test_review_reboot.js` (tự bật mock `--reboot` + Edge headless): trước review
  `/slots ready=false`, `/curve count=0`; mở Result → client tự `POST /reviewlast` → bảng
  hiện badge P/N/S, `ready=true`; "View chart" → chart vẽ đủ điểm. **ALL PASSED.**
- `python tools/sse_test_server.py selftest` → OK (đường mặc định không đổi).

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t uploadall`.
