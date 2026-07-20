# 2026-07-17 — Nút green/red/white hiển thị chức năng theo trạng thái

Thay vì hiện tên màu cố định ("GREEN/RED/WHITE"), chip nút giờ hiện **chức năng của nút
đó tại quy trình hiện tại**. VD màn hình bắt đầu (`escreenStart`): green = "Lysis",
red = "Amplification", white = không có (chip mờ).

## Vì sao firmware phải gửi

Client chỉ nhận `status.phase` (thô: heater/amplification/idle/finished) — không đủ để
suy ra nút làm gì. Chức năng nút phụ thuộc **chính xác `type_infor`**, mà state machine
nằm ở firmware. Nên firmware map và gửi kèm.

## Thay đổi

- `webDashboard.cpp` — `fillActions(JsonObject, e_statuslcd)` map từng state → nhãn 3 nút
  (mirror `handleShortPress_*` trong `button.cpp`); thêm `actions{green,red,white}` vào
  payload event `home`.
- `data/script.js` — `chipLabel(id, label)` set text chip theo `d.actions`; rỗng → "-" +
  class `noact`.
- `data/style.css` — `.btn-chip.noact { opacity: .45 }` (mờ khi nút không có tác dụng).
- `tools/sse_test_server.py` — mock gửi `actions` theo pha để test được.

## Map hiện tại (theo state machine)

| State | green (B_BLUE) | red | white |
|---|---|---|---|
| `escreenStart` | Lysis | Amplification | - |
| `ewaitLysisTube` | - | Start lysis | Return |
| `ewaitphase2` | Amplification | - | Return |
| `epreheat67` | Skip preheat | - | Return |
| `ewaitampTube` | - | Start | Return |
| `escreenFinished` | - | Errors | Next test |
| `escreenReview` | - | Errors | Return |
| `eSettingMenu` | WiFi | Upload | Back |
| `eUpdateOTA` | Dismiss | Update | - |
| `errprocess` | - | - | - (nút bị đóng băng khi lỗi) |
| còn lại (đang chạy) | - | - | Return |

Text tĩnh "GREEN/RED/WHITE" trong HTML giữ làm fallback khi chưa có SSE (offline).

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t upload` **và** `-t uploadfs`.
