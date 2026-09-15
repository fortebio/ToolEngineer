# Kế hoạch phát triển FBT Home Server

> Duyệt từ ý tưởng sơ bộ [plan.txt](plan.txt) ngày 2026-07-11, qua đánh giá đa góc nhìn (dữ liệu/DB, API Flutter, quản lý–giám sát, bảo mật–backup) + 1 vòng phản biện. Đây là kế hoạch chính thức thay cho plan.txt; plan.txt giữ lại làm bản ghi ý tưởng gốc.

## Đánh giá tổng quan plan.txt

**Nền tảng đúng, giữ nguyên:** Debian headless + PostgreSQL + SSH/Tailscale + receiver.py stdlib.

**Hai điều chỉnh lớn so với ý tưởng gốc:**
1. **Bỏ ý tưởng tự viết app desktop Windows** quản lý file/server/hiệu năng — đó là viết lại Cockpit + btop + WinSCP, tốn hàng tuần công sức và phải bảo trì mãi mãi. Thay bằng **Cockpit** truy cập qua Tailscale (cài 1 lệnh, RAM ~0 khi không mở nhờ socket-activation). Quản lý file đã có VS Code Remote-SSH/WinSCP.
2. **Host website: bắt đầu bằng chính Tailscale Funnel serve file tĩnh**, chưa cài nginx/Caddy. Chỉ thêm Caddy khi cần web động/nhiều site.

---

## 🔑 Secret đã lộ — hoãn khi chạy thử, BẮT BUỘC xoay trước khi chạy thật (~30 phút)

> Quyết định chủ dự án 2026-07-11: dự án đang chạy thử nên chấp nhận rủi ro này, chưa xoay ngay. Mục này trở thành **điều kiện tiên quyết trước khi đưa dữ liệu thật / mở ra Internet (Funnel)**.

Thư mục dự án nằm trong **OneDrive (tự đồng bộ lên cloud)** đang chứa toàn bộ chìa khóa vào server dạng plaintext:
- `note.md`: mật khẩu **root**, mật khẩu **engineer**, và **RECEIVER_TOKEN** thật (trùng token trong systemd unit đang chạy).
- `.ssh/id_ed25519`: **khóa riêng SSH** — ai có nó là vào thẳng server.

→ Coi như các secret này **đã lộ**. Xử lý theo đúng thứ tự (tránh lặp sự cố tự khóa ngày 2026-07-08):

1. Trên server: đổi mật khẩu `passwd` (engineer) và `sudo passwd root`.
2. Tạo token mới: `openssl rand -hex 24`. Cập nhật vào EnvironmentFile (mục GĐ2 bên dưới) + cập nhật thiết bị/script gửi data.
3. Tạo cặp khóa SSH **mới** trên laptop (`ssh-keygen -t ed25519 -f C:\Users\<user>\.ssh\fbt_new` — **ngoài OneDrive**), thêm `.pub` vào `authorized_keys` trên server. **Mở phiên SSH thứ 2 test đăng nhập bằng key mới OK rồi mới** xóa key cũ khỏi `authorized_keys`.
4. Di chuyển `.ssh/` và gỡ secret khỏi `note.md`, đưa ra khỏi OneDrive; xóa cả **version history/recycle bin của OneDrive online**.

---

## Thứ tự làm (quick win trước, phụ thuộc sau)

| # | Việc | Ghi chú |
|---|------|---------|
| 1 | Xoay secret + gỡ khỏi OneDrive | **Hoãn khi chạy thử** (quyết định 2026-07-11) — bắt buộc trước khi chạy thật/mở Funnel |
| 2 | Tách token sang EnvironmentFile chmod 600 | Làm cùng bước 1 |
| 3 | Sửa tài liệu (link chết) + xác nhận `fbt-receiver` đang chạy thật | `systemctl status fbt-receiver`; cập nhật tiến độ README |
| 4 | SSH hardening theo thứ tự an toàn | Cần key mới từ bước 1 |
| 5 | Backup tự động + **test restore ngay** | Chặn mất dữ liệu |
| 6 | An toàn vận hành: BIOS auto power-on, unattended-upgrades, trần journald | Cài 1 lần rồi quên |
| 7 | Nạp PostgreSQL + API đọc cho Flutter | Việc lớn, làm khi nền đã an toàn |

---

## Giai đoạn 1 — Nền tảng + dữ liệu + API (mục tiêu: Flutter đọc được dữ liệu)

