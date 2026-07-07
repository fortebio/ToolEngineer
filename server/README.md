# FBT_RAPID — Server tự host (Docker + Postgres + API)

Backend **miễn phí, tự host** cho app FBT_RAPID, chạy **song song** với Google Apps Script (nguồn
cloud thứ 4 chọn được trong app). Thiết bị POST kết quả → lưu Postgres → **admin** đọc từ app, public
ra ngoài qua **DuckDNS (DNS động miễn phí) + Caddy (HTTPS tự động)**.

```
[Thiết bị] --POST /ingest (X-Device-Key)--> ┐  Router (port-forward 80,443)
[App admin] --GET /api?action=… (Bearer)--> ┤        │
                                            ▼        ▼
            https://ten.duckdns.org → [caddy: TLS] → [api:3000] → [postgres]
                                       [duckdns: cập nhật IP nhà]
```

## 0. Yêu cầu
- **Docker Desktop** (Windows/macOS) hoặc Docker Engine (Linux) trên **một máy luôn bật** (PC, laptop
  cũ, hoặc Raspberry Pi). *Máy phải bật để nhận dữ liệu — cái giá của "free, không thẻ".*
- Để có URL công khai (§3): quyền **mở port 80 + 443 trên router** và **IP công khai thật** (không
  CGNAT, ISP không chặn 80/443). Cách kiểm tra ở cuối §3.

## 1. Cấu hình
```bash
cd server                 # thư mục server/ nằm trong dự án app
cp .env.example .env      # rồi mở .env điền: POSTGRES_PASSWORD, ADMIN_TOKEN, DEVICE_KEY
```
- `ADMIN_TOKEN` — admin nhập trong app (Cài đặt → Server riêng → Token).
- `DEVICE_KEY` — firmware gửi khi POST.

## 2. Chạy cục bộ & kiểm thử (chưa cần public)
```bash
docker compose up -d --build      # chỉ db + api
curl localhost:3000/health
# → {"ok":true}

# Bơm 1 run mẫu (giả lập thiết bị POST):
curl -X POST localhost:3000/ingest \
  -H "X-Device-Key: <DEVICE_KEY>" -H "Content-Type: application/json" \
  --data @sample_run.json
# → {"ok":true,"id":"1"}

# Đọc như app (cần Bearer):
curl -H "Authorization: Bearer <ADMIN_TOKEN>" "localhost:3000/api?action=ids"
curl -H "Authorization: Bearer <ADMIN_TOKEN>" "localhost:3000/api?action=runs&id=RPLTest&limit=10&offset=0"
curl -H "Authorization: Bearer <ADMIN_TOKEN>" "localhost:3000/api?action=run&fileId=1"
```
Kỳ vọng `action=run`: `result` là chữ `N/P/S/E`, `ct` là số, `curves` là 10 chuỗi `"a,b,c,…"`.
Sai token → `401`. Sai device key → `401`.

> PowerShell: `curl` là alias của `Invoke-WebRequest`. Dùng `curl.exe` (kèm `.exe`) để có cú pháp
> giống trên, hoặc `Invoke-RestMethod`.

## 3. URL công khai miễn phí bằng DuckDNS + mở port

> **Vì sao không dùng Cloudflare Tunnel "miễn phí"?** Domain free duy nhất còn sống (`.eu.org`) **không
> add được vào Cloudflare gói Free** (Error 1049 — `.eu.org` không nằm trong ICANN Public Suffix List),
> mà Cloudflare Tunnel named hostname BẮT BUỘC domain là zone trên Cloudflare. Freenom (.tk/.ml/…) đã
> chết từ 2023. Quick Tunnel thì URL đổi mỗi lần chạy (chỉ test). → Muốn free + cố định thì dùng DuckDNS
> + mở port (dưới đây); muốn dùng Cloudflare Tunnel thì phải **mua** domain (~$10/năm) rồi `--profile tunnel`.

**Tạo domain DuckDNS (miễn phí, không cần thẻ):**
1. Vào **https://www.duckdns.org** → **đăng nhập** bằng Google/GitHub/Reddit/Twitter.
2. Ô **"sub domain"** gõ tên muốn (vd `fbtrapid`) → bấm **add domain** → có `fbtrapid.duckdns.org`.
3. Copy **token** (chuỗi hiện ở đầu trang).

