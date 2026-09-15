"""Tài khoản đăng nhập app — dispatch action theo HỢP ĐỒNG userAuth.js (Apps Script).

App Flutter chuyển đăng nhập từ Google Sheet về đây mà gần như KHÔNG đổi code:
POST /auth body JSON {action: login | changePassword | changeEmail | listUsers |
saveUser | deleteUser, ...} — response LUÔN HTTP 200 {ok:true,...} / {ok:false,error}
(app đọc cờ `ok`, không đọc HTTP status). Các action admin re-verify
adminUser+adminPassword MỖI LẦN và yêu cầu role root (app không lưu mật khẩu).
"""
from app import config, db
from app.logic import hash_password, parse_active, parse_ids, verify_password

LOGIN_FAIL_MSG = "Sai tài khoản hoặc mật khẩu"  # dùng CHUNG cho mọi lỗi login (không lộ thông tin)
NO_PERMISSION_MSG = "Không có quyền"
# Vai trò hợp lệ. `manager`/`operator` thêm 2026-09-07 cho xưởng sản xuất
# (docs/plan/tai-khoan-nha-may.md ở repo app).
_ROLES = ("root", "admin", "manager", "operator", "user")
# Vai trò được phép quản lý tài khoản, và phạm vi của từng cái.
_MANAGE_SCOPE = {"root": _ROLES, "manager": ("operator",)}


def dispatch(body: dict) -> dict:
    """Điều phối theo body.action. Lỗi bất ngờ do caller (route) bắt → {ok:false}."""
    action = str(body.get("action") or "").strip()
    handler = {
        "login": _login,
        "changePassword": _change_password,
        "changeEmail": _change_email,
        "listUsers": _list_users,
        "saveUser": _save_user,
        "deleteUser": _delete_user,
    }.get(action)
    if handler is None:
        return {"ok": False, "error": "Hành động không hợp lệ"}
    return handler(body)


def _str(v) -> str:
    """Ép primitive về chuỗi; object/array → '' (khớp toPrimitiveStr_ userAuth.js)."""
    return str(v) if isinstance(v, (str, int, float, bool)) else ""


def _check_login(username: str, password: str) -> dict | None:
    """Xác thực 1 cặp user/pass: trả record nếu hợp lệ, None nếu sai/khoá/không có."""
    if not username:
        return None
    u = db.get_user(username)
    if u is None or not u["active"] or not verify_password(password, u["password"]):
        return None
    return u



def api_token_for(role: str) -> str:
    """Token API phát cho client SAU khi đăng nhập thành công, theo VAI TRÒ.

    Nhân sự (root/admin) nhận `OTA_ADMIN_TOKEN` — token DUY NHẤT qua được các
    route ghi OTA (`ota_admin`). Khách hàng nhận token thiết bị `TOKEN`: đọc
    được, không ghi được.

    Vì sao phát theo vai trò thay vì nhúng vào app: bản web nằm ở
    `hub.fortebio.tech/app/main.dart.js` — CÔNG KHAI. Nhúng token admin vào đó
    là ai tải file JS về cũng nạp được firmware cho cả 109 máy. Phát sau khi đã
    xác thực là cách duy nhất vừa tiện vừa không lộ.

    Trước 2026-08-19 mọi vai trò đều nhận `TOKEN`, nên admin đăng nhập trên máy
    mới (điện thoại, bản web) đọc được hết nhưng **chọn bản OTA thì 401** — và
    cách chữa duy nhất là dán tay token admin vào Cài đặt trên TỪNG máy.

    `OTA_ADMIN_TOKEN` rỗng (tính năng chưa bật) → rơi về `TOKEN` y như cũ, nên
    deploy file này một mình KHÔNG đổi hành vi gì.

    **Vai trò xưởng (`manager`/`operator`) nhận `TOKEN`, KHÔNG nhận token admin.**
    Đây là nửa quan trọng nhất của bước A: máy trạm đặt ở xưởng ghi được hồ sơ
    ATE (`PUT /ate/records` chỉ cần token thường) và tải được `.bin` để nạp,
    nhưng KHÔNG arm được firmware cho 109 máy ngoài thị trường. Trước 2026-09-07
    muốn cho thao tác viên vào tab Sản xuất thì phải cấp `admin`, tức là trao
    luôn quyền đó. Bước B sẽ tách tiếp `STATION_TOKEN` để họ cũng không đọc được
    dữ liệu lâm sàng (docs/plan/tai-khoan-nha-may.md §4).
    """
    if role in ("root", "admin") and config.OTA_ADMIN_TOKEN:
        return config.OTA_ADMIN_TOKEN
    return config.TOKEN

def _login(body: dict) -> dict:
    u = _check_login(_str(body.get("username")).strip(), _str(body.get("password")))
    if u is None:
        return {"ok": False, "error": LOGIN_FAIL_MSG}
    ids = list(u["ids"] or [])
    return {
        "ok": True,
        "username": u["username"],
        "name": u["name"] or "",
        "email": u["email"] or "",
        "role": u["role"],
        "ids": ids,
        # Full view CHỈ root hoặc ids chứa '*' (app tự tính lại, trả kèm cho đủ hợp đồng)
        "allowAll": u["role"] == "root" or "*" in ids,
        # Token API cấp SAU đăng nhập thành công — app (nhất là bản web, không nhúng
        # token vào JS được) dùng cho các call Bearer còn lại (/sessions, /devices...).
        # Đánh đổi (chấp nhận, ngang hàng desktop nhúng FBT_TOKEN): user hợp lệ nào
        # cũng cầm token đọc toàn bộ API — lọc theo máy vẫn ở client (canSee).
        # LUÔN là token CHÍNH, không bao giờ là một token cũ đang trong cửa sổ xoay
        # (config.TOKENS_OLD) — phát ra token sắp bị khai tử thì app sẽ 401 ngay khi
        # cửa sổ đóng. Đây là lý do token chính tách riêng thay vì một danh sách.
        "apiToken": api_token_for(u["role"]),
    }


