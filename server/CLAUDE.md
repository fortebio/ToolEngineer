# CLAUDE.md — hướng dẫn cho Claude trong dự án này

> **Từ 2026-09-15 nằm trong monorepo `ToolEngineer` tại `server/`.** App ở `apps/fbt_rapid/`
> (bản web build ra `apps/fbt_rapid/build/web_prod`), firmware ở `firmware/<product>/`, registry sản
> phẩm `system/products.yaml` (khoá `product`, tiền tố mã máy → `OTA_LEGACY_PRODUCT_BY_PREFIX`,
> mẫu env `deploy/fbt-receiver.env.example`, kiểm bằng `python tools/registry_check.py`).

## Dự án
**FBT Home Server** — MiniPC dựng thành server 24/7 (Debian headless + PostgreSQL), nhận data từ thiết bị và cho code từ xa. Repo này chứa tài liệu vận hành + code nhận data.

## Cấu trúc
```
app/                   # Package service (FastAPI) — chạy: uvicorn app.main:app
  config.py            #   Cấu hình từ biến môi trường (DATA_DIR, TOKEN, DB, OTA_DIR, LOGS_DIR, ATE_DIR,
                       #   OTA_LEGACY_PRODUCT[_BY_PREFIX], OTA_REQUIRE_TAG = danh sách kho | "1" → require_tag(product))
  logic.py             #   Hàm thuần: safe_name, check_auth, validate, hash (scrypt + sha256$ legacy Sheet), parse_*,
                       #   + hồ sơ ATE: validate/normalize_ate_record, ate_stats (FPY/Pareto), DEFAULT_ATE_LIMITS
                       #   + OTA: product_key (khoá [a-z0-9-], cấm check|products|target), parse_image_tags (thẻ
                       #   `FBTIMG1;product=;ver=;hw=;;` nhúng trong .bin), parse_app_desc (esp_app_desc_t @0x20 của ảnh
                       #   ESP-IDF), image_tag(raw, products) (FBTIMG1 > app_desc, app_desc chỉ khi project_name ∈ kho
                       #   cho phép — core Arduino nhúng "arduino-lib-builder"), norm_version (so version GIỮ hậu tố),
                       #   ver_from_name, expected_bin_name
  ota.py               #   KHO OTA THEO SẢN PHẨM (2026-09-11): OTA_DIR/products/<product>/{*.bin, *.bin.json manifest,
                       #   target.json}; resolve(product, device, hw, cache) = ghim máy > bản chung, lọc hw, ghim mất file → None;
                       #   upload (đọc thẻ, server đặt tên, cùng tên khác sha256 → 409, trong _UPLOAD_LOCK + tmp duy nhất);
                       #   migrate_legacy (gốc → products/<LEGACY_PRODUCT>, chạy ở LIFESPAN startup). Đọc config qua HÀM
                       #   (test vá được). OtaError → HTTPException. QUẢN LÝ MÁY (2026-09-18): devices.json gán tay
                       #   (read_devices/assign_product/unassign_product), product_for(device, declared) = tự khai > gán tay >
                       #   tiền tố > legacy (+conflict), device_status → ota.state ∈ OTA_STATES (on|offered|waiting|skipped|
                       #   unknown|none — MỘT chỗ so version, giữ hậu tố), md5_for, listing/products_summary(effective) → stale/devices
  calib.py             #   ỐNG CHUẨN HIỆU CHUẨN (2026-09-21, thay Sheet+Apps Script bàn giao): pha loãng C1V1=C2V2
                       #   (plan_steps/template), hồi quy linear_fit, blank_stats + lod_nM (3,3·SD/slope theo WI DxD Hub),
                       #   rank_combinations (mọi tổ hợp 1 ống/nồng độ, R²↓ slope↓, PASS theo limits, suggested_sets
                       #   KHÔNG trùng ống), kho file CALIB_DIR/{batches,sets}/<id>.json + limits.json + history.jsonl;
                       #   create_sets tính LẠI hồi quy, chặn ống trùng bộ; update_set theo SET_TRANSITIONS.
                       #   Plan: docs/plan/calib-ong-chuan.md
  monitor.py           #   Số liệu tab Giám sát (root): /proc + statvfs + db.monitor_flow, KHÔNG psutil; route GET /monitor
                       #   gác ota_admin (nhánh ota-rollout-docs-tests 28/08 — ghép lại vào main 2026-09-12)
  db.py                #   Truy cập PostgreSQL: insert_session + query đọc + CRUD bảng users + monitor_flow
  auth.py              #   Tài khoản app: dispatch action HỢP ĐỒNG userAuth.js (login/saveUser/...) cho POST /auth
                       #   5 vai trò (root/admin/manager/operator/user); _MANAGE_SCOPE: root quản lý mọi vai trò,
                       #   manager (quản lý SX) CHỈ quản lý operator; api_token_for: chỉ root/admin nhận token ghi OTA
  main.py              #   FastAPI app + routes (/auth + ingest POST catch-all + /devices /sessions + /ota + log CSKH:
                       #   PUT /devices/{id}/logs, GET /logs/{file} + hồ sơ ATE: PUT|GET /ate/records, /ate/sn/{sn},
                       #   /ate/stats?batch= + TIÊU CHUẨN THEO LÔ: GET|PUT /ate/limits?batch=, GET /ate/limits/list
                       #   + ỐNG CHUẨN /calib/*: GET template · GET|PUT limits · GET|PUT batches · GET|PUT|DELETE
                       #   batches/{id} · GET batches/{id}/rank · PUT batches/{id}/sets · GET sets[/{id}] · PUT sets/{id})
                       #   OTA: /ota/check?device&ver&updated&product&hw · GET /ota[?product=] · GET /ota/products ·
                       #   route CŨ không {product} (= kho LEGACY_PRODUCT, app đang phát hành gọi) · route MỚI
                       #   PUT /ota/{product}[/{file}] (không tên = server đặt từ thẻ) · PUT|DELETE /ota/{product}/target[/{f}]
                       #   · DELETE|GET /ota/{product}/{file} · GET /ota/{product}/progress (trước /{product}/{file}).
                       #   /devices = _device_rows() HỢP NHẤT sessions ∪ fw_seen ∪ devices.json + product_assigned/
                       #   product_effective/product_conflict/last_check + khối ota{state…}; PUT /devices/product?ids= (hàng
                       #   loạt, trước /devices/{device}/…) · PUT|DELETE /devices/{device}/product?clean=. /ota/check ghi
                       #   fw_seen[id].offered = bản vừa mời (_fw_report), kho theo ota.product_for.
scripts/               # CLI dùng lại logic của app/
  migrate_ota.py       #   Xem trước (--dry-run) / chạy tay di cư kho OTA phẳng → products/<legacy>/ (server tự làm lúc restart)
  reconcile.py         #   Nạp bù file data_plus/ vào DB (idempotent qua dedup nội dung)
  import_accounts.py   #   Di cư tài khoản từ CSV (export Google Sheet Accounts) vào bảng users
  import_drive_backup.py #  Nạp log Drive vào kho backup `drive_sessions` (TÁCH khỏi sessions)
  deploy.ps1           #   scp CẢ app/*.py + scripts/migrate_ota.py (+ bản web apps/fbt_rapid/build/web_prod, -WebProd đổi) lên box; -DryRun in lệnh;
                       #   sau scp tự `venv/bin/python -c 'import app.main'` trên box (IMPORT_OK) rồi in lệnh restart/rollback
  check_deploy.py      #   So openapi.json PUBLIC của prod với app.openapi() local (không SSH, không token): route chỉ-local/chỉ-prod/
                       #   đổi chữ ký + md5 app/*.py bản CRLF để đối chiếu md5sum trên box. exit 0 = giống hệt. Chạy TRƯỚC và SAU mỗi deploy
  localtest.ps1        #   Bật/tắt bộ test local: Postgres :5433 + uvicorn :8080 + tài khoản test. KHÔNG có Postgres portable
                       #   mà có `docker` → container `fbt-localtest-pg` (postgres:18-alpine, volume mount /var/lib/postgresql),
                       #   Base %USERPROFILE%\fbt-localtest, tự nạp schema + 3 tài khoản lần đầu (máy Admin 2026-09-21). Base THẬT =
                       #   %LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest (tự rơi sang khi
                       #   đường mặc định trống); dọn postmaster.pid cũ; đặt OTA_ADMIN_TOKEN/OTA_LEGACY_PRODUCT_BY_PREFIX/
                       #   OTA_REQUIRE_TAG=rapid4p giống env production. Web build trước vào apps/fbt_rapid/build/web.
tests/                 # test_logic.py + test_monitor.py (thuần) + test_api.py (smoke, TestClient) + test_auth_roles.py (phạm vi
                       #   quản lý tài khoản, thay app.db bằng kho RAM) + test_ota_products.py (kho OTA theo sản
                       #   phẩm; fixture `kho` vá config.OTA_DIR mỗi test) + test_ota_devices.py (quản lý máy: devices.json,
                       #   ota.state, offered, progress, app_desc, khoá upload; vá cả main._FW_FILE + db.list_devices)
                       #   + test_calib.py (ống chuẩn: hàm thuần theo số liệu sheet bàn giao + template LOD, luồng
                       #   lô → số đo → xếp hạng → bộ → cấp phát; fixture `kho` vá config.CALIB_DIR; ĐỪNG setdefault
                       #   OTA_ADMIN_TOKEN ở đầu file — test_ota_products chạy chung sẽ rớt 401, monkeypatch trong test)
                       #   — chạy được không cần Postgres.
                       #   Env test đặt bằng os.environ.setdefault ở MỌI module (module nào import app.main trước
                       #   cũng ra cùng thư mục; gán đè sau import là module sau trỏ vào thư mục app không dùng)
deploy/                # schema.sql + fbt-receiver.service (deploy lên server)
                       # ⚠️ unit nào chạy python PHẢI trỏ venv/bin/python — python3 hệ thống KHÔNG có psycopg
legacy/                # receiver.py (stdlib cũ) + import_drive_logs.py (Drive — đã bỏ) — giữ tham khảo
requirements.txt       # fastapi, uvicorn, psycopg (pin version)
.gitignore             # loại secret (.ssh, note.md, *.env) + data + venv
README.md              # Tổng quan + tra cứu nhanh + tiến độ
CLAUDE.md              # File này — quy ước làm việc
docs/plan/             # Các kế hoạch phát triển dự án (.md)
  KE_HOACH_PHAT_TRIEN.md    # Kế hoạch chính thức (3 giai đoạn + thứ tự làm)
  ate-ho-so-nghiem-thu.md   # Phần server của trạm ATE (/ate/*) — vì sao lưu file, khi nào chuyển sang bảng
  ota-nhieu-san-pham.md     # Phần server của OTA nhiều sản phẩm: hợp đồng /ota/*, bố cục kho, env; bản đầy đủ
                            #   (firmware + app + lộ trình 4 giai đoạn) ở `apps/fbt_rapid/docs/plan/ota-nhieu-san-pham.md`
  calib-ong-chuan.md        # (2026-09-21) ống chuẩn hiệu chuẩn: quy trình đối chiếu bàn giao Khai ↔ WI DxD Hub, lệch đơn vị
                            #   stock 52 mM/µM, hợp đồng /calib/*, kho file, quyền, lộ trình P1 đọc raw qua UART + in nhãn
  ota-quan-ly-may-nhieu-san-pham.md  # (2026-09-18, B1–B3 ĐÃ DEPLOY 2026-09-23; B4–B5 chưa) quản lý MÁY theo kho: product_effective = tự khai >
                            #   gán tay devices.json > tiền tố > legacy; /devices hợp nhất sessions∪fw_seen∪devices.json
                            #   + ota.state tính ở server; offered trong fw_seen; /ota/{product}/progress; khoá upload;
                            #   thẻ từ esp_app_desc_t (ảnh ESP-IDF); lộ trình B1–B5
  HUONG_DAN_DUNG_SERVER.md  # Runbook dựng lại server từ đầu trên máy mới
  QUY_TRINH_DEPLOY.md       # Checklist deploy server 9 bước (+ web) và đợt deploy đang chờ (§A: 2026-09-20 B1–B3)
  plan.txt                  # Ý tưởng sơ bộ gốc của chủ dự án
docs/history/          # Lịch sử chỉnh sửa (mỗi ngày 1 file YYYY-MM-DD.md)
docs/data_sample/      # Mẫu dữ liệu thiết bị gửi lên (data_RPL.json)
```

