# Cloudflare Tunnel đứng trước server FastAPI hiện tại (đường A)

> Chốt 2026-08-17. **Giữ nguyên** MiniPC + FastAPI + Postgres; Cloudflare chỉ làm đường vào.
> KHÔNG phải bước cắt sang `server-cf/` (Worker + D1 + R2) — cái đó là đường B, xem
> [`server-cf/README.md`](../../../server-cf/README.md), làm sau và chỉ khi thật cần bỏ box.

## Tại sao A trước B

Cả hai đều cần **mua domain** (zone Cloudflare — `.eu.org` free không add được, Error 1049).
Khác nhau ở chỗ A không đụng gì tới thứ đang chạy: Postgres giữ nguyên, mật khẩu `scrypt$` giữ
nguyên (Workers không có scrypt → đường B bắt đặt lại mật khẩu), `app/ratelimit.py` giữ nguyên
(Worker chưa port được vì mỗi isolate một bộ nhớ), không tốn $5/tháng gói Paid cho PBKDF2.

Đổi lại A **không** bỏ được MiniPC. Đó là cái giá, và hiện tại nó rẻ.

## Điều kiện trước

- [ ] Mua domain, add vào Cloudflare (mua qua Cloudflare Registrar thì tự thành zone).
- [ ] Kiểm firmware `FBT-DXD` **có pin root CA không** — xem mục "Bẫy 2" dưới. Phải biết
      TRƯỚC khi đổi URL của máy, không phải sau.

## ⚠️ Trước tiên: tài khoản Cloudflare là của AI?

Hai cách dựng, chọn đúng cái hợp hoàn cảnh — **đừng trộn**:

| | **Locally-managed** (mục dưới) | **Remotely-managed** (người khác giữ account) |
|---|---|---|
| Cần quyền vào dashboard | ✅ có (`cloudflared tunnel login` mở trình duyệt) | ❌ không |
| Cấu hình ingress | file `/etc/cloudflared/config.yml` trên box | trong dashboard, tab Public Hostname |
| Thứ cần xin | — | **token** connector (chuỗi `eyJ...`) |

**Bên mình đang ở cột PHẢI** (2026-08-17): domain nằm trong tài khoản người khác. Khi đó BỎ QUA
`tunnel login` / `tunnel create` / `config.yml` — có `config.yml` cùng lúc với token là xung đột.
Chỉ cài `cloudflared` (bước 1 dưới) rồi chạy đúng một lệnh:

```bash
sudo cloudflared service install <TOKEN>      # token = credential, đừng dán vào chat/commit
systemctl status cloudflared --no-pager
sudo journalctl -u cloudflared -n 30 --no-pager   # SUDO bắt buộc
```

⚠️ **`journalctl` KHÔNG có sudo thì ra `-- No entries --`**, không phải "service im lặng": user
`engineer` không ở nhóm `adm`/`systemd-journal` (chính systemd in Hint nói điều đó, rất dễ lướt qua).

⚠️ **`Active: active (running) since … 20ms ago` CHƯA phải là xanh** — crash-loop cũng hiện đúng
như vậy. Chờ ~10s chạy lại `systemctl status`: `since` tăng dần = thật sự chạy; lại vài chục ms =
đang restart vòng lặp.

Token nằm trong `/etc/systemd/system/cloudflared.service` (bình thường — nhưng vì vậy đừng
commit/copy file unit đó đi đâu).

**Mốc kiểm 1**: log có `Registered tunnel connection` (2–4 dòng, mỗi dòng một datacenter). Hoặc
nhìn dashboard bên họ: tunnel `fbt` = **HEALTHY**. Có rồi thì **họ** thêm Public Hostname; DNS
Cloudflare tự tạo, bên mình không đụng gì. Form điền: Subdomain `fbt` · Path **trống** · Service
URL **`http://127.0.0.1:8080`**.

(UI có 2 kiểu: loại tách ô **Type** + ô **URL** — chọn `HTTP` rồi gõ `127.0.0.1:8080`; loại một ô
**Service URL** — gõ NGUYÊN `http://127.0.0.1:8080` kèm scheme. Nhìn ô đang có mà điền.)

