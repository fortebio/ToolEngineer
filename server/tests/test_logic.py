"""Kiểm thử hàm thuần (không cần DB/mạng). Chạy: python tests/test_logic.py  hoặc  pytest."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app.logic import (canonical_sha256, check_auth, hash_password, parse_active,
                       parse_amplification, parse_ids, parse_ts, payload_time, safe_name,
                       validate, verify_password)


def test_safe_name():
    assert safe_name("RPL02013") == "RPL02013"
    for bad in ("../../etc/passwd", "a/b", "..", "/", ""):
        n = safe_name(bad)
        assert "/" not in n and ".." not in n and n, f"unsafe name from {bad!r}: {n!r}"


def test_check_auth():
    assert check_auth("Bearer s3cret", "s3cret") is True
    assert check_auth("Bearer wrong", "s3cret") is False
    assert check_auth("", "s3cret") is False             # có token mà thiếu header -> chặn
    assert check_auth("Bearer x", "") is False           # fail-closed: chưa đặt token -> từ chối
    assert check_auth("", "") is False                   # fail-closed
    assert check_auth("Bearer éè", "s3cret") is False   # header non-ASCII -> False, không TypeError


def test_validate():
    ok = {"id_device": "RPL02013", "CT_value": [1.0] * 10, "amplification": ["1,2,3"] * 10}
    assert validate(ok) is None
    assert validate({"id_device": "X"}) is None          # thiếu mảng vẫn hợp lệ (format khác)
    assert validate({}) == "missing id_device"
    assert validate([1, 2]) == "missing id_device"
    assert validate({"id_device": "X", "CT_value": [1.0] * 9}) is not None   # thiếu phần tử
    assert validate({"id_device": "X", "result": "abc"}) is not None         # sai kiểu


def test_canonical_sha256():
    # format khác nhau, nội dung như nhau -> cùng hash
    a = json.loads('{"id_device": "X", "b": 1}')
    b = json.loads('{\n  "b": 1,\n  "id_device": "X"\n}')
    assert canonical_sha256(a) == canonical_sha256(b)
    assert canonical_sha256(a) != canonical_sha256({"id_device": "Y", "b": 1})


def test_parse_amplification():
    slots = parse_amplification({"CT_value": [22.3], "result": ["22.3 | N"],
                                 "amplification": ["150,153,155,"]})  # đuôi thừa dấu phẩy
    assert slots[0]["slot"] == 1 and slots[0]["points"] == [150.0, 153.0, 155.0]
    assert slots[0]["ct_value"] == 22.3 and slots[0]["result"] == "22.3 | N"
    assert parse_amplification({}) == []
    # token không phải số bị bỏ qua, không raise
    s2 = parse_amplification({"amplification": ["1,abc,3,,4"]})
    assert s2[0]["points"] == [1.0, 3.0, 4.0]


def test_parse_ts():
    # ISO dùng ':' (tên gốc Drive / rclone) và '_' (tải trên Windows) -> cùng mốc UTC
    a = parse_ts("Log_RPL02013-2026-07-08T03:32:22.479Z.txt")
    b = parse_ts("Log_RPL02013-2026-07-08T03_32_22.479Z.txt")
    assert a is not None and a == b
    assert a.year == 2026 and a.hour == 3 and a.utcoffset().total_seconds() == 0  # UTC
    # Dạng khác DD-MM-YYYY, không có Z -> giờ VN (+7)
    c = parse_ts("Log_RA-05-05-2025 14:41:21.txt")
    assert c is not None and c.day == 5 and c.month == 5 and c.utcoffset().total_seconds() == 7 * 3600
    # Không khớp -> None
    assert parse_ts("linh-tinh.txt") is None
    # Khớp digit nhưng ngày/giờ ngoài khoảng -> None (không raise ValueError)
    assert parse_ts("Log_X-45-45-2025 25_61_61.txt") is None
    assert parse_ts("2026-13-01T00:00:00Z") is None


def test_payload_time():
    t = payload_time({"time": "06-08-2025 17:14:27"})   # DD-MM-YYYY, giờ VN
    assert t is not None and t.day == 6 and t.month == 8 and t.hour == 17
    assert t.utcoffset().total_seconds() == 7 * 3600
    assert payload_time({"time": "N/A"}) is None
    assert payload_time({}) is None
    assert payload_time("khong-phai-dict") is None


def test_password_hash():
    h = hash_password("s3cret")
    assert h.startswith("scrypt$") and "s3cret" not in h   # không lưu thô
    assert verify_password("s3cret", h) is True
    assert verify_password("wrong", h) is False
    assert verify_password("s3cret", "chuoi-hong") is False   # băm hỏng -> False, không raise
    assert hash_password("s3cret") != hash_password("s3cret")  # muối ngẫu nhiên -> khác nhau


def test_password_hash_sha256_legacy():
    # Hash di cư từ Google Sheet userAuth.js: sha256(salt + password), salt nối CHUỖI.
    # Mẫu tự dựng: sha256("abc" + "123456")
    import hashlib
    legacy = "sha256$abc$" + hashlib.sha256(b"abc123456").hexdigest()
    assert verify_password("123456", legacy) is True
    assert verify_password("sai", legacy) is False
    assert verify_password("123456", "md5$abc$deadbeef") is False  # thuật toán lạ -> False


def test_parse_ids():
    assert parse_ids("RPL02007, RPL02100; RPL02007  *") == ["RPL02007", "RPL02100", "*"]
    assert parse_ids(["a", "a", " b "]) == ["a", "b"]
    assert parse_ids(None) == [] and parse_ids("") == []


def test_parse_active():
    assert parse_active(True) and not parse_active(False)
    assert parse_active("") and parse_active(None) and parse_active("TRUE")
    for v in ("false", "0", "no", "N"):
        assert not parse_active(v)


if __name__ == "__main__":
    ran = 0
    for _name, _fn in sorted(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            _fn()
            ran += 1
            print(f"ok {_name}")
    print(f"{ran} tests passed")
