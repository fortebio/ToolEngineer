"""Truy cập PostgreSQL: kết nối (peer auth), ghi phiên đo, các truy vấn đọc cho API.

psycopg import trễ trong _conn() để test logic thuần chạy được khi chưa cài psycopg.
"""
from datetime import datetime

from app.config import DB
from app.logic import canonical_sha256, parse_amplification

# Nguồn dữ liệu duy nhất là POST từ thiết bị: mỗi POST = 1 hàng mới (posted_at=now(), không dedup).
_INSERT = """
INSERT INTO sessions (id_device, version, kit_id, type_upload, method, body_sha256, payload, received_at, posted_at)
VALUES (%s, %s, %s, %s, %s, %s, %s, COALESCE(%s, now()), now())
RETURNING id
"""
# reconcile nạp bù (dedup=True): bỏ qua nếu nội dung đã có -> chạy lại an toàn.
_EXISTS = "SELECT 1 FROM sessions WHERE body_sha256 = %s LIMIT 1"

# Kho backup Drive (drive_sessions) — dedup theo nội dung, độc lập với sessions.
_INSERT_DRIVE = """
INSERT INTO drive_sessions (id_device, version, kit_id, type_upload, method, body_sha256, payload, received_at)
VALUES (%s, %s, %s, %s, %s, %s, %s, COALESCE(%s, now()))
ON CONFLICT (body_sha256) DO NOTHING
RETURNING id
"""

# Lọc + phân trang cho /sessions — dùng chung cho cả query đếm và query lấy dòng
_WHERE = """WHERE (%(device)s::text IS NULL OR id_device = %(device)s)
             AND (%(from)s::timestamptz IS NULL OR received_at >= %(from)s::timestamptz)
             AND (%(to)s::date IS NULL OR received_at < %(to)s::date + 1)"""


def _conn():
    import psycopg

    return psycopg.connect(DB)


def insert_session(data: dict, received_at: datetime | None = None, dedup: bool = False):
    """Ghi 1 phiên đo (posted_at=now()).

    dedup=False (POST thiết bị): LUÔN thêm 1 hàng mới, trả id.
    dedup=True  (reconcile nạp bù): bỏ qua nếu nội dung đã có -> None (chạy lại an toàn).
    """
    from psycopg.types.json import Jsonb

    sha = canonical_sha256(data)
    vals = (
        str(data.get("id_device")),
        data.get("version"),
        str(data.get("kitId") or "") or None,
        data.get("type_Upload"),
        data.get("method"),
        sha,
        Jsonb(data),
        received_at,
    )
    with _conn() as conn, conn.cursor() as cur:
        if dedup:
            cur.execute(_EXISTS, (sha,))
            if cur.fetchone():
                return None
        cur.execute(_INSERT, vals)
        return cur.fetchone()[0]


def insert_drive_session(data: dict, received_at: datetime | None = None):
    """Ghi 1 bản đo vào kho BACKUP Drive (drive_sessions) — dedup theo nội dung (idempotent).
    Trả id, hoặc None nếu nội dung đã có. Không đụng bảng 'sessions' (device POST)."""
    from psycopg.types.json import Jsonb

    with _conn() as conn, conn.cursor() as cur:
        cur.execute(_INSERT_DRIVE, (
            str(data.get("id_device")),
            data.get("version"),
            str(data.get("kitId") or "") or None,
            data.get("type_Upload"),
            data.get("method"),
            canonical_sha256(data),
            Jsonb(data),
            received_at,
        ))
        row = cur.fetchone()
        return row[0] if row else None


def list_devices() -> list[dict]:
    """Danh sách thiết bị + số phiên + lần gửi cuối + version firmware.

    `version` = version của phiên GẦN NHẤT CÓ ghi version (FILTER bỏ NULL —
    payload thiếu field 'version' thì không xoá mất version đã biết trước đó).

    ⚠️ Đây chỉ là bản máy chạy LÚC ĐO GẦN NHẤT — nạp firmware xong mà chưa ai chạy mẫu
    thì cột này vẫn là bản CŨ. Từ v2.4.5 máy tự khai version ở `/ota/check?ver=` và
    `main.devices()` ĐÈ giá trị đó lên; cột này là đường lui cho firmware cũ.
    """
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("""SELECT id_device, count(*), max(received_at),
                              (array_agg(version ORDER BY received_at DESC)
                               FILTER (WHERE version IS NOT NULL))[1]
                       FROM sessions GROUP BY 1 ORDER BY 3 DESC""")
        return [{"id_device": d, "sessions": n, "last_seen": t, "version": v}
                for d, n, t, v in cur.fetchall()]