## ⚙️ Quy ước TỰ ĐỘNG (Claude phải tự làm, không cần nhắc)
1. **Có kế hoạch phát triển dự án** → viết thành file `.md` trong `docs/plan/`.
2. **Mỗi lần chỉnh sửa/thay đổi dự án** → ghi lại vào `docs/history/YYYY-MM-DD.md` (gộp theo ngày, thêm mục vào file của ngày hiện tại; ngày lấy từ context, chuyển ngày tương đối → tuyệt đối).
3. **Khi dự án có update** → cập nhật ngay `README.md` (tổng quan/tiến độ) và `CLAUDE.md` (nếu cấu trúc/quy ước đổi).
4. Tiếp tục cập nhật plan tương ứng trong `docs/plan/` sau mỗi thay đổi (đừng chỉ trả lời trong chat).

## Thông tin server (để có ngữ cảnh nhanh)
- Hostname `fbt-server` (tên Tailscale: `fbt`) · user `engineer` · IP LAN `192.168.0.103` · truy cập xa qua Tailscale.
- **Deploy code lên box**: `ssh fbt-server` chạy THẲNG từ PowerShell máy dev (host cấu hình sẵn trong
  `~/.ssh/config` → IP Tailscale `100.109.127.87`; bản sao config/key nằm ở `Server/.ssh/`). Box giữ
  **bản copy PHẲNG** ở `~/fbt_server/` (KHÔNG phải git clone) → deploy = `scp` các file đã sửa vào
  `~/fbt_server/app|scripts/` rồi `ssh fbt-server "sudo systemctl restart fbt-receiver"` (gián đoạn ~3s,
  thiết bị tự POST lại). ⚠️ **Box KHÔNG chắc chạy code của `main`** — 28/08 nó nhận code từ nhánh
  `ota-rollout-docs-tests` (monitor.py) và deploy từ `main` 2026-09-12 đã ghi đè mất `/monitor`. Trước
  khi scp: `git fetch` + so `ls app/*.py` trên box với local; `scripts/deploy.ps1 -Server` tự `cp -a app
  app.bak.<stamp>` trước khi ghi. Từ 2026-09-12 box ADM SSH được thẳng (`tailscale set --ssh=false` trên
  box + key `id_ed25519` của ADM trong `authorized_keys`; Tailscale SSH đã TẮT — ai vào bằng Tailscale SSH
  cũ phải chuyển sang key/mật khẩu). Không SSH được thì còn **Cockpit** `https://100.109.127.87:9090`
  (Terminal trong trình duyệt, tài khoản `engineer`) — chuyển file bằng cách dán `base64` của `tar.gz`. `sudo` cần mật khẩu thì phải chạy tương tác — session AI non-interactive sẽ kẹt;
  AI cũng KHÔNG được phép restart/ghi production qua SSH (permission classifier chặn) → AI chỉ scp + kiểm
  tra, 2 lệnh sudo (restart, GRANT) đưa user tự dán. **Kiểm "code mới đã NẠP chưa"** (file mới trên đĩa ≠
  tiến trình đang chạy): `curl -s http://127.0.0.1:8080/openapi.json` (public, khỏi token) xem route có mặt
  — mọi POST đều 401/catch-all nên không phân biệt được bằng gọi thử; so code đĩa vs local bằng `md5sum`.
  `engineer` KHÔNG đọc được `/etc/fbt-receiver.env` (cần sudo) → không lấy token test từ box non-interactive.
