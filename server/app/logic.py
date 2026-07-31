"""Hàm thuần + tiện ích băm — không phụ thuộc fastapi/psycopg. Test được độc lập."""
import hashlib
import hmac
import json
import os
import re
from datetime import datetime, timedelta, timezone

from app.config import ARRAY_FIELDS

_VN = timezone(timedelta(hours=7))
# Thời điểm đo nằm trong TÊN FILE. Khớp cả 2 kiểu phân tách giờ: ':' (gốc Drive) và '_' (tải trên Windows).
_ISO_RE = re.compile(r"(\d{4})-(\d{2})-(\d{2})T(\d{2})[_:](\d{2})[_:](\d{2})(?:\.\d+)?Z")
_ALT_RE = re.compile(r"(\d{2})-(\d{2})-(\d{4})[ _](\d{2})[_:](\d{2})[_:](\d{2})")


def parse_ts(name: str):
    """Đọc thời điểm đo từ chuỗi (tên file hoặc trường 'time'); None nếu không khớp/không hợp lệ."""
    try:
        m = _ISO_RE.search(name)
        if m:
            y, mo, d, h, mi, s = map(int, m.groups())
            return datetime(y, mo, d, h, mi, s, tzinfo=timezone.utc)  # có Z -> UTC
        m = _ALT_RE.search(name)
        if m:
            d, mo, y, h, mi, s = map(int, m.groups())  # DD-MM-YYYY, không Z -> giờ VN
            return datetime(y, mo, d, h, mi, s, tzinfo=_VN)
    except ValueError:  # regex khớp nhưng ngày/giờ ngoài khoảng (tháng 45, giờ 25...) -> coi như không có
        return None
    return None


def payload_time(data) -> datetime | None:
    """Thời điểm đo từ trường 'time' trong payload (vd '06-08-2025 17:14:27', giờ VN).
    None nếu không có / 'N/A' / không parse được — caller lùi về tên file rồi now()."""
    if not isinstance(data, dict):
        return None
    return parse_ts(str(data.get("time") or ""))


def safe_name(s: str) -> str:
    """Chỉ giữ ký tự an toàn cho tên file — chống path traversal (../, /...)."""
    return re.sub(r"[^A-Za-z0-9_.-]", "_", s).strip("._")[:64] or "unknown"


def check_auth(auth_header: str, token: str) -> bool:
    """Chỉ cho qua nếu header khớp 'Bearer <token>'.

    fail-CLOSED: chưa đặt token -> TỪ CHỐI hết. Server chạy public qua Funnel nên không được
    mở toang khi env token trống/lỗi cấu hình (thà 401 toàn bộ để phát hiện, còn hơn lộ dữ liệu).
    """
    if not token:
        return False
    # so sánh dạng bytes: header có ký tự non-ASCII cũng không làm compare_digest ném TypeError
    return hmac.compare_digest(auth_header.encode("utf-8", "ignore"), f"Bearer {token}".encode("utf-8"))


def validate(data) -> str | None:
    """Trả thông báo lỗi, None nếu hợp lệ. Chỉ chặn rác hiển nhiên, không ép đủ schema RPL."""
    if not isinstance(data, dict) or not str(data.get("id_device", "")).strip():
        return "missing id_device"
    for f in ARRAY_FIELDS:
        if f in data and not (isinstance(data[f], list) and len(data[f]) == 10):
            return f"{f} must be a list of 10 items"
    return None


def hash_password(password: str, salt: bytes | None = None) -> str:
    """Băm mật khẩu (scrypt, có muối). Lưu dạng 'scrypt$<salt_hex>$<hash_hex>' — không lưu thô."""
    salt = salt or os.urandom(16)
    dk = hashlib.scrypt(password.encode("utf-8"), salt=salt, n=16384, r=8, p=1, dklen=32)
    return f"scrypt${salt.hex()}${dk.hex()}"


def verify_password(password: str, stored: str) -> bool:
    """So khớp mật khẩu với chuỗi băm đã lưu (timing-safe).

    Hỗ trợ 2 định dạng: 'scrypt$<salt>$<hash>' (server tạo) và
    'sha256$<salt>$<hash>' (di cư từ Google Sheet userAuth.js —
    hash = sha256(salt + password), giữ nguyên để user cũ khỏi đổi mật khẩu;
    đổi mật khẩu trên server sẽ tự nâng lên scrypt).
    """
    try:
        algo, salt_hex, hash_hex = stored.split("$")
        if algo == "scrypt":
            dk = hashlib.scrypt(password.encode("utf-8"), salt=bytes.fromhex(salt_hex),
                                n=16384, r=8, p=1, dklen=32)
            computed = dk.hex()
        elif algo == "sha256":  # legacy Sheet: salt nối chuỗi, không phải bytes
            computed = hashlib.sha256((salt_hex + password).encode("utf-8")).hexdigest()
        else:
            return False
    except (ValueError, AttributeError):
        return False
    return hmac.compare_digest(computed, hash_hex)


def parse_ids(raw) -> list[str]:
    """Chuẩn hoá danh sách mã máy (khớp parseIds_ của userAuth.js): nhận list
    hoặc chuỗi tách bởi phẩy/chấm phẩy/khoảng trắng; trim, bỏ rỗng, dedupe, giữ '*'."""
    parts = raw if isinstance(raw, list) else re.split(r"[\s,;]+", str(raw or ""))
    out, seen = [], set()
    for p in parts:
        token = str(p).strip()
        if token and token not in seen:
            seen.add(token)
            out.append(token)
    return out


def parse_active(v) -> bool:
    """TRUE/FALSE robust (khớp parseActive_ userAuth.js): trống/lạ = active."""
    if isinstance(v, bool):
        return v
    s = str(v or "").strip().lower()
    return s not in ("false", "0", "no", "n")


def canonical_sha256(data: dict) -> bytes:
    """Hash nội dung đã chuẩn hóa (sort_keys, compact) — độc lập cách format file/POST."""
    canon = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canon.encode("utf-8")).digest()


def parse_amplification(payload: dict) -> list[dict]:
    """10 chuỗi CSV '150,153,...' → mảng số sẵn cho fl_chart vẽ."""
    ct = payload.get("CT_value") or []
    res = payload.get("result") or []
    slots = []
    for i, csv in enumerate(payload.get("amplification") or []):
        pts = []
        for x in str(csv).split(","):
            x = x.strip()
            if not x:
                continue
            try:
                pts.append(float(x))
            except ValueError:  # token không phải số -> bỏ qua, không làm hỏng cả endpoint
                pass
        slots.append({
            "slot": i + 1,
            "ct_value": ct[i] if i < len(ct) else None,
            "result": res[i] if i < len(res) else None,
            "points": pts,
        })
    return slots