def list_sessions(device, from_, to, page, limit) -> dict:
    """Danh sách phiên đo (mới nhất trước), lọc theo thiết bị/khoảng ngày, phân trang."""
    params = {"device": device, "from": from_, "to": to,
              "limit": limit, "off": (page - 1) * limit}
    with _conn() as conn, conn.cursor() as cur:
        cur.execute(f"SELECT count(*) FROM sessions {_WHERE}", params)
        total = cur.fetchone()[0]
        cur.execute(f"""SELECT id, id_device, received_at, posted_at, version, type_upload,
                               payload->'CT_value', payload->'result'
                        FROM sessions {_WHERE}
                        ORDER BY received_at DESC LIMIT %(limit)s OFFSET %(off)s""", params)
        items = [{"id": i, "id_device": d, "received_at": t, "posted_at": p, "version": v,
                  "type_upload": u, "ct_value": ct, "result": r}
                 for i, d, t, p, v, u, ct, r in cur.fetchall()]
    return {"total": total, "page": page, "limit": limit, "items": items}


def get_session(sid: int) -> dict | None:
    """Chi tiết 1 phiên — bỏ amplification (nặng, lấy riêng). None nếu không có."""
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("""SELECT id, id_device, received_at, posted_at, version, type_upload, method,
                              payload - 'amplification'
                       FROM sessions WHERE id = %s""", (sid,))
        row = cur.fetchone()
    if not row:
        return None
    i, d, t, p, v, u, m, payload = row
    return {"id": i, "id_device": d, "received_at": t, "posted_at": p, "version": v,
            "type_upload": u, "method": m, "payload": payload}


def session_errors(sid: int) -> list[dict] | None:
    """Lỗi cảm biến máy báo về TRONG lần đo này. None nếu không có phiên `sid`.

    **Vì sao phải ghép chứ không đọc thẳng payload**: firmware gửi lỗi bằng một POST RIÊNG
    (`{method:"error", error:[{Slot, error_code, error_msg}]}` — `errorCheck.cpp`), không
    nhét vào payload kết quả. Nó vào cùng đường `/ingest` nên nằm trong `sessions` như một
    hàng riêng, `CT_value` NULL.

    **Luật ghép — HAI chặn, phải có cả hai**: bản ghi lỗi của CÙNG máy, nằm
      * sau lần đo liền TRƯỚC (để không nuốt lỗi của run trước), **VÀ**
      * trong vòng 2 GIỜ trước lần đo này (`postError_Googlesheet()` bắn ngay lúc lỗi phát
        sinh, có thể sớm hơn kết quả gần một giờ: lysis 10 phút + khuếch đại tối đa ~43
        phút + sấy),
    và không muộn hơn lần đo này quá 15 phút (`postError_fullGoogleSheet()` xả cả sổ ở màn
    kết thúc — cùng chỗ kết quả được gửi, mà TLS mất 30-90 s mỗi đích nên có thể tới SAU).

    ⚠️ **Chặn 2 giờ KHÔNG thừa** — bỏ nó là lỗi thật đã dẫm phải 2026-08-20: máy RPL02007 báo
    3 lỗi ngày **04/06**, lần đo kế tiếp của nó mãi **03/08** mới có, và luật "sau lần đo liền
    trước" một mình gán tuốt 3 lỗi hai tháng tuổi vào lần đo tháng 8. Máy nằm im hàng tháng là
    chuyện thường ngoài hiện trường, nên vế "liền trước" không tự chặn được gì.

    ⚠️ Vẫn là SUY LUẬN theo thời gian, không phải khoá ngoại — payload lỗi không mang mã
    lần đo nào cả. App phải nói rõ điều đó.
    """
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("SELECT id_device, received_at FROM sessions WHERE id = %s", (sid,))
        row = cur.fetchone()
        if not row:
            return None
        dev, at = row
        cur.execute("""
            SELECT id, received_at, payload->'error'
            FROM sessions
            WHERE id_device = %s
              AND payload->>'method' = 'error'
              AND received_at <= %s + interval '15 minutes'
              -- GREATEST của hai chặn: lấy cái CHẶT HƠN. Thiếu vế 2 giờ thì máy nằm im
              -- hàng tháng sẽ kéo theo lỗi cũ mèm; thiếu vế "liền trước" thì hai run sát
              -- nhau lẫn lỗi vào nhau.
              AND received_at > GREATEST(
                    %s - interval '2 hours',
                    COALESCE((
                      SELECT max(received_at) FROM sessions
                      WHERE id_device = %s AND received_at < %s
                        AND payload->>'method' IS DISTINCT FROM 'error'
                    ), to_timestamp(0))
                  )
            ORDER BY received_at
        """, (dev, at, at, dev, at))
        out = []
        for eid, eat, arr in cur.fetchall():
            for e in (arr if isinstance(arr, list) else []):
                if not isinstance(e, dict):
                    continue
                out.append({
                    "at": eat,
                    "session_id": eid,
                    "slot": str(e.get("Slot") or ""),
                    "code": str(e.get("error_code") or ""),
                    "message": str(e.get("error_msg") or ""),
                })
        return out


