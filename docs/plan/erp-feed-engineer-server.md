# Kế hoạch: API cho RAPID ERP kéo dữ liệu từ Engineer Server (`/erp/v1`)

> Ngày lập: 2026-09-17 · Trạng thái: **đề xuất, chờ chốt §10** · Phạm vi: `server/` (code) +
> `system/` (registry, contract) + RAPID ERP `fbterp` (repo ngoài, team ERP — phần này là ĐỀ XUẤT
> để họ làm, không phải việc của monorepo).
> Nguồn đã đọc để lập plan: `server/app/main.py`, `db.py`, `logic.py`, `deploy/schema.sql`;
> `firmware/rapidplus/src/Bluetooth.cpp::postJsonToAllTargets`; `system/products.yaml`;
> ERP `backend/app/api/{test_results,device_registry,diagnostics,serial_tracking,production}.py`,
> `migrations/{050_test_results,140_device_fw_provenance}.sql`, `tasks/__init__.py` (beat),
> `services/sheets_sync/ingest.py`.

## 0. Hiện trạng & kết luận định hướng

| | Engineer Server (`hub.fortebio.tech`) | RAPID ERP (`api.fortebio.tech`) |
|---|---|---|
| Nhận kết quả đo | `POST /{path}` catch-all → Postgres `sessions` (+ file `data_plus/`) | `POST /api/v1/results/ingest` → `test_results` + `channel_results`; `method:"error"` → `device_errors` |
| Nguồn kết quả | **Firmware POST trực tiếp** (`SECRET_INGEST_URL`, Bearer) | **Firmware POST trực tiếp** (`SECRET_ERP_URL`, `X-API-Key`) + `sheets_sync` kéo GAS Sheet (fw cũ) |
| Danh tính máy | `id_device` suy từ phiên đo + `fw_seen.json` (máy tự khai ở `/ota/check`) | `device_registry` (instrument_id, SKU, khách, bảo hành, `qc_status`, `software_version` + `software_version_reported_at` — chỉ tươi khi máy **gửi kết quả/lỗi**) |
| Firmware/OTA | Kho OTA theo sản phẩm, `target.json`, `fw_seen` (version/product/hw/at) | không biết OTA; version chỉ cập nhật qua ingest |
| Sản xuất / QC | **Hồ sơ ATE** `/ate/*` (SN, verdict, fail_code, batch, fw_sha256, pcb_version, steps) — lưu file JSON | `device_qc_records` nhập tay (INCOMING_INSPECTION), `qc_status` PENDING_QC/CLEARED/FAILED; `serial_tracking` cấp SN |
| CSKH | `PUT/GET /devices/{id}/logs` (file log máy do CSKH tải lên) | `crm_issues`, `incidents`, `warranty` — không có log máy |
| Kéo dữ liệu ngoài | không | **Celery beat** (`sheets_sync` 5 phút, `sync_notion` 15 phút, `fx` ngày) + bảng state |

**Kết luận:**

1. **KHÔNG đồng bộ lại kết quả đo** — ERP đã nhận thẳng từ máy, cùng payload. Chỉ mở đường
   *backfill* (GĐ3) cho lúc ERP ingest hụt (ERP down/đổi key) vì Engineer giữ bản gốc + `body_sha256`.
2. **Cái ERP thiếu mà chỉ Engineer có** → đó là nội dung feed, theo giá trị giảm dần:
   - **Hồ sơ ATE sản xuất** (máy nào đã qua ATE, PASS/FAIL, lô, firmware/PCB thật) → đóng vòng
     *sản xuất → nhập kho → xuất khách* trong ERP; hiện ERP nhập QC tay.
   - **Trạng thái fleet**: version máy **tự khai** (kể cả máy không chạy mẫu), `product`/`hw`,
     lần check-in gần nhất, target OTA & máy đã/chưa cập nhật → cột "firmware thật" + "đã lâu
     không lên mạng" cho CSKH/bảo hành.
   - **Log CSKH** (metadata + nội dung) → gắn vào ticket bảo hành/incident.
   - **Backfill phiên đo** khi ERP hụt.
