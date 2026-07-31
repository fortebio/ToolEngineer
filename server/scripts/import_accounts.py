#!/usr/bin/env python3
"""Di cư tài khoản từ Google Sheet (userAuth.js) vào bảng users — chạy 1 lần.

Cách lấy dữ liệu: mở Google Sheet accounts → tab "Accounts" → File → Download
→ CSV, chép lên server rồi chạy:

    ~/fbt_server/venv/bin/python scripts/import_accounts.py accounts.csv

Idempotent: chạy lại upsert theo username (giữ mật khẩu server nếu ô CSV trống).
Mật khẩu: hash "sha256$<salt>$<hash>" của Sheet được giữ NGUYÊN (verify_password
đã hỗ trợ); ô còn plaintext (chưa chạy migratePasswordsToHash) → băm scrypt luôn.
Cột dò theo TÊN header (không phân hoa/thường): username,password,role,ids,name,
email(tuỳ chọn),active — khớp schema tab Accounts.
"""
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # để import package app
from app.db import upsert_user
from app.logic import hash_password, parse_active, parse_ids

_HASH_RE = re.compile(r"^sha256\$[0-9a-f]+\$[0-9a-f]{64}$")
_ROLES = ("root", "admin", "user")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = Path(sys.argv[1])
    created = updated = skipped = 0
    with path.open(encoding="utf-8-sig", newline="") as f:  # utf-8-sig: CSV Google có BOM
        for row in csv.DictReader(f):
            r = {str(k or "").strip().lower(): str(v or "").strip() for k, v in row.items()}
            username = r.get("username", "")
            if not username:
                skipped += 1
                continue
            role = (r.get("role", "") or "user").lower()
            if role not in _ROLES:
                role = "user"
            pw = r.get("password", "")
            # hash sheet giữ nguyên; plaintext còn sót → băm scrypt; trống → giữ mật khẩu cũ trên server
            pw_hash = pw if _HASH_RE.match(pw) else (hash_password(pw) if pw else None)
            try:
                is_new = upsert_user(username, role, parse_ids(r.get("ids")),
                                     r.get("name", ""), parse_active(r.get("active")),
                                     pw_hash, r.get("email") or None)
            except Exception as e:  # 1 hàng hỏng không chặn các hàng sau
                print(f"SKIP {username}: db error: {e}")
                skipped += 1
                continue
            if is_new:
                created += 1
            else:
                updated += 1
            print(f"{'CREATE' if is_new else 'UPDATE'} {username} ({role})")
    print(f"created {created}, updated {updated}, skipped {skipped}")
    return 0 if skipped == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
