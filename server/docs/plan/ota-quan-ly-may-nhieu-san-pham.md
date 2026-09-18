# Phương án — Quản lý MÁY trong OTA nhiều sản phẩm (phần server)

> Viết 2026-09-18 sau đợt rà soát OTA nhiều thiết bị (kiểm thực tế trên server local, xem
> `docs/history/2026-09-18.md`). Nối tiếp [ota-nhieu-san-pham.md](ota-nhieu-san-pham.md) (giai đoạn 0
> — kho tách theo sản phẩm, ĐÃ chạy) và bản đầy đủ ở `apps/fbt_rapid/docs/plan/ota-nhieu-san-pham.md`.
> Giai đoạn 0 tách được **KHO**; file này giải bài toán còn lại: **MÁY** — máy nào thuộc kho nào,
> máy nào đã lên/được mời/chưa poll, và làm sao thấy được điều đó cho nhiều dòng máy trong một bảng.
>
> **Trạng thái: B1 + B2 + B3 ĐÃ CODE + TEST cùng ngày 2026-09-18** (`tests/test_ota_devices.py`, 19 test;
> cả bộ 116/116; kiểm lại 5 lỗ hổng bằng HTTP thật + `rapid4p.bin` thật trên server local) — **CHƯA
> deploy**. B4 (nhóm/lô) và B5 (registry) chưa làm. Hợp đồng đã thực thi ghi ở mục "Bổ sung 2026-09-18"
> của `ota-nhieu-san-pham.md`. Lệch so với bản viết dưới (cố ý, khi làm):
> 1. **app_desc chỉ được coi là thẻ khi `project_name` = kho đích hoặc một kho đã có** — core Arduino
>    nhúng `project_name = "arduino-lib-builder"` (đọc từ `libapp_update.a` của framework-arduinoespressif32)
>    vào MỌI ảnh Rapid+/Reader, và chuỗi đó hợp lệ theo regex khoá; nhận vô điều kiện là mọi upload
>    Rapid+ bị 400 "nhầm kho". Ảnh rapid4p tải nhầm vào `rapidplus` vẫn bị 400 vì kho `rapid4p` đã có.
> 2. `state = waiting` định nghĩa bằng "lượt poll gần nhất KHÔNG được mời đúng bản này" (`offered.file ≠
>    target`), không so `last_check < target_at` — không cần đồng hồ, đúng cả khi target đổi giữa chừng.
> 3. `/ota/{product}/progress` cần DB như `/devices` (máy chỉ có phiên đo cũ vẫn phải được đếm).
> 4. `OTA_REQUIRE_TAG` hạ chữ thường như `LEGACY_PRODUCT`; từ dành riêng/khoá sai bị bỏ im lặng.
> 5. Khoá upload là `_UPLOAD_LOCK` riêng (không kẹp `_CFG_LOCK`) để `/ota/check`+`set_target` không đợi
>    một upload 2,4 MB.

## 0. Hiện trạng đã đo (2026-09-18)

| Dòng máy | Gọi `/ota/check` | Tự khai `product/hw` | Ảnh tự nhận dạng | Kho trên server |
|---|---|---|---|---|
| Rapid+ fleet ~109 máy, `v2.4.5AT1` / `v2.4.5a1` | có (boot + 6 h) | **không** | không (`FBTIMG1` chưa nhúng) | `rapidplus` (theo tiền tố `RPL`) |
| Rapid4P (dev, 1 máy thật) | có (boot + 6 h, **tự nạp + restart**) | có (`rapid4p`, `P4C5-43`) | có sẵn `esp_app_desc_t` (ESP-IDF) | `rapid4p` |
| Reader v2.6.7 | **không** — OTA GitHub raw riêng | — | — | `reader` = kho chết |

Năm lỗ hổng đã tái hiện bằng HTTP thật (`ota_probe.py`, server local):

1. **Kho `rapidplus-a` không máy nào tra.** Máy `a1` không tự khai → rơi về `rapidplus` theo tiền tố;
   đặt target ở `rapidplus-a` là im lặng. Đặt bản chung `fbt_v2.4.6AT1.bin` ở `rapidplus` → máy `a1`
   **được mời nạp bản AT** (khác quy tắc lâm sàng). Cách duy nhất hiện nay: ghim tay từng máy.
2. **`/devices` bỏ máy chỉ có trong `fw_seen.json`** (lấy từ bảng `sessions` rồi mới đắp) → máy vừa
   nạp xong chưa chạy mẫu, Rapid4P chưa có bo cảm biến: vô hình trong bảng tiến độ.