### 1a. Hoàn thiện việc dở dang
- ✅ 2026-07-11: `fbt-receiver` xác nhận đang chạy (active từ 08/07, RAM ~9.4MB). ~~Service đang **disabled**~~ → **✅ 2026-07-13: đã `enabled`** (xác nhận `systemctl is-enabled postgresql tailscaled ssh fbt-receiver` → cả 4 đều `enabled`). Service có `Restart=always` + `WantedBy=multi-user.target` nên tự bật lại sau reboot. Còn lại cho tình huống **mất nguồn**: BIOS `Restore on AC Power Loss = Power On` + rút điện test 1 lần (xem mục 2c).
- Đặt timezone server: `sudo timedatectl set-timezone Asia/Ho_Chi_Minh` — server đang chạy **EDT (giờ Mỹ)**; mốc thời gian nhận là timestamp duy nhất của bản ghi (thiết bị không gửi thời điểm đo) nên tên file/giờ dữ liệu hiện lệch so với giờ VN.

> ✅ **Hoàn thành 2026-07-11:** bảng `sessions` + 4 endpoint chạy thật; đã nạp **3.372 phiên / 88 thiết bị** từ kho Drive (`import_drive_logs.py`), thiết bị vẫn POST realtime, API đọc chạy qua `https://fbt.basa-luma.ts.net`. Đã đặt timezone VN cho role engineer. Còn lại: viết app Flutter dùng các endpoint này.

### 1b. Nạp dữ liệu vào PostgreSQL (mấu chốt của cả dự án)
File JSON rời không lọc/phân trang/index được — không phục vụ nổi mục tiêu "Flutter duyệt dữ liệu". Nguyên tắc: **ghi FILE gốc trước (nguồn chân lý), INSERT vào DB sau cùng request**; DB lỗi thì chỉ log, file vẫn còn để nạp bù. Không cần queue/job batch (volume thấp, INSERT vài KB không đáng kể).

Bảng hybrid — cột hay lọc tách riêng, còn lại để JSONB (không chuẩn hóa 10 slot sớm — YAGNI):

```sql
CREATE TABLE sessions (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  id_device   text NOT NULL,
  version     text, kit_id text, type_upload text, method text,
  received_at timestamptz NOT NULL DEFAULT now(),
  body_sha256 bytea NOT NULL UNIQUE,   -- chống trùng khi thiết bị retry / import lặp
  payload     jsonb NOT NULL           -- giữ nguyên bản gốc
);
CREATE INDEX ON sessions (id_device, received_at DESC);
```

- Kết nối bằng **peer auth qua unix socket** (service chạy user `engineer`, tạo role Postgres trùng tên) → không nhúng mật khẩu DB vào code.
- Sửa 3 lỗi receiver hiện tại: `datetime.now(timezone.utc)` (đang thiếu tz), validate nhẹ ~10 dòng (id_device tồn tại, các mảng đủ 10 phần tử — sai trả 400), dedup theo `sha256(raw)`.
- Script `reconcile.py` (~30 dòng) nạp bù file chưa vào DB, idempotent nhờ `ON CONFLICT (body_sha256) DO NOTHING` — chạy tay hoặc timer 1 giờ/lần; đồng thời import 1 lần toàn bộ file cũ trong `data_plus/`.

### 1c. API đọc cho Flutter
- **FastAPI + uvicorn (1 worker, ~60–90MB RAM) + psycopg**, **gộp receiver + API đọc vào 1 service** (1 systemd, 1 cổng 8080, chung auth). Tự viết routing/phân trang trên stdlib tốn code hơn cài FastAPI; Swagger `/docs` tự sinh giúp test API khi code Flutter. *(Đã cân nhắc PostgREST — loại: thêm 1 service riêng, không parse được amplification phía server.)*
- Endpoint:
  - `GET /devices` — danh sách thiết bị + số phiên + lần gửi cuối.
  - `GET /sessions?device=&from=&to=&page=&limit=` — phân trang LIMIT/OFFSET đơn giản (đủ tới hàng chục nghìn dòng).
  - `GET /sessions/{id}` — chi tiết, trả `payload - 'amplification'` (bỏ phần nặng).
  - `GET /sessions/{id}/amplification` — parse 10 chuỗi CSV thành mảng số sẵn cho fl_chart, chỉ tải khi mở biểu đồ.
- Auth: **1 Bearer token tĩnh** qua env là đủ (JWT/bảng user là thừa; tách READ token riêng chỉ khi app phát hành ra ngoài).
- Flutter kết nối: cài Tailscale trên điện thoại → gọi `http://fbt-server:8080` (MagicDNS). Funnel chỉ khi chạy trên máy không cài được Tailscale.