3. **Phân vai nguồn sự thật** (đề xuất chốt, trả lời điểm "P2 chốt nguồn sự thật" ở
   `products.yaml`): **ERP = danh tính thương mại** (khách, SKU, bảo hành, SN cấp phát);
   **Engineer = trạng thái kỹ thuật** (firmware thật chạy, product/hw, OTA, ATE, log). Feed **một
   chiều Engineer → ERP**, **ERP chủ động kéo** (pull, Celery beat) — khớp cách ERP đang tích hợp
   mọi nguồn ngoài; Engineer không cần biết ERP tồn tại, không giữ key của ERP, không có retry queue.
4. Khoá ghép: `sessions.id_device` = ATE `sn` = ERP `instrument_id`, so **`LOWER(TRIM())`**
   (ERP `_match_device` đã làm vậy; bỏ qua `"RAPIDPlus"` — máy chưa gán mã).

## 1. Mục tiêu / không-mục-tiêu

**Mục tiêu:** ERP có bức tranh kỹ thuật của từng máy (ATE, firmware thật, OTA, log) **tự động,
idempotent, ≤ 5 phút trễ**, qua một API chỉ-đọc có token riêng, có hợp đồng (schema) trong
`system/contracts/`, không đụng hành vi hiện có của app/firmware.

**Không-mục-tiêu (đợt này):** ERP ghi ngược vào Engineer; bỏ POST trực tiếp firmware → ERP; dời
`sessions` sang ERP; đồng bộ tài khoản/người dùng; webhook/push từ Engineer (pull đủ, thêm push
khi ERP cần < 1 phút).

## 2. Kiến trúc

```
 Máy Rapid+ ──POST kết quả/lỗi──▶ Engineer Server (sessions, fw_seen, ota, ate/, logs/)
      │                                   │
      └──POST kết quả/lỗi (X-API-Key)──▶  │  GET /erp/v1/*  (Bearer ERP_READ_TOKEN, chỉ đọc,
                                          │   since/cursor, JSON theo system/contracts/erp-feed-v1)
                                          ▼
                              RAPID ERP  Celery beat `engineer_sync.poll` (5 phút)
                              ├─ engineer_sync_state (cursor mỗi resource)
                              ├─ device_registry  ← fw thật, product/hw, last_seen, ota_target
                              ├─ device_qc_records ← hồ sơ ATE (procedure_type = ATE_FACTORY)
                              └─ device_logs_ext   ← metadata log CSKH (GĐ3)
```

Nguyên tắc thiết kế API (bám gotcha server): **chỉ GET** (POST catch-all nuốt hết — quy tắc 4
CLAUDE.md gốc); prefix riêng `/erp/v1/` để không đụng route app; mọi resource có **watermark**
(`updated_at` ISO-8601 UTC `Z`) + tham số `since` + phân trang `cursor` để ERP kéo tăng dần; bao
`{"items": [...], "next_cursor": null|"...", "server_time": "..."}`; ID ổn định để ERP upsert
idempotent (`sessions.id`, tên file hồ sơ ATE, tên file log).

## 3. Hợp đồng API `/erp/v1` (Engineer Server)