3. **Hai PUT cùng tên đồng thời lách luật 409**: `ota.upload` không khoá, chung file tạm `.tmp-<name>`
   → Windows 500 `PermissionError` (exception trần), Linux bản sau đè.
4. **Ghim mồ côi**: máy ghim ở kho A rồi tự khai kho B → ghim ở A còn mãi, `pinned` vẫn đếm.
5. **Server không biết máy đã "được mời" hay chưa**: `/ota/check` trả `update:true` rồi quên; bảng
   tiến độ không phân biệt được "chưa poll từ lúc đặt target" với "đã mời, chờ người bấm RED".

Ngoài server: app tính "đã lên bản" bằng regex cắt hậu tố (`fbt_v2.4.5AT1.bin` → `2.4.5`) nên với bản
fleet đang chạy bảng tiến độ **ngược** (ghi ở `apps/fbt_rapid/CLAUDE.md`). Phương án dưới đây kéo
phép so đó **về server** để mọi client (app, web, feed ERP) cùng một câu trả lời.

## 1. Nguyên tắc giữ nguyên (bất biến)

- `/ota/check` **không đổi hợp đồng** với fleet ≤ v2.4.5: `version` = TÊN FILE, kho `rapidplus` giữ
  `fbt_v<ver>.bin`. Mọi thứ thêm vào là trường mới, máy cũ bỏ qua.
- **File là chân lý, không DB** cho OTA (DDL trên box cần sudo; một worker; `_CFG_LOCK` + ghi nguyên tử).
- Route ghi = PUT/DELETE (catch-all `POST /{path}`); GET cố định khai báo TRƯỚC GET động cùng hình dạng.
- Khoá sản phẩm theo `system/products.yaml`; **không đổi khoá đã phát hành**.
- Server không quyết "mới hơn/cũ hơn": so **bằng/khác** chuỗi version (hạ bản có chủ ý là tính năng).

## 2. Thiết kế

### 2.1 Danh tính sản phẩm của một máy — 4 nguồn, thứ tự cố định

```
product_effective(device) =
    (1) máy TỰ KHAI  ?product= ở /ota/check            (fw ≥ v2.4.6, rapid4p)   — luôn thắng
  > (2) GÁN TAY trên server  devices.json[id].product   (người vận hành khai)
  > (3) TIỀN TỐ mã máy       OTA_LEGACY_PRODUCT_BY_PREFIX
  > (4) OTA_LEGACY_PRODUCT
```

(1) thắng vì máy biết nó đang chạy gì; nhưng nếu (1) ≠ (2) thì ghi `product_conflict: true` — nghĩa là
có người nạp tay một bản khác với hồ sơ, đáng để app tô cảnh báo chứ không im.

**(2) là mảnh còn thiếu** để quản lý fleet Rapid+ hôm nay: 109 máy cùng tiền tố `RPL`, không tự khai,
nhưng người vận hành **biết** máy nào là biến thể `a` (theo `ver` máy báo `v2.4.5a1`, theo hồ sơ ATE,
theo khách hàng). Gán `RPL00123 → rapidplus-a` là `/ota/check` của máy đó tra kho `rapidplus-a` — kho
đó sống lại, và bản chung của `rapidplus` không còn mời nhầm máy `a`. Không cần chờ firmware v2.4.6.

File `OTA_DIR/devices.json` (đọc/ghi qua `_CFG_LOCK`, `_atomic_json`, trần `_FW_MAX_DEVICES`):

```json
{ "RPL00123": {"product": "rapidplus-a", "group": "", "note": "lô 2026-09-A", "by": "root", "at": "…"} }
```

`group` để sẵn cho 2.5. Mục méo → bỏ (đọc phòng thủ như `pin_entry`).

### 2.2 `/devices` = bảng HỢP NHẤT, trạng thái OTA tính ở server

Nguồn: `sessions` (DB) ∪ `fw_seen.json` ∪ `devices.json`. Máy chỉ có ở `fw_seen` → `sessions: 0,
last_seen: null, version = fw_seen.version`. Mỗi dòng thêm khối `ota`:

```json
{
  "id_device": "RPL00123", "sessions": 41, "last_seen": "…", "version": "v2.4.5a1",
  "product": "", "hw": "", "product_assigned": "rapidplus-a", "product_effective": "rapidplus-a",
  "product_conflict": false, "last_check": "2026-09-18T01:02:03+00:00",
  "ota": {
    "target": "fbt_v2.4.6a1.bin", "ver": "v2.4.6a1", "pinned": false, "reason": "",
    "state": "offered", "offered_at": "2026-09-18T01:02:03+00:00"
  }
}
```

