# CLAUDE.md — hướng dẫn cho Claude trong dự án này

## Dự án
**FBT Home Server** — MiniPC dựng thành server 24/7 (Debian headless + PostgreSQL), nhận data từ thiết bị và cho code từ xa. Repo này chứa tài liệu vận hành + code nhận data.

## Cấu trúc
```
app/                   # Package service (FastAPI) — chạy: uvicorn app.main:app
  config.py            #   Cấu hình từ biến môi trường (DATA_DIR, TOKEN, DB, OTA_DIR, LOGS_DIR, ATE_DIR,
                       #   OTA_LEGACY_PRODUCT[_BY_PREFIX], OTA_REQUIRE_TAG...)
  logic.py             #   Hàm thuần: safe_name, check_auth, validate, hash (scrypt + sha256$ legacy Sheet), parse_*,
                       #   + hồ sơ ATE: validate/normalize_ate_record, ate_stats (FPY/Pareto), DEFAULT_ATE_LIMITS
                       #   + OTA: product_key (khoá [a-z0-9-], cấm check|products|target), parse_image_tags (thẻ
                       #   `FBTIMG1;product=;ver=;hw=;;` nhúng trong .bin), ver_from_name, expected_bin_name
  ota.py               #   KHO OTA THEO SẢN PHẨM (2026-09-11): OTA_DIR/products/<product>/{*.bin, *.bin.json manifest,
                       #   target.json}; resolve(product, device, hw) = ghim máy > bản chung, lọc hw, ghim mất file → None;
                       #   upload (đọc thẻ, server đặt tên, cùng tên khác sha256 → 409); migrate_legacy (gốc → products/
                       #   <LEGACY_PRODUCT>, chạy ở LIFESPAN startup). Đọc config qua HÀM (test vá được). OtaError → HTTPException
  monitor.py           #   Số liệu tab Giám sát (root): /proc + statvfs + db.monitor_flow, KHÔNG psutil; route GET /monitor
                       #   gác ota_admin (nhánh ota-rollout-docs-tests 28/08 — ghép lại vào main 2026-09-12)
  db.py                #   Truy cập PostgreSQL: insert_session + query đọc + CRUD bảng users + monitor_flow
  auth.py              #   Tài khoản app: dispatch action HỢP ĐỒNG userAuth.js (login/saveUser/...) cho POST /auth
                       #   5 vai trò (root/admin/manager/operator/user); _MANAGE_SCOPE: root quản lý mọi vai trò,
                       #   manager (quản lý SX) CHỈ quản lý operator; api_token_for: chỉ root/admin nhận token ghi OTA
  main.py              #   FastAPI app + routes (/auth + ingest POST catch-all + /devices /sessions + /ota + log CSKH:
                       #   PUT /devices/{id}/logs, GET /logs/{file} + hồ sơ ATE: PUT|GET /ate/records, /ate/sn/{sn},
                       #   /ate/stats?batch= + TIÊU CHUẨN THEO LÔ: GET|PUT /ate/limits?batch=, GET /ate/limits/list)
                       #   OTA: /ota/check?device&ver&updated&product&hw · GET /ota[?product=] · GET /ota/products ·
                       #   route CŨ không {product} (= kho LEGACY_PRODUCT, app đang phát hành gọi) · route MỚI
                       #   PUT /ota/{product}[/{file}] (không tên = server đặt từ thẻ) · PUT|DELETE /ota/{product}/target[/{f}]
                       #   · DELETE|GET /ota/{product}/{file}. /devices trả thêm product/hw/product_effective.
scripts/               # CLI dùng lại logic của app/
  migrate_ota.py       #   Xem trước (--dry-run) / chạy tay di cư kho OTA phẳng → products/<legacy>/ (server tự làm lúc restart)
  reconcile.py         #   Nạp bù file data_plus/ vào DB (idempotent qua dedup nội dung)
  import_accounts.py   #   Di cư tài khoản từ CSV (export Google Sheet Accounts) vào bảng users
  import_drive_backup.py #  Nạp log Drive vào kho backup `drive_sessions` (TÁCH khỏi sessions)
  deploy.ps1           #   scp CẢ app/*.py + scripts/migrate_ota.py (+ bản web build/web_prod) lên box; -DryRun in lệnh
  localtest.ps1        #   Bật/tắt bộ test local: Postgres portable :5433 + uvicorn :8080 + tài khoản test
tests/                 # test_logic.py (thuần) + test_api.py (smoke, TestClient) + test_auth_roles.py (phạm vi
                       #   quản lý tài khoản, thay app.db bằng kho RAM) + test_ota_products.py (kho OTA theo sản
                       #   phẩm; fixture `kho` vá config.OTA_DIR mỗi test) — chạy được không cần Postgres.
                       #   Env test đặt bằng os.environ.setdefault ở MỌI module (module nào import app.main trước
                       #   cũng ra cùng thư mục; gán đè sau import là module sau trỏ vào thư mục app không dùng)
deploy/                # schema.sql + fbt-receiver.service (deploy lên server)
legacy/                # receiver.py (stdlib cũ) + import_drive_logs.py (Drive — đã bỏ) — giữ tham khảo
requirements.txt       # fastapi, uvicorn, psycopg (pin version)
.gitignore             # loại secret (.ssh, note.md, *.env) + data + venv
README.md              # Tổng quan + tra cứu nhanh + tiến độ
CLAUDE.md              # File này — quy ước làm việc
docs/plan/             # Các kế hoạch phát triển dự án (.md)
  KE_HOACH_PHAT_TRIEN.md    # Kế hoạch chính thức (3 giai đoạn + thứ tự làm)
  ate-ho-so-nghiem-thu.md   # Phần server của trạm ATE (/ate/*) — vì sao lưu file, khi nào chuyển sang bảng
  ota-nhieu-san-pham.md     # Phần server của OTA nhiều sản phẩm: hợp đồng /ota/*, bố cục kho, env; bản đầy đủ
                            #   (firmware + app + lộ trình 4 giai đoạn) ở repo app `docs/plan/ota-nhieu-san-pham.md`
  HUONG_DAN_DUNG_SERVER.md  # Runbook dựng lại server từ đầu trên máy mới
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
