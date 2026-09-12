# Phương án — Quản lý OTA cho NHIỀU dòng sản phẩm / biến thể / phần cứng

> Trạng thái (2026-09-11): **giai đoạn 0 (server) ĐÃ CODE + TEST, chờ deploy** — hợp đồng đã
> thực thi ở `server/docs/plan/ota-nhieu-san-pham.md`, nhật ký `server/docs/history/2026-09-11.md`.
> Giai đoạn 1 (firmware) · 2 (app) · 3 chưa làm. Liên quan: app tab *Quản lý máy*
> (`lib/screens/manager_machine_screen.dart`), server `server/app/ota.py` + mục OTA `main.py`,
> firmware `FBT-DXD/src/updateOTA.cpp` và scaffold `FBT-RapidPlus-Production/src/ota/`.
>
> Lệch so với bản đề xuất bên dưới (cố ý, khi làm giai đoạn 0): (1) server ĐÃ đọc thẻ nhúng và
> áp luật 409 "cùng tên khác sha256" ngay từ giai đoạn 0 (mềm: `OTA_REQUIRE_TAG` tắt tới khi fleet
> có thẻ); (2) di cư kho phẳng chạy TỰ ĐỘNG ở lifespan startup, script chỉ để `--dry-run`;
> (3) thêm `PUT /ota/{product}` (upload không tên, server đặt tên từ thẻ) và `product_effective`
> ở `/devices`; (4) `/ota/check` với `product` sai cú pháp → `reason:"product"` (fail-closed).

## 0. "Nhiều production" nghĩa là gì trong tài liệu này

Server hiện chỉ biết **một** kho firmware. Thực tế sắp có nhiều thứ cùng gọi `/ota/check`:

| Trục | Ví dụ thật | Ai quyết định | Cần tách kho? |
|---|---|---|---|
| **Dòng sản phẩm** | RapidPlus (`FBT-DXD`), RapidPlus-Production (repo mới), Reader, ReaderMax, Dxdhub | lúc build (compile-time) | **Có** — ảnh khác nhau hoàn toàn |
| **Biến thể firmware** cùng dòng | `v2.4.5AT` vs `v2.4.5a` (`-DSHAPE_RULE_NEGATIVE`) | lúc build | **Có** — quy tắc lâm sàng khác, không được trộn |
| **Phần cứng** | PCB `V1.1`/`V1.2`/`V1.3` (`parameter.PCB_version`, EEPROM) | lúc lắp máy / ATE `ID-02` | Không tách kho, nhưng **phải lọc tương thích** |
| **Lô sản xuất / nhóm khách** | `batch` trong hồ sơ ATE | lúc xuất xưởng | Không — chỉ cần **mục tiêu theo nhóm** |

→ Mô hình: **`product` (khoá sản phẩm, gộp cả biến thể) là trục CHÍNH của kho**; `hw` là bộ lọc
tương thích; nhóm/lô là lớp chọn mục tiêu nằm giữa "ghim từng máy" và "bản chung".

Quy ước khoá: `product` = `[a-z0-9-]{1,24}`, ví dụ `rapidplus`, `rapidplus-a`, `reader`, `readermax`.
Biến thể gộp vào khoá (`rapidplus-a`) chứ không thêm trục riêng — người vận hành nhìn "kho" nào
là biết đang đẩy gì; hai chiều `product × variant` sẽ sinh ma trận mà không ai kiểm hết.

## 1. Vì sao hiện trạng KHÔNG chịu được nhiều sản phẩm

1. **Bản chung là toàn server.** Đặt target cho Reader là 109 máy RapidPlus cũng được mời nạp
   ảnh Reader ở lượt poll 6 h tới. Cách chống duy nhất hiện có là ghim tay từng máy.
2. **Firmware không tự giới thiệu.** `/ota/check` chỉ có `device`, `ver`, `updated`. Server không
   có dữ liệu để lọc dù muốn.
3. **Firmware không có cửa khoá nào ngoài tên file.** `name != "fbt_" + FirmwareVer + ".bin"` ⇒
   mời cập nhật. `HTTPUpdate` chỉ kiểm `x-MD5` + magic header ESP32 — **ảnh Reader nạp lên
   RapidPlus vẫn "thành công"**, rồi chạy code điều khiển bộ gia nhiệt trên sai chân GPIO. Hai
   slot OTA cứu được khỏi brick, KHÔNG cứu được khỏi chạy sai firmware (không có rollback tự động).