`ota.state` — **một chỗ duy nhất** quyết định, so `normalize(version) == normalize(manifest.ver)`
(bỏ khoảng trắng, hạ hoa/thường, bỏ `v` đầu, **GIỮ hậu tố**):

| state | Nghĩa | Điều kiện |
|---|---|---|
| `none` | kho chưa có bản cho máy này | `resolve` → `none` / `pin-missing` / `tag` |
| `skipped` | bị lọc | `reason: hw` |
| `on` | đã đúng bản | version máy == `manifest.ver` |
| `offered` | đã được mời, chưa nạp | `fw_seen.offered.file == target` và version ≠ ver |
| `waiting` | chưa poll từ lúc đặt target | `last_check < target_at` (hoặc chưa poll bao giờ) |
| `unknown` | máy chưa báo version | `version` rỗng |

Chi phí: `resolve` mỗi máy đọc `target.json` + manifest → **cache trong một request** theo
`(product)` và `(product, file)`; 500 máy × vài KB là không đáng kể.

### 2.3 Ghi "đã mời" vào `fw_seen.json`

`/ota/check` trả `update:true` → `seen[device].offered = {"file", "ver", "at"}`; trả `update:false` →
`offered = null, reason`. Ba trường nhỏ, không vào `fw_log.json`. Đây là thứ cho phép phân biệt
"máy chưa lên vì chưa poll" với "máy chưa lên vì đang chờ người bấm RED" — với Rapid+ (nạp cần người
đồng ý) hai ca này cần hai hành động khác nhau ngoài hiện trường.

### 2.4 Tiến độ theo kho: `GET /ota/{product}/progress`

```json
{ "product": "rapidplus", "target": "fbt_v2.4.6AT1.bin", "ver": "v2.4.6AT1", "target_at": "…",
  "counts": {"on": 60, "offered": 30, "waiting": 15, "skipped": 2, "unknown": 2, "none": 0},
  "devices": [{"id_device": "…", "version": "…", "state": "…", "last_check": "…", "pinned": false}] }
```

Lọc `/devices` theo `product_effective == product`. Khai báo TRƯỚC `GET /ota/{product}/{filename}`
(`progress` không có đuôi `.bin` nên tải ảnh không đụng). App/CSV rollout đọc thẳng, không tự so nữa.

### 2.5 Ghim theo NHÓM/LÔ (giai đoạn 2 của plan gốc)

`target.json.groups = {"<tên>": pin}`, `devices.json[id].group = "<tên>"`. `resolve`: ghim máy → ghim
nhóm → bản chung (chỗ đã để sẵn). Nhóm tạo từ hồ sơ ATE (`batch`) hoặc từ bộ lọc version. Làm sau 2.1–2.4
vì cần `devices.json` đã có.

### 2.6 Upload an toàn + md5 trong manifest

- Kiểm "đã có?" + ghi file trong **cùng khoá** (`_CFG_LOCK`, hoặc khoá theo tên); file tạm
  `.tmp-<name>.<pid>.<rand>` hoặc mở `O_EXCL`; `OSError` → `OtaError(500/507)` có chữ, không exception trần.
- `build_manifest` thêm `md5`; `ota_download` lấy từ manifest (thiếu → tính rồi ghi lại) thay vì băm
  2,4 MB mỗi request.

### 2.7 Thẻ nhận dạng đọc từ `esp_app_desc_t` (ảnh ESP-IDF) — không cần sửa firmware

Mọi ảnh ESP-IDF có `esp_app_desc_t` tại offset `0x20`: `magic_word 0xABCD5432`, `version[32]` @`0x30`,
`project_name[32]` @`0x50`. Đã đọc từ `firmware/rapid4p/build/rapid4p.bin` thật: `project_name=rapid4p`,
`version=0.1.0`. → `logic.parse_app_desc(raw)`; coi là thẻ khi `project_name` là khoá sản phẩm hợp lệ:
`{product: project_name, ver: "v"+version, hw: null}`. Ưu tiên `FBTIMG1` > `app_desc`.

- Firmware Rapid4P phải giữ CMake `project(... VERSION x.y.z)` **= `R4P_FW_VERSION`** (nay `0.1.0` vs
  `v0.1.0`, server chuẩn hoá chữ `v` nhưng số phải trùng) — tốt nhất CMake đọc từ `main/rapid4p.h`.