| # | Route | GĐ | Nguồn trong server | Ghi chú |
|---|---|---|---|---|
| 1 | `GET /erp/v1/health` | 1 | — | `{ok, server_time, feed_version:"1", products:[...]}`; ERP dùng làm đèn "kết nối Engineer" |
| 2 | `GET /erp/v1/devices?since=&cursor=&limit=` | 1 | `db.list_devices()` + `_fw_seen()` + `ota.resolve` | 1 hàng/máy: `device_id, product, product_effective, hw, fw_version, fw_source("check"\|"session"), fw_reported_at, last_seen_at, last_session_at, sessions_count, ota_target_version, ota_status("up_to_date"\|"pending"\|"unknown"), updated_at` |
| 3 | `GET /erp/v1/ate/records?since=&sn=&batch=&verdict=&cursor=` | 1 | `_ate_metas()` (đã có cache mtime) | meta hồ sơ, `id` = tên file; `since` lọc theo `received_at` |
| 4 | `GET /erp/v1/ate/records/{id}` | 1 | `_ate_doc` | hồ sơ đầy đủ kể cả `steps`, `calib` |
| 5 | `GET /erp/v1/ate/sn/{sn}` | 1 | `ate_by_sn` | lịch sử mọi lần thử của 1 SN (first-pass hay chạy lại) |
| 6 | `GET /erp/v1/ate/limits?batch=` | 2 | `ate_limits_get` | tiêu chuẩn lô đã chấm — ERP gắn vào batch record sản xuất |
| 7 | `GET /erp/v1/device-logs?since=&device=&cursor=` | 3 | `_log_meta` | metadata (tên, máy, size, uploaded_at, by, note); không nội dung |
| 8 | `GET /erp/v1/device-logs/{file}` | 3 | `device_log_get` | nội dung text (trần 4 MB) |
| 9 | `GET /erp/v1/sessions?since=&device=&cursor=` | 3 | `db.list_sessions` + `body_sha256` hex | tóm tắt để ERP đối chiếu thiếu |
| 10 | `GET /erp/v1/sessions/{id}/payload` | 3 | `db.get_session` | JSON gốc để ERP tự `_ingest_result` lại (backfill) |
| 11 | `GET /erp/v1/ota/products` | 2 | `ota.list_products` + `target.json` | target chung/ghim theo máy, danh sách .bin (tên, ver, sha256) |

**Quy ước chung:**

- `since`: ISO-8601; trả các bản ghi có `updated_at > since`; **không có `since` = từ đầu**
  (ERP full-refresh mỗi giờ như `sheets-sync-full-refresh`). `cursor` mờ (base64 của
  `updated_at|id`), `limit` mặc định 200, trần 1000.
- Thời gian **luôn UTC `Z`**; ERP hiển thị SGT/VN tự đổi. Giá trị rỗng = `""`/`null`, không bịa.
- Lỗi: 401 sai token, 404 không có, 422 tham số hỏng — cùng bảng mã ERP đang dùng cho external API.
- **Không PII khách hàng** trong feed (Engineer không có); có tên `operator`/`station` ATE (nội bộ).
- Ví dụ `devices` (1 hàng):

```json
{"device_id":"RPL02013","product":"rapidplus","product_effective":"rapidplus","hw":"v3",
 "fw_version":"v2.4.5","fw_source":"check","fw_reported_at":"2026-09-16T02:11:07Z",
 "last_seen_at":"2026-09-16T02:11:07Z","last_session_at":"2026-09-15T09:40:12Z",
 "sessions_count":312,"ota_target_version":"v2.4.5","ota_status":"up_to_date",
 "updated_at":"2026-09-16T02:11:07Z"}
```

- Ví dụ `ate/records` (meta): `{"id":"20260917T081200Z_RPL02077_pass.json","sn":"RPL02077",
  "batch":"L2026-09A","verdict":"pass","fail_code":"","fw_version":"v2.4.5","fw_sha256":"…",
  "pcb_version":"v3","station":"ATE-01","operator":"…","started_at":"…","finished_at":"…",
  "received_at":"…","updated_at":"…"}` (đúng các trường `normalize_ate_record`).

## 4. Bảo mật & vận hành phía Engineer

- **Token riêng, chỉ đọc**: env `ERP_READ_TOKENS` (danh sách `,` → xoay có cửa sổ như
  `RECEIVER_TOKENS_OLD`). Dependency mới `erp_reader()` trong `main.py`: nhận `Authorization:
  Bearer` **hoặc** `X-API-Key` (ERP quen header này; nhận cả hai để client ERP một dòng), so bằng
  `check_auth` (fail-closed: chưa đặt env → 401 toàn bộ `/erp/*`). **Không** cho token thiết bị
  hay `OTA_ADMIN_TOKEN` đi qua `/erp/*` và ngược lại — rò một bên không mở bên kia.
- Ghi vào `deploy/fbt-receiver.env.example` + `HUONG_DAN_DUNG_SERVER.md`; token sinh bằng
  `openssl rand -hex 32`, đưa team ERP qua kênh riêng (không dán vào chat/commit — quy tắc 1).
