# 2026-07-17 — Tab Result (xem lại) + đặt tên slot trên Home trước khi Start

Hai tính năng theo yêu cầu, kèm **1 lỗi firmware nghiêm trọng** phát hiện khi review
(xem mục 4 — quan trọng nhất file này).

## 1. Process → Result: xem lại dữ liệu trong máy

Tab **Process** đổi thành **Result** (`data-screen="result"`, `#screen-result`), mục đích
rõ ràng: **xem lại run đã lưu trong máy** — bảng kết quả (tên · CT · P/N/S/E/B) + chart
"Stored run curve" vẽ từ `/curve` khi bấm **View chart**.

## 2. Home: đặt tên slot trước khi Start

Vào pha khuếch đại — **preheat xong** (`sensor6035.cpp` → `ewaitampTube`) **hoặc**
**skip preheat** (`button.cpp:583` → `ewaitampTube`) — cả hai đều hội tụ về `ewaitampTube`,
nên **firmware chỉ cần đổi 1 dòng**: `fillStatus` map state này sang phase riêng
**`"waitamp"`** (trước là `"idle"`, client không phân biệt được với `ewaitLysisTube`).

Home thành **3 chế độ** (state machine client trong `renderHome`):

| Chế độ | Điều kiện | Hiển thị |
| --- | --- | --- |
| DEFAULT | còn lại | temps đầy đủ (2 card) |
| NAMING | `waitamp` && chưa confirm | card đặt tên slot, **Start (đỏ) khoá**, temps đầy đủ |
| CHART | (`waitamp` && confirmed) \|\| `amplification` \|\| `finished` | chart live, **Start mở**, temps thu về **1 strip nhỏ gọn** |

- **Khoá Start chỉ phía web** (class `.locked` nuốt click) — **nút vật lý trên máy vẫn
  chạy**. Gate là để nhắc đặt tên, không chặn phần cứng.
- **Confirm** (`#confirmNamesBtn`): ô trống → mặc định `#1`–`#10` (`applyNamesTo`).
- **Reload giữa run**: `confirmed` mất, nhưng `phase == "amplification"` tự set lại `true`
  → không kẹt ở màn naming. Run xong → reset để run sau lại phải đặt tên.
- Bảng naming chỉ dựng 1 lần mỗi phiên waitamp (`namingBuilt`) → không cướp focus khi gõ.

**Hai chart độc lập**: `homeView` (live, ăn `new_readings` + backfill `/curve`) và
`resultView` (snapshot, vẽ từ `/curve`). Tên slot + ẩn/hiện đồng bộ giữa 2 bảng và 2 chart
qua `slotNames` + `data-slot`.

## 3. Chart trên Home sống qua lúc run xong, chỉ mất khi bấm nút trắng

`chartMode` **bao gồm cả `phase == "finished"`** → run xong đường cong **vẫn nằm trên
Home** (kèm temps thu gọn), không biến mất đột ngột.

Nó chỉ mất khi **nút trắng** (trên máy, hoặc chip "Return"/"Next test" trên web) đưa máy
rời màn finished: `escreenFinished` --white--> `escreenRestart` (`button.cpp:672`), mà
`escreenRestart` không có trong `fillStatus` → rơi về `default` → `phase "idle"` →
`chartMode = false` → Home trở lại temps đầy đủ. Đúng một nguồn điều khiển: **trạng thái
máy**, không cần cờ riêng phía client.

Kèm lợi: reload trang khi máy đang ở màn finished → `chartMode` vẫn true →
`loadCurve(homeView)` lấy lại **đúng run vừa xong** nhờ fix `lastRunLoops` ở mục 4.

## 4. LỖI FIRMWARE: `/curve` trả rỗng ngay khi run xong (đã sửa)

**Review adversarial bắt được; mock đã che mất lỗi này.**

`handleCurve` lấy độ dài run từ `_sensor6035.getCurrentLoop()` = `COUNTER`. Nhưng
`sensor6035.cpp:1908` **xoá `COUNTER = 0` đúng lúc** `type_infor = escreenFinished`:

```cpp
sensorStep = eSensormaintain;
COUNTER = 0;                            // <-- ngay tại đây
_displayCLD.type_infor = escreenFinished;
```

⇒ Run vừa xong là `/curve` trả `{count: 0, series: [[],...]}` → chart "Stored run curve"
**trắng trơn**, đúng lúc tab Result cần dùng. **Toàn bộ mục đích của tab Result hỏng trên
máy thật.** Dữ liệu vẫn còn nguyên: `sensor67Value[10][130]` không bị xoá (và
`screen_Result()` còn gọi `getDataAmplificationEEPROM()` nạp lại từ EEPROM) — **chỉ mỗi
độ dài bị mất**.

**Vì sao mock che mất**: `amp_rounds_done()` của mock *cap* ở full count sau khi hết run
thay vì về 0 → test trên mock thấy chart đầy đủ, nạp lên máy thật thì trắng.

**Cách sửa** (giữ lại độ dài, **không đụng state machine sensor**):

- `sensor6035.h`: thêm `uint8_t lastRunLoops = 0;` + getter `getLastRunLoops()`.
- `sensor6035.cpp:1908`: `lastRunLoops = COUNTER;` **trước** `COUNTER = 0;`.
- `sensor6035.cpp clear()` (run mới): `lastRunLoops = 0;` → run mới không lẫn độ dài cũ.
- `webDashboard.cpp handleCurve`: `if (n == 0) n = _sensor6035.getLastRunLoops();`

