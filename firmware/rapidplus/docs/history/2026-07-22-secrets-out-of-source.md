# 2026-07-22 — Chuyển endpoint + API token ra khỏi source (gitignored)

## Vấn đề

`Bluetooth.cpp` hardcode 3 URL + 2 credential upload:
- `serverName` (GAS/Google Sheet), `serverName2` (ingest), `server_engineerToken` (Bearer),
- `serverERP` (ERP api.fortebio), `server_erpToken` (X-API-Key).

Chúng nằm thẳng trong source được commit → **lộ qua repo/git history**. Xác nhận cả 2 token
đã vào history: engineer token ở commit `0607037`, ERP token ở `2c68182`.

## Fix

Gom toàn bộ endpoint + credential vào **`src/secrets.h`** (macro `SECRET_*`), **gitignored**;
`Bluetooth.cpp` chỉ `#include "secrets.h"` và gán `const char* = SECRET_*`. Kèm
**`src/secrets.example.h`** (committed) làm template.

- `.gitignore`: thêm `src/secrets.h`.
- Build cần `src/secrets.h` tồn tại; thiếu → compile fail (cố ý, để một secrets thiếu lộ ra
  ngay thay vì âm thầm ship placeholder).

Kết quả: từ nay token **không** đi vào repo. Source hiện tại không còn literal nào.

## Về token cũ trong git history — ĐÃ CÂN NHẮC, QUYẾT ĐỊNH KHÔNG ROTATE

Chuyển ra `secrets.h` **không xoá** giá trị đã có trong git history (`0607037`, `2c68182`,
đã push lên `github.com/wuanpham/FBT-DXD`). Về lý thuyết muốn đóng hẳn thì phải đổi token mới
phía server rồi cập nhật `src/secrets.h`.

**Quyết định (2026-07-22): KHÔNG rotate.** Lý do: repo là **private** (GitHub API trả
`Not Found`), chỉ người được cấp quyền mới đọc được, nên rủi ro được đánh giá là chấp nhận
được. Nếu sau này repo chuyển public, hoặc có người ngoài nhóm từng được cấp quyền / còn giữ
bản clone, thì nên rotate lại (ingest Bearer + ERP X-API-Key), cập nhật `src/secrets.h` rồi
nạp lại firmware cho mọi máy.

Ghi chú: firmware embedded luôn chứa token trong ảnh flash; biện pháp này chỉ đóng đường lộ
qua **repo**, không phải qua dump flash thiết bị (cần secure element cho việc đó).

## Kiểm chứng

- `git check-ignore src/secrets.h` → khớp (đã ignored).
- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).

## Thiết lập build (máy mới / CI)

```bash
cp src/secrets.example.h src/secrets.h   # rồi điền giá trị thật
pio run -e esp32dev
```