- Log truy cập: 1 dòng/req `erp_feed route=… since=… n=… ms=…` để thấy ERP có kéo không.
- Tải: ERP kéo 5 phút/lần, ~10 req/lần; `_ate_metas` glob toàn kho mỗi lần nhưng đã cache theo
  mtime — đủ cho vài nghìn hồ sơ. Khi kho ATE > ~20k file → chuyển bảng Postgres (đã dự trù ở
  `server/docs/plan/ate-ho-so-nghiem-thu.md`), API không đổi.
- Public qua Funnel HTTPS như hiện nay; không mở cổng mới.

## 5. Phía ERP (đề xuất cho team ERP — repo `fbterp`)

Bám đúng pattern `sheets_sync` họ đã có:

1. **Config**: `ENGINEER_SERVER_URL=https://hub.fortebio.tech`, `ENGINEER_READ_TOKEN=…`,
   `ENGINEER_SYNC_ENABLED=true` (`.env`, `config.py`).
2. **Beat**: `app/tasks/engineer_sync_tasks.py::poll` (300 s) + `full_refresh` (3600 s), đăng ký ở
   `tasks/__init__.py::beat_schedule`. Worker sync (psycopg2) như `sheets_sync/ingest.py`.
3. **Bảng state** `engineer_sync_state(resource PK, cursor, last_ok_at, last_error, rows_seen)`.
4. **Migration** (một file, idempotent, thuần cộng thêm — đúng phong cách `140_…`):
   - `device_registry`: `hardware_version_reported_at`, `product_key`, `engineer_last_seen_at`,
     `ota_target_version`, `ota_status`, `engineer_synced_at`.
   - `device_qc_records`: `source` (`MANUAL`|`ENGINEER_ATE`), `external_id` **UNIQUE** (= `id` hồ
     sơ ATE) → upsert idempotent; `procedure_type` thêm giá trị `ATE_FACTORY`; `payload JSONB`
     (hồ sơ đầy đủ) cho truy vết.
   - (GĐ3) `device_logs_ext(external_id UNIQUE, device_registry_id, device_id_raw, file_name,
     size, uploaded_at, note, fetched BOOLEAN)`.
5. **Mapping & quy tắc**:
   - `devices` → `UPDATE device_registry` theo `LOWER(TRIM(instrument_id))`; `software_version`
     chỉ đè khi `fw_reported_at > software_version_reported_at` (tái dùng guard của
     `_sync_reported_firmware` — đừng làm version lùi); máy **không có** trong registry → ghi
     bảng/log "unmatched" (như ERP đang làm với orphan results), **không tự tạo** ở GĐ2.
   - `ate/records` → upsert `device_qc_records(source=ENGINEER_ATE, external_id=id)`; **PASS
     lần đầu** của SN chưa có trong registry → *đề xuất* tự tạo `device_registry` với
     `status='IN_STOCK'`, `qc_status='PENDING_QC'` (ATE là bằng chứng đã sản xuất; QC nhập kho
     vẫn do ERP quyết) — **điểm chốt §10.3**. FAIL → không tạo task tự động ở GĐ2 (tránh nhiễu
     `project_tasks`), chỉ hiện trong tab QC của máy.
   - Không bao giờ xoá theo feed (kho ATE là append-only; hồ sơ mất bên Engineer = giữ nguyên ERP).
6. **UI** (tối thiểu): trang Device Registry thêm cột *Firmware thật / Lần cuối online / OTA*, badge
   "Engineer: OK hh:mm" từ `/health`; tab QC của máy hiện hồ sơ ATE (steps) — đọc từ
   `payload`, không gọi Engineer trực tiếp từ trình duyệt (token không ra frontend).

## 6. Registry & hợp đồng (`system/`)

- `products.yaml › external_systems.erp` thêm:
  `engineer_feed: { path: /erp/v1, auth: Bearer ERP_READ_TOKENS, contract: system/contracts/erp-feed-v1.schema.json, poll: 300s }`.
- `system/contracts/erp-feed-v1.schema.json`: JSON Schema cho 4 body GĐ1 (`health`, `devices`,
  `ate_record_meta`, `ate_record`) — **cùng file** được `server/tests/test_erp_feed.py` dùng để
  validate response (jsonschema có sẵn trong venv test) → hợp đồng không trôi khỏi code.
- `tools/registry_check.py`: thêm kiểm `engineer_feed.path` có trong `app.openapi()` (mở đường cho
  snapshot `system/openapi/engineer-server.json` P2 của plan monorepo).
