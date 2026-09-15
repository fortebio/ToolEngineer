# Hồ sơ nghiệm thu ATE trên FBT Home Server

Phần **server** của kế hoạch trạm ATE. Bản đầy đủ (P0→P5, gồm app + firmware + jig) nằm ở repo app:
`03. FBT-ToolRapidPlus/docs/plan/ate-san-xuat.md`; tài liệu thiết kế phía app: `docs/08-tram-san-xuat-ate.md`.

Trích sang đây theo đúng luật 1 của repo này (kế hoạch phát triển → `.md` trong `docs/plan/`).

## Server chịu trách nhiệm gì

Đúng hai việc, không hơn:

1. **Nhận** hồ sơ trạm đẩy lên và giữ nó **bất biến** (`PUT /ate/records`).
2. **Trả lời ba câu hỏi**: máy này có hồ sơ gì (`/ate/sn/{sn}`), khoảng thời gian này chạy ra sao
   (`/ate/records`, `/ate/stats`), và đang chấm theo bộ ngưỡng nào (`/ate/limits`).

**KHÔNG** chấm PASS/FAIL. Việc chấm nằm ở app, vì ngưỡng là dữ liệu có version — đổi ngưỡng không được
kéo theo một lần deploy server.

## Trạng thái

| Pha | Nội dung | Trạng thái |
|---|---|---|
| P0 | `/ate/records` (PUT + GET + chi tiết), `/ate/sn`, `/ate/stats`, `/ate/limits` — lưu file | **Xong 2026-09-07**, chờ deploy |
| P1 | (không cần server mới — app gửi thêm bước quang/nhiệt vào `steps[]`) | — |
| P2+ | Có thể cần: chuyển sang bảng `ate_records`, thống kê xu hướng theo số đo | Chưa làm |

## Hợp đồng

| Method | Path | Việc |
|---|---|---|
| `PUT` | `/ate/records` | Trạm đẩy hồ sơ. Idempotent theo `<sn, started_at, sha256(body)>` (= tên file). Trả `{ok, id, sn, verdict}` |
| `GET` | `/ate/records?sn=&verdict=&from=&to=&page=&limit=` | Tra cứu, mới nhất trước, KHÔNG kèm `steps` |
| `GET` | `/ate/records/{id}` | Hồ sơ đầy đủ (`id` = tên file) |
| `GET` | `/ate/sn/{sn}` | Hồ sơ khai sinh (lần PASS đầu tiên) + mọi lần test lại |
| `GET` | `/ate/stats?from=&to=` | FPY, sản lượng theo ngày, Pareto `fail_code` |
| `GET` `PUT` | `/ate/limits` | Bộ ngưỡng hiện hành (PUT gác bằng token admin OTA) |

Body hồ sơ: `{sn, verdict(pass|fail|aborted), limits_ver, steps[], started_at, finished_at, station,
operator, fw_version, fw_sha256, pcb_version, mac, calib, note, app}`; mỗi bước
`{code, name, verdict(pass|fail|skip|info), value?, unit?, min?, max?, detail?, raw?, started_at, took_ms}`.

## Khi nào nên chuyển sang bảng Postgres

Hiện `/ate/records` và `/ate/stats` phải đọc **cả kho** để sắp theo thời gian (tên file bắt đầu bằng số
máy nên xếp theo tên không phải xếp theo thời gian) — có cache theo `(mtime, size)` nên lần sau rẻ.
Chuyển sang bảng `ate_records` (schema đã vẽ sẵn trong kế hoạch app §7.2) khi gặp một trong hai mốc:

- kho vượt ~20.000 hồ sơ, hoặc
- cần truy vấn theo **số đo bên trong `steps`** (vd "vẽ xu hướng dốc gia nhiệt theo lô") — chuyện đó
  jsonb làm tốt, glob thì không.

Chuyển đổi không đụng app: giữ nguyên hợp đồng REST, `id` đổi từ tên file sang số thì app chỉ dùng nó
như một chuỗi đục.
