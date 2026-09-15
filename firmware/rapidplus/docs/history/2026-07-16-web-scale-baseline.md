# 2026-07-16 — Web full-width + chart trừ baseline

Hai chỉnh sửa client-only (`data/`), không đụng firmware.

## 1. Giao diện scale theo chiều ngang

Trước: desktop cap `.screens { max-width: 940px }` + card chart cap 900px → không giãn
theo cửa sổ rộng (chừa trống bên phải). Sửa (`style.css`):

- `.screens { max-width: none }` → nội dung lấp đầy chiều rộng.
- Bỏ cap chart/process card (chỉ Setting còn cap 900px cho dễ đọc form).

Verify screenshot 1600px: banner/temps/buttons/chart giãn đầy chiều ngang.

## 2. Chart Realtime LAMP Amplification trừ baseline

Chart giờ vẽ **(giá trị - baseline)** để đường khuếch đại xuất phát ~0, thấy rõ phần
tăng (giống cách thiết bị phân tích). Client-side (`script.js plotResult`):

- Mỗi kênh: baseline = trung bình `BASELINE_N` (=5) điểm đầu; plot `y - baseline[i]`.
- `resetAmplification()` reset baseline + xoá đường chart khi **bắt đầu run mới**
  (phase `home` chuyển sang `amplification`, kiểm trong `renderHome`).
- Nhãn trục Y: "Fluorescent - baseline (nm FAM)".

Không cần firmware (baseline tính từ chuỗi `new_readings` sẵn có).

## 3. Trục chart

- **Trục tung**: ẩn tên trục, **hiện số giá trị**, **nấc 5** từ 0
  (`yAxis { title: null, labels: enabled, tickInterval: 5, min: 0 }`). Ngưỡng nền trong
  `plotResult`: hiện là **< 0 → 0** (chỉ kẹp giá trị âm; ban đầu đặt < 5, đã chỉnh xuống 0).
- **Trục hoành hiển thị thời gian**: x = **elapsed phút** kể từ lúc run bắt đầu
  (`(Date.now() - runStart) / 60000`, `runStart` set trong `resetAmplification`),
  `labels: { enabled: true }`, title "Time (min)". Khớp cách thiết bị đo theo phút.
  > **Đã thay đổi** (xem [2026-07-17-chart-backfill.md](2026-07-17-chart-backfill.md)):
  > `runStart`/`Date.now()` bỏ hẳn, x giờ = **chỉ số vòng đo của thiết bị** để backfill
  > và điểm live cùng một gốc thời gian.

## 4. Làm mượt đường (smooth) — HIỆN ĐANG TẮT

Đã thêm **moving-average** (window `SMOOTH_W`=5) trên giá trị đã trừ baseline; buffer
`smoothBuf[10][]` reset trong `resetAmplification`. Dùng moving-average (giữ type `line`),
KHÔNG spline để tránh overshoot làm sai lệch dữ liệu khuếch đại.

**Trạng thái: tạm comment out theo yêu cầu** — `plotResult` vẽ thẳng giá trị đã trừ
baseline (`var sm = y - baseline[i]`). Khối moving-average giữ nguyên dạng comment ngay
dưới để bật lại dễ; `SMOOTH_W`/`smoothBuf` vẫn khai báo.

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs` (không cần `upload`).
