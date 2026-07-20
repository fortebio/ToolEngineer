# 2026-07-17 — Chart vẽ lại từ đầu run (backfill khi mất kết nối)

## Vấn đề

Mất kết nối web giữa run → đoạn chart trong lúc mất kết nối **mất hẳn**, nối lại chỉ vẽ
tiếp từ điểm hiện tại. Mở web muộn cũng vậy: chart bắt đầu từ lúc mở, không phải từ đầu run.

Nguyên nhân: SSE chỉ đẩy điểm **mới** (`new_readings` mỗi vòng đo), client không có lịch
sử để tự bù. Thiết bị thì vẫn giữ đủ trong `sensor67Value[10][130]` — chỉ là chưa có
đường nào hỏi lấy.

## Hướng xử lý

**Thiết bị là nguồn sự thật, client không tự nhớ.** Thêm endpoint trả toàn bộ đường cong,
client gọi lại mỗi khi cần vẽ đủ.

### 1. `GET /curve` (firmware — `webDashboard.cpp`)

```
{count, intervalMs, series: [[cal, cal, ...] x10]}
```

- `count = _sensor6035.getCurrentLoop()` (số vòng đã xong, kẹp 130).
- Giá trị calibrated **cùng công thức** `new_readings`: `(raw - origins[i]) / slopes[i]`.
- `intervalMs = OPTO_INTERVAL` → client tự suy ra phút/vòng, không hardcode.

### 2. Trục X = chỉ số vòng của thiết bị (không phải wall-clock trình duyệt)

Đây là phần cốt lõi. Trước đây x = `(Date.now() - runStart)/60000` — đồng hồ của trình
duyệt. Nếu backfill dùng chỉ số vòng còn điểm live dùng `Date.now()` thì hai đoạn **lệch
nhau đúng bằng thời gian mất kết nối**. Nên chuyển cả hai về một gốc duy nhất:

- `new_readings` gửi thêm `i` = chỉ số vòng (`COUNTER-1`); client vẽ `x = i * phút/vòng`.
- `/curve` trả `j = 0..count-1`, mà live point là `idx = loop-1` với `count = loop` →
  điểm cuối backfill **trùng đúng** điểm live, không hụt không trùng.
- `runStart` bỏ hẳn khỏi `script.js`.

### 3. Client (`data/script.js`)

- `loadCurve()` — fetch `/curve`, tính lại baseline (5 điểm đầu) rồi `setData()` cả đường;
  set `nextIdx = count`.
- Gọi ở **2 chỗ**: bấm "View chart", và SSE event `open` (reconnect) khi chart đang hiện →
  nối lại mạng là tự bù đoạn đã mất.
- `plotResult()` đọc `jsonValue.i` (bỏ qua key không bắt đầu bằng `#`), `addPoint` với
  `shift = false` — **bỏ cửa sổ trượt 40 điểm** để giữ nguyên cả run.

### 4. Mock (`tools/sse_test_server.py`)

- Thêm `/curve` + `current_tick()` — **một timeline chung** cho mọi client và `/curve`
  (giống thiết bị thật: run vẫn chạy dù không ai mở web). Trước đó mỗi kết nối SSE đếm
  tick riêng từ 0 nên `/curve` không thể khớp.
- SSE gửi kèm `i`.

## Kiểm chứng

- `pio run -e esp32dev` → SUCCESS (RAM 22.8%, Flash 68.2%).
- Mock: `/curve` trả đúng 10 kênh × `count` điểm, toàn scalar.
- Timeline khớp: `i` trong SSE tăng đều 1, `curve.count >= i`, và
  `curve.series[0][i] == SSE["#1"]` tại cùng `i` → backfill nối liền điểm live.

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t upload` **và** `-t uploadfs`.
