# Hướng dẫn dựng lại FBT Home Server trên máy mới

Runbook dựng từ đầu (thay cho `KE_HOACH_DUNG_SERVER.md` đã thất lạc). Giả định: Debian headless,
user `engineer` có sudo. Mã nguồn đặt tại `~/fbt_server/`. Thay các chỗ `<...>` bằng giá trị thật,
chạy tuần tự từng mục.

---

## 1. Cài gói hệ thống

```bash
sudo apt update
sudo apt install -y postgresql python3-venv rsync
# (tùy chọn) giám sát + tiện ích:
sudo apt install -y cockpit btop
```

## 2. PostgreSQL — tạo DB

```bash
sudo -u postgres psql -c "CREATE DATABASE mydb;"
# role 'engineer' + bảng + quyền do schema.sql tạo ở bước 5 (peer auth qua unix socket).
```

## 3. Tailscale (truy cập xa + domain)

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up          # mở link, đăng nhập tài khoản Tailscale
tailscale ip -4            # ghi lại IP 100.x
# (khuyến nghị) tắt key-expiry cho node trong admin console để khỏi rớt.
```

## 4. Mã nguồn + venv + thư viện

```bash
mkdir -p ~/fbt_server && cd ~/fbt_server
# copy từ máy cũ/laptop:  scp -r app scripts deploy requirements.txt <máy>:~/fbt_server/
python3 -m venv venv
venv/bin/pip install -r requirements.txt      # fastapi, uvicorn[standard], psycopg[binary], httpx
mkdir -p data_plus                            # nơi lưu file JSON gốc (nguồn chân lý)
```

## 5. Schema DB (bảng sessions + users)

```bash
sudo -u postgres psql mydb < ~/fbt_server/deploy/schema.sql
# Kiểm tra: đăng nhập bằng chính engineer (peer auth), không cần mật khẩu
psql mydb -c "SELECT count(*) FROM sessions; SELECT count(*) FROM users;"
```

## 6. Token API + biến môi trường

```bash
openssl rand -hex 24                          # sinh token, copy lại
sudo sh -c 'echo "RECEIVER_TOKEN=<token vừa sinh>" > /etc/fbt-receiver.env'
sudo chmod 600 /etc/fbt-receiver.env
```

## 7. Service systemd (fbt-receiver)

```bash
sudo cp ~/fbt_server/deploy/fbt-receiver.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now fbt-receiver       # enable = tự chạy sau reboot
systemctl status fbt-receiver --no-pager | head -3
# Test nội bộ:
curl -s -H "Authorization: Bearer <token>" http://localhost:8080/devices
```

## 8. Domain công khai (Tailscale Funnel → cổng 8080)

```bash
sudo tailscale set --hostname fbt              # đổi tên máy (tùy chọn)
sudo tailscale funnel --bg 8080                # lần đầu mở link duyệt HTTPS/Funnel trong admin console
tailscale funnel status                        # in URL: https://<host>.<tailnet>.ts.net
```

## 9. Tài khoản đăng nhập app Flutter

```bash
# add <username> <password> [role] [ids_csv] [name] [email] [fw]
~/fbt_server/venv/bin/python ~/fbt_server/scripts/manage_users.py add admin '<mật_khẩu>' admin "RPL02013,RPL03010" "Quản trị" admin@fbt.vn v2.4.2
~/fbt_server/venv/bin/python ~/fbt_server/scripts/manage_users.py list
# App gọi:  POST https://<domain>/login  body {"username","password"}
```

## 10. Vận hành an toàn (khuyến nghị)

```bash
sudo timedatectl set-timezone Asia/Ho_Chi_Minh        # múi giờ VN (received_at/posted_at đúng)
# BIOS: bật 'Restore on AC Power Loss = Power On' (tự lên sau mất điện)
# SSH key-only (giữ 1 phiên mở để test trước khi restart ssh):
#   /etc/ssh/sshd_config.d/10-hardening.conf: PasswordAuthentication no / PermitRootLogin no
sudo apt install -y unattended-upgrades && sudo dpkg-reconfigure -plow unattended-upgrades
```

---

## Cập nhật code (đã dựng rồi, chỉ deploy bản mới)

```bash
# từ laptop:  scp app/*.py scripts/*.py <máy>:~/fbt_server/app|scripts/
sudo systemctl restart fbt-receiver
```

## Chạy test

```bash
~/fbt_server/venv/bin/python ~/fbt_server/tests/test_logic.py
~/fbt_server/venv/bin/python ~/fbt_server/tests/test_api.py
```

## Nạp bù khi DB từng down (file data_plus/ chưa vào DB)

```bash
~/fbt_server/venv/bin/python ~/fbt_server/scripts/reconcile.py
```

## Migration (chỉ cho DB đang tồn tại — máy mới KHÔNG cần vì schema.sql đã gộp)

- `deploy/migrate_posted_at.sql` — thêm cột posted_at + đổi ràng buộc.
- `deploy/migrate_post_always_add.sql` — bỏ UNIQUE(body_sha256).
- `deploy/migrate_users.sql` — bảng users.

## Ghi chú

- DB: `mydb`, role `engineer` (peer auth qua unix socket — service chạy user engineer nên không cần mật khẩu DB).
- Nguồn dữ liệu: **chỉ POST từ thiết bị** (đã bỏ Drive). Mỗi POST = 1 hàng.
- Bí mật (token, khóa SSH) KHÔNG để trong thư mục đồng bộ cloud; xoay token khi nghi lộ.
