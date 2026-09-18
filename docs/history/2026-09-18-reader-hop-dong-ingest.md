# 2026-09-18 — Hợp đồng ingest cho Forte Rapid Reader (bước 1 của kế hoạch gửi dữ liệu)

Phần monorepo (`system/` + `server/`) của kế hoạch
`firmware/FBT-Reader/docs/plan/2026-09-18-gui-du-lieu-engineer-server.md`. Phần firmware (bước 2)
nằm ở repo FBT-Reader, chưa làm.

## Bốn quyết định (Kane, 2026-09-18)

| # | Câu hỏi | Chốt |
| --- | --- | --- |
| 1 | Token ingest: cổng web → EEPROM hay `secrets.h` biên dịch? | **Cổng web → EEPROM** (chốt lần đầu `secrets.h`, đổi cùng ngày): OTA v2.6.8 ([2026-09-18-reader-ota-server.md](2026-09-18-reader-ota-server.md)) đã lưu cùng Bearer `RECEIVER_TOKEN` vào EEPROM qua `POST /api/token`; một token một nơi lưu, và `.bin` v2.6.8 còn phải phát qua GitHub public một lần nên không biên dịch token vào |
| 2 | Tiền tố mã máy Reader | **`RE`** (= mặc định firmware), thay `RDR` đoán trước đó |
| 3 | Gửi cả lần đo lỗi cảm biến? | **Có** (`verdict: "E"`) |
| 4 | Mốc nhóm bằng `group_id` thay hàng `ST`/`EN`? | **Có** (Sheet vẫn giữ `ST`/`EN`) |

## Trước → nay

| File | Trước | Nay |
| --- | --- | --- |
| `system/contracts/ingest-reader.schema.json` | không có | **Mới.** Cùng quy ước N khe của rapid4p, `slots` ghim 1: `slot_result`/`slot_positive`/`calib_min`/`calib_max` 1 phần tử; `readings`/`readings_ok` 3 phần tử (3 lần đọc thô); `verdict` `P/N/E`; `group` enum `Prawn/Fish/Pig/Chicken` (đúng chữ `Sample.cpp`); `sick` enum 8 khoá bệnh; `slope/origin/rsq` scalar riêng Reader; `group_id`; `time` `DD-MM-YYYY HH:MM:SS` |
| `system/products.yaml › reader` | `id_prefix: RDR # TODO`, `optical_slots: null`, `payload.contract: null`, `endpoints` không có ingest | `id_prefix: RE`, `optical_slots: 1`, `temp_channels: 0`, `payload.contract` + `array_fields: [slot_result, slot_positive, calib_min, calib_max]`, `endpoints: [engineer_ingest, engineer_ota_check, google_apps_script]`. `registry_check.py` ĐẠT, reader hết cảnh báo TODO (trừ `hw: []`) |
| `server/deploy/fbt-receiver.env.example`, `server/app/config.py` (comment) | `RDR=reader` | `RE=reader` |
| `server/docs/data_sample/data_reader.json` | không có | Mẫu payload chuẩn, qua schema |
| `server/tests/test_logic.py::test_validate` | chỉ case rapid4p N khe | thêm Reader `slots:1`: hợp lệ / `slot_result` 3 phần tử → lỗi / `calib_min: []` → lỗi |
| `server/tests/test_api.py` | — | `test_ingest_reader_theo_hop_dong`: mẫu qua `jsonschema` → `POST /reader/results` 200, file `RE0012_…`, `type_Upload == reader_result`; mảng sai → 400; `verdict:"E"` → 200 |

**Server không sửa code**: catch-all `POST /{path}` + `validate()` N khe (2026-09-17) đã nhận
payload này. Test: **117 xanh** (venv scratchpad: fastapi/httpx/pytest/jsonschema/psycopg).

## Bằng chứng đã đối chiếu trước khi viết hợp đồng

- `server/app/logic.py::validate` chỉ đòi `id_device`; có `slots` thì mọi mảng trong
  `SLOT_ARRAY_FIELDS` phải đúng `slots` phần tử; `readings` không nằm trong đó nên 3 phần tử
  không bị đòi = 1.
- `payload_time()` parse `DD-MM-YYYY HH:MM:SS` (regex `_ALT_RE`) → `received_at` = giờ đo;
  `"N/A"` → `now()`.
- 500 chỉ khi file **và** DB cùng hỏng (`main.py` ingest) → firmware retry 5xx an toàn; `-11`
  (đã gửi xong body) không retry vì `insert_session(dedup=False)` sẽ nhân đôi hàng.
- Firmware Reader: `animals.groupName` là `Prawn/Fish/Pig/Chicken` (không phải `PRAWN` như bản
  nháp kế hoạch); `threshold` uint16; `sampleNames` char[15].

## Còn lại (không thuộc bước này)

- **Box**: `/etc/fbt-receiver.env` `OTA_LEGACY_PRODUCT_BY_PREFIX=RE=reader,RPL=rapidplus` + restart
  (sudo, người dùng tự làm). Nhân tiện so `md5sum app/logic.py app/config.py` trên box với cây làm
  việc — `validate()` N khe có thể chưa deploy (`server/CLAUDE.md` 2026-09-18); không chặn Reader
  nhưng lưới "mảng sai → 400" chỉ có sau khi deploy.
- **Firmware** (repo FBT-Reader, bước 2): `engineerPostResult()` trong `engineerApi.cpp` (đã có
  từ OTA v2.6.8), `NetJob` snapshot + hàng đợi 8 + đếm rơi, `ADDR_GROUP_ID 216`, fallback
  `id_device` mặc định → `RE-<6 hex MAC>`, `/api/status` thêm `srv/drop/sent`. Token dùng nguyên
  `engineerAddAuth()` (EEPROM) của OTA v2.6.8.
- `hw` của Reader vẫn `[]` — firmware gửi `hw: ""` tới khi có người xác nhận PCB.