def _change_password(body: dict) -> dict:
    username = _str(body.get("username")).strip()
    new = _str(body.get("newPassword"))
    if not username or new == "":
        return {"ok": False, "error": "Thiếu thông tin"}
    u = _check_login(username, _str(body.get("oldPassword")))
    if u is None:
        return {"ok": False, "error": LOGIN_FAIL_MSG}
    db.set_password(u["username"], hash_password(new))  # hash cũ sha256$ → nâng scrypt
    return {"ok": True, "changed": u["username"]}


def _change_email(body: dict) -> dict:
    username = _str(body.get("username")).strip()
    if not username:
        return {"ok": False, "error": "Thiếu thông tin"}
    u = _check_login(username, _str(body.get("password")))
    if u is None:
        return {"ok": False, "error": LOGIN_FAIL_MSG}
    email = _str(body.get("email")).strip()
    db.set_email(u["username"], email)
    return {"ok": True, "email": email}


def _require_admin(body: dict) -> dict | None:
    """Gate quản lý tài khoản: đúng mật khẩu + vai trò có quyền quản lý.

    `root` quản lý MỌI vai trò; `manager` (quản lý sản xuất) chỉ quản lý
    `operator` — xưởng tự thêm/khoá người theo ca mà không với tới được tài khoản
    kỹ thuật hay tài khoản khách hàng. Phạm vi cụ thể do [_may_touch] chốt; hàm
    này chỉ trả người gọi, sai → None (caller trả NO_PERMISSION).

    Mật khẩu vẫn phải gửi MỖI LẦN (app không lưu mật khẩu) — giữ nguyên cơ chế
    cũ, không phát minh phiên admin riêng.
    """
    u = _check_login(_str(body.get("adminUser")).strip(), _str(body.get("adminPassword")))
    return u if (u is not None and u["role"] in _MANAGE_SCOPE) else None


def _may_touch(caller: dict, target_role: str) -> bool:
    """Người gọi có được đụng tài khoản mang vai trò `target_role` không."""
    return target_role in _MANAGE_SCOPE.get(caller["role"], ())


def _list_users(body: dict) -> dict:
    caller = _require_admin(body)
    if caller is None:
        return {"ok": False, "error": NO_PERMISSION_MSG}
    # Quản lý sản xuất chỉ THẤY thao tác viên: danh sách tài khoản kỹ thuật và
    # khách hàng không phải việc của xưởng, và không thấy thì không dò được.
    users = [{"username": u["username"], "role": u["role"], "ids": list(u["ids"] or []),
              "name": u["name"] or "", "email": u["email"] or "", "active": u["active"]}
             for u in db.list_users() if _may_touch(caller, u["role"])]
    return {"ok": True, "users": users}


def _save_user(body: dict) -> dict:
    caller = _require_admin(body)
    if caller is None:
        return {"ok": False, "error": NO_PERMISSION_MSG}
    u = body.get("user")
    if not isinstance(u, dict):
        return {"ok": False, "error": "Thiếu thông tin user"}
    username = _str(u.get("username")).strip()
    if not username:
        return {"ok": False, "error": "Thiếu username"}
    role = _str(u.get("role")).strip().lower() or "user"
    if role not in _ROLES:
        return {"ok": False, "error": "role không hợp lệ (chỉ root/admin/user)"}

    ids = parse_ids(u.get("ids"))
    name = _str(u.get("name")).strip()
    active = parse_active(u.get("active"))
    # Bỏ trống password/email khi cập nhật → None = GIỮ giá trị cũ.
    pw = _str(u.get("password"))
    email = _str(u.get("email")).strip()

    existing = db.get_user(username)
    # Phạm vi của người gọi: kiểm CẢ vai trò mới LẪN vai trò cũ. Thiếu vế "cũ"
    # thì quản lý sản xuất chỉ cần gửi role=operator là sửa/hạ quyền được một tài
    # khoản admin.
    if not _may_touch(caller, role) or (existing and not _may_touch(caller, existing["role"])):
        return {"ok": False, "error": NO_PERMISSION_MSG}

    # CHẶN hạ quyền/khoá root hoạt động CUỐI CÙNG (chống tự khoá mình ra ngoài).
    if (existing and existing["role"] == "root" and existing["active"]
            and (role != "root" or not active)
            and db.count_roots(active_only=True) <= 1):
        return {"ok": False, "error": "Không thể hạ quyền/khoá root cuối cùng"}

    created = db.upsert_user(username, role, ids, name, active,
                             hash_password(pw) if pw else None,
                             email if email else None)
    return {"ok": True, "saved": username, "created": created}


def _delete_user(body: dict) -> dict:
    caller = _require_admin(body)
    if caller is None:
        return {"ok": False, "error": NO_PERMISSION_MSG}
    username = _str(body.get("username")).strip()
    if not username:
        return {"ok": False, "error": "Thiếu username"}
    target = db.get_user(username)
    if target is None:
        return {"ok": False, "error": f"Không tìm thấy user: {username}"}
    if not _may_touch(caller, target["role"]):
        return {"ok": False, "error": NO_PERMISSION_MSG}
    if target["role"] == "root" and db.count_roots() <= 1:
        return {"ok": False, "error": "Không thể xoá root cuối cùng"}
    db.delete_user(username)
    return {"ok": True, "deleted": username}
