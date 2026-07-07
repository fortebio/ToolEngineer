# 02 — Xác thực & phân quyền

## 1. Ba vai trò

| Vai trò (`UserRole`) | Mã | Ý nghĩa | `isStaff` | `canManageUsers` |
|---|---|---|:---:|:---:|
| `root` | `root` | Root — super-admin, quản lý tài khoản | ✅ | ✅ |
| `admin` | `admin` | Nhân viên — full app, KHÔNG quản lý tài khoản | ✅ | ❌ |
| `user` | `user` | Khách hàng — chỉ xem (read-only) | ❌ | ❌ |

Mã: [lib/models/user_session.dart](../lib/models/user_session.dart).

## 2. Luồng đăng nhập

```mermaid
sequenceDiagram
  participant U as Người dùng
  participant L as LoginScreen
  participant A as AuthApi
  participant GAS as Apps Script (userAuth.js)
  participant S as SessionStore

  U->>L: nhập tài khoản + mật khẩu
  L->>A: login(username, password)
  A->>GAS: POST {action:"login", ...}  (followRedirects=false)
  GAS-->>A: 302 Location → script.googleusercontent.com
  A->>A: drain() body redirect (tránh 401 hop sau)
  A->>GAS: GET Location
  GAS-->>A: 200 JSON {ok, username, role, ids, ...}
  A->>A: UserSession.fromJson (tự tính allowAll)
  A-->>L: UserSession
  L->>S: SessionStore.save(session)
  Note over S: current = session + lưu shared_preferences
  L->>U: vào HomeShell
```

### 2.1. Giải thuật xử lý 302 của Apps Script (quan trọng)

`http.post` của Dart **không** tự đi theo redirect tới URL "echo" của Apps Script
→ phải tự lái tay:

```mermaid
flowchart TD
  A["POST /exec<br/>followRedirects=false<br/>Content-Type: text/plain"] --> B{"status 3xx?"}
  B -- không --> Z["đọc body → utf8.decode → jsonDecode"]
  B -- "có (hops&lt;5)" --> C["lấy header Location"]
  C --> D{"Location rỗng?"}
  D -- có --> Z
  D -- không --> E["await stream.drain()<br/>(BẮT BUỘC, không thì hop sau 401)"]
  E --> F["GET Location<br/>followRedirects=false"]
  F --> B
```

Điểm chí tử (đã gặp lỗi thật):
- **Phải `drain()`** body của response 302, nếu không kết nối tái dùng "bẩn" →
  hop GET sau nhận **401**.
- **`Content-Type: text/plain`** để Apps Script vẫn đọc `e.postData.contents`
  mà tránh CORS preflight.
- Giải mã `utf8.decode(resp.bodyBytes)` để giữ dấu tiếng Việt.

Mã: [lib/services/auth_api.dart](../lib/services/auth_api.dart) (`_post`).

## 3. Tính `allowAll` — phạm vi xem máy

`UserSession.fromJson` **TỰ tính** `allowAll` (không tin backend cũ):

```mermaid
flowchart LR
  A["role, ids từ backend"] --> B{"role == root?"}
  B -- có --> AA["allowAll = true"]
  B -- không --> C{'ids chứa "*"?'}
  C -- có --> AA
  C -- không --> NN["allowAll = false<br/>→ chỉ thấy đúng ids được cấp"]
```

> **Admin KHÔNG còn auto thấy mọi máy** — lọc theo `ids` được cấp như khách hàng
> (chỉ `*` mới full). `isStaff` chỉ quyết định **canWrite** + hiện tab Kỹ Thuật,
> KHÔNG quyết định phạm vi xem.

`canSee(deviceId)`:

```mermaid
flowchart TD
  S["canSee(id)"] --> A{allowAll?}
  A -- có --> T[true]
  A -- không --> B{"id rỗng?"}
  B -- có --> F[false]
  B -- không --> C{"ids chứa id?<br/>(không phân biệt hoa thường)"}
  C -- có --> T
  C -- không --> F
```

## 4. Ma trận quyền

| Khả năng | Cờ | root | admin | user |
|---|---|:---:|:---:|:---:|
| Xem lịch sử các máy được phép | `canSee` | mọi máy | theo `ids` | theo `ids` |
| Ghi mạnh (Đồng bộ/Xóa/Lấy-từ-máy) | `canWrite = isStaff` | ✅ | ✅ | ❌ |
| Hiện tab Kỹ Thuật | `isStaff` | ✅ | ✅ | ❌ |
| Quản lý tài khoản | `canManageUsers = isRoot` | ✅ | ❌ | ❌ |
| Chọn nguồn cloud (Google/RAPID ERP) | `canWrite` | ✅ | ✅ | ❌ |
| Lưu ảnh đồ thị về thư mục | `canSaveCharts` | ✅ | ✅ | ✅ |

Lọc dữ liệu **enforce ở client**: màn `cloud_devices`/`history` fetch hết rồi
mới lọc `canSee`; nút ghi bọc `if (SessionStore.canWrite)`.

## 5. Quản lý tài khoản (root)

Vì `SessionStore` **KHÔNG lưu mật khẩu**, các action admin (listUsers/saveUser/
deleteUser) **hỏi lại mật khẩu admin mỗi lần** (chỉ giữ trong RAM):

```mermaid
sequenceDiagram
  participant R as Root (UserManagementScreen)
  participant A as AuthApi
  participant GAS as userAuth.js

  R->>R: nhập lại mật khẩu admin (giữ RAM)
  R->>A: listUsers(adminUser, adminPassword)
  A->>GAS: POST {action:"listUsers", adminUser, adminPassword}
  GAS-->>A: {ok, users:[...]}  (KHÔNG kèm mật khẩu)
  A-->>R: List AccountInfo
  R->>A: saveUser / deleteUser (kèm adminUser+adminPassword)
  A->>GAS: POST tương ứng (gate role=="root")
```

Mật khẩu băm `sha256$<salt>$<hash>` ở backend; app gửi plaintext qua HTTPS
(server băm + so khớp).

## 6. Vòng đời phiên

```mermaid
stateDiagram-v2
  [*] --> ChuaDangNhap
  ChuaDangNhap --> DangNhap: login OK → SessionStore.save
  DangNhap --> DangNhap: đổi email → copyWith + save
  DangNhap --> ChuaDangNhap: Đăng xuất → SessionStore.clear
  DangNhap --> DangNhap: mở lại app → SessionStore.load (khôi phục)
```
