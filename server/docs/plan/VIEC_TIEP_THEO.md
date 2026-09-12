# Việc tiếp theo — chốt ngày 2026-08-17

> Một trang duy nhất trả lời "giờ làm gì". Cập nhật mỗi khi xong một mục.
> Chi tiết kỹ thuật xem [docs/history/2026-08-17.md](../history/2026-08-17.md).

## Đang ở đâu

| Thứ | Trạng thái |
|---|---|
| Server FastAPI trên MiniPC | ✅ **đang chạy thật**, 109 máy đẩy dữ liệu vào |
| App: tab Quản lý máy (OTA + Trạng thái máy) | ✅ code xong, test xong |
| Server: API cho tab đó (`/ota*`, `/devices` trả version) | ✅ **deploy 2026-08-17**, routes live, app đọc được |
| Bản Cloudflare Workers (`server-cf/`) | ⚠️ code xong, 15/15 test, **chưa tạo tài nguyên, chưa deploy** |
| Firmware tự cập nhật OTA | ✅ **v2.4.4 gọi được `/ota/check`** (2026-08-17) — còn lại là nạp cho fleet |

## Nút thắt: ~~firmware~~ → nay là **nạp v2.4.4 cho fleet**

Nút thắt cũ đã gỡ: firmware **v2.4.4 gọi được `GET /ota/check`** (2026-08-17, chi tiết ở
`FBT-DXD243/docs/history/2026-08-17-ota-github-to-server.md`). Nút "chọn bản firmware" trong
app từ giờ là thật, không còn chỉ đánh dấu.

Nút thắt MỚI: 109 máy ngoài hiện trường vẫn chạy bản cũ, nạp cứng URL `*.ts.net`. Đợt chuyển
giao v2.4.4 đầu tiên **không đi bằng OTA được** (bản cũ chưa biết gọi) → phải `POST /otaupload`
từ trình duyệt hoặc cầm dây. Xong đợt đó thì mọi lần sau mới đi OTA, và mới đổi được URL sang
Cloudflare.

⚠️ Kéo theo: `.bin` mang `RECEIVER_TOKEN` ở 4 KB đầu → **đừng publish `.bin` lên repo public**,
và **rotate `RECEIVER_TOKEN` sau khi fleet lên v2.4.4 là tự khoá mình ra ngoài** (mọi máy 401,
mất luôn đường OTA). Muốn rotate thì phải tách token OTA riêng TRƯỚC.

Firmware nằm ở repo RIÊNG `FBT-DXD` (PlatformIO/ESP), **không có trong repo này**.

---

## Làm theo thứ tự này

### ✅ Bước 1 — Deploy phần server đã code xong (XONG 2026-08-17)

> Kết quả: `systemctl is-active` → `active`; openapi có đủ `/ota`, `/ota/check`,
> `/ota/target/{filename}`, `/ota/target`, `/ota/{filename}`; app đọc được kho OTA thật.
>
> ⚠️ Vấp một lần đáng nhớ: scp đúng 2 file đã sửa (`main.py`, `db.py`) làm **service chết
> crash-loop** vì `config.py` trên box là bản cũ chưa có `OTA_DIR`. Box tụt lại **4/6 file**.
> Từ giờ **đối chiếu md5 CẢ GÓI trước khi restart** (xem lệnh dưới). Bản cũ đã sao lưu ở
> `~/fbt_server/app.bak.20260817/`.


Cho tab Quản lý máy trong app chạy thật. Không đụng gì tới luồng dữ liệu hiện có.

```bash
scp server/app/main.py server/app/db.py fbt-server:~/fbt_server/app/
ssh -t fbt-server "sudo systemctl restart fbt-receiver"   # -t BẮT BUỘC, xem dưới
```

