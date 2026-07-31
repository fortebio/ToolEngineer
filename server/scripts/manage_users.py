#!/usr/bin/env python3
"""Quản lý tài khoản đăng nhập app (mật khẩu tự băm khi tạo).

  Tạo:      ~/fbt_server/venv/bin/python scripts/manage_users.py add <username> <password> \
                [role] [ids_csv] [name] [email] [fw]
  Liệt kê:  ~/fbt_server/venv/bin/python scripts/manage_users.py list
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app import db


def main(argv) -> int:
    if not argv or argv[0] == "list":
        for u in db.list_users():
            print(u)
        return 0
    if argv[0] == "add" and len(argv) >= 3:
        username, password = argv[1], argv[2]
        role = argv[3] if len(argv) > 3 else "user"
        ids = [x for x in argv[4].split(",") if x] if len(argv) > 4 else []
        name = argv[5] if len(argv) > 5 else None
        email = argv[6] if len(argv) > 6 else None
        fw = argv[7] if len(argv) > 7 else None
        uid = db.create_user(username, password, role=role, ids=ids,
                             name=name, email=email, fw_version=fw)
        print(f"created id={uid} username={username} role={role} ids={ids}")
        return 0
    print("dùng:  add <username> <password> [role] [ids_csv] [name] [email] [fw]  |  list")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