**Điền `.env`:**
```
DUCKDNS_SUBDOMAIN=fbtrapid
DUCKDNS_TOKEN=<token vừa copy>
PUBLIC_HOST=fbtrapid.duckdns.org
```

**Mở port-forward trên router**: TCP **80** và **443** → **IP LAN của máy chạy Docker** (vd 192.168.1.x).
(Đặt IP tĩnh/đặt trước cho máy đó trong router để khỏi lệch.)

**Chạy kèm DuckDNS + Caddy:**
```bash
docker compose --profile duckdns up -d --build
```
- `duckdns` tự cập nhật `fbtrapid.duckdns.org` = IP công khai hiện tại (kể cả khi IP nhà đổi).
- `caddy` tự xin + gia hạn chứng chỉ **Let's Encrypt** cho host đó, proxy mọi path vào `api:3000`.
  (Lần đầu cần ~10–30s để có cert; xem `docker compose logs -f caddy`.)

**Kiểm tra public** (từ mạng KHÁC, vd 4G điện thoại — không qua wifi nhà):
```bash
curl https://fbtrapid.duckdns.org/health      # → {"ok":true}, TLS hợp lệ
```
- Nếu treo/không tới: thường do **CGNAT** (ISP không cho IP công khai) hoặc **chặn cổng 80/443**. Khi đó
  port-forward vô hiệu → phải đổi sang **ngrok free static domain** hoặc **mua domain + Cloudflare Tunnel**.

## 4. Cấu hình trong app (admin)
Đăng nhập **admin/root** → **Cài đặt → Server riêng**:
- **URL**: `https://fbtrapid.duckdns.org/api` (hoặc `http://localhost:3000/api` khi test cùng máy).
- **Token**: đúng `ADMIN_TOKEN`.
Sang tab **Lịch sử (Cloud)** → gạt **"Server riêng"** → xem dữ liệu; nút **Đồng bộ về máy** chỉ admin thấy.

## 5. Firmware đẩy dữ liệu (riêng)
Để có dữ liệu thật, firmware (PlatformIO — repo **FBT-DXD** riêng) POST **thêm** lên server này — song song POST
Apps Script cũ:
- `POST https://fbtrapid.duckdns.org/ingest`, header `X-Device-Key: <DEVICE_KEY>` (hoặc `?key=<DEVICE_KEY>`),
  body = đúng JSON kết quả hiện tại (`sample_run.json`). Trùng `id_device`+`time` sẽ cập nhật, không nhân bản.

## API tóm tắt
| Method | Path | Auth | Mô tả |
|---|---|---|---|
| GET | `/health` | — | `{ok:true}` |
| POST | `/ingest` | `X-Device-Key` | Nhận 1 run (raw JSON firmware) |
| GET | `/api?action=ids` | `Bearer` | Danh sách máy `{id,runCount,latest,version}` |
| GET | `/api?action=runs&id=&limit=&offset=` | `Bearer` | Trang run (summary, không curves) |
| GET | `/api?action=run&fileId=` | `Bearer` | 1 run đầy đủ (kèm curves) |

## Vận hành
- Xem log: `docker compose logs -f api` · `docker compose logs -f caddy duckdns`
- Dừng: `docker compose --profile duckdns down` (giữ dữ liệu) · Xóa sạch: thêm `-v`.
- Volume: `pgdata` (DB), `caddy_data` (chứng chỉ HTTPS — đừng xóa kẻo phải xin lại cert). RAW JSON
  firmware giữ nguyên trong cột `raw` (JSONB), kể cả `outcome`/`peak_features`/`origins`/`LED_power`.
- **Phương án public KHÁC** (nếu DuckDNS+port không chạy được): `--profile tunnel` (Cloudflare Tunnel,
  **cần domain trả phí** làm zone trên Cloudflare) — token ở `.env` (`TUNNEL_TOKEN`).
- **Nâng cấp 24/7 thật**: bê nguyên stack lên VPS (vd Oracle Cloud Always Free) nếu sau này chịu xác
  minh thẻ — không đổi code, chỉ đổi nơi chạy.