⚠️ **Phải có `-t`**: `ssh host "lệnh"` không cấp TTY nên `sudo` không hỏi được mật khẩu →
`sudo: a terminal is required to read the password`. `-t` ép cấp TTY. (Hoặc `ssh fbt-server`
vào hẳn rồi gõ lệnh.)

Kiểm code MỚI đã nạp chưa — file trên đĩa ≠ tiến trình đang chạy:

```bash
ssh fbt-server "curl -s http://127.0.0.1:8080/openapi.json | grep -c 'ota/target'"
# ra số > 0 = route mới đã sống; ra 0 = service chưa restart
```

Xong thì mở app → tab Quản lý máy: mục OTA hết báo 405, cột Firmware hiện `v2.4.3`.

### ✅ Bước 2 — Vá 4/5 lỗi (XONG + ĐÃ DEPLOY 2026-08-17 14:43)

Đã xác minh trên production: `idx_users_username_lower` tồn tại · `engineer` có
`INSERT,SELECT,UPDATE,DELETE` · service `active` · rate limit chặn thật khi gọi
`https://fbt.basa-luma.ts.net/auth` sai 12 lần.
Lỗi 2 tách riêng (đổi mô hình xác thực, phải sửa cả app) — **chưa làm**.

| # | Lỗi | Trạng thái |
|---|---|---|
| 1 | `/ingest` trả `{ok:true}` cả khi ghi file LẪN insert DB đều hỏng → thiết bị không retry, **mất hẳn phiên đo** | ✅ cả 2 đường chết → **500** |
| 2 | Đăng nhập cấp thẳng **master token** + server không lọc `ids` → khách hàng nào cũng đọc mọi máy + POST kết quả giả | ❌ **chưa làm** |
| 3 | `/auth` không giới hạn tần suất → dò mật khẩu thoải mái qua Funnel public | ✅ `app/ratelimit.py`, chặn theo `(ip, username)` |
| 4 | `migrate_users.sql` thiếu `GRANT DELETE` → xoá user lỗi 500 | ✅ + migration cho box cũ |
| 5 | `users.username` UNIQUE phân biệt hoa/thường trong khi truy vấn dùng `lower()` | ✅ unique index trên `lower(username)` |

Chạy nốt (đã kiểm: không có username trùng nên migration không fail):

```bash
ssh -t fbt-server "sudo -u postgres psql mydb -f ~/fbt_server/migrate_2026_08_17_fixes.sql"
ssh -t fbt-server "sudo systemctl restart fbt-receiver"
ssh fbt-server "systemctl is-active fbt-receiver"     # phải ra 'active'
```

Ghi chú thiết kế `ratelimit.py`: chỉ đếm lần **thất bại**, khoá theo **`(ip, username)`**
chứ không theo mình username — nếu khoá theo username thì kẻ xấu khoá được tài khoản người
khác. Kèm ngưỡng theo IP rộng hơn để chặn quét nhiều tài khoản. ⚠️ Cần xác minh Tailscale
Funnel có gắn `X-Forwarded-For` không; nếu không thì mọi IP công khai dồn vào một khoá
`127.0.0.1` (`journalctl -u fbt-receiver | grep ratelimit` xem IP ghi ra là gì).

### Bước 3 — Firmware biết OTA (việc lớn, ở repo `FBT-DXD`)

👉 **Bản bàn giao tự chứa: [FIRMWARE_OTA_BRIEF.md](FIRMWARE_OTA_BRIEF.md)** — copy sang repo
firmware rồi làm việc ở đó, không cần đọc repo này. Gồm hợp đồng `/ota/check` đầy đủ, các
bước firmware phải làm, cạm bẫy ESP32 đã biết, và trạng thái mọi thứ.

Tóm tắt: gọi `GET /ota/check` (Bearer, kèm `?device=<id>`) lúc khởi động + định kỳ → so
`version` với bản đang chạy → khác thì tải `url`, kiểm `sha256`, ghi phân vùng OTA, reboot
có rollback. **Không bao giờ nạp khi đang chạy một lượt đo.**

