# CLAUDE.md — hướng dẫn cho Claude trong dự án này

## Dự án
**FBT Home Server** — MiniPC dựng thành server 24/7 (Debian headless + PostgreSQL), nhận data từ thiết bị và cho code từ xa. Repo này chứa tài liệu vận hành + code nhận data.

## Cấu trúc
```
app/                   # Package service (FastAPI) — chạy: uvicorn app.main:app
  config.py            #   Cấu hình từ biến môi trường (DATA_DIR, TOKEN, DB...)
  logic.py             #   Hàm thuần: safe_name, check_auth, validate, hash (scrypt + sha256$ legacy Sheet), parse_*
  db.py                #   Truy cập PostgreSQL: insert_session + query đọc + CRUD bảng users
  auth.py              #   Tài khoản app: dispatch action HỢP ĐỒNG userAuth.js (login/saveUser/...) cho POST /auth
  main.py              #   FastAPI app + routes (/auth + ingest POST catch-all + /devices /sessions...)
scripts/               # CLI dùng lại logic của app/
  reconcile.py         #   Nạp bù file data_plus/ vào DB (idempotent qua dedup nội dung)
  import_accounts.py   #   Di cư tài khoản từ CSV (export Google Sheet Accounts) vào bảng users
  import_drive_backup.py #  Nạp log Drive vào kho backup `drive_sessions` (TÁCH khỏi sessions)
tests/                 # test_logic.py (thuần) + test_api.py (smoke, TestClient) — chạy được không cần pytest
deploy/                # schema.sql + fbt-receiver.service (deploy lên server)
legacy/                # receiver.py (stdlib cũ) + import_drive_logs.py (Drive — đã bỏ) — giữ tham khảo
requirements.txt       # fastapi, uvicorn, psycopg (pin version)
.gitignore             # loại secret (.ssh, note.md, *.env) + data + venv
README.md              # Tổng quan + tra cứu nhanh + tiến độ
CLAUDE.md              # File này — quy ước làm việc
docs/plan/             # Các kế hoạch phát triển dự án (.md)
  KE_HOACH_PHAT_TRIEN.md    # Kế hoạch chính thức (3 giai đoạn + thứ tự làm)
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
  thiết bị tự POST lại). `sudo` cần mật khẩu thì phải chạy tương tác — session AI non-interactive sẽ kẹt;
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
