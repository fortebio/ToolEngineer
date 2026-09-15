# 2026-07-17 — Ẩn thanh nav khi đang chạy lysis/amp

Vào quy trình lysis hoặc amplification → **ẩn bottom nav** (desktop: sidebar), người vận
hành ở lại Home. Chạy xong → nav trở lại.

Client-only, **không đụng firmware**: đã có sẵn `status.busy` + `status.calib` từ
webDashboard.

## Điều kiện: `busy && !calib`, không phải `busy`

Ẩn theo `busy` trần sẽ ẩn luôn lúc **calib** — mà wizard calib **nằm trong tab Setting**,
ẩn nav là **nhốt người dùng** trong đó, không ra được. Nên loại calib ra.

| Trạng thái | Nav |
| --- | --- |
| idle / finished | hiện |
| lysis, preheat, waitamp, amplification | **ẩn** |
| calib (mọi bước) | hiện — wizard ở tab Setting, phải với tới được |

## Hai chi tiết dễ sót

1. **Thu hồi khoảng trống của nav.** Chỉ `display:none` là còn chừa chỗ: mobile có
   `body { padding-bottom: var(--nav-h) }`, desktop có `padding-left: var(--side-w)` vì
   nav **chính là** sidebar. → `body.nonav` bỏ cả hai.
2. **Run có thể bắt đầu khi user đang ở tab Result/Setting.** Ẩn nav lúc đó = kẹt ở tab
   không thoát được. `applyRunNav()` chuyển về Home **trước** khi ẩn.

## Kiểm chứng (Edge headless + CDP, 10/10 PASS)

- idle: nav hiện, không có `.nonav`
- đang ở Result → bắt đầu lysis → nav **ẩn**, user **tự về Home**,
  `padding-bottom = 0px`
- amplification: vẫn ẩn
- run xong: nav **trở lại**
- đang calib: nav **vẫn hiện**

## Nạp

Chỉ đổi `data/` → `pio run -e esp32dev -t uploadfs` (không cần `upload`).