- Domain public: `https://fbt.basa-luma.ts.net` (Tailscale Funnel → cổng 8080, bật 2026-07-11).
- OS Debian headless · PostgreSQL 17 (DB `mydb`, owner `myuser`).
- `receiver.py` chạy nền bằng systemd `fbt-receiver`, cổng 8080.
- Cấu hình: 3.7GB RAM · 4 nhân · ổ 113GB.

## Ghi chú làm việc
- Trả lời bằng tiếng Việt.
- Kế hoạch phát triển: [docs/plan/KE_HOACH_PHAT_TRIEN.md](docs/plan/KE_HOACH_PHAT_TRIEN.md).
- Dựng lại server từ đầu: [docs/plan/HUONG_DAN_DUNG_SERVER.md](docs/plan/HUONG_DAN_DUNG_SERVER.md) (thay `KE_HOACH_DUNG_SERVER.md` đã thất lạc).
- **Deploy bản mới lên box**: theo [docs/plan/QUY_TRINH_DEPLOY.md](docs/plan/QUY_TRINH_DEPLOY.md); AI chạy được
  `python scripts/check_deploy.py` (public openapi, không SSH) để biết prod lệch gì — KHÔNG thử ssh.
  Script import `app.main` nên **python hệ thống thiếu fastapi → lỗi ngay**; venv `%LOCALAPPDATA%\fbt-localtest`
  shell AI không thấy → tạo venv trong scratchpad: `pip install fastapi httpx pytest jsonschema psycopg[binary]
  uvicorn` (thiếu `jsonschema` thì `tests/test_api.py::test_ingest_reader_theo_hop_dong` fail, 116/117 —
  không phải lỗi code). **Máy Admin**: venv IDF `C:\Espressif\python_env\idf5.5_py3.11_env` đã cài thêm
  fastapi/uvicorn/jsonschema (2026-09-21) → `…\Scripts\python.exe -m pytest tests -q` chạy thẳng cả bộ (130 test),
  nhớ `PYTHONIOENCODING=utf-8` vì thông báo test tiếng Việt. Kiểm 2026-09-21: prod vẫn 36 route (bản 09-12),
  đợt B1–B3 + `/calib/*` + `/logs` ĐÃ DEPLOY 2026-09-23 (prod = local 56 route). SSH từ máy Admin: `deploy.ps1 -Server -Target fbt-server` (host mặc định không khớp `~/.ssh/config` → Permission denied).
