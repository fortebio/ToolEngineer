# FBT Home Server

MiniPC cũ dựng thành server chạy 24/7: **Debian + PostgreSQL**, truy cập/code từ xa qua **Tailscale + VS Code Remote-SSH**.

📖 Kế hoạch phát triển (3 giai đoạn + thứ tự làm): [docs/plan/KE_HOACH_PHAT_TRIEN.md](docs/plan/KE_HOACH_PHAT_TRIEN.md)

> ⚠️ File hướng dẫn dựng cũ `KE_HOACH_DUNG_SERVER.md` đã thất lạc (thử khôi phục từ OneDrive version history). Lệnh tra cứu chính vẫn ở mục dưới.

## Cấu trúc repo

| | |
|---|---|
| [app/](app/) | Package service FastAPI (`config`/`logic`/`db`/`auth`/`main`) — chạy `uvicorn app.main:app` |
| [scripts/](scripts/) | CLI: `reconcile.py` (nạp bù file `data_plus/` khi DB down) · `import_accounts.py` (di cư tài khoản từ Sheet CSV) |
| [tests/](tests/) | `test_logic.py` + `test_api.py` — chạy `python tests/test_logic.py` hoặc `pytest` |
| [deploy/](deploy/) | `schema.sql` + `fbt-receiver.service` + migration |
| 📖 **Dựng lại từ đầu** | [docs/plan/HUONG_DAN_DUNG_SERVER.md](docs/plan/HUONG_DAN_DUNG_SERVER.md) |
| [legacy/](legacy/) | `receiver.py` (stdlib cũ), `import_drive_logs.py` (Drive — đã bỏ) — tham khảo |
| [docs/plan/](docs/plan/) | Kế hoạch phát triển dự án |
| [docs/history/](docs/history/) | Lịch sử chỉnh sửa (theo ngày) |
| [CLAUDE.md](CLAUDE.md) | Quy ước làm việc cho Claude |

## Thông số

| | |
|---|---|
| OS | Debian (headless, không GUI) |
| Database | PostgreSQL 17 |
| RAM / CPU / Ổ | 3.7 GB / 4 nhân / 113 GB (trống ~104 GB) |
| Hostname | `fbt-server` |
| User | `engineer` |
| IP LAN | `192.168.0.103` |
| Truy cập từ xa | Tailscale (SSH key, không mở port ra Internet) |
| Domain public | `https://fbt.basa-luma.ts.net` (Tailscale Funnel → cổng 8080) |

## Tra cứu nhanh

```bash
# Kết nối (đã cấu hình ~/.ssh/config)
ssh fbt-server

# Database
sudo -u postgres psql -l                 # liệt kê DB
sudo -u postgres psql mydb               # vào mydb

# Trạng thái dịch vụ
systemctl status postgresql tailscaled ssh fbt-receiver

# Data nhận từ thiết bị
ls -l ~/fbt_server/data_plus/            # file đã lưu
journalctl -u fbt-receiver -f            # xem log nhận realtime

# Tài nguyên
free -h ; df -h / ; htop

# Tailscale
tailscale status ; tailscale ip -4
```

- **Database:** `mydb` (owner `myuser`) — dữ liệu ở `/var/lib/postgresql/17/main/`, cấu hình ở `/etc/postgresql/17/main/`.
- **Nhận data từ thiết bị:** [receiver.py](receiver.py) lắng nghe POST ở cổng **8080**, lưu mỗi request thành file JSON trong `~/fbt_server/data_plus/`. Chạy nền bằng systemd `fbt-receiver`. Đặt `RECEIVER_TOKEN` → yêu cầu header `Authorization: Bearer <token>`. Mở ra Internet qua **Tailscale Funnel** — domain chính thức: **`https://fbt.basa-luma.ts.net`** (bật 2026-07-11, đã xoay token trước khi mở). Thiết bị POST và app Flutter đều dùng URL này + header `Authorization: Bearer <token>`. Lệnh quản lý Funnel ở [kế hoạch phát triển](docs/plan/KE_HOACH_PHAT_TRIEN.md) mục 2e.
- **Code từ xa:** VS Code → `Remote-SSH: Connect to Host` → `fbt-server` (Tailscale bật trên laptop).
- **Backup:** dùng `pg_dump`, KHÔNG copy file DB trực tiếp (xem [kế hoạch phát triển](docs/plan/KE_HOACH_PHAT_TRIEN.md) mục 2b).

## Tiến độ dựng server

- [x] Cài Debian headless
- [x] Cấp quyền sudo cho `engineer`
- [x] SSH key từ laptop → server
- [x] Cài PostgreSQL 17 + tạo `mydb`/`myuser`
- [x] Deploy `receiver.py` (nhận POST → lưu JSON) + service `fbt-receiver` — xác nhận đang chạy 2026-07-11
- [x] `fbt-receiver` **enabled** (xác nhận 2026-07-13: `postgresql tailscaled ssh fbt-receiver` đều `enabled` → tự bật lại sau reboot) · timezone VN đã đặt cho role engineer
- [ ] An toàn mất nguồn: BIOS `Restore on AC Power Loss = Power On` + rút điện test 1 lần (mắt xích cuối cho tình huống cúp điện thật)
- [x] Cố định IP (DHCP Reservation trên router)
- [x] Cài Tailscale (server + laptop) để truy cập từ xa
- [ ] Xoay secret bị lộ (note.md + khóa SSH trong OneDrive) — hoãn khi chạy thử, bắt buộc trước khi chạy thật
- [ ] Backup tự động hằng ngày + test restore
- [ ] Bảo mật SSH: tắt password sau khi key chạy ổn
- [x] Nạp dữ liệu vào PostgreSQL (bảng `sessions`) + API đọc cho Flutter — deploy + chạy 2026-07-11: **3.372 phiên / 88 thiết bị** từ Drive, thiết bị vẫn POST realtime, API chạy qua `https://fbt.basa-luma.ts.net`
- [x] Tài khoản đăng nhập app chuyển từ Google Sheet về bảng `users` — deploy 2026-07-14 (`POST /auth` hợp đồng userAuth.js, 3 tài khoản đã import; còn thiếu GRANT DELETE — xem [docs/history/2026-07-14.md](docs/history/2026-07-14.md))
- [ ] Host app WEB tại **`https://fbt.basa-luma.ts.net/app/`** — code + file đã lên box 2026-07-14 (mount `/app` ← `~/fbt_server/web`, cùng origin hết CORS; login trả `apiToken`); CHỜ restart `fbt-receiver` để nạp

> Xem chi tiết + thứ tự làm trong [docs/plan/KE_HOACH_PHAT_TRIEN.md](docs/plan/KE_HOACH_PHAT_TRIEN.md).
