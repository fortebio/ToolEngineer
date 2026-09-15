# 2026-07-22 — View Chart lần đầu trống, phải reload web (poll review hết giờ quá sớm)

## Triệu chứng

Web app → Result → bấm **View Chart** lần đầu: **chart trống**. Phải **reload trang** mới xem
được đường cong run cũ.

## Điều tra — hai giả thuyết đầu đều SAI (đo trên máy thật bác bỏ)

Đo bằng CDP (Edge headless) thẳng vào thiết bị `192.168.1.14`:

| Giả thuyết | Đo được | Kết luận |
| --- | --- | --- |
| `highcharts.js` không load kịp → `makeChart` trả null | `typeof Highcharts = object`, `resultView.chart` có | ❌ sai |
| Chart tạo lúc container ẩn → plot size 0 | `plotWidth = 365` (thật) / `510` (mock) | ❌ sai |

Mock (`test_review_reboot.js`) luôn PASS vì nó chỉ kiểm `series.data.length`, **không kiểm
kích thước** — nên đã bổ sung assert `plotWidth > 50` vào test.

## Reproduce + số đo quyết định

Reset máy (cache RAM trống → `/slots ready=false`, `/curve count=0`) rồi bấm View Chart ngay:
→ **`points: 0`**, và reload cũng 0 → **tái hiện đúng bug**.

Đo thời gian review thật:

```
POST /reviewlast -> {"ok":true,"queued":true}
>>> review became ready after 8101 ms      # ~8,1 GIÂY
```

Client chỉ chờ **15 × 200 ms = 3 giây** (`reviewStoredRun` poll, data/script.js) rồi **bỏ cuộc**.
Comment cũ ghi *"SettingTask drains in ~10ms"* — sai thực tế ~800 lần: review phải đọc record
EEPROM **và chạy lại `bResultGet`** (thuật toán phát hiện trên cả 10 slot).

**3 s < 8,1 s → client bỏ cuộc trước khi data về → bảng/chart trống. Reload thì cache đã sẵn
(`/curve count=120`) nên hiện ra ngay** — đúng như người dùng mô tả.

## Fix (chỉ `data/script.js`)

1. **Tăng ngân sách poll**: `15 × 200ms (3s)` → **`60 × 250ms (15s)`**, dư trên mức đo 8,1 s.
   Khi review xong, poll thấy `ready` → `if (resultShown) loadCurve(resultView)` tự vẽ lại.
2. **Chỉ báo chờ**: trong `loadCurve`, nếu `/curve` trả 0 điểm mà đang `reviewing` →
   `chart.showLoading("Loading stored run…")`, ngược lại `hideLoading()` — để người dùng đợi
   thay vì tưởng hỏng rồi reload (reload chính là thứ che mất bug).

## Kiểm chứng (máy thật, cùng điều kiện)

| | Trước fix | Sau fix |
| --- | --- | --- |
| `/curve` lúc bấm View Chart | 0 | 0 |
| Sau khi chờ | **points 0** (phải reload) | **points 120**, plotWidth 365 ✓ |

- `node tools/test_review_reboot.js` → **ALL PASSED** (thêm assert `plotWidth > 50`).
- Nạp `uploadfs` COM18.

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs --upload-port COMxx`.
