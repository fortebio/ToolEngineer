# OTA nhiều sản phẩm — phần SERVER (giai đoạn 0, đã làm 2026-09-11)

> Kế hoạch đầy đủ (vì sao, firmware, app, 4 giai đoạn, việc cần chốt) ở repo app:
> `docs/plan/ota-nhieu-san-pham.md`. File này là **hợp đồng đã thực thi** ở server, để
> firmware (giai đoạn 1) và app (giai đoạn 2) bám vào. Code: `app/ota.py` + mục OTA `app/main.py`.

## Bố cục kho

```
~/fbt_server/ota/
  products/<product>/<file>.bin        ảnh
  products/<product>/<file>.bin.json   manifest {product, ver, hw[]|null, size, sha256, tag|null,
                                                  tag_product|null, by, note, at, source}
  products/<product>/target.json       {target: pin|null, devices: {<id>: pin}}   pin = {file, by, at}
  fw_seen.json · fw_log.json           version máy tự khai (thêm product, hw mỗi máy)
```

- Khoá sản phẩm `[a-z0-9-]{1,24}`, cấm `check|products|target` (đoạn path cố định). Biến thể gộp
  vào khoá (`rapidplus-a`).
- **`OTA_LEGACY_PRODUCT`** (mặc định `rapidplus`): kho cho máy KHÔNG khai `?product=` — toàn bộ
  fleet ≤ v2.4.5. `OTA_LEGACY_PRODUCT_BY_PREFIX="RPL=rapidplus,RDR=reader"` (tuỳ chọn) chọn theo
  tiền tố mã máy, tiền tố dài hơn thắng.
- **`OTA_REQUIRE_TAG`** (mặc định tắt): bật thì ảnh không thẻ chỉ lên được với `?force=1`.
- File `.bin`/`target.json` ở GỐC `ota/` (kho phẳng cũ) được **tự dời** vào
  `products/<legacy>/` ở lifespan startup (log `ota migrate: …` trong journal). Trùng tên: cùng
  sha256 → xoá bản gốc; khác → giữ bản trong kho, dời bản gốc thành `<tên>.conflict` (xử lý tay).
  Xem trước: `python3 -m scripts.migrate_ota --dry-run`.

## Thẻ nhận dạng nhúng (firmware ≥ v2.4.6 — giai đoạn 1)

`FBTIMG1;product=rapidplus;ver=v2.4.6;hw=V1.1,V1.2,V1.3;;` — chuỗi ASCII nằm bất kỳ đâu trong
`.bin`, kết thúc `;;`, ≤ 160 byte sau magic. Server quét lúc upload (`logic.parse_image_tags`):
`ver` giữ NGUYÊN chuỗi (phải trùng `FirmwareVer` firmware khai), `hw` viết HOA. Không thẻ →
`ver` suy từ tên (`<prefix>_v<ver>.bin`), `hw = null` = không lọc.

## Hợp đồng REST

Đọc: Bearer thiết bị hoặc admin. Ghi: `ota_admin` (token admin OTA nếu đã đặt). Mọi ghi là
PUT/DELETE (catch-all `POST /{path}` nuốt POST).

| Route | Ý nghĩa |
|---|---|
| `GET /ota/check?device&ver&updated&product&hw` | Máy hỏi bản. Không `product` → kho legacy; `product` sai cú pháp → `{update:false, reason:"product"}` (fail-closed). Có bản: `{update:true, version:<TÊN FILE>, ver, product, hw[]|null, size, sha256, url:/ota/<product>/<file>}`. Không: `{update:false, reason: none|hw|pin-missing|tag|product}` |
| `GET /ota?product=` | Kho một sản phẩm (không tham số = legacy): `{product, target, target_by, target_at, devices, files[{name,size,modified,ver,hw,sha256,tag,by,note}]}` |
| `GET /ota/products` | `{legacy, products[{product, files, target, pinned, legacy}]}` |
| `PUT /ota/{product}/{file}.bin?force&by&note` | Upload có tên. Thẻ phải khớp kho + tên = tên server đặt (`fbt_v<ver>.bin` kho legacy, `<product>_v<ver>.bin` kho khác). Cùng tên: cùng sha → 200 `existed:true`; khác → **409** |
| `PUT /ota/{product}` | Upload KHÔNG tên: server đặt tên từ thẻ; không thẻ → 400 |
| `PUT /ota/{product}/target/{file}?device&by` | Bản chung (không `device`) / ghim máy |
| `DELETE /ota/{product}/target?device` | Bỏ bản chung (GIỮ ghim riêng) / gỡ ghim máy |
| `DELETE /ota/{product}/{file}` | Xoá ảnh + manifest + dọn mọi ghim trỏ tới nó |
| `GET /ota/{product}/{file}` | Tải `.bin`, header `x-MD5` |
| `PUT /ota/{file}.bin` · `PUT /ota/target/{file}` · `DELETE /ota/target` · `DELETE /ota/{file}` · `GET /ota/{file}` | **Đường cũ** = kho legacy, app đang phát hành gọi y nguyên |
| `GET /devices` | thêm `product`, `hw` (máy tự khai, rỗng = chưa) và `product_effective` (kho `/ota/check` thật sự tra) |

