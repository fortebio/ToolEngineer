# Phát hành v2.4.3 qua OTA — file cần đẩy lên repo OTA

**Chưa đẩy gì cả.** Đây chỉ là các file soạn sẵn. Xem
[docs/plan/2026-07-28-ota-fleet-upgrade-243.md](../../docs/plan/2026-07-28-ota-fleet-upgrade-243.md)
mục 1 (ma trận publish).

## Chặn cứng trước khi đẩy

1. **Rotate ingest Bearer + ERP X-API-Key, redeploy GAS deployment mới**, rồi cập nhật
   `src/secrets.h` và **build lại**. `strings` trên `.bin` cho ERP key @offset 3144 và ingest
   Bearer @3251 — nằm trong 4 KB đầu, không cần dịch ngược. Bản đã publish (v2.4.0) **không**
   chứa 2 token này ⇒ đẩy `.bin` của cây hiện tại lên repo public **là lần lộ đầu tiên**.
2. **Gate go/no-go**: `uxTaskGetStackHighWaterMark(NetworkTask)` sau một lần OTA thật.
   Stack bị hạ 8192→6144 mà phải chứa mbedTLS + HTTPClient + Update; tràn là **panic**, không
   phải `HTTP_UPDATE_FAILED` — cả cơ chế phát hành tự nó hỏng.

## Vì sao đẩy lên branch của version CŨ

`baseUrl` gắn version **đang chạy** (`src/updateOTA.cpp:11`):

```text
https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/<FirmwareVer đang chạy>/
```

Máy 2.4.2 hỏi branch `v2.4.2`, máy 2.4.0 hỏi branch `v2.4.0`. Nên bản 2.4.3 phải nằm trên
branch của các version **cũ hơn**, không phải branch `v2.4.3`.

Repo hiện chỉ có `main`, `v2.3.6`…`v2.4.0` → **không có `v2.4.2`** ⇒ máy 2.4.2 ngoài đồng đang
nhận **404** mỗi lần boot. Đó là lý do OTA "im lặng" lâu nay.

| Branch | Trạng thái | Phủ máy nào | `versionCode` |
| --- | --- | --- | ---: |
| `v2.4.0` | đã có, cập nhật JSON | v2.4.0 (`currentVersion` 16) | 19 |
| `v2.4.0.x` | **tạo mới** | v2.4.0.x HotlidDisable (16) | 19 |
| `v2.4.2` | **tạo mới** | cả hai bản 2.4.2 (17 **và** 18) | 19 |
| `v2.4.3` | **không tạo** | máy đã lên 2.4.3 (19) | — |

Mỗi branch cần **2 file ở gốc**: `updateOTA.json` (trong thư mục này) + `firmware.bin`
(`.pio/build/esp32dev/firmware.bin`, **sau khi đã rotate token**). `fileName` là đường dẫn
tương đối trên **cùng branch đó**.

`httpUpdate` **tắt follow-redirect** mặc định (`HTTPUpdate.cpp:38`) → URL phải trả **200 trực
tiếp**, đó là lý do dùng `raw.githubusercontent.com` chứ không phải link release.

## Không tạo branch `v2.4.3`

Máy đã lên 2.4.3 chạy `currentVersion = 19`. Nếu branch `v2.4.3` có `versionCode` > 19 thì mọi
máy sẽ prompt lại mỗi lần boot; nếu = 19 thì không prompt (`>` chứ không `>=`). Để trống là an
toàn nhất: `checkFirmware()` nhận 404 → `otaCheckFailed`, không prompt gì.

## Cách đẩy (một branch)

```bash
git clone https://github.com/wuanpham/FBTRapidplusOTA && cd FBTRapidplusOTA
git checkout -b v2.4.2 main            # hoặc: git checkout v2.4.0
cp <repo firmware>/tools/ota-release/v2.4.2/updateOTA.json .
cp <repo firmware>/.pio/build/esp32dev/firmware.bin .
git add updateOTA.json firmware.bin && git commit -m "v2.4.3 (versionCode 19)"
git push -u origin v2.4.2
```

Kiểm lại bằng đúng URL mà máy sẽ gọi:

```bash
curl -sI https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/v2.4.2/firmware.bin
# phải là 200, KHÔNG phải 301/302
curl -s  https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/v2.4.2/updateOTA.json
```

## Sau khi đẩy

Cửa sổ OTA lúc boot rất hẹp (~2 s, xem mục 4 của kế hoạch) và `checkFirmware()` **không bao
giờ chạy lại** trong phiên. Máy nào trượt hoài thì tắt/bật vài lần, hoặc dùng
`POST /otaupload?md5=<32 hex>` từ trình duyệt (không cần internet), hoặc đành cầm dây.

Tải thất bại → máy park ở `OTA_FAILED`, **không tự thử lại** — phải power-cycle.