4. **Tên file là định danh duy nhất của ảnh** — đã dính "hai ảnh một tên `fbt_v2.4.5.bin`"
   (xem `define.h` mục FIRMWARE_VERSION). Thêm sản phẩm là thêm cơ hội lặp lại.
5. **Một Bearer cho cả fleet mọi sản phẩm** (`SECRET_INGEST_TOKEN` nằm trong 4 KB đầu mọi `.bin`).
   Không chặn được ngay, nhưng phải để đường mở cho token theo sản phẩm.

## 2. Kiến trúc đề xuất — 3 lớp bảo vệ, lớp nào hỏng lớp sau vẫn đỡ

```
 [firmware]  tự khai product+hw  ──►  [server] kho theo product, lọc hw, ghim/nhóm/chung
                                              │
                                              ▼ trả manifest {product, ver, hw[], sha256, url}
 [firmware]  ĐỐI CHIẾU manifest với chính mình trước khi tải  (từ chối nếu lệch)
                                              │
                                              ▼
 [server]    lúc UPLOAD: đọc THẺ NHẬN DẠNG nhúng trong .bin → tự sinh manifest, tự đặt tên
```

### 2.1 Thẻ nhận dạng nhúng trong ảnh firmware (image tag) — gốc của mọi thứ

Mỗi ảnh tự nói nó là gì, **do build sinh ra**, không ai gõ tay:

```c
// firmware: fbt_ota.h (module dùng chung cho mọi dòng máy)
#define FBT_IMAGE_TAG "FBTIMG1;product=" FBT_PRODUCT ";ver=" FIRMWARE_VERSION \
                      ";hw=" FBT_HW_SUPPORTED ";;"
// __attribute__((used)) + in ra Serial lúc boot để linker không bỏ và để log/ATE đọc được
extern const char fbtImageTag[];
```

- `FBT_PRODUCT` = `-DFBT_PRODUCT=\"rapidplus\"` trong từng `[env:…]` PlatformIO (env `shape_neg`
  → `rapidplus-a`). `FBT_HW_SUPPORTED` = `"V1.1,V1.2,V1.3"` (build hiện tại tự chuyển theo
  `PCB_version` lúc chạy — `PIDControl.cpp:1671`, `sensor6035.cpp:731` — nên khai hết).
- Server lúc `PUT` **quét bytes tìm `FBTIMG1;`**, đọc tới `;;` (≤ 160 byte), parse thành manifest.
  Không có thẻ → **400**, trừ `?force=1` (chỉ cho ảnh cũ ≤ v2.4.5, ghi rõ vào manifest
  `tag: null`). Có ≥ 2 thẻ khác nội dung → 400.
- **Tên file do server đặt từ thẻ**, app không đặt nữa: `rapidplus` giữ `fbt_v<ver>.bin` (bắt
  buộc, xem §5), sản phẩm khác `<product>_v<ver>.bin`. Cùng tên đã có mà **sha256 khác → 409**
  (đúng luật "một version = một nội dung" đã áp cho `/ate/limits`).

### 2.2 Server — kho theo sản phẩm

```
ota/
  products/
    rapidplus/
      fbt_v2.4.6.bin
      fbt_v2.4.6.bin.json     # manifest: {product, ver, hw[], size, sha256, tag, by, at, note}
      target.json             # {target, devices{id: pin}, groups{name: pin}}  (dạng _pin_entry cũ)
    rapidplus-a/ …
    reader/ …
  fw_seen.json                # thêm product, hw cho mỗi máy
  fw_log.json
```

Vẫn **file, không bảng DB** (cùng lý do cũ: DDL trên box cần sudo, một worker, file là chân lý).
Mỗi sản phẩm một `target.json` → `/ota/check` của 109 máy RapidPlus không đọc file của Reader.

**Thứ tự chọn bản cho một máy** (`_ota_target(product, device, hw)`):

```
ghim máy (devices[id])  →  ghim nhóm (groups[nhóm của máy])  →  bản chung (target)
       │ có bản ─► manifest.hw có khai VÀ máy có gửi hw VÀ hw ∉ manifest.hw  ⇒ {update:false, reason:"hw"}
       │ ghim trỏ file đã mất ⇒ None (KHÔNG rơi về bản chung — giữ luật hiện tại)
```

`reason` trả về để app/log giải thích được "vì sao máy này không được mời", thay vì im lặng.

**Máy cũ không gửi `product`** (toàn bộ fleet v2.4.4/v2.4.5): server gán `LEGACY_PRODUCT`
(env, mặc định `rapidplus`). Tuỳ chọn thêm `LEGACY_PRODUCT_BY_PREFIX` (`RPL=rapidplus,RDR=reader`)
nếu sau này có firmware cũ của dòng khác cũng trỏ về đây. Đây là đường bắt buộc: **không được đòi
firmware mới rồi mới chạy** — đường sửa từ xa duy nhất tới máy ngoài hiện trường đi qua chính
request này.