- Đổi feed sau này = `/erp/v2` + schema v2, giữ v1 tới khi ERP chuyển; không sửa nghĩa trường v1.

## 7. Lộ trình

| GĐ | Việc | File chạm | Ước lượng | Tiêu chí xong |
|---|---|---|---|---|
| **0 Chốt** | Trả lời §10; sinh `ERP_READ_TOKENS`; hẹn team ERP | — | 0.5 ngày | §10 có đáp án ghi vào plan này |
| **1 Server** | `config.ERP_READ_TOKENS` + `erp_reader()`; module mới `app/erp_feed.py` (router `/erp/v1`: health, devices, ate/records, ate/records/{id}, ate/sn/{sn}); `since/cursor` helper dùng chung; schema v1 + registry + `registry_check`; tests; env example; `docs/history` | `server/app/{config,main,erp_feed}.py`, `server/tests/test_erp_feed.py`, `system/contracts/erp-feed-v1.schema.json`, `system/products.yaml`, `tools/registry_check.py`, `server/deploy/fbt-receiver.env.example` | 1.5–2 ngày | pytest xanh (không cần Postgres — vá `db` như `test_auth_roles`); `curl` localtest 401 không token / 200 có token; `registry_check` xanh; `openapi.json` trên box có `/erp/v1/*` sau deploy |
| **2 ERP** (team ERP) | beat task + state + migration + mapping devices/ATE + UI cột | `fbterp/backend/...` | 2–3 ngày (họ) | sau 1 chu kỳ poll: máy thật trên registry có `engineer_last_seen_at`; 1 hồ sơ ATE test xuất hiện ở tab QC; chạy poll 2 lần không tạo bản ghi trùng |
| **3 Mở rộng** | `/erp/v1/device-logs*`, `/sessions*` backfill (cần chốt cách băm: ERP băm `raw_payload` **cùng** quy tắc `logic.canonical_sha256` — ghi vào contract), `/ota/products`, cảnh báo "máy > N ngày không lên mạng" → task ERP; snapshot openapi CI | server + ERP | 2 ngày + 2 ngày (họ) | ERP tự phát hiện & nạp bù phiên thiếu; log máy mở được từ ticket bảo hành |

Thứ tự bắt buộc: GĐ1 deploy **trước** (route có, ERP mới test được) → team ERP làm GĐ2 trên
`hub.fortebio.tech` thật với token riêng; không cần môi trường staging chung (feed chỉ đọc).

## 8. Kiểm thử & nghiệm thu

- Đơn vị: `server/tests/test_erp_feed.py` — 401 khi thiếu/sai token, token thiết bị **không** qua
  `/erp/*`, `since` lọc đúng, `cursor` không trùng/không sót khi 2 bản ghi cùng `updated_at`,
  response khớp schema v1, hồ sơ ATE méo không làm 500 danh sách (kế thừa `_ate_metas`).
- Local: `server\scripts\localtest.ps1` thêm `ERP_READ_TOKENS=erplocal`; kiểm
  `curl -H "Authorization: Bearer erplocal" http://127.0.0.1:8080/erp/v1/devices` (đọc token từ
  file/biến, không dán trần — gotcha classifier).
- Sau deploy: `curl -s https://hub.fortebio.tech/openapi.json` có `/erp/v1/*`; team ERP bật
  `ENGINEER_SYNC_ENABLED` → xem `engineer_sync_state.last_ok_at` chạy.

## 9. Rủi ro / gotcha đã biết

- `POST /{path}` catch-all: mọi route feed là GET → an toàn; nếu ngày nào cần ERP ghi ngược → PUT.
- `fw_seen.json` là 1 file sửa tay được, mục méo → `_fw_entry` đã phòng thủ; feed phải đi qua đúng
  hàm đó, đừng đọc file trực tiếp.
- Máy chỉ thấy ở `/devices` khi đã có **phiên đo**; máy mới qua ATE chưa đo → chỉ có ở
  `ate/records` → ERP phải ghép từ cả hai resource (đã tính ở §5.5).
