"""Kiểm thử hàm thuần (không cần DB/mạng). Chạy: python tests/test_logic.py  hoặc  pytest."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app.logic import (DEFAULT_ATE_LIMITS, ate_first_fail, ate_limits_conflict,
                       ate_match, ate_meta, ate_stats,
                       canonical_sha256, check_auth, expected_bin_name,
                       hash_password, normalize_ate_record, parse_active,
                       parse_amplification, parse_ids, parse_image_tags, parse_ts,
                       payload_time, product_key, safe_name, validate,
                       validate_ate_limits, validate_ate_record, ver_from_name,
                       verify_password)


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
    # Sản phẩm N khe: mảng slot_* phải đúng `slots` phần tử (rapid4p 5 khe, 2026-09-17)
    r4p = {"id_device": "R4P01", "slots": 5, "slot_value": [1] * 5, "slot_result": [2] * 5, "slot_positive": [False] * 5}
    assert validate(r4p) is None
    assert validate({**r4p, "slot_result": [2] * 4}) is not None             # thiếu 1 khe
    assert validate({**r4p, "slots": 0}) is not None
    assert validate({**r4p, "slots": True}) is not None                      # bool không phải int
    assert validate({"id_device": "R4P01", "slots": 4, "slot_value": [1] * 4}) is None   # 4 khe vẫn hợp lệ
    # Reader 1 khe (system/contracts/ingest-reader.schema.json, 2026-09-18): cùng quy ước, slots ghim 1;
    # `readings` (3 số đọc thô) KHÔNG thuộc SLOT_ARRAY_FIELDS nên không bị đòi = slots.
    rdr = {"id_device": "RE0012", "slots": 1, "slot_result": [812], "slot_positive": [True],
           "calib_min": [0], "calib_max": [3000], "readings": [805, 814, 817], "readings_ok": [True] * 3}
    assert validate(rdr) is None
    assert validate({**rdr, "slot_result": [805, 814, 817]}) is not None   # bug firmware gửi 3 số thô vào slot_result → 400 ngay
    assert validate({**rdr, "calib_min": []}) is not None


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




# --- Trạm ATE ---------------------------------------------------------------

def _step(code, verdict="pass"):
    return {"code": code, "name": code, "verdict": verdict}


def _rec(sn, verdict, started, fail_code=None, steps=None):
    return {"sn": sn, "verdict": verdict, "started_at": started,
            "fail_code": fail_code, "steps": steps or [_step("FW-01")]}


def test_validate_ate_record():
    ok = {"sn": "RPL02013", "verdict": "pass", "limits_ver": "p0",
          "steps": [_step("FW-01"), _step("ID-01")]}
    assert validate_ate_record(ok) is None
    assert validate_ate_record(dict(ok, sn="")) is not None
    assert validate_ate_record(dict(ok, verdict="ok")) is not None      # verdict lạ
    assert validate_ate_record(dict(ok, limits_ver="")) is not None     # thiếu bộ ngưỡng
    assert validate_ate_record(dict(ok, steps=[])) is not None          # không bước nào
    assert validate_ate_record(dict(ok, steps=[{"name": "x", "verdict": "pass"}])) is not None
    assert validate_ate_record(dict(ok, steps=[_step("FW-01", "hmm")])) is not None
    assert validate_ate_record("chuỗi") is not None
    # Hồ sơ FAIL vẫn phải VÀO ĐƯỢC kho — đó là dữ liệu quý nhất của trạm.
    assert validate_ate_record(dict(ok, verdict="fail",
                                    steps=[_step("BOOT-01", "fail")])) is None
    # sn dài hơn 9 ký tự (giới hạn firmware) KHÔNG bị server chặn: app chấm ID-01
    # hỏng và hồ sơ hỏng đó phải lưu lại được.
    assert validate_ate_record(dict(ok, sn="RPL020131234")) is None


def test_ate_first_fail_va_normalize():
    steps = [_step("FW-01"), _step("BOOT-01", "fail"), _step("ID-01", "fail")]
    assert ate_first_fail(steps) == "BOOT-01"      # bước hỏng ĐẦU TIÊN
    assert ate_first_fail([_step("FW-01")]) == ""
    doc = normalize_ate_record(
        {"sn": " RPL02013 ", "verdict": "FAIL", "limits_ver": "p0", "steps": steps},
        received_at="2026-09-07T02:00:00+00:00")
    assert doc["sn"] == "RPL02013" and doc["verdict"] == "fail"
    assert doc["fail_code"] == "BOOT-01"           # client không gửi -> tự suy
    assert doc["received_at"] == "2026-09-07T02:00:00+00:00"
    assert doc["note"] == "" and doc["calib"] is None


def test_ate_stats_fpy_theo_lan_thu_dau():
    metas = [
        # Máy A: fail rồi mới pass -> KHÔNG tính first-pass
        _rec("A", "fail", "2026-09-01T08:00:00Z", fail_code="BOOT-01"),
        _rec("A", "pass", "2026-09-01T09:00:00Z"),
        # Máy B: pass ngay
        _rec("B", "pass", "2026-09-01T10:00:00Z"),
        # Máy C: chỉ có bản aborted -> không tính là một lần thử
        _rec("C", "aborted", "2026-09-02T10:00:00Z"),
    ]
    s = ate_stats(metas)
    assert s["total"] == 4 and s["pass"] == 2 and s["fail"] == 1 and s["aborted"] == 1
    assert s["machines"] == 2 and s["first_pass"] == 1 and s["fpy"] == 0.5
    assert s["pareto"] == [{"code": "BOOT-01", "count": 1}]
    assert [d["day"] for d in s["by_day"]] == ["2026-09-01", "2026-09-02"]
    assert ate_stats([])["fpy"] is None            # kho rỗng -> None, không chia 0


def test_ate_stats_thu_tu_khong_phu_thuoc_dau_vao():
    """Danh sách tới theo thứ tự MỚI NHẤT TRƯỚC (như _ate_metas trả về) thì
    'lần thử đầu' vẫn phải là bản ghi cũ nhất."""
    metas = [_rec("A", "pass", "2026-09-01T09:00:00Z"),
             _rec("A", "fail", "2026-09-01T08:00:00Z", fail_code="ID-01")]
    assert ate_stats(metas)["first_pass"] == 0


def test_ate_match_loc():
    m = {"sn": "RPL02013", "verdict": "fail", "started_at": "2026-09-05T10:00:00Z"}
    assert ate_match(m)
    assert ate_match(m, sn="rpl02013")             # không phân biệt hoa thường
    assert not ate_match(m, sn="RPL02014")
    assert ate_match(m, verdict="fail") and not ate_match(m, verdict="pass")
    assert ate_match(m, from_="2026-09-05", to="2026-09-05")
    assert not ate_match(m, from_="2026-09-06")
    assert not ate_match(m, to="2026-09-04")
    # Hồ sơ thiếu ngày: lọt qua khi KHÔNG lọc ngày, rơi ra khi có lọc ngày
    assert ate_match({"sn": "X", "verdict": "pass"})
    assert not ate_match({"sn": "X", "verdict": "pass"}, from_="2026-09-01")


def test_validate_ate_limits():
    assert validate_ate_limits({"version": "v1", "sn_max_len": 9}) is None
    assert validate_ate_limits({"sn_max_len": 9}) is not None   # thiếu version
    assert validate_ate_limits([]) is not None
    assert validate_ate_limits({"version": "x" * 65}) is not None
    assert validate_ate_limits(DEFAULT_ATE_LIMITS) is None      # bộ mặc định phải hợp lệ



def test_ate_limits_conflict():
    old = {"version": "v1", "bright_min": 800, "updated_at": "x", "updated_by": "y"}
    assert ate_limits_conflict(old, {"version": "v1", "bright_min": 800}) is None  # y hệt
    assert ate_limits_conflict(old, {"version": "v2", "bright_min": 900}) is None  # đổi version
    assert ate_limits_conflict(None, {"version": "v1"}) is None                    # chưa có gì
    msg = ate_limits_conflict(old, {"version": "v1", "bright_min": 900})
    assert msg and "đổi version" in msg


def test_ate_match_theo_lo():
    m = {"sn": "RPL02013", "verdict": "pass", "batch": "L2609A",
         "started_at": "2026-09-05T10:00:00Z"}
    assert ate_match(m, batch="l2609a")          # không phân biệt hoa thường
    assert not ate_match(m, batch="L2609B")
    assert not ate_match({"sn": "X", "verdict": "pass"}, batch="L2609A")


def test_normalize_giu_ma_lo():
    doc = normalize_ate_record(
        {"sn": "RPL02013", "batch": " L2609A ", "verdict": "pass",
         "limits_ver": "L2609A-1", "steps": [_step("FW-01")]},
        received_at="2026-09-07T02:00:00+00:00")
    assert doc["batch"] == "L2609A"
    assert ate_meta(doc, "f.json")["batch"] == "L2609A"


# --- OTA nhiều sản phẩm: khoá, thẻ nhúng, tên file --------------------------------------

def test_product_key():
    assert product_key("rapidplus") == "rapidplus"
    assert product_key("rapidplus-a") == "rapidplus-a"
    for bad in ("Reader", "rapid plus", "", "a" * 25, "../x", None):
        assert product_key(bad) is None, bad
    # ba từ dành riêng = đoạn path cố định của /ota/*
    for reserved in ("check", "products", "target"):
        assert product_key(reserved) is None


def test_parse_image_tags():
    tag = b"FBTIMG1;product=rapidplus;ver=v2.4.6;hw=V1.1,v1.2, V1.3;;"
    raw = b"\xe9\x00\x00" * 100 + tag + bytes(50) + tag + b"tail"
    tags = parse_image_tags(raw)
    assert len(tags) == 1  # hai lần cùng nội dung = một thẻ
    t = tags[0]
    assert t["product"] == "rapidplus" and t["ver"] == "v2.4.6"
    assert t["hw"] == ["V1.1", "V1.2", "V1.3"]  # viết HOA để so với PCB_version
    assert t["raw"] == tag.decode()
    # không thẻ / thẻ cụt (không có ';;' trong 160 byte) → bỏ qua
    assert parse_image_tags(b"firmware without tag") == []
    assert parse_image_tags(b"FBTIMG1;product=reader;ver=v1" + bytes(200)) == []
    # product sai cú pháp → None, caller từ chối; hw trống → []
    t2 = parse_image_tags(b"FBTIMG1;product=Reader;ver=v1.0.0;hw=;;")[0]
    assert t2["product"] is None and t2["hw"] == []
    # hai thẻ KHÁC nhau → trả cả hai (caller coi là ảnh dị dạng)
    two = parse_image_tags(b"FBTIMG1;product=a;ver=v1;;" + b"FBTIMG1;product=b;ver=v1;;")
    assert [x["product"] for x in two] == ["a", "b"]


def test_ver_from_name_va_expected_bin_name():
    assert ver_from_name("fbt_v2.4.5AT.bin") == "v2.4.5AT"
    assert ver_from_name("reader_V1.0.0.bin") == "v1.0.0"
    assert ver_from_name("firmware.bin") == ""
    assert ver_from_name("fbt_vx.bin") == ""
    # kho kế thừa GIỮ tiền tố fbt_ (firmware ≤ v2.4.5 so đúng chuỗi "fbt_<ver>.bin")
    assert expected_bin_name("rapidplus", "v2.4.6", "rapidplus") == "fbt_v2.4.6.bin"
    assert expected_bin_name("rapidplus", "V2.4.6", "rapidplus") == "fbt_v2.4.6.bin"
    assert expected_bin_name("reader", "1.0.0", "rapidplus") == "reader_v1.0.0.bin"
    # đổi LEGACY_PRODUCT thì kho kế thừa đổi theo, không dính cứng chữ "rapidplus"
    assert expected_bin_name("rapidplus", "v2.4.6", "reader") == "rapidplus_v2.4.6.bin"


if __name__ == "__main__":
    ran = 0
    for _name, _fn in sorted(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            _fn()
            ran += 1
            print(f"ok {_name}")
    print(f"{ran} tests passed")