**REST** (mọi route ghi = PUT/DELETE vì `POST /{path}` catch-all nuốt POST; GET cố định đứng
trước GET có tham số):

| Route | Ý nghĩa |
|---|---|
| `GET /ota/check?device&ver&updated&product&hw` | như cũ + 2 tham số mới; trả thêm `product`, `ver`, `hw[]`, `reason` |
| `GET /ota/products` | danh sách khoá sản phẩm + số file + bản chung |
| `GET /ota?product=` | kho của một sản phẩm (không tham số = gộp tất cả, có cột `product`) |
| `PUT /ota/{product}/{file}?force=1&note=` | upload; server đọc thẻ, kiểm `thẻ.product == {product}` |
| `PUT /ota/{product}/target/{file}?device=&group=&by=` | chọn bản chung / ghim máy / ghim nhóm |
| `DELETE /ota/{product}/target?device=&group=` | huỷ chọn (giữ nguyên ngữ nghĩa hiện tại) |
| `DELETE /ota/{product}/{file}` | xoá + dọn mọi ghim trỏ tới nó |
| `GET /ota/{product}/{file}` | tải `.bin` (header `x-MD5` như cũ) |
| `PUT /ota/{product}/groups/{group}?devices=a,b,c` · `DELETE …` | định nghĩa nhóm (giai đoạn 2) |
| `GET /ota/{file}` · `PUT /ota/{file}` · `PUT /ota/target/{file}` · … | **đường lùi**: khớp `.bin` → chuyển vào `LEGACY_PRODUCT` để app/firmware cũ chạy nguyên tới khi cập nhật |

`/devices` trả thêm `product`, `hw` (từ `fw_seen.json`) và `/devices/{id}/fw-log` giữ nguyên.

**Migration** `scripts/migrate_ota.py` chạy MỘT lần lúc deploy: dời `ota/*.bin` + `target.json`
→ `ota/products/<LEGACY_PRODUCT>/`, sinh manifest cho từng file cũ: `ver` lấy từ tên
(`fbt_v(.+)\.bin`), `hw: null` (= không biết = **không lọc**, giữ đúng hành vi hôm nay),
`tag: null`. Idempotent, có `--dry-run`.

### 2.3 Firmware — tự khai và tự đối chiếu

Trong `updateOTA.cpp` (và module dùng chung cho các dòng khác):

1. **Gửi** `&product=<FBT_PRODUCT>&hw=<urlSafe(parameter.PCB_version)>` trong chính lượt
   `/ota/check` đã có (không thêm request, không thêm endpoint — cùng triết lý `?ver=`).
2. **Đối chiếu manifest trước khi tải** (thay cho phép so tên file):
   ```
   json.product != FBT_PRODUCT                       → từ chối, log "[ota] offered <p>, I am <q>"
   json.hw có VÀ PCB_version ∉ json.hw                → từ chối, log
   json.ver == FirmwareVer                            → "đã đúng bản" (OTA_IDLE)
   khác                                              → OTA_AVAILABLE   (khác = mời, kể cả hạ bản — giữ chính sách hiện tại)
   ```
   Thiếu `product` trong phản hồi (server chưa nâng) → **rơi về so tên file như cũ** để firmware
   mới vẫn chạy với server cũ trong quãng chuyển tiếp.
3. **Nhúng `fbtImageTag`** (§2.1), in ra Serial ngay sau banner boot → `log_triage`/ATE `FW-02`
   đọc được, và `strings firmware.bin | grep FBTIMG1` kiểm được bằng tay.
4. `httpUpdate.update(client, fwUrl, FirmwareVer, addBearer)` — truyền version thật ở tham số 3
   để thư viện gửi `x-ESP32-version`; server có thể log ai đang tải gì mà không cần thêm gì.

**Cứng hoá (giai đoạn 3, cần đo trước):** với firmware ĐANG chạy là bên duy nhất tin được, lớp
mạnh nhất là *quét thẻ trong luồng tải trước khi commit* (đọc `FBTIMG1;` trong bộ đệm trượt khi
stream vào `Update`, lệch product → `Update.abort()`). Việc này phải rời `httpUpdate.update()`
sang vòng tải tay, tức đụng đúng ngân sách stack 6144 B của NetworkTask mà `updateOTA.cpp` đã đo
— **không làm trước khi có số**. Song song, kiểm tra core arduino-esp32 đang dùng có
`verifyRollbackLater()`/`Update.rollBack()` (rollback bootloader) để lần boot đầu sau OTA tự
kiểm rồi mới đánh dấu hợp lệ — scaffold `FBT-RapidPlus-Production` đã chốt "hai app slot" nên
đặt cùng một chỗ.

