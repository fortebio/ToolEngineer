# 2026-08-19 — `/auth` phát token API theo VAI TRÒ

## Triệu chứng

Admin đăng nhập trên **điện thoại** (hoặc bất kỳ máy mới nào, kể cả bản web) thì
đọc dữ liệu bình thường nhưng **chọn bản firmware OTA báo lỗi token (401)**.

## Nguyên nhân

`auth.py` trả `"apiToken": config.TOKEN` cho **mọi** vai trò — đó là
`RECEIVER_TOKEN`, token thiết bị. Nhưng từ 2026-08-18 bốn route ghi OTA
(`PUT /ota/target/{file}`, `DELETE /ota/target`, `PUT /ota/{file}`,
`DELETE /ota/{file}`) gác bằng **`ota_admin`** → chỉ `OTA_ADMIN_TOKEN` qua được.

Phía app: `AuthApi.login` chỉ lưu `apiToken` **khi ô token đang TRỐNG**. Máy cũ
của kỹ sư đã dán tay token admin nên không thấy lỗi; máy mới thì nhận token
thiết bị và hỏng — rất dễ đổ nhầm cho "server chưa deploy".

## Sửa

`app/auth.py` — tách hàm **`api_token_for(role)`**:

- `root` / `admin` → `OTA_ADMIN_TOKEN`
- vai trò khác → `TOKEN` (đọc được, không ghi được)
- `OTA_ADMIN_TOKEN` rỗng → rơi về `TOKEN` y như cũ ⇒ **deploy file này một mình
  KHÔNG đổi hành vi gì**, an toàn để lên trước.

## Vì sao KHÔNG nhúng token vào app

Bản web nằm ở `hub.fortebio.tech/app/main.dart.js` — **công khai**. Nhúng token
admin vào đó (`--dart-define`) là ai tải file JS về cũng nạp được firmware cho
cả 109 máy. Phát token **sau khi đã xác thực, theo vai trò** là cách duy nhất
vừa tiện vừa không lộ.

⚠️ Đổi lại: **mọi tài khoản `admin` giờ cầm quyền nạp firmware toàn fleet**.
Đúng bằng thực tế đang diễn ra (token admin vẫn được dán tay lên từng máy kỹ
sư), nhưng từ nay nó tự động — nên **soát lại danh sách tài khoản `admin`**.
Ai chỉ cần xem thì để vai trò `user`.

## Test

`tests/test_logic.py::test_api_token_phat_theo_vai_tro` — phủ cả 3 nhánh
(nhân sự / khách hàng / chưa bật `OTA_ADMIN_TOKEN`). Đã gieo 2 lỗi để kiểm test
có cắn: "quay về hành vi cũ" và "phát nhầm token admin cho khách hàng" — **cả
hai đều bị bắt**.

## Thứ tự triển khai — QUAN TRỌNG

**Server TRƯỚC, app SAU.** App sau bản này ghi đè `engineerToken` bằng token
server trả về **mỗi lần đăng nhập**. Chạy app mới với server chưa vá thì token
admin đã dán tay bị ghi đè bằng token thiết bị → OTA 401.

Kiểm server đã vá chưa (đăng nhập bằng tài khoản root, so với
`OTA_ADMIN_TOKEN` trong `/etc/fbt-receiver.env`):

```bash
curl -s -X POST https://hub.fortebio.tech/auth \
  -H 'Content-Type: text/plain' \
  -d '{"action":"login","username":"<root>","password":"<mk>"}' | grep -o '"apiToken":"[^"]*"'
```
