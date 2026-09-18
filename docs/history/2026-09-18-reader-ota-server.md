# 2026-09-18 — Reader lên OTA qua Engineer Server (firmware v2.6.8)

Chạm 3 phần: `firmware/FBT-Reader` (repo riêng — commit bên đó), `system/products.yaml`,
`server/tests`. Nhật ký chi tiết phía firmware (bằng chứng, phương án đã loại, đợt chuyển
giao): `firmware/FBT-Reader/docs/history/2026-09-18-ota-engineer-server.md`.

## Trước thế nào

Server đã có kho OTA theo sản phẩm `products/reader/` từ 2026-09-11 (`server/app/ota.py`) và
test `test_ota_products.py` đã dùng khoá `reader` làm ví dụ — nhưng **không máy Reader nào
gọi server**: firmware v2.6.7 vẫn đọc `raw.githubusercontent.com/wuanpham/FBTRapidReaderOTA/
<FirmwareVer>/updateOTA.json`, không xác thực, không `x-MD5`, không tự khai version. Registry
ghi `endpoints: [google_apps_script]`, `embeds_image_tag: false`.

## Nay thế nào

| Phần | Thay đổi |
| --- | --- |
| `firmware/FBT-Reader` | v2.6.8: `src/updateOTA.cpp` + `src/engineerApi.cpp` mới — `GET /ota/check?device&ver&product=reader&hw[&updated=1]` + Bearer (token nhập qua web, EEPROM), 2 host `hub.fortebio.tech` → `fbt.basa-luma.ts.net`, so `ver` == `FIRMWARE_VERSION` khớp chính xác, poll 6 h, thẻ `FBTIMG1;product=reader;ver=…;hw=;;` nhúng `.bin`, web `POST /api/token` + `POST /api/ota`. Xoá kênh GitHub khỏi code. Flash 80,9 → **81,7 %**. **Chưa nạp máy thật.** |
| `system/products.yaml › reader` | `version_source.regex` theo macro `#define FIRMWARE_VERSION`; `embeds_image_tag: true`; `endpoints: [engineer_ota_check, google_apps_script]`; chú thích `hw: []` → firmware gửi `hw=` rỗng tới khi chốt PCB (điền registry **và** `define.h` cùng lúc). `registry_check.py` ĐẠT, ảnh kỳ vọng `reader_v2.6.8.bin`. |
| `server/tests/test_ota_products.py` | `test_hop_dong_firmware_reader`: ghim đúng 3 request firmware gửi (upload không tên → `reader_v2.6.8.bin`, `hw=None`; nhầm kho `rapidplus` → 400; check `hw=` rỗng → `ver`/`url`; `&updated=1` → `fw_log how:"update"`, `fw_seen product:"reader"`; tải có `x-MD5`, thiếu Bearer 401; không khai product → tiền tố `RE` không có trong map → kho legacy trống). 97 pass. **Server không sửa code.** |
| `CLAUDE.md` gốc | dòng bản đồ FBT-Reader: v2.6.8, OTA qua server |

Hợp đồng đã kiểm bằng **chính `firmware.bin` build ra** qua `TestClient` của server (script
tạm trong scratchpad, không commit): server đọc thẻ ở offset 10 644, tự đặt tên
`reader_v2.6.8.bin`, từ chối khi đẩy vào kho `rapidplus`.

## Quyết định

- **Token qua web → EEPROM, không `secrets.h`** (khác rapidplus): ảnh v2.6.8 phải lên repo
  GitHub public cho đội máy v2.6.7 tải một lần (chúng chỉ biết đường đó). Sau đợt này mọi
  `.bin` chỉ nằm sau Bearer trên server → lúc đó mới xét lại, cùng với nợ GAS URL.
- **Không giữ GitHub làm dự phòng khi chưa có token**: một đường, không mang theo hai cái bẫy
  cũ; máy chưa token thì `/api/status` báo `tok:false`, hai đường còn lại (web upload, cáp)
  không đổi. rapid4p cũng chỉ một đường.
- **So `ver`** (như rapid4p), tên file chỉ là đường lùi khi ảnh không có thẻ.

## Việc còn lại (ngoài phạm vi commit này)

1. Nạp một máy bench, đi 5 tình huống (chưa token / 401 / có bản mới / poll / web update khi
   đang đo → 409), đo stack `Net_Task` sau một lần OTA thật.
2. Đợt chuyển giao: tag `fw/reader/v2.6.8`, upload kho `reader` trên app, xuất bản
   `firmware.bin` + manifest `versionCode ≥ 2` lên **nhánh `v2.6.7`** của
   `wuanpham/FBTRapidReaderOTA`.
3. QĐ 2 của kế hoạch ingest (mã máy thật `RE`/`RDR`?) — máy tự khai `product=reader` nên
   sai tiền tố không còn ảnh hưởng OTA, chỉ còn `/devices`.
4. App *Quản lý máy*: kiểm chọn kho `reader` + upload không tên (server đặt tên) trên bản
   web đang phát hành.