### 2.4 App — tab Quản lý máy

- **Chọn sản phẩm** ở đầu mục OTA (segmented; nhớ lựa chọn). Danh sách file, bản chung, ghim,
  tiến độ — tất cả theo sản phẩm đang chọn. Không còn màn nào hiện "bản chung" mà không nói của
  sản phẩm nào.
- **Upload không gõ version nữa**: chọn file → gửi lên → server trả thẻ đã đọc
  (`rapidplus · v2.4.6 · hw V1.1,V1.2,V1.3`) → người dùng xác nhận. Ảnh không có thẻ → hộp thoại
  "ảnh cũ không có thẻ nhận dạng, bạn tự chịu trách nhiệm tên/phiên bản" + nhập tay + `force=1`.
  `otaFileNameFor` chỉ còn phục vụ đường `force`.
- **Bảng Trạng thái máy** thêm cột `Sản phẩm`, `PCB`; lọc theo sản phẩm; máy chưa báo product
  hiện "cũ → rapidplus" (chữ nghiêng) chứ không hiện trống.
- **Cảnh báo tương thích**: máy có `hw` không nằm trong `hw[]` của bản chung → ô tiến độ hiện
  "bỏ qua (PCB)", đúng như server sẽ làm — người vận hành không phải tự đoán vì sao máy không lên.
- **Ghim theo nhóm** (giai đoạn 2): chip nhóm cạnh chip ghim; nhóm có thể **tạo từ hồ sơ ATE**
  ("mọi máy lô 2026-09-A") vì `batch` đã nằm ở `/ate/records`.
- Gate quyền: mọi thao tác ghi vẫn `canWriteOta`; đọc `canSeeOta`.

## 3. Hợp đồng `/ota/check` mới (tương thích ngược)

Request (firmware ≥ v2.4.6):
```
GET /ota/check?device=RPL03003&ver=v2.4.5AT&product=rapidplus&hw=V1.3[&updated=1]
```
Response:
```json
{
  "update": true,
  "version": "fbt_v2.4.6.bin",
  "ver": "v2.4.6",
  "product": "rapidplus",
  "hw": ["V1.1", "V1.2", "V1.3"],
  "size": 2411520,
  "sha256": "…",
  "url": "https://fbt.basa-luma.ts.net/ota/rapidplus/fbt_v2.4.6.bin"
}
```
- `version` **GIỮ = TÊN FILE** — firmware ≤ v2.4.5 so đúng chuỗi này.
- `ver` mới = version thật (từ thẻ); `hw` = `null` nếu ảnh cũ không có thẻ.
- Không có bản: `{"update": false, "reason": "none|hw|pin-missing"}` (`reason` mới, máy cũ bỏ qua).

## 4. Lộ trình — mỗi bước tự đứng được, không bước nào đòi bước sau

| Giai đoạn | Phạm vi | Cần deploy | Giá trị ngay |
|---|---|---|---|
| **0 — Server tách kho** ✅ code+test 2026-09-11 | §2.2 (products/, LEGACY_PRODUCT, đường lùi, migrate tự động, manifest — có thẻ thì đọc thẻ, không thì suy từ tên) + 17 test | server (`deploy.ps1 -Server` + restart) | Dòng máy thứ 2 dùng server được **ngay**, không thể đẩy nhầm ảnh sang RapidPlus. Fleet cũ không biết gì đã đổi. |
| **1 — Firmware tự khai** | §2.1 + §2.3 (thẻ, gửi product/hw, đối chiếu manifest) — phát hành `v2.4.6`; upload đọc thẻ | firmware + server (nhỏ) | Hết vụ "hai ảnh một tên"; server lọc được PCB; firmware từ chối ảnh lạ dù server đặt sai. |
| **2 — App + nhóm** | §2.4, ghim nhóm, nhóm từ lô ATE, cột product/hw | app + server | Vận hành nhiều dòng trong một màn; đẩy theo lô. |
| **3 — Cứng hoá** | quét thẻ trước commit, rollback tự kiểm, token theo sản phẩm (`RECEIVER_TOKENS` theo product) | firmware + server | Sai cấu hình server hay lộ token một dòng không kéo dòng khác. |