def get_amplification(sid: int) -> list[dict] | None:
    """Đường cong khuếch đại đã parse thành mảng số. None nếu không có phiên."""
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("SELECT payload FROM sessions WHERE id = %s", (sid,))
        row = cur.fetchone()
    return parse_amplification(row[0]) if row else None


# ---------- Tài khoản đăng nhập app (bảng users) ----------

def get_user(username: str) -> dict | None:
    """Lấy 1 tài khoản theo username (KHÔNG phân biệt hoa/thường, khớp hành vi
    userAuth.js cũ) — kèm cột password (hash). None nếu không có."""
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("""SELECT username, password, role, ids, name, active, email, fw_version, date_create
                       FROM users WHERE lower(username) = lower(%s)""", (username,))
        row = cur.fetchone()
    if not row:
        return None
    un, pw, role, ids, name, active, email, fw, dc = row
    return {"username": un, "password": pw, "role": role, "ids": ids, "name": name,
            "active": active, "email": email, "fw_version": fw, "date_create": dc}


def create_user(username, password, role="user", ids=None,
                name=None, email=None, fw_version=None) -> int:
    """Tạo tài khoản — mật khẩu được BĂM trước khi lưu. Trả id."""
    from app.logic import hash_password

    with _conn() as conn, conn.cursor() as cur:
        cur.execute("""INSERT INTO users (username, password, role, ids, name, email, fw_version)
                       VALUES (%s, %s, %s, %s, %s, %s, %s) RETURNING id""",
                    (username, hash_password(password), role, ids or [], name, email, fw_version))
        return cur.fetchone()[0]


def list_users() -> list[dict]:
    """Liệt kê tài khoản (KHÔNG kèm mật khẩu)."""
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("""SELECT username, role, ids, name, active, email, fw_version, date_create
                       FROM users ORDER BY username""")
        return [{"username": u, "role": r, "ids": i, "name": n, "active": a,
                 "email": e, "fw_version": f, "date_create": dc}
                for u, r, i, n, a, e, f, dc in cur.fetchall()]


def upsert_user(username, role, ids, name, active,
                password_hash: str | None, email: str | None) -> bool:
    """Tạo mới / cập nhật tài khoản (khoá theo username không phân hoa/thường).

    password_hash/email = None → GIỮ giá trị cũ (hợp đồng saveUser của app:
    bỏ trống = không đổi). password_hash là chuỗi ĐÃ băm. Trả True nếu TẠO MỚI.
    """
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("SELECT id FROM users WHERE lower(username) = lower(%s)", (username,))
        row = cur.fetchone()
        if row:
            cur.execute("""UPDATE users SET username=%s, role=%s, ids=%s, name=%s, active=%s,
                                  password = COALESCE(%s, password),
                                  email    = COALESCE(%s, email)
                           WHERE id=%s""",
                        (username, role, ids, name, active, password_hash, email, row[0]))
            return False
        cur.execute("""INSERT INTO users (username, password, role, ids, name, active, email)
                       VALUES (%s, %s, %s, %s, %s, %s, %s)""",
                    (username, password_hash or "", role, ids, name, active, email))
        return True


def delete_user(username: str) -> bool:
    """Xóa tài khoản. Trả True nếu có hàng bị xóa."""
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("DELETE FROM users WHERE lower(username) = lower(%s)", (username,))
        return cur.rowcount > 0


def set_password(username: str, password_hash: str):
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("UPDATE users SET password = %s WHERE lower(username) = lower(%s)",
                    (password_hash, username))


def set_email(username: str, email: str):
    with _conn() as conn, conn.cursor() as cur:
        cur.execute("UPDATE users SET email = %s WHERE lower(username) = lower(%s)",
                    (email, username))


def count_roots(active_only: bool = False) -> int:
    """Số tài khoản root (guard 'không xoá/hạ quyền root cuối cùng')."""
    q = "SELECT count(*) FROM users WHERE role = 'root'"
    if active_only:
        q += " AND active"
    with _conn() as conn, conn.cursor() as cur:
        cur.execute(q)
        return cur.fetchone()[0]