Thứ tự chọn bản (`ota.resolve`): ghim máy → bản chung; ghim trỏ file mất → `pin-missing`
(KHÔNG rơi về bản chung); manifest `tag_product` ≠ kho → `tag`; máy khai `hw` và manifest có
`hw[]` mà không chứa → `hw`.

## Bổ sung 2026-09-18 — quản lý MÁY (B1–B3 của [ota-quan-ly-may-nhieu-san-pham.md](ota-quan-ly-may-nhieu-san-pham.md))

Thêm file `ota/devices.json` = `{<id>: {product, group, note, by, at}}` (gán tay máy → kho).
Kho máy THẬT SỰ tra (`ota.product_for`): tự khai `?product=` > gán tay > tiền tố > legacy.

| Route | Ý nghĩa |
|---|---|
| `GET /devices` | HỢP NHẤT `sessions ∪ fw_seen ∪ devices.json` (máy chỉ poll: `sessions:0, last_seen:null`). Thêm `product_assigned`, `product_conflict` (tự khai ≠ gán tay), `last_check`, khối **`ota{target, ver, pinned, reason, state, offered_at}`** — `state ∈ on|offered|waiting|skipped|unknown|none`, so version GIỮ hậu tố (`logic.norm_version`). App/CSV đọc `state`, không tự so tên file |
| `PUT /devices/{id}/product?product&by&note&clean` | gán máy vào kho; `clean=1` gỡ ghim của máy ở kho khác. Gác `ota_admin` |
| `PUT /devices/product?product&ids=a,b,c&by` | gán hàng loạt (khai báo trước `/devices/{device}/…`) |
| `DELETE /devices/{id}/product` | bỏ gán → về tiền tố/legacy |
| `GET /ota/{product}/progress` | `{product, target, ver, target_at, target_by, counts{state: n}, total, devices[]}` — lọc `/devices` theo `product_effective`; khai báo trước `/ota/{product}/{file}` |
| `GET /ota/check` | **không đổi với máy**; server ghi thêm `fw_seen[id].offered = {file, ver, at}` (null khi `update:false`) |
| `GET /ota?product=` | ghim thêm `stale` (máy đã sang kho khác); file thêm `md5`, `tag_source: fbtimg\|app_desc\|null` |
| `GET /ota/products` | thêm `devices` (số máy tra kho — 0 = đặt target ở đó là im lặng), `stale_pins`; liệt kê cả kho chưa có thư mục nếu có máy tra |
| `PUT /ota/{product}[/{file}]` | thẻ thứ hai: `esp_app_desc_t` (ảnh ESP-IDF, offset 0x20) — chỉ nhận khi `project_name` = kho đích hoặc một kho đã có (core Arduino nhúng `arduino-lib-builder` vào mọi ảnh Rapid+/Reader, KHÔNG được coi là thẻ). Phản hồi thêm `md5`, `tag`, `tag_source` |
| `GET /ota/{product}/{file}` | `x-MD5` lấy từ manifest (`ota.md5_for` tính bù cho manifest cũ) |

Env: `OTA_REQUIRE_TAG` nhận **danh sách kho** (`rapid4p,rapidplus-prod`; `1`/`true` = tất cả như cũ).
Upload: kiểm-trùng + ghi trong `_UPLOAD_LOCK`, file tạm `.tmp-<name>.<pid>.<rand>` — hai PUT cùng
tên đồng thời ra đúng 200 + 409.

## Lệch so với kế hoạch (cố ý)

1. **Đọc thẻ + luật 409 kéo lên giai đoạn 0** (kế hoạch để giai đoạn 1): rẻ, thuần, có test, và
   là thứ chặn đúng vụ `fbt_v2.4.5.bin` hai ảnh một tên. Chính sách vẫn mềm: chưa có ảnh nào có
   thẻ nên `OTA_REQUIRE_TAG` tắt.
2. **Di cư TỰ ĐỘNG ở lifespan** thay vì script chạy tay: quên chạy script là cả fleet thấy kho
   trống. Script chỉ còn để xem trước. Ban đầu đặt ở lúc import — sai: `app/__init__.py` import
   `app.main`, nên `--dry-run` cũng dời file thật.
3. **Chưa có ghim nhóm/lô** (giai đoạn 2) — `resolve` để sẵn chỗ giữa ghim máy và bản chung.
4. Thêm `product_effective` ở `/devices` để app đối chiếu tiến độ đúng kho mà không phải tự
   nhân bản luật tiền tố.