- **Tích hợp RAPID ERP** (2026-09-17, chờ chốt §10): plan xuyên phần `docs/plan/erp-feed-engineer-server.md`
  (gốc monorepo) — ERP **kéo** qua `/erp/v1/*` **chỉ GET** (POST catch-all), token riêng chỉ-đọc
  `ERP_READ_TOKENS`; KHÔNG đồng bộ lại kết quả đo vì firmware đã POST cùng payload thẳng vào ERP
  (`postJsonToAllTargets`). Source ERP `fbterp` trên box ADM: `..\..\04. fbterp` (repo khác, chỉ đọc;
  FastAPI + Celery beat, bảng `device_registry`/`test_results`/`device_qc_records`).

## Gotchas server / deploy / box (chuyển từ CLAUDE.md app 2026-09-15 — đã gặp thật)

- **Sản phẩm N khe (rapid4p, 2026-09-17)**: `logic.validate` KHÔNG ghim độ dài mảng bằng số cứng như Rapid+
  (`ARRAY_FIELDS` = 10) mà đọc `payload["slots"]` (1..16) và đòi mọi mảng `SLOT_ARRAY_FIELDS`
  (`slot_value/slot_result/slot_positive/calib_min/calib_max`, config.py) đúng `slots` phần tử. Hợp đồng:
  `system/contracts/ingest-rapid4p.schema.json`; đổi số khe = đổi `BOARD_SENSOR_SLOTS` firmware + registry
  `optical_slots`, server không cần sửa. **Reader (1 khe) đi CÙNG đường này** với `slots: 1` ghim trong
  `system/contracts/ingest-reader.schema.json` (2026-09-18) — không mở nhánh riêng cho sản phẩm 1 khe;
  3 số đọc thô của Reader nằm ở `readings[3]` (không thuộc `SLOT_ARRAY_FIELDS`) nên không bị đòi = `slots`.
  Mẫu: `docs/data_sample/data_reader.json`, test `test_api.py::test_ingest_reader_theo_hop_dong`.