### 1d. Deploy GĐ1 lên server — thứ tự BẮT BUỘC (env file + venv trước, restart sau)

> ✅ Code xong + test local + review đa agent 2026-07-11: `app.py`, `reconcile.py`, `deploy/schema.sql`, `deploy/fbt-receiver.service`. Unit mới đòi `EnvironmentFile=/etc/fbt-receiver.env` — restart trước khi tạo file này là service chết.

```bash
# Từ laptop: đẩy code lên
scp app.py reconcile.py deploy/schema.sql deploy/fbt-receiver.service fbt-server:~/fbt_server/

# Trên server (ssh fbt-server):
sudo timedatectl set-timezone Asia/Ho_Chi_Minh          # 0. múi giờ (đang EDT)
sudo -u postgres psql mydb -f ~/fbt_server/schema.sql   # 1. bảng + role (1 lần)
sudo apt install -y python3-venv                        # 2. venv + thư viện (1 lần)
python3 -m venv ~/fbt_server/venv
~/fbt_server/venv/bin/pip install fastapi 'uvicorn[standard]' 'psycopg[binary]'
                                                        # 3. token → file riêng, TRƯỚC khi thay unit
sudo sh -c 'echo "RECEIVER_TOKEN=<token thiết bị đang dùng>" > /etc/fbt-receiver.env'
sudo chmod 600 /etc/fbt-receiver.env
                                                        # 4. thay unit + bật (sửa luôn lỗi disabled)
sudo cp ~/fbt_server/fbt-receiver.service /etc/systemd/system/fbt-receiver.service
sudo systemctl daemon-reload
sudo systemctl enable fbt-receiver
sudo systemctl restart fbt-receiver
systemctl status fbt-receiver                           # phải thấy uvicorn running
~/fbt_server/venv/bin/python ~/fbt_server/reconcile.py  # 5. nạp file cũ vào DB (idempotent)
curl -s -H "Authorization: Bearer <token>" http://localhost:8080/devices   # 6. test đọc
```

Rollback nếu hỏng: đổi lại `ExecStart=/usr/bin/python3 /home/engineer/fbt_server/receiver.py` trong unit (receiver.py cũ vẫn nằm nguyên trên server), `daemon-reload` + `restart`.

## Giai đoạn 2 — Vận hành & bảo mật

### 2a. Bảo mật
- **Token ra EnvironmentFile**: `/etc/fbt-receiver.env` (1 dòng `RECEIVER_TOKEN=...`), `chmod 600`, unit dùng `EnvironmentFile=` thay `Environment=`. Luôn đặt token kể cả chỉ dùng LAN.
- **SSH hardening** (drop-in `/etc/ssh/sshd_config.d/10-hardening.conf`): `PasswordAuthentication no`, `PermitRootLogin no`. **Giữ 1 phiên SSH mở suốt quá trình, test phiên mới trước khi restart ssh.**
- **Bind receiver khỏi `0.0.0.0`**: về `127.0.0.1` nếu chỉ nhận qua Funnel, hoặc IP Tailscale/IP LAN cố định nếu thiết bị POST trực tiếp. ⚠️ Trước khi đổi: xác nhận thiết bị đang gửi qua đường nào để không cắt luồng data.
- **Loại bỏ có chủ đích**: fail2ban (không còn bề mặt brute-force sau CGNAT + key-only), ufw (bind đúng IP đã đóng cửa LAN gọn hơn), Netdata/Prometheus/Grafana (ngốn RAM), TimescaleDB, Docker/K8s, JWT, UPS.

### 2b. Backup (3-2-1 bản nhẹ) — backup chưa test restore = chưa có backup
- `/usr/local/bin/fbt-backup.sh`: `pg_dump -Fc mydb` + `tar czf data_plus` → `~/fbt_server/backups/`; xóa bản >7 ngày, chủ nhật giữ thêm bản weekly (giữ 4).
- systemd timer `OnCalendar=02:30` + `Persistent=true` (chạy bù nếu đúng giờ đó server tắt).
- Offsite: **rclone → Google Drive** (`fbt.engineer@gmail.com`, 15GB free). **Không dùng OneDrive** — chính nó là nơi vừa rò rỉ secret, không gom trứng vào giỏ thủng.
- Chạy tay lần đầu + **restore thử vào DB tạm** để chứng minh backup dùng được; lặp lại mỗi quý.