- Ảnh Arduino (Rapid+, Reader) cũng có struct này nhưng `project_name/version` là của core
  (dự kiến `arduino-lib-builder` — **kiểm trên `fbt_v*.bin` thật trước**: `xxd -s 0x20 -l 0x80`) → không
  dùng được, Rapid+ vẫn theo đường `FBTIMG1` (giai đoạn 1 firmware).
- `OTA_REQUIRE_TAG` đổi thành **danh sách theo sản phẩm**: `OTA_REQUIRE_TAG=rapid4p,rapidplus-prod`
  (`1`/`true` = tất cả, giữ tương thích). Kho legacy không bắt buộc tới khi fleet lên v2.4.6.

### 2.8 Ghim mồ côi

`listing()`/`products_summary()`: ghim có `product_effective(device) ≠ product` → `stale: true`;
`products_summary` thêm `devices` (số máy có `product_effective == key`) và `stale_pins`. App có số
"kho này N máy tra" để cảnh báo kho 0 máy, và có nút dọn (`DELETE /ota/{product}/target?device=`
như hiện tại). `PUT /devices/{id}/product` nhận `?clean=1` để gỡ luôn ghim ở kho cũ.

### 2.9 Registry lên box (pha 2 của `system/products.yaml`)

`tools/registry_export.py` sinh `products.json` (khoá, tiền tố, `image_name`, `legacy_name_locked`)
→ `deploy.ps1 -Server` chép lên; server (`FBT_REGISTRY`, tuỳ chọn) dùng để: `list_products` đủ cả kho
rỗng (app chọn được `rapidplus-a` khi chưa có file), tiền tố legacy thay env, `expected_bin_name` theo
`image_name` thay vì hard-code `fbt`. Không có file → hành vi như nay. Không cần pyyaml trên box.

## 3. Hợp đồng REST mới (ghi = `ota_admin`)

| Route | Ý nghĩa |
|---|---|
| `GET /devices` | như cũ + hợp nhất 3 nguồn + `product_assigned`, `product_conflict`, `last_check`, khối `ota{target, ver, pinned, reason, state, offered_at}` |
| `PUT /devices/{id}/product?product=&by=&note=&clean=` | gán máy vào kho (2.1); `clean=1` gỡ ghim ở kho khác |
| `DELETE /devices/{id}/product` | bỏ gán → về tiền tố/legacy |
| `PUT /devices/product?product=&ids=a,b,c&by=` | gán hàng loạt (từ bộ lọc version của app); khai báo TRƯỚC `/devices/{device}/…` |
| `PUT /devices/{id}/group?group=` · `DELETE …` | nhóm/lô (2.5) |
| `GET /ota/{product}/progress` | tiến độ theo kho (2.4); khai báo TRƯỚC `/ota/{product}/{filename}` |
| `PUT /ota/{product}/groups/{group}/target/{file}` · `DELETE /ota/{product}/groups/{group}/target` | ghim nhóm (2.5) |
| `GET /ota/products` | thêm `devices`, `stale_pins` mỗi kho |
| `GET /ota?product=` | mỗi ghim thêm `stale`; mỗi file thêm `md5`, `tag_source: fbtimg|app_desc|null` |
| `GET /ota/check` | **không đổi** với máy; server ghi thêm `offered` vào `fw_seen` |

`devices` **phải** thêm vào `PRODUCT_RESERVED`? Không: `/devices/…` nằm ngoài `/ota/`. `progress`,
`groups` là đoạn SAU `{product}` nên chỉ cần thứ tự khai báo.

## 4. Lộ trình — mỗi bước tự đứng, deploy riêng được

| Bước | Nội dung | Env mới | Ước lượng | Ai hưởng ngay |
|---|---|---|---|---|
| **B1 — vá lỗi thuần** ✅ 2026-09-18 | 2.6 (khoá upload, tmp duy nhất, md5) + `/devices` hợp nhất `fw_seen` (chưa có khối `ota`) | — | ½ ngày | máy mới nạp hiện ra; hết 500 upload |
| **B2 — danh tính máy + trạng thái** ✅ 2026-09-18 | 2.1 `devices.json` + API gán; 2.2 khối `ota` ở `/devices`; 2.3 `offered`; 2.8 `stale`/`devices` count | — | 1 ngày | fleet `a` quản được ngay bằng gán tay; app bỏ regex tự so (fix tiến độ ngược) |
| **B3 — thẻ + tiến độ** ✅ 2026-09-18 | 2.7 `app_desc` + `OTA_REQUIRE_TAG` theo sản phẩm; 2.4 `/progress` | `OTA_REQUIRE_TAG=rapid4p` | ½ ngày | Rapid4P upload không cần gõ tên; CSV rollout đọc server |
| **B4 — nhóm/lô** | 2.5 | — | 1 ngày | đẩy theo lô ATE |
| **B5 — registry** | 2.9 | `FBT_REGISTRY` | ½ ngày | app thấy đủ kho; tiền tố hết nằm ở env |

