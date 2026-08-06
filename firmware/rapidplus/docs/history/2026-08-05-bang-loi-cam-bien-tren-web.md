# Bảng lỗi cảm biến lên web, thay chỗ chart ở tab Result (2026-08-05)

## Vấn đề

Máy có bảng lỗi (`screen_errorResult()`, nút **ĐỎ** trên màn finished/review): 10 kênh, mỗi kênh
hoặc một mã lỗi hoặc `----`. **Web không có gì tương đương** — người đọc kết quả bằng điện thoại
không thấy được kênh nào hỏng, trên đúng màn hình có nhiệm vụ trả lời "từng slot ra sao".

## `GET /errors` — và vì sao nó là SNAPSHOT

```json
{"ready": true, "slots": [{"code": null}, {"code": 102, "text": "[Sensor Light]- no data ..."}, ...]}
```

**Đây là phần quan trọng nhất của thay đổi.** `error.error` là `std::vector<ErrorRecord_t>` mà
**ControlTask `push_back()` từ ~28 chỗ** bất kỳ lúc nào (các đường PID safety). `push_back` **cấp
phát lại buffer** — duyệt vector đó từ task AsyncTCP là **use-after-free** chờ ngày nổ, đúng lớp
lỗi mà repo này đã bị cắn nhiều lần.

Nên route **không đọc vector**. Ảnh chụp được lấy trong `dashboardSetResults()`, chạy trên
**DisplayTask** (`screen_Result`) hoặc **SettingTask** (`/reviewlast`) — **đúng task đã đọc vector
đó để vẽ bảng trên TFT**, nên không thêm phơi nhiễm nào chưa có. Route chỉ chạm mảng
`gErrRec[10]` tĩnh; `decodeError()` nhận record **theo giá trị** và chỉ index vào bảng chuỗi
static, nên gọi từ AsyncTCP là an toàn.

Chụp **cùng thời điểm** với cache CT/outcome → `/errors` và `/slots` **không thể** mô tả hai run
khác nhau. `ready` dùng **cùng biểu thức** (`gResultsReady && type_infor != eoptoreading`). Đây là
bài học desync bảng-vs-chart 2026-07-21, áp trước thay vì sửa sau.

**Mirror đúng truy vấn của TFT** — `errorLightSensor` + `errorNoData` + `eSensor1stReading` — và in
**cùng mã 4 chữ số** (`module*1000 + type*100 + step*10 + slot`) để đọc chéo hai màn hình. Nới rộng
truy vấn ở một bên là hai bảng bắt đầu bất đồng về cùng một run: chính xác cái vừa phải sửa ở
`fillStatus`/`fillActions` hôm nay.

**`ready=false` không phải "không có lỗi"** — client nói *"No stored run to report on yet."* Cấp
giấy chứng nhận sạch cho một máy chưa chạy gì thì tệ hơn im lặng.

## UI: thay chỗ, không xếp chồng

Nút **"Error table"** cạnh "View chart". `showResultPane("chart"|"errors")` hiện cái này thì ẩn
cái kia — hai card **dùng chung một chỗ** trong bố cục.

Xếp chồng thì sai: cả hai trả lời cùng một câu hỏi về cùng một run từ hai phía, và card chart cao
bằng viewport (`--chart-h`) nên đặt bảng lỗi dưới nó là bắt người ta cuộn qua thứ họ không xem.

Chi tiết đã cân nhắc:

- **`.result-actions` phải `flex-wrap`** — ở 320px/131% font hai nút không vừa một hàng, mà flex
  item không co xuống dưới min-content của chính nó. Đúng cơ chế từng làm chip nút tràn trang và
  khiến Chrome nới layout viewport ("giao diện lệch sang phải").
- **Nút phụ dùng viền, không làm mờ.** Làm mờ đọc ra là *disabled* trên một thiết bị mà một nửa
  control thật sự bị disable một nửa thời gian.
- **Luôn liệt kê đủ 10 slot.** Bảng chỉ hiện kênh lỗi thì không phân biệt được với bảng nạp hỏng.
- **Hàng lỗi có nền + chữ đậm**, cùng luật với hàng `P`/`S` ở bảng kết quả: màu dẫn mắt, **chữ mới
  mang nghĩa**.
- Bảng lỗi **không** bị ghim chiều cao viewport — 10 hàng ngắn trong một hộp cao lêu nghêu chính là
  lỗi CLAUDE.md đã ghi cho bảng kết quả.

## Guard

`node tools/test_error_table.js` (cần mock chạy sẵn, `--reboot`) — chạy trên **trang thật** qua
browser, đo ở **390px, 320px/131% font, 1280px**:

