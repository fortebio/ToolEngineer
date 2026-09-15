"""Phạm vi quản lý tài khoản của `manager` (quản lý sản xuất) — KHÔNG cần Postgres.

Thay `app.db` bằng một kho tài khoản trong RAM: phần cần kiểm ở đây là LUẬT
(ai được đụng ai), không phải SQL. Chạy: pytest tests/test_auth_roles.py

Vì sao đáng viết: đây là chỗ duy nhất chặn "quản lý xưởng tự nâng mình lên
admin rồi arm firmware cho cả fleet". Một dòng sai là mất luôn ranh giới đó.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from app import auth  # noqa: E402
from app.logic import hash_password  # noqa: E402

PW = "matkhau123"


def _user(username, role, active=True):
    return {"username": username, "password": hash_password(PW), "role": role,
            "ids": [], "name": username, "active": active, "email": "",
            "fw_version": None, "date_create": None}


class FakeDb:
    """Đủ dùng cho auth.dispatch: get_user / list_users / upsert_user / delete_user."""

    def __init__(self):
        self.users = {u["username"]: u for u in [
            _user("root1", "root"),
            _user("ky_thuat", "admin"),
            _user("quanly", "manager"),
            _user("tho1", "operator"),
            _user("phongkham", "user"),
        ]}
        self.deleted = []

    def get_user(self, username):
        for k, v in self.users.items():
            if k.lower() == str(username).lower():
                return v
        return None

    def list_users(self):
        return list(self.users.values())

    def upsert_user(self, username, role, ids, name, active, password_hash, email):
        created = self.get_user(username) is None
        self.users[username] = _user(username, role, active)
        return created

    def delete_user(self, username):
        self.deleted.append(username)
        self.users.pop(username, None)
        return True

    def count_roots(self, active_only=False):
        return sum(1 for u in self.users.values()
                   if u["role"] == "root" and (u["active"] or not active_only))


def setup_function(_fn):
    auth.db = FakeDb()          # thay module db bằng kho RAM


def call(action, who, **kw):
    return auth.dispatch({"action": action, "adminUser": who, "adminPassword": PW, **kw})


def save(who, username, role, **kw):
    return call("saveUser", who, user={"username": username, "role": role, **kw})


# ---------------------------------------------------------------- vai trò hợp lệ

def test_role_moi_duoc_chap_nhan():
    assert auth._ROLES == ("root", "admin", "manager", "operator", "user")
    assert save("root1", "tho9", "operator")["ok"] is True
    assert save("root1", "ql9", "manager")["ok"] is True
    assert save("root1", "x", "superuser")["ok"] is False  # vai trò lạ vẫn bị chặn


def test_token_theo_vai_tro():
    """Vai trò XƯỞNG không bao giờ nhận token ghi OTA."""
    auth.config.OTA_ADMIN_TOKEN = "admintok"
    auth.config.TOKEN = "devicetok"
    try:
        assert auth.api_token_for("root") == "admintok"
        assert auth.api_token_for("admin") == "admintok"
        assert auth.api_token_for("manager") == "devicetok"
        assert auth.api_token_for("operator") == "devicetok"
        assert auth.api_token_for("user") == "devicetok"
    finally:
        auth.config.OTA_ADMIN_TOKEN = ""


# ------------------------------------------------------- phạm vi của quản lý SX

def test_quan_ly_chi_thay_thao_tac_vien():
    r = call("listUsers", "quanly")
    assert r["ok"] is True
    assert [u["username"] for u in r["users"]] == ["tho1"]
    # root thấy tất cả
    assert len(call("listUsers", "root1")["users"]) == 5


def test_quan_ly_tao_va_khoa_thao_tac_vien():
    assert save("quanly", "tho2", "operator", password=PW)["ok"] is True
    assert save("quanly", "tho1", "operator", active=False)["ok"] is True


def test_quan_ly_KHONG_tao_duoc_vai_tro_cao_hon():
    for role in ("admin", "root", "manager", "user"):
        r = save("quanly", "nguoi_moi", role)
        assert r["ok"] is False and r["error"] == auth.NO_PERMISSION_MSG, role


def test_quan_ly_KHONG_ha_quyen_duoc_tai_khoan_ky_thuat():
    """Gửi role=operator cho một tài khoản admin — chặn theo vai trò CŨ."""
    r = save("quanly", "ky_thuat", "operator")
    assert r["ok"] is False and r["error"] == auth.NO_PERMISSION_MSG
    assert auth.db.get_user("ky_thuat")["role"] == "admin"  # không đổi


def test_quan_ly_chi_xoa_duoc_thao_tac_vien():
    assert call("deleteUser", "quanly", username="tho1")["ok"] is True
    r = call("deleteUser", "quanly", username="ky_thuat")
    assert r["ok"] is False and r["error"] == auth.NO_PERMISSION_MSG
    assert "ky_thuat" not in auth.db.deleted


def test_vai_tro_khac_khong_quan_ly_duoc_ai():
    for who in ("ky_thuat", "tho1", "phongkham"):
        assert call("listUsers", who)["ok"] is False, who
        assert save(who, "ai_do", "operator")["ok"] is False, who
        assert call("deleteUser", who, username="tho1")["ok"] is False, who


def test_sai_mat_khau_van_bi_chan():
    r = auth.dispatch({"action": "listUsers", "adminUser": "quanly",
                       "adminPassword": "sai"})
    assert r["ok"] is False


def test_khong_ha_quyen_root_cuoi_cung():
    r = save("root1", "root1", "admin")
    assert r["ok"] is False and "root" in r["error"]


if __name__ == "__main__":
    ran = 0
    for _name, _fn in sorted(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            setup_function(_fn)
            _fn()
            ran += 1
            print(f"ok {_name}")
    print(f"{ran} tests passed")