Thứ tự 0 → 1 là cố ý: giai đoạn 0 không cần chạm fleet; giai đoạn 1 phát hành như một bản OTA
bình thường qua chính kho đã tách.

## 5. Ràng buộc BẮT BUỘC & cạm bẫy

- **`rapidplus` phải giữ tên `fbt_v<ver>.bin` và `/ota/check` phải giữ `version` = tên file.**
  Firmware ≤ v2.4.5 so `name != "fbt_" + FirmwareVer + ".bin"` — đổi convention là cả fleet
  "You have the lasted version" mãi mãi (hoặc ngược lại, bị mời liên tục). Sản phẩm mới không
  chịu ràng buộc này.
- **Không bao giờ build lại dưới version đã phát** — thẻ + luật 409 sha256 ở server biến điều
  này từ "nhớ" thành "máy từ chối".
- **`PCB_version` là dữ liệu do người nhập** (mặc định `"V1.3"` khi chưa cấu hình,
  `define.h:141`). Máy V1.2 chưa được ATE `ID-02` đặt đúng sẽ tự khai V1.3 → lọc `hw` chỉ đáng
  tin khi ATE là đường duy nhất khai sinh máy. Vì vậy `hw: null` = không lọc, và bảng app phải
  phân biệt "V1.3 (mặc định)" với "V1.3 (đã khai)" — khó, nên ít nhất ATE `ID-02` phải bắt buộc
  điền `PCB version`.
- **Tham số query phải qua cùng bộ lọc `_ID_OK`/`urlSafe`** hai đầu: `V1.3`, `rapidplus-a` đều
  lọt; đừng dùng ký tự khác.
- **Route mới không được POST**; `GET /ota/check`, `/ota/products` đặt TRƯỚC `/ota/{product}/…`
  và `/ota/{file}`; phân biệt `{file}` với `{product}` bằng đuôi `.bin`.
- **Một worker + `_FW_LOCK`**: `fw_seen.json` thêm hai trường, không thêm file; giữ trần
  `_FW_MAX_DEVICES` theo TỔNG máy mọi sản phẩm.
- **Migration là thao tác trên production**: chạy `--dry-run` trước, sao lưu `ota/` (vài chục MB),
  restart `fbt-receiver` sau; app cũ (chưa giai đoạn 2) vẫn chạy nhờ đường lùi `/ota/{file}`.
- **Firmware Reader/ReaderMax/Dxdhub** hiện không dùng hợp đồng này (OTA GitHub `updateOTA.json`
  cũ) — chuyển từng dòng bằng cách nhúng module `fbt_ota` dùng chung, KHÔNG sửa tay từng bản.

## 6. Việc cần chốt trước khi làm giai đoạn 0

1. Khoá sản phẩm chính thức cho từng dòng (đề xuất: `rapidplus`, `rapidplus-a`, `rapidplus-prod`
   cho repo Production nếu nó là ảnh khác hẳn, `reader`, `readermax`, `dxdhub`).
2. `LEGACY_PRODUCT` = `rapidplus`? (mọi máy đang gọi `/ota/check` hôm nay là RapidPlus.)
3. Biến thể `AT`/`a`: gộp vào khoá như đề xuất, hay coi `a` là "kênh thử nghiệm" của
   `rapidplus` (ghim nhóm thay vì kho riêng)? Đề xuất kho riêng — quy tắc lâm sàng khác nhau.
4. Chính sách "khác version = mời cập nhật" (cho phép hạ bản có chủ ý) giữ hay đổi sang
   "chỉ mời khi mới hơn"? Đề xuất **giữ** — hạ bản khẩn cấp là tính năng, và so sánh
   `v2.4.5AT` với `v2.4.6` lớn/bé không có định nghĩa sạch.

## 7. Kiểm thử tối thiểu

- `server/tests/test_ota_products.py`: check không `product` → LEGACY; upload thẻ lệch path
  → 400; không thẻ không `force` → 400; cùng tên khác sha → 409; hw ∉ manifest → `update:false,
  reason:hw`; hw manifest null → không lọc; ghim máy > nhóm > chung; xoá file dọn ghim ở mọi
  tầng; migrate idempotent; đường lùi `/ota/{file}` trả đúng file trong kho legacy.
- Firmware (`test/` host, C++ thuần): parser thẻ; hàm quyết định `otaDecide(manifest, self)` với
  bảng ca (đúng/ lệch product/ lệch hw/ thiếu product → so tên).
- App: widget test dialog upload hiện đúng thẻ server trả; `isDeviceOnTarget` nhận `ver` thật
  thay vì suy từ tên file.
