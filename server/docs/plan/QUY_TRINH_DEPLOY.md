# Quy trình deploy Engineer Server (box `fbt-server`) + đợt deploy 2026-09-20

> Box giữ bản copy PHẲNG `~/fbt_server/` (không git). Deploy = `scp` file đã sửa + restart `fbt-receiver`
> (gián đoạn ~3 s, thiết bị tự POST lại). Phiên AI **không SSH được** (classifier chặn) → AI chuẩn bị +
> kiểm bằng `openapi.json` public; người dùng chạy `deploy.ps1` và 1 lệnh `sudo`.

## A. Đợt 2026-09-20 — server B1–B3 (quản lý MÁY trong OTA nhiều sản phẩm)

### A1. Phạm vi

| | |
|---|---|
| **Deploy** | `server/app/*.py` (9 file) + `scripts/migrate_ota.py` — `deploy.ps1 -Server` chép cả bộ |
| **KHÔNG deploy** | web `/app/` — `apps/fbt_rapid/lib` không đổi từ lần deploy web 2026-09-12 (`7b04560`); chưa build `web_prod` |
| **Route mới** (4) | `PUT /devices/product?ids=` · `PUT\|DELETE /devices/{device}/product` · `GET /ota/{product}/progress` |
| **Route đổi** (3) | `GET /devices` (thêm `product_*` + khối `ota{state…}`) · `GET /ota/check` (thêm `product`, `hw` — mặc định rỗng) · `GET /ota/products` (thêm `devices`, `stale_pins`) — **chỉ THÊM trường/tham số**, app đang phát hành không gãy |
| **File không lộ qua openapi** | `logic.py` (`parse_app_desc`, `image_tag`, `norm_version`), `ota.py` (`devices.json`, khoá upload, `md5`), `config.py` (`OTA_REQUIRE_TAG`) → kiểm bằng md5 |
| **Env** (tuỳ chọn) | thêm `OTA_REQUIRE_TAG=rapid4p` vào `/etc/fbt-receiver.env` — kho rapid4p bắt buộc ảnh có thẻ (ESP-IDF luôn có `esp_app_desc_t`). Không thêm = tắt, hành vi như cũ |
| **Migration** | KHÔNG. `devices.json` tạo khi gán lần đầu; `fw_seen[id].offered` thêm theo lượt poll; manifest cũ được `md5_for` ghi bù khi máy tải |

### A2. Đã kiểm trước deploy (2026-09-20, từ box ADM)

- `pytest tests -q` → **117 passed**.
- `python scripts/check_deploy.py` → prod 36 route / local 40; chỉ-có-ở-local = đúng 4 route trên; **chỉ-có-ở-prod = không**
  (không có code lạ trên box sẽ bị ghi đè — bài học 2026-09-12); 3 route đổi chữ ký như bảng.
- `git fetch` + `git branch -r --no-merged main` → không nhánh nào chưa merge; cây làm việc sạch.
- Production đang sống: `GET /` → `{"ok":true}`, `openapi.json` 200 trong 1,5 s.

### A3. Bước làm (người dùng chạy, PowerShell tại `server/`)

```powershell
# 1. Chép code (tự cp -a app → app.bak.<stamp>, in ls *.py trên box, scp 9+1 file, md5 hai bên,
#    rồi KIỂM IMPORT bằng venv thật của box: phải in IMPORT_OK — không thì DỪNG, khôi phục app.bak)
.\scripts\deploy.ps1 -Server
```

```bash
# 2. (tuỳ chọn) bật REQUIRE_TAG cho kho rapid4p — sửa file env trên box (sudo)
ssh -t engineer@fbt.basa-luma.ts.net "sudo sh -c 'grep -q ^OTA_REQUIRE_TAG= /etc/fbt-receiver.env || echo OTA_REQUIRE_TAG=rapid4p >> /etc/fbt-receiver.env'; grep OTA_ /etc/fbt-receiver.env"
```

```bash
# 3. Restart + xem trạng thái/journal (sudo, chạy tay)
ssh -t engineer@fbt.basa-luma.ts.net "sudo systemctl restart fbt-receiver && sleep 2 && systemctl is-active fbt-receiver && journalctl -u fbt-receiver -n 15 --no-pager"
```

### A4. Kiểm sau restart

```powershell
# từ máy dev, không cần SSH/token — phải ra "openapi giống hệt: CÓ" (exit 0)
python scripts\check_deploy.py
```

```bash
# md5 3 file openapi không phản ánh — so với bảng md5 check_deploy.py in ra (bản CRLF cây làm việc)
ssh engineer@fbt.basa-luma.ts.net "md5sum ~/fbt_server/app/logic.py ~/fbt_server/app/ota.py ~/fbt_server/app/config.py"
```

- Route mới phải **401** khi thiếu token: `curl -s -o /dev/null -w "%{http_code}\n" https://fbt.basa-luma.ts.net/ota/rapidplus/progress` → `401`.
- Máy thật vẫn vào: sau 5–10 phút `journalctl -u fbt-receiver --since -10min --no-pager | grep -c 'saved '` > 0
  (fleet Rapid+ POST đều, mỗi phiên một dòng `saved <file> (… db=ok)`), không dòng `Traceback`/`db=FAIL`.