Ba chỗ người ngoài hay điền sai (họ không biết bên mình chạy gì):
- **`https` → 502.** Uvicorn chạy HTTP trần, TLS kết thúc ở edge Cloudflare. Placeholder xám của
  ô Service URL lại đang gợi ý `https://localhost:8080` — **đừng làm theo placeholder**.
- **`localhost` → có thể 502.** Debian phân giải `localhost` ra `::1` (IPv6) trước, mà uvicorn
  bind `0.0.0.0` = **chỉ IPv4** → gõ cửa chỗ không ai nghe. Ghi thẳng **`127.0.0.1`**.
- **Không phải IP LAN / IP public / link `ts.net`** — `cloudflared` chạy trên chính box đó nên
  "server" đứng từ góc nhìn của nó là localhost.

**Mốc kiểm 2** — `curl -s https://fbt.<domain>/openapi.json` **từ 4G/ngoài LAN** (trong LAN dính
hairpin NAT, timeout dù đúng hết). Đọc lỗi:

| Thấy gì | Nghĩa là |
|---|---|
| JSON | xong phần hạ tầng |
| **502** | tunnel sống nhưng uvicorn không nghe ở `127.0.0.1:8080` |
| **1033** | hostname chưa gắn đúng tunnel (việc của họ) |

**Việc CHỈ họ làm được** (cần dashboard) — xin luôn một thể, đừng để sau:
- **Cache Rule bypass `/ota/*`** (bẫy 1 dưới — quên là OTA phát bản cũ, im lặng).
- WAF Rate limiting cho `/auth`.
- Hoặc: nhờ họ mời mình làm **Member** của account thì tự làm được hết, khỏi qua lại.

## Dựng tunnel — cách locally-managed (chỉ khi TỰ giữ account)

```bash
# 1. Cài cloudflared từ repo chính chủ
#    ⚠️ ĐỪNG tự dò codename bằng $VERSION_CODENAME: box là Debian 13 **trixie**, mà
#    pkg.cloudflare.com CHƯA có trixie → "does not have a Release file". Ghim `bookworm`
#    (gói gần như không phụ thuộc gì, chạy tốt trên trixie).
curl -fsSL https://pkg.cloudflare.com/cloudflare-main.gpg \
  | sudo tee /usr/share/keyrings/cloudflare-main.gpg >/dev/null
echo "deb [signed-by=/usr/share/keyrings/cloudflare-main.gpg] \
https://pkg.cloudflare.com/cloudflared bookworm main" \
  | sudo tee /etc/apt/sources.list.d/cloudflared.list
sudo apt update && sudo apt install cloudflared

# 1b. Repo vẫn giở chứng → bỏ apt, cài thẳng .deb (không cần keyring/source list).
#     Đánh đổi: `apt upgrade` KHÔNG nâng nó, và `service install` sinh unit chạy với cờ
#     `--no-autoupdate` → cũng KHÔNG tự cập nhật. Đi đường này là phải tự nhớ nâng cấp tay.
# curl -fsSL https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb \
#   -o /tmp/cloudflared.deb && sudo dpkg -i /tmp/cloudflared.deb
# sudo rm /etc/apt/sources.list.d/cloudflared.list

# 2. Gắn tài khoản + tạo tunnel (mở URL hiện ra trên trình duyệt máy dev, chọn zone)
cloudflared tunnel login
cloudflared tunnel create fbt          # in ra UUID + đường dẫn file credentials .json
cloudflared tunnel route dns fbt fbt.<domain>
```

`/etc/cloudflared/config.yml`:

```yaml
tunnel: fbt
credentials-file: /etc/cloudflared/<UUID>.json
ingress:
  - hostname: fbt.<domain>
    service: http://127.0.0.1:8080
  - service: http_status:404
```

```bash
# 3. Credentials phải nằm chỗ service (chạy bằng root) đọc được
sudo cp ~/.cloudflared/<UUID>.json /etc/cloudflared/
sudo cloudflared service install
sudo systemctl enable --now cloudflared
systemctl status cloudflared --no-pager
```

`fbt-receiver.service` **không đổi một chữ** — vẫn `--host 0.0.0.0 --port 8080` (giữ `0.0.0.0`
để LAN 192.168.0.103 gọi thẳng được). Không mở port trên router, không cert trên box.

