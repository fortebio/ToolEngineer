"""Xuất dữ liệu Postgres (MiniPC) → các file .sql nạp được vào D1.

CHẠY TRÊN BOX (nơi có Postgres), trong venv của server cũ:
    ~/fbt_server/venv/bin/python export_pg_to_d1.py --out /tmp/d1
rồi copy thư mục /tmp/d1 về máy dev và nạp:
    for f in d1/*.sql; do npx wrangler d1 execute fbt --remote --file "$f"; done

Chuyển kiểu (khớp server-cf/schema.sql):
    timestamptz -> chuỗi ISO UTC 'YYYY-MM-DDTHH:MM:SS.sssZ'
    jsonb       -> TEXT JSON
    bytea       -> TEXT hex
    text[] ids  -> TEXT mảng JSON

⚠️ Mật khẩu 'scrypt$...' KHÔNG chạy được trên Workers (không có scrypt). Script vẫn
xuất nguyên, nhưng in ra danh sách tài khoản cần ĐẶT LẠI mật khẩu sau khi chuyển.
Tài khoản 'sha256$...' (di cư từ Google Sheet) thì Worker đọc được, không phải làm gì.
"""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

MAX_BYTES = 4_000_000  # mỗi file .sql giữ dưới ~4MB cho wrangler nuốt trôi


def q(v) -> str:
    """Literal SQL: NULL, số, hoặc chuỗi đã escape nháy đơn."""
    if v is None:
        return "NULL"
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, (int, float)):
        return str(v)
    return "'" + str(v).replace("'", "''") + "'"


def iso(dt) -> str | None:
    """timestamptz -> ISO UTC có 'Z' (so sánh chuỗi = so sánh thời gian trong SQLite)."""
    if dt is None:
        return None
    if isinstance(dt, str):
        return dt
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond // 1000:03d}Z"


class Chunker:
    """Gom câu INSERT thành nhiều file nhỏ."""

    def __init__(self, out: Path, prefix: str):
        self.out, self.prefix, self.n, self.buf, self.size = out, prefix, 0, [], 0

    def add(self, stmt: str):
        self.buf.append(stmt)
        self.size += len(stmt)
        if self.size >= MAX_BYTES:
            self.flush()

    def flush(self):
        if not self.buf:
            return
        self.n += 1
        path = self.out / f"{self.prefix}_{self.n:03d}.sql"
        path.write_text("\n".join(self.buf) + "\n", encoding="utf-8")
        print(f"  {path} ({self.size / 1e6:.1f} MB, {len(self.buf)} câu)")
        self.buf, self.size = [], 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dsn", default="dbname=mydb connect_timeout=5")
    ap.add_argument("--out", default="d1_export")
    args = ap.parse_args()

    import psycopg

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    with psycopg.connect(args.dsn) as conn, conn.cursor() as cur:
        # --- users ---
        print("users:")
        users = Chunker(out, "01_users")
        need_reset = []
        cur.execute("""SELECT username, password, role, ids, name, active, email,
                              date_create, fw_version FROM users ORDER BY username""")
        for un, pw, role, ids, name, active, email, dc, fw in cur.fetchall():
            if str(pw or "").startswith("scrypt$"):
                need_reset.append(un)
            users.add(
                "INSERT INTO users (username, password, role, ids, name, active, email, date_create, fw_version) "
                f"VALUES ({q(un)}, {q(pw)}, {q(role)}, {q(json.dumps(list(ids or []), ensure_ascii=False))}, "
                f"{q(name)}, {q(bool(active))}, {q(email)}, {q(iso(dc))}, {q(fw)});"
            )
        users.flush()

        # --- sessions ---
        print("sessions:")
        sess = Chunker(out, "02_sessions")
        cur.execute("""SELECT id, id_device, version, kit_id, type_upload, method,
                              received_at, posted_at, body_sha256, payload
                       FROM sessions ORDER BY id""")
        total = 0
        for sid, dev, ver, kit, tu, method, rec, post, sha, payload in cur:
            total += 1
            sess.add(
                "INSERT INTO sessions (id, id_device, version, kit_id, type_upload, method, "
                "received_at, posted_at, body_sha256, payload) "
                f"VALUES ({sid}, {q(dev)}, {q(ver)}, {q(kit)}, {q(tu)}, {q(method)}, "
                f"{q(iso(rec))}, {q(iso(post))}, {q(bytes(sha).hex())}, "
                f"{q(json.dumps(payload, ensure_ascii=False, separators=(',', ':')))});"
            )
        sess.flush()

    print(f"\nXong: {total} phiên, {users.n + sess.n} file .sql trong {out}/")
    print(f"Sinh lúc {datetime.now(timezone.utc).isoformat()}")
    if need_reset:
        print(f"\n⚠️ {len(need_reset)} tài khoản băm scrypt — KHÔNG đăng nhập được trên Workers,")
        print("   phải đặt lại mật khẩu sau khi chuyển:")
        for u in need_reset:
            print(f"   - {u}")


if __name__ == "__main__":
    main()
