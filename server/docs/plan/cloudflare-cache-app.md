# Cloudflare: thôi cache đè lên `/app/*` (đo 2026-09-25)

> Việc của người giữ tài khoản Cloudflare zone `fortebio.tech`. Phiên AI không vào được dashboard.
> Đo bằng `curl --resolve` vì DNS router không phân giải `hub.fortebio.tech` (xem `server/CLAUDE.md`).

## 1. Triệu chứng

Deploy web xong, người dùng **vẫn thấy bản cũ tới 4 giờ**, không báo lỗi gì. Đã dính thật
2026-09-23 (tab Hiệu chuẩn "không thấy" dù box đã đúng).

## 2. Nguyên nhân (đã đo, không phải suy đoán)

| Đường dẫn | Origin gửi | Cloudflare trả | |
|---|---|---|---|
| `/app/index.html`, `/app/manifest.json` | `no-cache` | `no-cache` · `cf-cache-status: DYNAMIC` | ✅ |
| `/app/*.js` (kể cả `esptool.js`, `canvaskit/*.js`) | `no-cache` | **`max-age=14400`** · `MISS`/`REVALIDATED` | ❌ |
| `/` , `/openapi.json` | (không gửi) | `DYNAMIC` | ✅ API không bị cache |

Origin (FastAPI `StaticFiles`) gửi `Cache-Control: no-cache` + `ETag` cho MỌI file dưới `/app/`.
Cloudflare **ghi đè** thành `max-age=14400` cho các đuôi file tĩnh — đây là setting
**Caching → Configuration → Browser Cache TTL = 4 hours** của zone.

## 3. Vì sao gắn hash tên file CHƯA đủ

`deploy-web.ps1` (2026-09-23) mới gắn hash cho **3** file: `main.<hash>.dart.js`,
`flutter_bootstrap.<hash>.js`, `favicon.<hash>.png`. Bản build còn **20 file tĩnh tên CỐ ĐỊNH** mà
app nạp lúc chạy:

```
canvaskit/canvaskit.{js,wasm} · canvaskit/chromium/… · canvaskit/skwasm*.{js,wasm} · canvaskit/wimp.*
assets/assets/fonts/{DMSans,JetBrainsMono,SourceSerif4}.ttf
assets/fonts/MaterialIcons-Regular.otf · esptool.js · flutter.js
```

Chúng đổi khi: **nâng Flutter SDK** (canvaskit, flutter.js), **đổi font thương hiệu**
(quy tắc 6 monorepo), **nâng bundle esptool-js**. Mỗi lần như vậy lại dính đúng bẫy 4 giờ.
Gắn hash tiếp cho nhóm này thì phải viết lại tham chiếu do `flutter.js`/bootstrap sinh **lúc chạy** —
mong manh, không nên.

## 4. Phương án

### 4A. KHUYẾN NGHỊ — Cache Rule giới hạn đúng `/app/*`

Rules → **Caching Rules** → Create rule

- **Tên**: `hub-app-respect-origin`
- **Biểu thức** (Edit expression):
  ```
  (http.host eq "hub.fortebio.tech" and starts_with(http.request.uri.path, "/app/"))
  ```
- **Then**:
  - Cache eligibility: **Eligible for cache**
  - Edge TTL: **Use cache-control header from origin**
  - Browser TTL: **Respect origin TTL**

Origin đang nói `no-cache` + có `ETag` → trình duyệt hỏi lại mỗi lần, origin trả **304** (vài trăm
byte) nếu chưa đổi. Deploy xong là thấy ngay, **không cần purge lần nào nữa**.

⚠️ **ĐỪNG sửa Browser Cache TTL toàn zone** (`Caching → Configuration`) cho nhanh: zone
`fortebio.tech` còn có **`api.fortebio.tech` của team RAPID ERP**. Đổi setting zone là đụng vào hệ
của họ mà không ai báo. Cache Rule ở trên chỉ khớp đúng host `hub.` + path `/app/`.

### 4B. Thay thế — Bypass cache (giống rule đã làm cho `/ota/*`)

Cùng biểu thức, nhưng **Then → Bypass cache**. Đơn giản hơn, nhưng mọi lần tải đều kéo nguyên file
từ box qua Tunnel (`main.dart.js` 3,8 MB + canvaskit ~6 MB) thay vì trả 304. Đội dùng app ít người
thì vẫn chấp nhận được; chọn 4A nếu muốn nhẹ đường truyền.

### 4C. Làm thêm sau (phía server, KHÔNG bắt buộc)

Cho `StaticFiles` gửi `Cache-Control: public, max-age=31536000, immutable` cho file **có hash** và
giữ `no-cache` cho file không hash. Khi đó bật lại cache ở CF là vừa nhanh vừa không bao giờ cũ.
Chỉ đáng làm nếu sau này mở app cho nhiều người ngoài xưởng.

## 5. Làm ngay một lần (dọn thứ đang nằm trong cache)

Caching → Configuration → Purge Cache → **Custom Purge** → URL:

```
https://hub.fortebio.tech/app/*
```

## 6. Kiểm sau khi đặt rule

DNS router không phân giải `hub.fortebio.tech` → ép IP (lấy IP bằng `nslookup hub.fortebio.tech 1.1.1.1`):

```bash
curl -sI --resolve hub.fortebio.tech:443:104.21.54.92 https://hub.fortebio.tech/app/main.dart.js | grep -iE "cache-control|cf-cache-status"
```

- **Đạt**: `cache-control: no-cache` và `cf-cache-status: BYPASS` (4B) hoặc `DYNAMIC`/`REVALIDATED`
  kèm `no-cache` (4A).
- **Chưa đạt**: còn thấy `max-age=14400` → rule chưa khớp (kiểm lại host/path trong biểu thức).

Kiểm luôn một file KHÔNG hash, vì đó mới là nhóm cần được cứu:

```bash
curl -sI --resolve hub.fortebio.tech:443:104.21.54.92 https://hub.fortebio.tech/app/canvaskit/canvaskit.js | grep -i cache
curl -sI --resolve hub.fortebio.tech:443:104.21.54.92 https://hub.fortebio.tech/app/esptool.js | grep -i cache
```

## 7. Sau khi xong thì sửa tài liệu

`apps/fbt_rapid/CLAUDE.md` và `server/docs/plan/QUY_TRINH_DEPLOY.md` §C đang dặn "nhờ purge
Cloudflare sau mỗi lần deploy" — đặt rule xong thì bỏ câu đó, và bỏ luôn đoạn cảnh báo purge mà
`deploy-web.ps1` in ra cuối mỗi lần chạy.