Thứ tự B1 → B2 cố ý: B1 không đổi hợp đồng nào; B2 mới thêm file `devices.json` (tạo trễ, không
migration). App có thể chuyển sang dùng `ota.state` ngay sau B2 mà không chờ B3.

## 5. Kiểm thử (bổ sung `tests/test_ota_products.py` hoặc file mới `test_ota_devices.py`)

- Upload đồng thời cùng tên khác nội dung → đúng một 200 + một 409 (thread hai `PUT`).
- `/devices` với `db.list_devices` vá rỗng và `fw_seen` có máy → máy hiện, `sessions: 0`.
- `product_effective`: tự khai > gán tay > tiền tố > legacy; tự khai ≠ gán → `product_conflict`.
- Gán `RPL1 → rapidplus-a` rồi `/ota/check?device=RPL1` (không product) → nhận bản của `rapidplus-a`;
  `DELETE` gán → về `rapidplus`.
- `ota.state` đủ 6 ca; hậu tố: máy `v2.4.5AT1` + target `fbt_v2.4.5AT1.bin` → `on`; máy `v2.4.5` → không `on`.
- `offered` ghi khi `update:true`, xoá khi `update:false`; `waiting` khi `last_check < target_at`.
- `parse_app_desc` với ảnh giả (magic + version + project_name) và ảnh không magic → None; ưu tiên `FBTIMG1`.
- `OTA_REQUIRE_TAG=rapid4p`: kho `rapid4p` không thẻ → 400, kho `rapidplus` không thẻ → 200.
- `stale` ghim khi máy đổi kho; `products_summary.devices` đếm đúng.
- Toàn bộ test cũ (43) giữ nguyên xanh — hợp đồng `/ota/check` không đổi.

## 6. Deploy & rủi ro

- `scripts/deploy.ps1 -Server` (chép cả `app/*.py`), người dùng restart; kiểm `openapi.json` có
  `/devices/{device}/product`, `/ota/{product}/progress`; **và** md5 `logic.py/ota.py` vì openapi không
  phản ánh đổi trong hai file đó.
- Không migration: `devices.json` tạo khi gán lần đầu; `fw_seen` thêm trường theo lượt poll.
- `/devices` nặng hơn (đọc `target.json` + manifest theo kho) — cache trong request; đo lại thời gian
  `/devices` với 109 máy trước/sau.
- **Gán nhầm kho là công cụ sắc**: máy AT gán vào `rapidplus-a` sẽ được mời bản `a`. Bắt buộc `by`,
  ghi `at`, in journal `ota assign …`; app xác nhận hai bước khi gán hàng loạt. Lớp chặn cuối vẫn là
  firmware tự đối chiếu (giai đoạn 1) — server không thay được lớp đó.
- `product_conflict` sẽ nổi lên ở máy được nạp tay bản khác → đó là thông tin đúng, không phải nhiễu.

## 7. Việc ngoài server phải đi kèm (không nằm trong file này)

- **App** (giai đoạn 2): chọn kho; bỏ `versionInFileName`/`isDeviceOnTarget`, đọc `ota.state`; màn gán
  máy vào kho (hàng loạt từ bộ lọc version); cột Sản phẩm/PCB; nhãn "máy sẽ được **mời**" cho Rapid+.
- **Firmware Rapid+** (giai đoạn 1): nhúng `FBTIMG1`, gửi `product/hw`, đối chiếu manifest — lớp chặn cuối.
- **Firmware Rapid4P**: đối chiếu `product`/`hw` trong phản hồi, gate `measure_busy()` trước khi tải/restart,
  CMake `VERSION` = `R4P_FW_VERSION`, cân nhắc hỏi người dùng thay vì tự nạp.
- **Reader**: quyết định chuyển về Engineer Server (nhúng module OTA chung) hay ghi rõ `ota: github` trong
  `system/products.yaml` để kho `reader` không bị hiểu nhầm là quản lý được từ app.
