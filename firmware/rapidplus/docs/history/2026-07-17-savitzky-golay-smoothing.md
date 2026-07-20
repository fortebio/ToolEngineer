# 2026-07-17 — Làm mượt chart bằng Savitzky-Golay

Chart amplification (Home live + Result backfill) giờ mượt bằng **Savitzky-Golay bậc 2**,
thay cho việc vẽ thẳng giá trị đã trừ baseline. Client-only (`data/script.js`).

## Vì sao SG (không phải moving-average)

SG khớp một đa thức bậc thấp vào cửa sổ trượt rồi lấy tâm — **lọc nhiễu mà giữ hình dạng
sigmoid và độ cao đỉnh**, không bo tròn peak như trung bình trượt. Bậc 2 khớp `sg_order=2`
mà chính thiết bị dùng trong thuật toán.

## Lõi (closed form, không cần thư viện)

Hệ số SG bậc 2 đối xứng cho nửa cửa sổ `m`:

```
w_j = 3(3m^2 + 3m - 1 - 5j^2) / ((2m+3)(2m+1)(2m-1)),  |j| <= m
```

Tổng = 1 (không lệch mức), và **thu về identity ở m=1**. `SG_HALF = 3` → cửa sổ 7 điểm
(~140s ở 20s/vòng). Precompute `SG_W[1..3]`.

**Xử lý biên = thu nhỏ cửa sổ đối xứng**: tại điểm i dùng `m = min(SG_HALF, i, n-1-i)`.
Ở **mép live** (điểm mới nhất chưa có điểm tương lai) m co về 0 → giá trị raw, không bịa
số cũng không trễ. Điểm cũ **tự mịn dần** khi có điểm mới → nên recompute cả kênh mỗi
điểm (`setData`), không `addPoint` (addPoint không sửa lại điểm đã vẽ).

## Kiến trúc

- Thêm `v.rawY[10][]` vào view: giá trị đã-trừ-baseline **giữ lại** (series chỉ chứa bản
  đã mịn). Reset trong `resetView`.
- `drawSmoothed(v, ch)` = `sgSmooth(rawY[ch])` → noise floor (`<5 → 0`) **sau** khi mịn →
  `setData`.
- `loadCurve`: nạp rawY từ `/curve` rồi `drawSmoothed` từng kênh.
- `plotPoint`: `rawY[ch][idx] = y - baseline` (index theo vòng → re-send ghi đè, không
  nhân đôi) rồi `drawSmoothed` — cả kênh re-smooth, ≤130 điểm/kênh nên rẻ.

Noise floor chuyển ra **sau** SG: kẹp trước sẽ tạo đoạn phẳng giả trước khi mịn.

## Kiểm chứng

- **SG math** (node, dữ liệu nhiễu): hệ số SG5 = `(-3,12,17,12,-3)/35` đúng; mọi cửa sổ
  tổng = 1; giữ **chính xác** đa thức bậc 2; giảm >40% phương sai nhiễu; không NaN.
- **Dữ liệu sensor THẬT** (16 vòng đầu từ máy, `<AmpStart>` user gửi): giảm gồ ghề
  **52–81%** ở 5 kênh, mean lệch 0.04, endpoint không đổi.
- **Trình duyệt** (Edge+CDP, mock full run): cả `plotPoint` (live) và `loadCurve`
  (backfill) chạy không lỗi SG; `rawY` được nạp; chart render đủ 10 kênh.
  (Mock là sigmoid **sạch** nên chênh nhỏ — nhiễu thật mới thấy rõ, xem trên.)

Bản moving-average tạm tắt trước đây ([2026-07-16-web-scale-baseline.md] mục 4) coi như
thay hẳn bằng SG.

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs`.