- ATE `sn` gõ tay ở trạm (cố ý không chặn `sn` sai để giữ bằng chứng FAIL) → ERP sẽ gặp SN không
  khớp registry; đó là dữ liệu đúng, cho vào "unmatched", đừng chuẩn hoá bịa.
- Hai image khác nhau từng cùng tên `v2.4.5` → so firmware bằng `fw_sha256` khi cần truy vết,
  không chỉ chuỗi version (đã có trong hồ sơ ATE và manifest OTA).
- Token ERP nằm trong `.env` ERP (đã có `secrets/`); xoay token = đặt 2 giá trị trong
  `ERP_READ_TOKENS`, ERP đổi, rồi bỏ giá trị cũ.
- Feed mở trên Funnel public: chỉ-đọc + token riêng là đủ; nếu muốn siết thêm, giới hạn `/erp/*`
  theo IP ERP ở tầng Tailscale/nginx (P3).

## 9b. So sánh phương án vận hành cho 2 phòng ban (Engineer giữ Engineer Server · Admin giữ ERP)

Bốn phương án khả dĩ; tiêu chí xếp theo thứ tự quan trọng với hai phòng ban **nhỏ, deploy độc lập,
không có người trực chung**:

| Tiêu chí | **A. ERP kéo** (`/erp/v1` chỉ đọc, beat 5 phút) | B. Engineer đẩy (webhook → ERP) | C. ERP đọc thẳng Postgres Engineer | D. A + "ping" (Engineer báo có mới → ERP kéo ngay) |
|---|---|---|---|---|
| Việc phía **Engineer** | API chỉ đọc + schema (1.5–2 ngày) | Outbox + retry + giữ key ERP + xử lý ERP down (3–4 ngày) | Mở DB qua Tailscale + dời ATE file → bảng (2–3 ngày) | A + 1 lệnh gọi fire-and-forget (+0.5 ngày) |
| Việc phía **Admin** | Beat task + state + migration + UI (2–3 ngày, **pattern `sheets_sync` có sẵn**) | Endpoint nhận + idempotent + migration + UI (2–3 ngày, pattern `ingest` có sẵn) | SQL trên schema người khác + mapping (2 ngày, phải học nội bộ Engineer) | A + 1 route nhận ping (+0.5 ngày) |
| Độ trễ | ≤ 5 phút | giây | tuỳ query | giây khi ping; ≤ 5 phút khi ping hụt |
| **Cô lập sự cố** | ERP down → Engineer không biết, ERP lên tự kéo bù. Engineer down → ERP giữ số cũ + cờ "stale" | ERP down → Engineer phải xếp hàng, hàng phình; Engineer down mà không outbox → **mất event** | DB Engineer bận/đổi schema → ERP lỗi ngay; query nặng của ERP → làm chậm Engineer | như A |
| Ai giữ bí mật của ai | Admin giữ **token đọc** của Engineer | Engineer giữ **key ghi** của ERP | Admin giữ **mật khẩu DB** Engineer (rộng nhất) | cả hai giữ của nhau (ping vô hại) |
| Rò bí mật thì lộ gì | đọc feed kỹ thuật | **ghi bịa** hồ sơ QC vào ERP | đọc toàn bộ DB Engineer | như A |
| Debug "thiếu dữ liệu" | **1 chỗ**: `engineer_sync_state.last_error` + log feed Engineer | 2 hàng chờ (outbox Engineer + ingest ERP) — hai bên đổ lỗi nhau | nhìn được ngay nhưng "ai đổi schema" khó quy | như A |
| Đổi hợp đồng | schema có version, thêm trường không phá | như A | **schema DB = hợp đồng ngầm, vỡ im lặng** | như A |
| Hợp với tổ chức | **Engineer = cung cấp, Admin = tiêu thụ**; mỗi bên deploy khi muốn | Engineer phải chạy theo lịch/sự cố ERP | Admin phải hiểu nội bộ Engineer; Engineer mất quyền đổi schema | như A, thêm 1 điểm phối hợp nhỏ |

