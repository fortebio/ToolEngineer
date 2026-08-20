# server-cf — FBT Server chạy trên Cloudflare Workers

Bản chuyển của [`server/`](../server) (FastAPI + Postgres + file trên MiniPC) sang
Cloudflare. **Giữ nguyên hợp đồng HTTP** nên app Flutter và firmware không phải sửa gì
ngoài URL.

| `server/` (MiniPC) | `server-cf/` (Cloudflare) |
|---|---|
| FastAPI + uvicorn + systemd | Worker (`src/index.js`) |
| Postgres `sessions` / `users` | **D1** (SQLite) — `schema.sql` |
| File JSON `data_plus/` | **R2** tiền tố `data/` |
| `ota/*.bin` + `target.json` | **R2** tiền tố `ota/` + bảng `settings` |
| Mount tĩnh `/app` | **Workers Assets** (`public/app/`) |
| Tailscale Funnel | domain trên Cloudflare |

## Chạy thử cục bộ

```bash
cd server-cf
npm install
npm run schema:local           # tạo bảng trong D1 giả lập
node scripts/seed_local.mjs    # 1 tài khoản root/rootpass để test đăng nhập
npm test                       # 15 test hợp đồng, tự bật/tắt wrangler dev
npm run dev                    # http://127.0.0.1:8787
```

`npm test` bật `wrangler dev --local` (D1 + R2 giả lập trên đĩa) rồi gọi HTTP thật vào —
đối chiếu với `server/tests/test_api.py`.

## Deploy

```bash
npx wrangler d1 create fbt                    # → dán database_id vào wrangler.toml
npx wrangler r2 bucket create fbt-data
npm run schema:remote
npx wrangler secret put RECEIVER_TOKEN        # DÙNG LẠI token thiết bị đang dùng
flutter build web --release --base-href /app/ # ở thư mục app, rồi copy build/web → public/app/
npm run deploy
```

Gắn domain: Cloudflare dashboard → Workers → Custom Domains. Domain phải là zone trên
Cloudflare (mua qua Cloudflare Registrar là tự động).

## ⚠️ Ba điều PHẢI biết trước khi cắt

### 1. Không cắt được trong một nhát — 109 máy đang nạp cứng URL cũ

Firmware POST thẳng vào `fbt.basa-luma.ts.net`. Đổi được URL đó chỉ bằng cách nạp lại
firmware — mà tính năng OTA thì **chính nó cũng chưa chạy được** (firmware chưa biết gọi
`/ota/check`). Vòng luẩn quẩn. Thứ tự an toàn:

- **Bước A** — Deploy Worker, nạp dữ liệu lịch sử, trỏ **app Flutter** sang domain mới.
  Box vẫn nhận POST của thiết bị.
- **Bước B** — Cho box **chuyển tiếp** mỗi lần ingest sang Worker (thêm ~10 dòng vào
  `server/app/main.py`: sau khi lưu local thì POST tiếp sang Worker). Không có bước này
  thì dữ liệu mới nằm ở box còn app đọc Worker → **app không thấy phiên mới**.
- **Bước C** — Firmware biết OTA → nạp bản mới trỏ thẳng domain Cloudflare → nghỉ box.

Đừng tắt Tailscale Funnel trước bước C.

### 2. Mật khẩu băm scrypt sẽ không đăng nhập được

Workers không có `scrypt` (WebCrypto chỉ có PBKDF2/SHA/HMAC). Kết quả:

| Dạng băm | Nguồn | Trên Workers |
|---|---|---|
| `sha256$salt$hash` | di cư từ Google Sheet | ✅ đăng nhập được |
| `scrypt$salt$hash` | tạo/đổi mật khẩu trên FastAPI | ❌ **phải đặt lại mật khẩu** |
| `pbkdf2$iter$salt$hash` | Worker tạo từ giờ | ✅ |

`scripts/export_pg_to_d1.py` in ra danh sách tài khoản dính. Worker trả thông báo riêng
("nhờ root đặt lại mật khẩu") thay vì "sai mật khẩu" để user khỏi mò.