Kiểm từ máy ngoài LAN (4G — trong LAN dễ dính hairpin NAT):

```bash
curl -s https://fbt.<domain>/openapi.json | head -c 200     # public, khỏi token
```

## Ba bẫy — làm luôn, đừng để sau

### Bẫy 1 — Cloudflare cache `.bin`, OTA sẽ phát bản CŨ

`.bin` nằm trong danh sách extension Cloudflare cache **mặc định**. Upload firmware trùng tên
qua app (`PUT /ota/{file}`) xong, `GET /ota/{file}` ở edge vẫn trả bản cũ. **Không có lỗi nào**,
và `x-MD5` cũng không cứu: header đó đi kèm chính file cũ đang được cache nên firmware kiểm
toàn vẹn thấy "khớp" rồi nạp nhầm bản.

→ Dashboard → Caching → Cache Rules: URI Path bắt đầu bằng `/ota` → **Bypass cache**.

✅ **ĐÃ LÀM 2026-08-17.** Cách xác minh (đừng tin dashboard, đọc header của chính response):
`GET /ota/<file>.bin` qua domain mới phải trả **`cf-cache-status: BYPASS`**, và `content-length`
khớp `size` mà `/ota/check` báo. Thấy `HIT`/`MISS` là rule CHƯA ăn đúng path.

### Bẫy 1b — Cloudflare Access bắt login (GẶP THẬT 2026-08-17)

Vào `https://hub.fortebio.tech` ra **trang đăng nhập** thay vì JSON `{"ok":true,…}`. Không phải
login của app — là **Cloudflare Access** chặn trước, thường do account công ty có Access
application wildcard `*.fortebio.tech` phủ luôn hostname mới tạo.

Access chỉ hiểu **người ngồi trước trình duyệt** → nó giết sạch client máy-với-máy: firmware POST
`Authorization: Bearer`, app Flutter, `GET /ota/check`. Thiết bị **không báo lỗi gì rõ ràng**,
chỉ đơn giản là mất dữ liệu đo. Phải TẮT, không có cách né.

```bash
curl -sI https://hub.fortebio.tech/ | head -20   # có `location: …cloudflareaccess.com…` = đúng nó
```

Dấu hiệu chắc chắn trong header trả về: **`www-authenticate: Cloudflare-Access`** (kèm 302 về
`…cloudflareaccess.com`). Request chưa hề chạm tới server → mọi thử nghiệm khác vô nghĩa tới khi
tắt.

Sửa (việc của người giữ account): Zero Trust → **Access** → **Applications** → app phủ hostname
đó → xoá, hoặc thêm policy **Bypass / Everyone**. Tắt không hở gì mới: API vốn đã bắt
`Authorization: Bearer` ở tầng ứng dụng.

Nếu công ty KHÔNG cho tắt: Access có **service token** (`CF-Access-Client-Id` +
`CF-Access-Client-Secret`) — app Flutter thêm 2 header thì dễ, **nhưng 109 máy ESP32 phải nạp lại
firmware mới thêm được**, mà nạp lại được thì đã không cần đường vòng này. Phương án cuối, không
phải lối thoát. Đường thật sự còn lại là xin hostname nằm ngoài app Access đó.

### Bẫy 2 — đổi CA, firmware pin cert sẽ đứt

`*.ts.net` là Let's Encrypt (ISRG Root X1). Cert edge Cloudflare do **Google Trust Services**
ký. Firmware v2.4.4 nạp OTA bằng `HTTPUpdate` qua HTTPS — nếu nó `setCACert()` một root cụ thể
thay vì dùng bundle, đổi domain là **mọi máy 401/TLS fail cùng lúc**, và cách sửa duy nhất lại
là... OTA. Kiểm ở repo `FBT-DXD` trước khi nạp bản trỏ domain mới.

### Tailscale còn làm gì sau khi có Cloudflare

Hai vai trò TÁCH BIỆT, Cloudflare chỉ thay được một:

- **Funnel** (`fbt.basa-luma.ts.net` → `localhost:8080`) — cổng public cho 109 máy firmware cũ.
  **Tạm**, tắt được sau khi fleet đổi URL.
