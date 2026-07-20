# 2026-07-17 — Đặt tên bệnh TRƯỚC heating + chọn từ danh sách bệnh cố định

Đổi quy trình Amplification: `nhấn Amplification → đặt tên bệnh → heating → start`
(trước đây đặt tên **sau** heating ở `waitamp`). Và tên slot giờ chọn từ **tập bệnh tôm
cố định** thay vì gõ tự do.

## 1. State machine: thêm `ewaitname` (đặt tên trước heating)

Yêu cầu là "đặt tên **rồi mới** heating" (cổng chặn, không chạy song song) → cần state
firmware mới, không làm được bằng web thuần.

- `displayCLD.h`: thêm enum `ewaitname` (cuối enum, không xê dịch giá trị cũ).
- `button.cpp` RED: `escreenStart` → `ewaitname` (**KHÔNG** heating — trước đây vào thẳng
  `eheating67`). `ewaitname` + RED → `eheating67` (setPreheat67 + sensor preheat — phần
  heating **dời** từ escreenStart sang đây).
- `button.cpp` WHITE: `ewaitname` → `escreenStart` (huỷ, chưa heating gì).
- `displayLCD.cpp`: `case ewaitname` vẽ `waitAmpTube()` (màn chờ; người dùng vật lý bấm
  RED để bắt đầu).
- `webDashboard.cpp`: `fillStatus` → `phase "waitname"`; `fillActions` → red
  "Confirm & heat", white "Return".

**Người dùng vật lý (không web)**: `ewaitname` thành một bước "bấm RED để bắt đầu" thừa ra
(tên mặc định #1-#10). Chấp nhận — đó là cái giá của tính năng đặt-tên-trước.

## 2. Web: naming ở `waitname` + `waitamp`

- `renderHome`: `naming = (phase=="waitname" || phase=="waitamp") && !confirmed`.
  `confirmed` **sống xuyên heating** (chỉ reset khi `phase=="idle"`), nên named-ở-waitname
  thì waitamp không hỏi lại. `resetView` cũng chạy khi vào `waitname`.
- Nút **Confirm** phân biệt theo phase: `waitname` → apply names + `POST /control?btn=red`
  (bắt đầu heating); `waitamp` → mở khoá Start như cũ. Nhãn nút + tiêu đề card đổi theo phase.

## 3. Chọn bệnh từ danh sách cố định

`DISEASES = ["PC","EHP","EMS","WSSV","TPD"]` (bệnh tôm). Ô tên → `<select>` (bấm "xổ" ra 5
lựa chọn + ô trống `—`). Ô trống → mặc định `#N`.

Dùng `<select>` (không popover chip tự chế) để **giữ nguyên mọi plumbing**: `.slot-name`
value, `onRename`, đồng bộ 2 bảng, `applyNamesTo`, `fitNameColumn` — tất cả chạy y nguyên.
Bền trên mobile (native picker), không vỡ layout bảng. Bệnh đã gán hiện như chip điền
(`.assigned`), ô trống mờ. Tên lạ (legacy free-text) thêm làm option để không mất.

## 4. Bug race sửa kèm (review finding #10, giờ cắn thật)

`loadNamingSlots`/`loadResultSlots` build **2 pha**: `buildTable(rỗng)` ngay + fetch
`/slots` rồi `buildTable(data)`. Chọn bệnh **trong** khe fetch → rebuild đè mất lựa chọn +
reset `slotNames`. Vô hại với gõ text (khe hẹp) nhưng **cắn với select** (chọn tức thì).
Fix: build **1 lần** sau khi `/slots` về, bỏ pre-build rỗng (offline mới build rỗng để vẫn
hiện 10 dòng).

## Kiểm chứng

- `pio run` → SUCCESS (RAM 22.9%). ASCII OK (em-dash dựng runtime `String.fromCharCode`).
- Mock + CDP (11/11 PASS): Amplification → **waitname** (card bệnh, chưa heating, 5 lựa
  chọn đúng) → chọn EHP+WSSV (chọn **tức thì**, không bị race đè) → Confirm → heating →
  waitamp (không hỏi tên lại, Start mở) → Start → amplification (series 1=EHP, 4=WSSV).
- **Máy thật** (192.168.1.10): script.js có disease picker; red → `waitname` (chưa
  heating, busy, nút "Confirm & heat"); white → huỷ về idle.

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t uploadall` (hoặc `upload`+`uploadfs`).