Làm xong bước này mới mở khoá được bước 4 và 5.

### Bước 4 — Rollout OTA có kiểm soát

Trước khi đẩy cho cả 109 máy: thêm **chọn bản theo từng máy** (hiện chỉ có target toàn
cục) và **ghi nhận máy nào đã hỏi / đã lên bản mới** (`/ota/check` hiện không biết ai gọi).
Không có 2 thứ này thì bấm cập nhật xong là mù, và hỏng firmware là hỏng cả fleet.

### Bước 5 — Cloudflare: **CHỐT đi đường A** (2026-08-17)

Hai đường khác nhau, đừng gộp làm một:

- **A — Tunnel đứng trước box FastAPI hiện tại. ← đang làm.** Giữ nguyên Postgres, mật khẩu
  `scrypt$`, `ratelimit.py`; Cloudflare chỉ thay Tailscale Funnel làm đường vào. Runbook đầy
  đủ: [CLOUDFLARE_TUNNEL.md](CLOUDFLARE_TUNNEL.md). Domain nằm trong tài khoản Cloudflare của
  **người khác** → đi kiểu remotely-managed bằng token, đã xin được token 2026-08-17.
  - [ ] `sudo cloudflared service install <token>` trên box → chờ `Registered tunnel connection`
  - [ ] Nhờ họ thêm Public Hostname `fbt.<domain>` → HTTP → `127.0.0.1:8080`
  - [ ] Nhờ họ thêm **Cache Rule bypass `/ota/*`** (quên = OTA phát bản cũ, im lặng)
  - [ ] Đổi `kDefaultEngineerUrl` (`lib/services/app_settings.dart:30`) + build lại Windows/web
  - [ ] **XOAY TOKEN** — token connector đã lọt vào transcript chat lúc trao đổi. Dashboard →
        tunnel `fbt` → Refresh token → chạy lại `service install`. Không gấp, nhưng đừng bỏ qua.
- **B — cắt hẳn sang Worker `server-cf/`.** Bỏ được MiniPC nhưng kéo theo: đặt lại mật khẩu mọi
  tài khoản `scrypt$`, gói Paid $5/tháng cho PBKDF2 (hoặc hạ số vòng), chống dò mật khẩu phải
  làm lại bằng Durable Object, nạp lại toàn bộ dữ liệu lịch sử. Xem `server-cf/README.md`.
  **Hoãn** — A đã cho đủ thứ cần (domain riêng, hết phụ thuộc `ts.net`), B chỉ thêm việc.

Cả hai đều cần mua domain trước. Và cả hai đều **đừng tắt Tailscale Funnel** cho tới khi 109
máy đã lên bản firmware trỏ domain mới.

---

## Song song, không phụ thuộc ai

- **Backup `pg_dump` + test restore thật.** Mục 5 trong thứ tự ưu tiên của chính
  [KE_HOACH_PHAT_TRIEN.md](KE_HOACH_PHAT_TRIEN.md); hiện **chưa có backup nào**.
- **Xoay secret đang lộ trong OneDrive** (mật khẩu root/engineer, `RECEIVER_TOKEN`, khoá
  SSH) — blocker tuyệt đối #4 trong [DANH_GIA_THI_TRUONG.md](DANH_GIA_THI_TRUONG.md).

## Nhắc lại điều dễ quên

[DANH_GIA_THI_TRUONG.md](DANH_GIA_THI_TRUONG.md) kết luận: server là **phần duy nhất đã đủ
tốt** và chỉ chiếm **<10%** khối lượng để ra thị trường. 90% còn lại — validate assay, sản
xuất/đăng ký kit, tìm khách trả tiền — **không có một dòng nào trong repo**. Nếu phải chọn
giữa "làm thêm cho server" và "đi kiểm chứng xét nghiệm", tài liệu của chính bạn nói chọn
cái sau.