- **Deploy từ box ADM — ĐÃ MỞ SSH thẳng 2026-09-12** (trước đó bị Tailscale SSH chặn: `tailnet policy
  does not permit you to SSH`; LAN 22 timeout; tên ngắn `fbt` không resolve → dùng IP `100.109.127.87`
  hoặc `fbt.basa-luma.ts.net`). Cách mở: trên box `sudo tailscale set --ssh=false` (OpenSSH nhận lại
  cổng 22) + thêm `~/.ssh/id_ed25519.pub` của ADM vào `authorized_keys` của `engineer` — người dùng tự
  làm qua **Cockpit** `https://100.109.127.87:9090` (cổng 9090 KHÔNG dính chính sách Tailscale SSH;
  có Terminal trong trình duyệt). Không SSH được thì chuyển file qua Cockpit bằng cách dán `base64` của
  `tar.gz` (KHÔNG zip của PowerShell 5.1 — nó ghi path bằng `\`, giải nén Linux ra tên hỏng). Quy trình
  gói sẵn trong **`server\scripts\deploy.ps1`** (`-Server` scp CẢ `app/*.py` + `scripts/migrate_ota.py`,
  tự `cp -a app app.bak.<stamp>` trên box trước; `-Web` scp NỘI DUNG `build\web_prod` đúng gotcha
  `scp -O`; `-DryRun` chỉ in lệnh); restart `fbt-receiver` vẫn phải tự chạy (sudo cần mật khẩu, `-t`).
  Bản web production build ra **`build\web_prod`** (`--output build/web_prod`, KHÔNG dart-define) để
  không đè bản test local ở `build\web`. Windows OpenSSH hỏi host key rồi KHÔNG nhận "yes" (lặp vô hạn)
  → thêm `-o StrictHostKeyChecking=accept-new`. Ba bẫy trong chính `deploy.ps1` (dính 17:44 cùng ngày):
  `Run` đi qua `Invoke-Expression` nên lệnh remote KHÔNG được chứa `$(…)`/`$biến` (PowerShell diễn giải
  lại — `2>/dev/null` thành `C:\dev
ull`), dùng `xargs -I{}`; dọn `web.bak.*` phải xếp theo TÊN
  (`sort -r`) vì `cp -a` giữ mtime → `ls -t` coi bản vừa chép là cũ nhất và xoá nó; bak cũ có thư mục
  `dr-x` → `chmod -R u+rwX` trước `rm`, và bước dọn kết thúc bằng `; true` để không chặn deploy.
  Sau deploy web, kiểm bằng **`fbt.basa-luma.ts.net`** (thẳng box); `hub.fortebio.tech` qua Cloudflare
  có thể còn `cf-cache-status: HIT` bản cũ tới hết TTL 4 h nếu object được cache TRƯỚC khi origin phát
  `no-cache` — purge trên dashboard hoặc đợi, không phải lỗi deploy. **Kiểm deploy KHÔNG cần token** (đủ để kết luận): (1)
  `curl …/openapi.json` rồi so `json.dumps(sort_keys)` với `app.openapi()` sinh từ code local (TestClient
  env tạm) — giống hệt = đúng code đang chạy, khác = liệt kê route lệch; (2) route mới phải **401** khi
  thiếu token (có route, gác nguyên); (3) web: md5 `main.dart.js` tải từ `/app/` == `build/web_prod` ==
  file trên box, và `grep -c` khoá i18n đặc trưng (`nav.monitor`, `statusError`, `binGone`) trong bundle.
  ⚠️ `md5sum` in dấu `\` ĐẦU DÒNG khi path có backslash (Windows) → so md5 bằng mắt/`uniq` sau khi bỏ
  ký tự đó, đừng `cut -c1-32` rồi kết luận "khác". Và so file trên box với **file trong cây làm việc**,
  KHÔNG với `git show HEAD:file | md5sum`: git lưu LF, checkout Windows ra CRLF và scp đẩy bản CRLF lên
  box → md5 blob git luôn "khác" dù nội dung y hệt (`monitor.py` trùng vì file đó vốn LF). **Trước khi bảo người dùng restart** (sudo, tay):
  `ssh … 'cd ~/fbt_server && venv/bin/python -c "import app.main"'` — import bằng venv THẬT của box bắt
  được thiếu package/lỗi cú pháp mà không phải hạ service (`__init__.py` nạp trễ nên import không có
  tác dụng phụ). Handler FastAPI `async def` (cần `await request.body()`) mà làm việc chặn (băm/ghi
  MB) thì `await run_in_threadpool(...)` — worker duy nhất, treo loop là treo cả `/ingest`.
- **Box production KHÔNG chắc chạy code của `main` — kiểm trước khi deploy** (dính thật 2026-09-12):
  box nhận code nhánh `origin/ota-rollout-docs-tests` (28/08: `server/app/monitor.py` + route
  `/monitor`, app có `monitor_screen.dart`, tải hàng loạt, CSV rollout) mà clone ADM chưa merge; scp từ
  `main` ghi đè `main.py`/`db.py`/`__init__.py` → tab Giám sát của bản web gãy ~35 phút. Dấu vết: file
  trên box **không có trong local** (`monitor.py` mtime cũ + `.pyc`). Đã ghép lại 4 file server từ commit
  `8bc627d`, rồi **merge cả nhánh vào `main`** (`7687b80`, 2026-09-12) và build+deploy web từ cây đã merge
  (bản `/app/` giờ có đủ Giám sát + tải hàng loạt + CSKH + ATE). Luật: `git fetch` + `git branch -r
  --no-merged` + so `ls app/*.py` trên box với local trước mọi lần deploy. Khi merge: tab Giám sát gác
  `isRoot` chứ KHÔNG `canManageUsers` (từ 09-07 manager cũng quản lý được tài khoản); `saveTextFileDialog`
  có cả `label` lẫn `extensions`, `label` trống = suy từ đuôi.
- **Test server `GET /ota/../../note.md` trả 405 chứ không 404 với httpx mới**: httpx chuẩn hoá `..`
  TRƯỚC khi gửi → request thành `GET /note.md` → khớp catch-all `POST /{path}` sai method → 405.
  Không phải lỗi server (path không tới handler, không lộ file) — assert nên là `in (404, 405)`.
- **`server/app/main.py` KHÔNG được có tác dụng phụ ghi đĩa lúc import** (ngoài `mkdir`):
  `app/__init__.py` import `app.main`, nên MỌI script/test import `app.*` đều chạy main.py —
  đặt `ota.migrate_legacy()` dưới `FastAPI(...)` làm `scripts/migrate_ota.py --dry-run` dời file
  THẬT (đã dính 2026-09-11). Việc "chạy một lần lúc khởi động" đặt trong `lifespan` (chỉ tiến
  trình uvicorn thật chạy; `TestClient` không dùng `with` thì cũng không chạy).
- **Web sau deploy: icon MỚI hiện thành ô trống, icon cũ vẫn hiện = FONT ICON CŨ bị cache, không phải
  lỗi build** (2026-09-12): Flutter tree-shake `MaterialIcons-Regular.otf` theo bộ icon của TỪNG build
  nhưng phát ở CÙNG URL; Cloudflare (`hub.fortebio.tech`) gắn `max-age=14400` cho file tĩnh → trình
  duyệt/CDN giữ font của bản trước 4 giờ trong khi `main.dart.js` đã mới. Chẩn đoán đúng thứ tự:
  (1) `curl` md5 font trên server so với `build/web_prod` (giống = server đúng); (2) `fonttools`
  (`TTFont(...).getBestCmap()`) so codepoint từ `packages/flutter/lib/src/material/icons.dart` → font mới
  có đủ glyph; (3) kết luận cache. Sửa gốc: middleware `Cache-Control: no-cache` cho `/app/*`
  (`main.py::_web_no_cache`, có test) — trình duyệt/CDN hỏi lại bằng ETag → 304, deploy xong thấy ngay;
  người dùng đang kẹt thì `Ctrl+Shift+R` hoặc Clear site data. Cảnh báo build "Expected to find fonts
  for … CupertinoIcons" là của framework, vô hại.
- **OTA nhiều thiết bị — 2 lỗ hổng đã kiểm thực tế trên server local (rà 2026-09-18, ĐÃ SỬA cùng ngày —
  giữ lại làm bài học; chi tiết `docs/history/2026-09-18.md`)**:
  (1) `ota.upload` KHÔNG nằm trong `_CFG_LOCK` và hai PUT cùng tên dùng CÙNG file tạm `.tmp-<name>` →
  hai upload đồng thời cùng tên khác nội dung: trên Windows một request **500 `PermissionError`** (exception
  trần, không phải `OtaError`), trên Linux cả hai 200 và bản sau đè → **luật 409 "một tên = một nội dung"
  bị lách**; sửa = khoá theo tên hoặc mở tmp `O_EXCL`. (2) `GET /devices` lấy danh sách từ bảng `sessions`
  rồi mới đắp `fw_seen.json` → máy CHỈ poll `/ota/check` mà chưa gửi phiên đo (vừa nạp xong, Rapid4P chưa
  có bo cảm biến) **vô hình** trong bảng tiến độ; sửa = union thêm khoá `fw_seen` chưa có (`sessions: 0`).
  Cách kiểm nhanh không cần Postgres: venv scratchpad (fastapi+uvicorn+httpx+pytest) chạy
  `uvicorn app.main:app` với `FBT_*_DIR` trỏ scratchpad — `/ota/*` chạy đủ, `/devices` 500 vì thiếu DB.
- **Ảnh Arduino (Rapid+, Reader) tự khai `esp_app_desc_t.project_name = "arduino-lib-builder"`** (dính
  2026-09-18 khi thêm thẻ app_desc cho ảnh ESP-IDF): MỌI ảnh ESP32 có struct này ở offset 0x20, và core
  Arduino bake `project_name`/`version` của lib-builder vào (đọc từ `~/.platformio/packages/
  framework-arduinoespressif32/tools/sdk/esp32/lib/libapp_update.a`) — chuỗi đó **hợp lệ theo regex khoá
  sản phẩm**, nên "nhận app_desc làm thẻ khi project_name hợp lệ" là mọi upload Rapid+ bị 400 "ảnh tự khai
  product 'arduino-lib-builder'". Luật: `logic.image_tag(raw, products)` chỉ coi app_desc là thẻ khi
  `project_name` ∈ {kho đích} ∪ kho đã có; FBTIMG1 vẫn là đường của ảnh Arduino. Test giữ luật:
  `test_ota_devices.py::test_anh_arduino_khong_bi_coi_la_the`. Kiểm ảnh lạ: `python -c
  "b=open(f,'rb').read();print(b[0x30:0x50],b[0x50:0x70])"`.
- **Test vá `config.OTA_DIR` CHƯA đủ cho fw_seen**: `main._FW_FILE`/`_FW_LOG_FILE` chụp đường dẫn lúc
  import → test đọc `fw_seen` phải `monkeypatch.setattr(main, "_FW_FILE", tmp)` (fixture `kho` của
  `test_ota_devices.py`), không thì các test dùng chung một file và máy của test trước lọt vào `/devices`
  của test sau.
- **Server: `pathlib.Path.glob('*.bin')` KHỚP CẢ dotfile** (khác glob của shell) → file tạm `.tmp-<name>.bin`
  của upload bị ngắt bị liệt kê/di cư như ảnh thật; mọi chỗ glob kho phải lọc `not p.name.startswith('.')`.
  Và **việc "chạy một lần lúc khởi động" trong `lifespan` phải bọc `try/except` + log** — ném lỗi ở đó là
  uvicorn không lên, mất luôn `/ingest` của cả fleet vì một thao tác dọn dẹp (code-review 2026-09-12).
- **Route server mới KHÔNG được là POST** (đã ghi ở mục OTA, lặp lại vì dễ quên): `POST /{path}`
  catch-all ingest nuốt mọi POST → 400 "invalid". Log CSKH dùng `PUT /devices/{id}/logs`; tên file
  trả về dài hơn 64 ký tự nên `GET /logs/{file}` kiểm tên bằng regex riêng, KHÔNG `safe_name` (cắt 64
  ký tự → đổi tên → 404 sai).
- **"Deploy rồi mà web chưa thấy tính năng" — dò theo CHUỖI THAM CHIẾU, đừng đoán cache**:
  `curl /app/` xem `index.html` trỏ bootstrap nào → `curl` bootstrap đó lấy `mainJsPath` →
  `curl` file `main.<hash>.dart.js` đó rồi so **md5 với `build/web/main.dart.js`** và grep khoá
  i18n. Khớp hết = server đúng, lỗi ở TRÌNH DUYỆT người dùng: `index.html` được trả **KHÔNG kèm
  `Cache-Control`** (chỉ `Last-Modified`) nên tab đang mở giữ JS cũ vô thời hạn → Ctrl+Shift+R,
  hoặc thử **cửa sổ ẩn danh** (phép thử dứt điểm). Service worker KHÔNG phải thủ phạm: Flutter đời
  này sinh bản "tự huỷ" (815 B, `unregister()` + reload) nên không cache app; nó giống nhau mọi
  lần build, deploy script bỏ qua là ĐÚNG.
- **Engineer Server trả 500 = tầng Postgres trên box chưa sẵn sàng** (đúng token vẫn 500): bảng
  `sessions`/role chưa tạo (chưa chạy `deploy/schema.sql`) hoặc Postgres/psycopg thiếu — KHÔNG phải
  lỗi app. `/ingest` vẫn 200 (file-first, catch lỗi DB) nên thiết bị đẩy được mà app không đọc được.
  Chẩn đoán trên box: `journalctl -u fbt-receiver -n 30`; sau khi tạo schema phải chạy
  `reconcile.py` nạp file JSON cũ vào DB, không thì `/devices` trả danh sách RỖNG.
- **`localtest.ps1` chế độ Docker (máy không có Postgres portable, 2026-09-21)**: ảnh `postgres:18-alpine` đổi chỗ
  dữ liệu — volume phải mount ở **`/var/lib/postgresql`** (PG ≤ 17 là `…/data`); mount kiểu cũ thì container
  thoát ngay với "unused mount/volume" và `docker exec … pg_isready` báo "container is not running" — xem
  `docker logs fbt-localtest-pg`, `docker rm -f` + `docker volume rm` rồi chạy lại. Tài khoản test chỉ tạo khi
  bảng `users` trống (kiểm bằng `db.list_users()` qua venv), nên chạy lặp vô hại. Base ở chế độ này là
  `%USERPROFILE%\fbt-localtest` (không dùng `AppData\Local` vì sandbox gói Claude ảo hoá — gotcha bên dưới).
  PowerShell CỦA NGƯỜI DÙNG máy Admin có `ExecutionPolicy Restricted` → script chạy từ tool AI được nhưng người
  dùng gõ `.\scripts\localtest.ps1` bị "running scripts is disabled": đưa lệnh dạng
  `powershell -ExecutionPolicy Bypass -File <đường dẫn>\localtest.ps1 [-Stop]` (hoặc họ tự
  `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` một lần).
  Tool PowerShell của AI gọi script LỒNG qua `powershell -File …` thì TREO tới timeout (uvicorn con giữ pipe) dù
  server đã lên — gọi thẳng `.\scripts\localtest.ps1` từ tool, hoặc kiểm bằng `curl /openapi.json` thay vì đợi.
  Server mount `FBT_WEB_DIR` lúc khởi động → build lại web xong phải chạy lại script mới thấy `/app/` (405 = chưa mount).
- **Kiểm server local (`localtest.ps1`) ĐỪNG tin `Test-Path "$env:LOCALAPPDATA\fbt-localtest\…"` từ tool
  shell của AI** (2026-09-17): trả `False` cho cả `pgsql`, `pgdata`, `venv` trong khi Postgres :5433 +
  uvicorn :8080 đang chạy đúng từ các path đó — Windows ảo hoá `AppData\Local` (gotcha Python 3.14 ở
  CLAUDE.md gốc) che thư mục với shell của tool; Bash cũng không `tail` được `uvicorn.err` ở đó. Kiểm
  đúng cách: `Get-NetTCPConnection -LocalPort 8080,5433 -State Listen` → `Get-CimInstance Win32_Process
  -Filter "ProcessId=<pid>"` xem CommandLine, rồi `curl http://127.0.0.1:8080/openapi.json`. Chạy
  `localtest.ps1` khi server đã bật cũng vô hại (tự kill bản cũ trên :8080 rồi bật lại).
- **`openapi.json` giống hệt CHƯA đủ kết luận "đã deploy"** (kiểm trước deploy 2026-09-18): openapi
  chỉ phản ánh route/chữ ký trong `main.py`; đổi CHỈ `logic.py`/`config.py` (vd `validate()` N khe
  rapid4p 09-17, thêm `SLOT_ARRAY_FIELDS`) thì openapi production vẫn khớp local 36/36 dù box chưa
  có code mới. Muốn chắc phải so `md5sum app/logic.py app/config.py` trên box với **bản CRLF trong
  cây làm việc**. Cùng phiên: `ssh` từ tool AI bị classifier chặn **kể cả lệnh chỉ đọc** (`ls`,
  `md5sum`, `systemctl is-active`) → đừng thử lại nhiều kiểu, đưa lệnh md5 cho người dùng tự chạy.
  Nhớ `logic.py` import `SLOT_ARRAY_FIELDS` từ `config.py` → scp lẻ một file rồi restart là
  `ImportError`, uvicorn không lên, mất `/ingest` cả fleet; `deploy.ps1 -Server` chép cả 9 file
  `app/*.py` là vì thế.