- App đang phát hành **chưa đọc** `ota.state` (việc app, plan §7) — tab Quản lý máy vẫn hiện như cũ là đúng; kiểm
  trường mới bằng app › *Kỹ thuật* › gọi `GET /devices` hoặc `GET /ota/products` (có token) thấy khối `ota{…}` / `devices`.

### A5. Quay lại (30 giây)

```bash
ssh -t engineer@fbt.basa-luma.ts.net "rm -rf ~/fbt_server/app && mv ~/fbt_server/app.bak.<stamp> ~/fbt_server/app && sudo systemctl restart fbt-receiver"
```
`<stamp>` = giá trị `deploy.ps1` in ở bước 1 (`ls -d app.bak.*`). Dữ liệu sinh bởi bản mới (`devices.json`,
trường `offered` trong `fw_seen.json`, `md5` trong manifest) là file JSON thêm khoá → bản cũ đọc bình thường, bỏ qua khoá lạ.

### A6. Rủi ro còn lại

- `/devices` nặng hơn (đọc `target.json` + manifest theo kho, có cache trong request) — đo `time curl …/devices` với ~109 máy sau deploy; nếu > 2 s thì ghi vào plan B4.
- Gán nhầm kho là công cụ sắc (máy AT gán vào `rapidplus-a` sẽ được mời bản `a`) — chỉ root/admin có token `ota_admin`; app xác nhận hai bước khi gán hàng loạt (việc app, chưa làm).
- Bật `OTA_REQUIRE_TAG=rapid4p` rồi upload ảnh rapid4p KHÔNG phải ESP-IDF (không có app_desc) → 400 — đúng thiết kế, không phải lỗi.

## B. Quy trình chuẩn mỗi lần deploy SERVER (rút từ các lần 09-11, 09-12, 09-18)

1. **Sạch + đủ**: `git status` sạch; `git fetch` + `git branch -r --no-merged main` rỗng (box từng chạy code nhánh chưa merge → deploy từ `main` xoá mất `/monitor`, 2026-09-12).
2. **Test**: `pytest tests -q` toàn bộ (không cần Postgres).
3. **So prod ↔ local KHÔNG cần SSH**: `python scripts/check_deploy.py` — mục *chỉ có ở prod* phải RỖNG; đọc mục *chỉ có ở local* = đúng những gì định deploy.
4. **Tương thích ngược**: route đổi chữ ký chỉ được THÊM tham số mặc định/THÊM trường trả về; route mới **không được là POST** (catch-all `POST /{path}` nuốt); firmware ngoài fleet không biết bản mới nên `/ota/check`, `POST /` (ingest) không được đổi hợp đồng.
5. **Env/migration**: đổi `config.py` đọc biến mới → cập nhật `deploy/fbt-receiver.env.example` (registry_check đọc file này) + ghi rõ trong plan là bắt buộc hay tuỳ chọn; đổi bảng → file `deploy/migrate_*.sql` + lệnh psql trong plan.
6. **Chép**: `deploy.ps1 -Server` (sao lưu `app.bak.<stamp>`, chép CẢ 9 file `app/*.py` — chép lẻ 1 file là `ImportError` vì `logic.py` import `config.py`; kiểm `IMPORT_OK` bằng venv box).
7. **Restart** (sudo, tay) → `systemctl is-active` + `journalctl -n 15` không `Traceback`.
8. **Kiểm sau**: `check_deploy.py` → *giống hệt: CÓ*; md5 file không lộ qua openapi; route mới 401 khi thiếu token; ingest vẫn chảy.
9. **Ghi**: `docs/history/YYYY-MM-DD.md` (giờ deploy, stamp bak, kết quả kiểm) + đổi "CHƯA deploy" → "ĐÃ DEPLOY" trong `README.md` + trạng thái plan liên quan.

## C. Deploy WEB `/app/` (chỉ khi `apps/fbt_rapid/lib` đổi)

```powershell
# trong apps/fbt_rapid — KHÔNG --dart-define (bundle nhúng URL/token test là deploy.ps1 từ chối)
$env:PUB_CACHE="$env:LOCALAPPDATA\Pub\Cache"; & C:\Users\ADM\fvm\versions\3.44.1\bin\flutter.bat build web --release --base-href /app/ --output build/web_prod
# rồi tại server/
.\scripts\deploy.ps1 -Web        # tự sao lưu web.bak.<stamp>, giữ 3 bản mới nhất, scp -O nội dung từng thư mục
```
Kiểm: md5 `main.dart.js` tải từ `https://fbt.basa-luma.ts.net/app/` == `build/web_prod`; `hub.fortebio.tech` qua
Cloudflare có thể giữ bản cũ tới 4 h (`cf-cache-status: HIT`) — purge hoặc đợi, không phải lỗi deploy. Không cần
restart service cho web (StaticFiles đọc đĩa).