**Khuyến nghị: A ngay, nâng lên D khi Admin cần < 1 phút** (ví dụ màn kho muốn thấy ATE PASS tức
thì). Ping ở D là *tối ưu*, không phải *đảm bảo* — poll vẫn là đường bảo đảm nên Engineer không cần
retry, không giữ trạng thái. Loại C vì biến schema nội bộ thành hợp đồng liên phòng; loại B vì đẩy
gánh vận hành (hàng chờ, key ghi ERP, chạy theo ERP down) sang phòng có ít người trực nhất.

### Phân vai vận hành (áp cho A/D)

| Việc | Engineer | Admin |
|---|---|---|
| Hợp đồng `/erp/v1` + `erp-feed-v1.schema.json` | **Sở hữu**; chỉ thêm trường, không đổi nghĩa; phá → `/erp/v2`, giữ v1 ≥ 30 ngày | Duyệt thay đổi; ERP **bỏ qua trường lạ** |
| `ERP_READ_TOKENS` | **Cấp & xoay** (lịch 6 tháng hoặc khi nghi rò; luôn 2 giá trị trong cửa sổ xoay) | Lưu `.env` ERP (`secrets/`), đổi trong cửa sổ, báo lại |
| Beat task, mapping, migration, UI ERP | Giải thích nghĩa trường (tài liệu §3) | **Sở hữu** |
| Đèn "Engineer link" + cảnh báo `last_ok_at` > 30 phút | — | **Sở hữu** (dashboard ERP + Telegram bot sẵn có) |
| Danh sách **unmatched** (máy Engineer có mà ERP không, và ngược lại) | Sửa SN gõ nhầm ở trạm ATE khi là lỗi trạm | **Rà hằng tuần**: tạo máy / gán khách / đánh dấu loại bỏ |
| Nguồn sự thật **kỹ thuật** (firmware thật, product/hw, OTA, ATE, log) | **Sở hữu** | Chỉ đọc — UI khoá, ghi "từ Engineer lúc hh:mm" |
| Nguồn sự thật **thương mại** (khách, SKU, bảo hành, SN cấp phát) | Chỉ đọc (khi sau này cần) | **Sở hữu** |
| Dữ liệu test cho Admin | Cung cấp **fixtures JSON** theo schema (Admin chạy test không cần server Engineer) + token đọc riêng cho môi trường dev | Dùng fixtures; thử thật trên `hub.fortebio.tech` |
| Deploy | Độc lập; báo trước 1 ngày nếu thêm trường lớn | Độc lập |

**Runbook "số liệu lệch" (3 bước, ai làm bước nào):**
1. Admin xem `engineer_sync_state` (`last_ok_at`, `last_error`, `cursor`) — 80% ca dừng ở đây (token
   hết hạn, cursor kẹt, ERP worker chết).
2. Admin gọi tay `GET /erp/v1/health` rồi resource lỗi với `since` nhỏ — 200 mà thiếu bản ghi → chuyển
   Engineer; 401/5xx → Engineer.
3. Engineer xem log `erp_feed` + kho (`fw_seen.json`, `ate/*.json`) — nếu Engineer có mà feed không ra
   là lỗi feed; feed ra mà ERP không ghi là lỗi mapping (quay lại Admin).

## 10. Điểm chờ chủ dự án chốt (trả lời rồi mới làm GĐ1)

1. **Pull (ERP kéo, Celery beat) như đề xuất** hay Engineer push webhook sang ERP? (Đề xuất: pull —
   khớp hạ tầng ERP, Engineer không giữ key ERP.)
2. Phạm vi GĐ1 = `devices` + `ate/*` (như bảng §3) — hay cần `sessions` ngay?
3. ATE PASS của SN chưa có trong `device_registry` → ERP **tự tạo** máy (`IN_STOCK`,
   `PENDING_QC`) hay chỉ báo "unmatched" chờ nhập tay? (Đề xuất: tự tạo — tránh nhập tay 2 lần.)
4. Nhận cả header `X-API-Key` cho `/erp/*` (tiện team ERP) hay chỉ `Bearer` (đồng nhất server)?
5. Ai giữ/ xoay `ERP_READ_TOKENS` (đề xuất: chủ dự án đặt trong `/etc/fbt-receiver.env`, gửi team
   ERP; ghi ngày cấp vào `docs/history`).
6. Có mở `ate/limits` + `ota/products` cho ERP ngay GĐ2 để gắn batch record sản xuất không?