Đã kiểm: `sensor67Value` luôn là **raw** (lưu raw ở `sensor6035.cpp:1861`; calibrate lúc
đọc ở 264/426/595; `getDataAmplificationEEPROM` memcpy raw từ EEPROM) → `handleCurve`
calibrate 1 lần vẫn đúng, **không** bị calibrate kép. Tốn thêm **1 byte** RAM.

## 4b. Chart "mới bắt đầu" lại hiện dữ liệu run cũ (đã sửa)

Hệ quả trực tiếp của fallback ở mục 4 — phát hiện khi chạy thử.

`sensor6035::clear()` (chỗ duy nhất reset `lastRunLoops` + xoá buffer) **gần như không
bao giờ được gọi**: chỉ ở lúc boot (`sensor6035.cpp:147`), còn chỗ trong displayLCD đã bị
comment (`// _sensor6035.clear();`). Nên:

1. Run A xong → `lastRunLoops = 44`, `COUNTER = 0`.
2. Run B tới `waitamp` → `COUNTER` vẫn 0, `clear()` không chạy ⇒ `lastRunLoops` **vẫn 44**.
3. `/curve` thấy `n == 0` → fallback ⇒ trả **nguyên đường cong run A**.
4. Confirm → chart mode → `loadCurve` ⇒ **chart của run mới hiện dữ liệu run cũ**.

Buffer `sensor67Value` cũng không được xoá giữa các run — chỉ **ghi đè dần từng vòng**.

**Không sửa ở firmware**, vì `/curve` trả run A lúc `waitamp` là **đúng cho tab Result**:
máy thật sự vẫn đang giữ run A, và `/slots` cũng vẫn trả kết quả run A ⇒ bảng + chart của
Result khớp nhau. Sai là ở **chart live trên Home**: run chưa chạy thì không có đường cong
của chính nó. Sửa phía client:

- `curveReady = phase === "amplification" || phase === "finished"` — **chỉ** khi đó mới
  `loadCurve(homeView)`; `setHomeMode(naming, chartMode, curveReady)` và handler SSE
  `open` dùng cờ `homeCurveOn` thay cho `homeChartOn`.
- `resetView(homeView)` khi **vào `waitamp`** (không chỉ khi vào `amplification`) → xoá
  đường cong run trước còn sót trên màn hình trình duyệt.
- Nút Confirm gọi `setHomeMode(false, true, false)` → chart mở ra **trống**.

**Mock phải tái hiện được bẫy này** (nếu không lại test với hợp đồng sai như mục 4):
tách `amp_rounds_done()` (COUNTER sống, = 0 ngoài pha amp) và `curve_count(tick,
ran_before)` (mirror `handleCurve`: `n = COUNTER; if (n==0) n = lastRunLoops`), thêm
`ran_once()`. Selftest assert thẳng cái bẫy: `curve_count(16, True) == RUN_ROUNDS`.

Kiểm chứng (CDP, mock chạy sang run B): `/curve` trả **44** ở waitamp (bẫy có thật) trong
khi chart Home **0 điểm** lúc naming, **0 điểm** sau Confirm, rồi **5 điểm và tăng dần**
khi run B chạy — tức chỉ dữ liệu run mới.

## 5. Bảng Result và chart lệch run (đã sửa)

`/slots` trả kết quả **cache của run trước** (`gResultsReady`), còn `/curve` trả **buffer
sống**. Đang chạy run B mà mở Result → bảng ghi kết quả run A, chart vẽ run B. Máy chỉ giữ
**1 run**, nên sửa stateless ngay chỗ đọc (`handleSlots`):

```cpp
bool ready = gResultsReady && (_displayCLD.type_infor != eoptoreading);
```

Đang amplify → bảng để trống (chưa có kết quả) + chart là run đang chạy ⇒ **hai nửa luôn
cùng một run**. Xong run → `escreenFinished` ≠ `eoptoreading` → bảng hiện kết quả bình thường.

## 6. Mock khớp lại hợp đồng firmware

- `amp_rounds_done`: **+1** — `/curve` phải **bao gồm** vòng vừa stream (firmware:
  `count = COUNTER`, `new_readings` gửi `idx = COUNTER-1`). Trước đó mock loại nó ra →
  reconnect trên mock tạo lỗ hổng 1 vòng mà máy thật không có.
- Giữ full count sau khi hết run — giờ **khớp firmware đã sửa** (`lastRunLoops`).
- `/slots`: `ready=false` khi đang amplify (mirror mục 5).
- Phase finished: `"idle"` → `"finished"` (đúng `fillStatus`).
- `new_readings` chỉ gửi trong amplification (như firmware chỉ gửi khi `eoptoreading`).

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.8%, Flash 68.2%).
- `python tools/sse_test_server.py selftest` → OK (assert `count == i+1` mọi tick amp).
- Live SSE: `new_readings` chỉ xuất hiện trong `amplification`.
- Chụp headless (Edge + CDP, poll DOM): Start **khoá** khi naming → **mở** sau Confirm;
  chart live + temps thu gọn khi chạy; **sau khi run xong**: Result hiện **44 vòng đường
  cong + bảng CT/badge cùng run**. Cả 2 breakpoint (mobile bottom-nav / desktop sidebar).
- Review 5 chiều × verify đối kháng: 9 findings thô → **5 confirmed** (2 high cùng gốc lỗi
  mục 4, 2 medium, 1 low), 4 bị bác (false alarm).

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t upload` **và** `-t uploadfs`.