| Ghim | Vì sao |
| --- | --- |
| swap là SWAP, cả hai chiều | hai card chồng nhau là lỗi này thay thế |
| đủ 10 hàng | bảng chỉ có lỗi ≡ bảng nạp hỏng |
| mã trùng mã của máy (`102`) | web tự đặt số là hai màn hình hết đọc chéo được |
| không tràn ngang ở 320px | cột chữ tự do là thứ đẩy trang trượt ngang |

Mock thêm `_errors()` **dùng chung biểu thức `ready` với `_slots()`** — mock cho hai cái lệch nhau
là che mất đúng cái desync firmware được xây để tránh.

## File đã sửa

| File | Việc |
| --- | --- |
| `src/webDashboard.cpp` | `#include "errorCheck.h"`; snapshot `gErrRec`/`gErrHas`; chụp trong `dashboardSetResults()`; `handleErrors` + đăng ký `/errors` |
| `data/index.html` | `#resultErrorCard` cùng chỗ với chart; `.result-actions` + `#viewErrorsBtn` |
| `data/script.js` | `showResultPane()`, `loadErrors()` |
| `data/style.css` | `.result-actions`, nút phụ, `.err-*` |
| `tools/sse_test_server.py` | `GET /errors` |
| `tools/test_error_table.js` | guard mới |
| `CLAUDE.md` · `docs/GUI_SSE/GUI.md` | route + bất biến snapshot |

## Kiểm chứng

- `node tools/test_error_table.js` ✓ (8 check × 3 kích thước)
- `test_web_assets` · `test_status_coverage` · `test_device_id` · `test_phase0_guards` ·
  `test_qr_payload` · `test_no_method_branch` · `test_no_hscroll` ✓
- Build SUCCESS, flash **72.2%**, 0 cảnh báo

**Còn phải thử trên máy thật**: chạy một run có kênh hỏng thật (hoặc rút một cảm biến), rồi so
bảng trên web với bảng ĐỎ trên TFT — **mã phải trùng từng số**. Và kiểm `/errors` trả `ready:false`
trên máy vừa nạp firmware chưa chạy run nào.

## Bổ sung: bảng lỗi cũng lên tab HOME

Hết run, bấm **ĐỎ** (chip web **hoặc** nút vật lý) → máy sang `escreenErrorResult` →
`phase "errortable"` → Home **đổi chart sang bảng lỗi**, giữ nguyên strip nhiệt gọn và bảng slot.
Bấm **TRẮNG** rời màn → về idle, mất cả hai.

Web **đi theo máy**, không tự bày một view riêng — nên ai đứng ở máy và ai cầm điện thoại nhìn
thấy cùng một thứ, bất kể ai bấm.

Ba thứ phải sửa kèm, cả ba đều là bẫy im lặng:

1. **`escreenErrorResult` phải có phase RIÊNG.** Ban đầu tôi gộp nó với `errprocess` thành
   `"error"` — nhưng cái kia là **máy hỏng thật**. Gộp lại thì một lỗi PID sẽ kéo bảng lỗi lên
   Home. Nay là `"errortable"`.
2. **`fillActions` không có case cho `escreenErrorResult`** → cả ba chip web **trống trơn**, trên
   đúng màn hình in dòng "Press white key to test next". Nút vẫn chạy, chỉ là không có nhãn.
3. **Mock hardcode `"phase": "finished"` trong nhánh `else`** → `errortable` rơi vào đó. Máy trạng
   thái của mock đã chuyển đúng (log: `press: red -> phase errortable`) nhưng payload SSE vẫn báo
   `finished`, nên client không bao giờ thấy. **Đây chính là hình dạng lỗi mà firmware sẽ mắc nếu
   `escreenErrorResult` bị gộp lại vào một case khác** — và nhìn từ ngoài mọi thứ vẫn hợp lý.

Guard `node tools/test_home_error_table.js` chạy **trọn một run** qua UI (đặt tên → confirm →
start → chờ hết) vì trạng thái cần kiểm chỉ tồn tại ở cuối một run; mọi lối tắt tới đó là kiểm một
fixture chứ không phải kiểm luồng.

### Hai lỗi trong chính guard của tôi

- `test_error_table.js` bấm nút bằng `getElementById(...).click()` — **cách đó chạy được cả trên
  phần tử `display:none`, kích thước 0, hoặc bị che**. Guard chứng minh handler chạy, **không**
  chứng minh người dùng bấm được. Đã thêm phép đo thật (kích thước, `display`/`visibility`/
  `opacity`, và hit-test bằng `elementFromPoint`).
- Phép đo đó **đỏ ngay lần đầu** với `topmost:false` ở cả 3 kích thước — nhưng **không có gì che
  nút cả**: `elementFromPoint` nhận toạ độ **viewport** và trả `null` cho điểm ngoài màn, mà card
  chart đẩy nút xuống dưới nếp gấp. Phải `scrollIntoView` trước, đúng như người dùng làm.