**CPU**: PBKDF2 100.000 vòng vượt hạn mức **10ms CPU/request của gói Workers FREE** →
login sẽ lỗi. Cần gói **Paid ($5/tháng)**, hoặc hạ `PBKDF2_ITER` xuống ~10000 trong
`wrangler.toml` (đổi lại độ an toàn).

### 3. Nợ mang theo — CỐ Ý không sửa trong bước này

`/auth` login vẫn trả `apiToken` = **master `RECEIVER_TOKEN`**, và Worker **không lọc theo
`ids`** (phạm vi máy vẫn enforce ở client). Nghĩa là lỗ hổng "user bất kỳ đọc được mọi máy
+ POST kết quả giả" **được bê nguyên sang Cloudflare**. Chuyển hạ tầng và vá bảo mật là 2
việc khác nhau — trộn vào nhau thì lúc hỏng không biết tại cái nào. Vá riêng sau (token
theo từng user trong bảng `users`, route đọc lọc theo `ids`).

**Cũng chưa port: chống dò mật khẩu.** Bản FastAPI đã có `app/ratelimit.py` (đếm thất bại
theo `(ip, username)`, state trong RAM — chạy được vì uvicorn 1 worker). Worker thì **mỗi
isolate một bộ nhớ riêng**, biến toàn cục không dùng làm bộ đếm chung được → phải làm bằng
**Durable Object** hoặc bảng D1. Chưa làm; nhớ trước khi mở `/auth` ra công khai.

## Khác biệt kỹ thuật so với bản FastAPI

- **`/ingest` cả 2 đường ghi hỏng → trả 500** (bản cũ trả `{ok:true}`, thiết bị không
  retry → mất hẳn phiên đo). Đây là sửa lỗi CÓ CHỦ Ý, không phải lệch hợp đồng.
- **`users` UNIQUE theo `lower(username)`** — bản Postgres để UNIQUE phân biệt hoa/thường
  trong khi mọi truy vấn dùng `lower()`, tạo được cả `admin` lẫn `Admin`.
- **Hash nội dung khác Python**: `json.dumps` của Python ghi `1.0`, `JSON.stringify` ghi
  `1` → cùng payload ra `body_sha256` khác nhau giữa 2 backend. Chỉ ảnh hưởng dedup CHÉO
  (bảng `sessions` vốn không unique theo hash); trong phạm vi Worker thì retry vẫn ra cùng
  key R2 nên không sinh file rác.
- **Thời gian là chuỗi ISO UTC** (SQLite không có `timestamptz`). Luôn ghi dạng
  `...Z` thì `ORDER BY` chuỗi = sắp theo thời gian.
- **`GET /ota/check` đọc cả file để tính sha256** — firmware .bin vài MB nên vẫn trong hạn
  mức, nhưng nếu file lớn lên thì nên lưu sẵn sha256 vào `settings` lúc upload.

## Hạn mức gói free (đủ xài cho quy mô hiện tại)

| | Free | Đang dùng |
|---|---|---|
| Workers | 100k request/ngày, **10ms CPU/req** | ~vài trăm/ngày · CPU là vấn đề DUY NHẤT (mục 2) |
| D1 | 5GB, 5M dòng đọc/ngày | 3.372 phiên ≈ 26MB |
| R2 | 10GB, **miễn phí egress** | ~28MB JSON + firmware |

## Nạp dữ liệu lịch sử

```bash
# Trên box: xuất Postgres → các file .sql
~/fbt_server/venv/bin/python export_pg_to_d1.py --out /tmp/d1

# Máy dev: nạp vào D1
for f in d1/*.sql; do npx wrangler d1 execute fbt --remote --file "$f"; done

# File JSON gốc → R2 (rclone qua endpoint S3 của R2 — nhanh hơn wrangler từng file)
rclone copy ~/fbt_server/data_plus r2:fbt-data/data
```