- **Tailnet** (`100.109.127.87`, MagicDNS) — đường `ssh fbt-server` + scp deploy. **Vĩnh viễn**;
  Cloudflare Tunnel chỉ expose HTTP 8080, KHÔNG cho shell.

⚠️ Tới bước nghỉ Funnel, lệnh đúng là **`sudo tailscale funnel off`** (tắt cổng public, giữ
tailnet). **ĐỪNG `tailscale down`/gỡ tailscaled** — mất luôn SSH, box headless, router không mở
port nào → không có đường lùi ngoài việc vác bàn phím tới tận nơi.

⚠️ **ĐỔI SANG TÀI KHOẢN TAILSCALE KHÁC = giết Funnel y như tắt nó.** Tailnet mới → hostname thành
`fbt.<tailnet-mới>.ts.net`, tên `basa-luma` **mất vĩnh viễn** (tên tailnet do Tailscale sinh,
không tự đặt) → 109 máy ngừng đẩy dữ liệu, chỉ sửa được bằng nạp lại firmware từng máy. Xếp lịch
SAU khi fleet đã đổi URL. Cloudflare KHÔNG dính (cloudflared tự gọi ra internet, không qua
tailnet).

Trước khi chuyển, hỏi mục đích — hai trong ba trường hợp KHÔNG cần chuyển:
- **Cho account khác truy cập box** → **share node**, giữ nguyên tailnet lẫn Funnel.
- **Bàn giao từ Gmail cá nhân sang account công ty** (tình huống thật 2026-08-17) → ⚠️ **KHÔNG
  chuyển Owner được**: tailnet tạo bằng **shared domain** (`gmail.com`) thì Tailscale **khoá hẳn**
  việc đổi chủ sở hữu ([docs](https://tailscale.com/docs/features/sharing/how-to/change-role)) —
  admin console chỉ hiện `admin`/`user`, không có Owner. Khoá vĩnh viễn, không phải "chưa tới lúc".
  Cách đi: **mời account công ty, đặt role `Admin`** — đủ toàn bộ việc vận hành (máy, ACL, auth
  key, Funnel), chỉ thiếu billing + quyền sở hữu. Không downtime, không đụng 109 máy. Muốn công ty
  sở hữu THẬT thì buộc phải tạo tailnet mới dưới domain công ty (Workspace) và chuyển box sang →
  rơi vào trường hợp dưới, hoãn tới sau khi fleet đổi URL.
- **Công ty ĐÃ CÓ tailnet riêng, bắt box vào đó** → chuyển thật, hostname chắc chắn đổi → hoãn tới
  sau khi fleet đổi URL. Khi làm: chạy từ **LAN `192.168.0.103`** hoặc tại chỗ, KHÔNG chạy
  `tailscale logout` qua chính SSH-over-Tailscale.

Kiểm sau mọi thay đổi tài khoản: `tailscale funnel status` vẫn in `fbt.basa-luma.ts.net`, và mở
URL đó **từ 4G** vẫn ra JSON.

### Bẫy 3 — chạy SONG SONG, đừng tắt Funnel

Funnel và Tunnel cùng trỏ `127.0.0.1:8080`, không xung đột. 109 máy còn nạp cứng
`fbt.basa-luma.ts.net`; chỉ tắt Funnel khi fleet đã lên bản trỏ domain mới **và** đã xác nhận
`last_seen` của cả 109 máy nhảy qua đường mới.

## Sau khi tunnel xanh

1. **WAF Rate limiting cho `/auth`** — `main.py` chưa rate-limit ở tầng nào công khai được, mà
   login thành công trả thẳng master `RECEIVER_TOKEN`. Đây là lúc rẻ nhất để bịt.
2. App: đổi `kDefaultEngineerUrl` trong `lib/services/app_settings.dart:30` → build lại.
   ⚠️ Máy đã lưu URL cũ trong `shared_preferences` **không tự đổi** (`_orDefaultUrl` chỉ áp khi
   ô TRỐNG) → user phải xoá trống ô URL trong Cài đặt.
3. Bản web: `flutter build web --release --base-href /app/` rồi scp lại `~/fbt_server/web/`
   (vẫn cùng origin với API → không phát sinh CORS).
4. Firmware: nạp bản trỏ `fbt.<domain>` — giờ đã đi được bằng OTA (v2.4.4 gọi `/ota/check`).