### 2c. Vận hành ít bảo trì
- BIOS: `Restore on AC Power Loss = Power On` (⏳ chưa xác nhận — mắt xích duy nhất còn lại cho tình huống mất nguồn); ✅ 2026-07-13 `systemctl is-enabled postgresql tailscaled ssh fbt-receiver` đều `enabled`; ⏳ rút điện test 1 lần (chưa làm).
- `unattended-upgrades` (chỉ kênh security) — cài 1 lần rồi quên.
- journald: `SystemMaxUse=300M` + `Storage=persistent` (receiver in stdout → journald là đủ, không cần logrotate thêm).
- Retention data_plus: gzip file >7 ngày (nén ~5–10×), xóa/chuyển archive file >30 ngày **sau khi** chắc đã vào DB. Ổ 113GB thừa sức nhiều năm (~7.7KB/phiên).

### 2d. Giám sát tối thiểu + quản lý
- 1 script `healthcheck.sh` (~20 dòng): disk >80%, service chết (`systemctl is-active`), nhiệt độ >80°C, `smartctl -H /dev/sda` — báo qua **Telegram bot** (1 lệnh curl), timer 15 phút/lần. Token bot chmod 600.
- **Cockpit** (`apt install --no-install-recommends cockpit`) → `https://<ip-tailscale>:9090`: service, log, CPU/RAM/disk, terminal — chỉ dùng trong tailnet. Thay thế hoàn toàn ý tưởng app desktop Windows. Kèm `btop` cho SSH.

### 2e. Domain công khai + host website
**✅ Đã bật 2026-07-11 — domain chính thức: `https://fbt.basa-luma.ts.net`** (Funnel → cổng 8080; đã đổi hostname `fbt-server`→`fbt`, tailnet → `basa-luma`; đã xoay RECEIVER_TOKEN trước khi mở theo đúng điều kiện tiên quyết). Đã cân nhắc domain riêng + Cloudflare Tunnel — để dành khi cần tên thương hiệu; tên tailnet chỉ chọn được từ danh sách Tailscale sinh sẵn, không tự đặt được.

Lưu ý sau khi đổi tên: `ssh fbt-server` từ laptop không ảnh hưởng (`~/.ssh/config` trỏ IP Tailscale `100.109.127.87`).

```bash
# 1. Xoay token (điều kiện bắt buộc trước khi public)
openssl rand -hex 24                       # sinh token mới
sudo nano /etc/fbt-receiver.env            # thay RECEIVER_TOKEN=<token mới>
sudo systemctl restart fbt-receiver        # rồi cập nhật token mới vào thiết bị + app

# 2. Bật Funnel: map https://fbt-server.<tailnet>.ts.net (cổng 443) → localhost:8080
sudo tailscale funnel --bg 8080
# Lần đầu sẽ in link duyệt quyền HTTPS certs/Funnel trong admin console — mở link, bấm Enable.
tailscale funnel status                    # in URL công khai đầy đủ

# 3. Test từ mạng ngoài (4G, tắt wifi):
curl -H "Authorization: Bearer <token mới>" https://fbt-server.<tailnet>.ts.net/devices
```

- Muốn URL đẹp hơn: admin console → DNS → đổi tên tailnet (Tailscale cho chọn trong danh sách tên sinh sẵn).
- Tắt public bất kỳ lúc nào: `sudo tailscale funnel --https=443 off`.
- Web tĩnh sau này: `tailscale funnel --bg --set-path=/www /home/engineer/www`, hoặc cài Caddy khi cần web động/nhiều site (~30–50MB RAM).

## Giai đoạn 3 — Tối ưu (chỉ làm khi ĐO được nhu cầu thật)

- Response chậm trên 4G → `GZipMiddleware` (1 dòng) + keyset pagination (`?before=<cursor>`).
- Cần lọc "slot dương tính P"/vẽ nhanh theo slot → tách bảng con `slot_result` (10 dòng/phiên, `amplification real[]`, index verdict), backfill từ JSONB sẵn có.
- Bảng lên hàng chục triệu dòng → partition `sessions` theo tháng (DROP partition cũ thay cho DELETE).
- Dead-man switch healthchecks.io (biết khi cả server chết/mất điện): 1 dòng curl thêm vào cuối healthcheck.sh.

---

## Vấn đề tài liệu tồn đọng

- `docs/plan/KE_HOACH_DUNG_SERVER.md` (hướng dẫn dựng chi tiết, từng được README/CLAUDE tham chiếu) **đã biến mất khỏi repo**. Thử khôi phục từ OneDrive online (version history / recycle bin). Nếu không được: các lệnh tra cứu chính đã nằm trong README, phần thiếu sẽ được tài liệu này thay thế dần.
