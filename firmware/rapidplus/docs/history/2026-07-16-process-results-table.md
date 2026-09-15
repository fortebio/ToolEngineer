# 2026-07-16 — Tab Process: bảng kết quả + đặt tên slot + ẩn/hiện

Thêm tính năng cho tab Process của web dashboard: bảng kết quả 10 slot, sửa tên slot
(lưu trên thiết bị), ẩn/hiện slot, và nút "View chart" (đồ thị ẩn đến khi đặt tên xong).

## Client (`data/`)

- `index.html` — Process section: card **bảng slot** (`Show` / `Slot` / `Name` / `CT` /
  `Result`) + nút **View chart**; card chart thêm `id="chartCard"` + class `hide`.
- `style.css` — `.slot-table`, `.slot-name`, badge `.res-P/N/S/E/B`, tiện ích `.hide`.
- `script.js` — `loadSlots()` (GET /slots) dựng bảng; `onRename` (POST /rename) +
  `series.update({name})`; `onToggle` (localStorage + `series.setVisible`); nút View chart
  hiện chart + áp tên/ẩn-hiện. Deep-link tab qua URL hash (`/#process`).

## Firmware

- `webDashboard.cpp` — `GET /slots` → `{ready, slots:[{name, ct, result}×10]}` (tên từ
  `/slotnames.json` LittleFS; ct/result từ cache). `POST /rename?slot=N&name=X` → ghi
  `/slotnames.json`. `dashboardSetResults(ct, result)` cache kết quả.
- `displayLCD.cpp screen_Result()` — gọi `dashboardSetResults(CT_value, result)` sau khi
  tính kết quả (cả nhánh 'f' upload lẫn nhánh local).

Compile SUCCESS (RAM 22.8%, Flash 68.2%).

## Bug desktop có sẵn (đã fix cùng lúc)

Khi test chuyển tab trên desktop phát hiện: media query `#screen-home { display: grid }`
(selector ID) đè `.screen { display: none }` → **Home hiện trên MỌI tab** ở màn ≥820px.
Sửa: `#screen-home.active { ... }` (gate bằng `.active`). Đã verify bằng screenshot.

## Quyết định thiết kế

- **Tên lưu LittleFS** (`/slotnames.json`), KHÔNG EEPROM: vùng EEPROM chật + "reserved"
  (record ở 1024 có dấu hiệu tràn), nhét tên dễ đụng settings. LittleFS đã mount sẵn, JSON
  dễ, sống qua reboot. Đánh đổi: `uploadfs` xoá → firmware tự tạo lại rỗng. (Muốn EEPROM
  thật thì làm sau.)
- **Ẩn/hiện = client-only** (localStorage) — chỉ tên là dữ liệu thiết bị, ẩn/hiện là ý
  thích xem của từng client.
- **Kết quả (CT/P/N/S)** chỉ có sau khi 1 run xong (`screen_Result` cache). Trước đó bảng
  hiện tên + ẩn/hiện, cột kết quả trống.

## Cập nhật: cỡ cột bảng (client-only)

- `table-layout: fixed` + JS `fitNameColumn()` đo tên dài nhất (hidden span) rồi set
  width cột Name. Các cột SLOT/CT/RESULT không set width → tự chia đều phần còn lại;
  SHOW cố định 3.2rem (checkbox).
- `loadSlots()` dựng 10 hàng ngay (placeholder) rồi `/slots` điền vào — khỏi nhấp nháy trống.
- Ô value clamp `overflow:ellipsis`; header không clip.

## Design pass tab Process (Clinical Light)

- **Canh giữa** giá trị ngắn (Show/Slot/CT/Result) trong cột rộng — trước canh trái nên
  lạc lõng; cột Name giữ canh trái (nội dung dài).
- **Hover sáng hàng** (`tbody tr:hover`), hàng cuối bỏ viền dưới.
- **Checkbox** dùng `accent-color: var(--blue)`, 15px, canh giữa.
- **Input tên**: nền dịu (#fbfcfe), bo 7px, transition viền; hover/focus rõ ràng.
- Số slot in đậm muted; header canh giữa, letter-spacing rộng hơn.
- **Zebra rows** (`tr:nth-child(even)`), hover đặt SAU zebra để đè lên.
- **View chart** thành CTA chính: icon nhịp + `#viewChartBtn` riêng (đậm, đổ bóng xanh,
  hover nâng bóng, `:active` scale) thay cho `.reload-btn` dùng chung.
- **Chart card**: gộp title + "Last update" vào `.card-head` (flex, title trái / meta phải).

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t upload` **và** `-t uploadfs`.
Riêng các tinh chỉnh UI sau (cỡ cột, baseline, smooth, trục, design pass) chỉ cần `-t uploadfs`.
