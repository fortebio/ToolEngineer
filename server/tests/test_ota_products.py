"""Kho OTA tách theo sản phẩm (app/ota.py + route /ota/* trong main.py) — TestClient, không
cần Postgres. Mỗi test một OTA_DIR riêng (fixture `kho` vá `config.OTA_DIR`; ota.py đọc
config qua hàm nên vá được sau import).

Chạy: pytest tests/test_ota_products.py -q
"""
import json
import os
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
# Cùng bộ env với test_api.py — module nào import app.main trước cũng ra cùng cấu hình.
os.environ.setdefault("FBT_DATA_DIR", tempfile.mkdtemp(prefix="fbt_test_"))
os.environ.setdefault("FBT_OTA_DIR", tempfile.mkdtemp(prefix="fbt_ota_test_"))
os.environ.setdefault("FBT_LOGS_DIR", tempfile.mkdtemp(prefix="fbt_logs_test_"))
os.environ.setdefault("FBT_ATE_DIR", tempfile.mkdtemp(prefix="fbt_ate_test_"))
os.environ.setdefault("RECEIVER_TOKEN", "testtok")
os.environ.setdefault("FBT_DB", "dbname=__nope__ connect_timeout=1")

from fastapi.testclient import TestClient  # noqa: E402

from app import config, ota  # noqa: E402
from app.main import _fw_seen, app  # noqa: E402

client = TestClient(app, raise_server_exceptions=False)
AUTH = {"Authorization": "Bearer testtok"}


def tag(product: str, ver: str, hw: str = "V1.1,V1.2,V1.3") -> bytes:
    return f"FBTIMG1;product={product};ver={ver};hw={hw};;".encode()


def img(product: str = "", ver: str = "", hw: str = "V1.1,V1.2,V1.3", body: bytes = b"code") -> bytes:
    """Ảnh giả: header ESP32 + (thẻ nhúng nếu có product) + thân."""
    t = tag(product, ver, hw) if product else b""
    return b"\xe9\x00\x00\x03" + bytes(28) + t + bytes(16) + body


@pytest.fixture
def kho(monkeypatch, tmp_path):
    monkeypatch.setattr(config, "OTA_DIR", tmp_path)
    monkeypatch.setattr(config, "LEGACY_PRODUCT", "rapidplus")
    monkeypatch.setattr(config, "LEGACY_PRODUCT_BY_PREFIX", {})
    monkeypatch.setattr(config, "OTA_REQUIRE_TAG", False)
    return tmp_path


def put_bin(path: str, raw: bytes, **q):
    return client.put(path, content=raw, params=q or None, headers=AUTH)


# --- đường cũ (app/firmware đang phát hành) vẫn chạy y nguyên ---------------------------

def test_duong_cu_vao_kho_legacy(kho):
    raw = img(body=b"old style, no tag")
    r = put_bin("/ota/fbt_v2.4.6.bin", raw)
    assert r.status_code == 200, r.text
    assert r.json()["product"] == "rapidplus" and r.json()["ver"] == "v2.4.6"
    assert (kho / "products" / "rapidplus" / "fbt_v2.4.6.bin").read_bytes() == raw
    assert (kho / "products" / "rapidplus" / "fbt_v2.4.6.bin.json").is_file()
    assert not (kho / "fbt_v2.4.6.bin").exists()  # KHÔNG còn ghi ra gốc

    assert client.put("/ota/target/fbt_v2.4.6.bin?by=cskh", headers=AUTH).status_code == 200
    c = client.get("/ota/check?device=RPL00001&ver=v2.4.5", headers=AUTH).json()
    assert c["update"] is True
    assert c["version"] == "fbt_v2.4.6.bin"  # firmware ≤ v2.4.5 so đúng chuỗi này
    assert c["ver"] == "v2.4.6" and c["product"] == "rapidplus" and c["hw"] is None
    assert c["url"].endswith("/ota/rapidplus/fbt_v2.4.6.bin")
    assert c["size"] == len(raw) and len(c["sha256"]) == 64

    d = client.get("/ota/rapidplus/fbt_v2.4.6.bin", headers=AUTH)
    assert d.status_code == 200 and d.content == raw and "x-md5" in {k.lower() for k in d.headers}
    d2 = client.get("/ota/fbt_v2.4.6.bin", headers=AUTH)  # tải kiểu cũ
    assert d2.status_code == 200 and d2.content == raw

    lst = client.get("/ota", headers=AUTH).json()  # không ?product= → kho legacy
    assert lst["product"] == "rapidplus" and lst["target"] == "fbt_v2.4.6.bin"
    assert lst["target_by"] == "cskh" and lst["files"][0]["ver"] == "v2.4.6"
    assert lst["files"][0]["tag"] is False

    assert client.delete("/ota/target", headers=AUTH).status_code == 200
    assert client.get("/ota/check?device=RPL00001", headers=AUTH).json() == {"update": False, "reason": "none"}
    assert client.delete("/ota/fbt_v2.4.6.bin", headers=AUTH).status_code == 200
    assert client.get("/ota/fbt_v2.4.6.bin", headers=AUTH).status_code == 404


def test_check_product_sai_la_fail_closed(kho):
    put_bin("/ota/fbt_v2.4.6.bin", img())
    client.put("/ota/target/fbt_v2.4.6.bin", headers=AUTH)
    # Máy khai product sai cú pháp: KHÔNG đoán hộ về kho legacy.
    assert client.get("/ota/check?device=X&product=Reader", headers=AUTH).json() == {"update": False, "reason": "product"}
    # Kho chưa tồn tại → không có gì.
    assert client.get("/ota/check?device=X&product=reader", headers=AUTH).json() == {"update": False, "reason": "none"}
    assert client.get("/ota/check?device=X&product=rapidplus", headers=AUTH).json()["update"] is True


# --- kho theo sản phẩm ----------------------------------------------------------------------

def test_hai_kho_khong_lan_nhau(kho):
    r = put_bin("/ota/reader/reader_v1.0.0.bin", img("reader", "v1.0.0", hw="R1"))
    assert r.status_code == 200, r.text
    assert r.json()["hw"] == ["R1"] and r.json()["ver"] == "v1.0.0"
    assert client.put("/ota/reader/target/reader_v1.0.0.bin", headers=AUTH).status_code == 200

    c = client.get("/ota/check?device=RDR001&ver=v0.9&product=reader&hw=R1", headers=AUTH).json()
    assert c["update"] is True and c["version"] == "reader_v1.0.0.bin" and c["product"] == "reader"
    assert c["url"].endswith("/ota/reader/reader_v1.0.0.bin")
    # Máy cũ (không khai product) KHÔNG bị mời ảnh Reader — đúng lý do tách kho.
    assert client.get("/ota/check?device=RPL00002", headers=AUTH).json() == {"update": False, "reason": "none"}
    assert client.get("/ota", headers=AUTH).json()["files"] == []

    p = client.get("/ota/products", headers=AUTH).json()
    assert p["legacy"] == "rapidplus"
    by = {x["product"]: x for x in p["products"]}
    assert by["reader"]["files"] == 1 and by["reader"]["target"] == "reader_v1.0.0.bin"
    assert by["rapidplus"]["legacy"] is True and by["rapidplus"]["files"] == 0  # luôn có mặt
    assert client.get("/ota?product=reader", headers=AUTH).json()["files"][0]["tag"] is True


def test_upload_the_nhung(kho):
    # thẻ khai product khác kho → 400
    r = put_bin("/ota/rapidplus/fbt_v9.9.9.bin", img("reader", "v9.9.9"))
    assert r.status_code == 400 and "reader" in r.json()["detail"]
    # tên nói một version, thẻ nói version khác → 400, báo tên đúng
    r = put_bin("/ota/reader/reader_v1.0.0.bin", img("reader", "v1.0.1"))
    assert r.status_code == 400 and "reader_v1.0.1.bin" in r.json()["detail"]
    # upload KHÔNG TÊN: server đặt tên từ thẻ
    r = put_bin("/ota/reader", img("reader", "v1.0.1"))
    assert r.status_code == 200 and r.json()["name"] == "reader_v1.0.1.bin"
    assert (kho / "products" / "reader" / "reader_v1.0.1.bin").is_file()
    # kho kế thừa: tên fbt_v… dù thẻ khai product rapidplus
    r = put_bin("/ota/rapidplus", img("rapidplus", "v2.4.6"))
    assert r.status_code == 200 and r.json()["name"] == "fbt_v2.4.6.bin"
    # không thẻ mà không tên → 400
    assert put_bin("/ota/reader", img()).status_code == 400
    # ảnh chứa hai thẻ khác nhau → 400
    r = put_bin("/ota/reader/reader_v2.bin", img("reader", "v2") + tag("reader", "v3"))
    assert r.status_code == 400
    # thẻ product sai cú pháp → 400
    assert put_bin("/ota/reader/reader_v2.bin", img("Reader", "v2")).status_code == 400
    # sản phẩm dành riêng / sai cú pháp → 404
    assert put_bin("/ota/check/x.bin", img()).status_code == 404
    assert client.get("/ota?product=target", headers=AUTH).status_code == 404
    assert put_bin("/ota/Reader/x.bin", img()).status_code == 404


def test_cung_ten_khac_noi_dung_409(kho):
    a = img(body=b"A")
    assert put_bin("/ota/reader/reader_v1.bin", a).status_code == 200
    r = put_bin("/ota/reader/reader_v1.bin", a)  # đẩy lại y hệt → idempotent
    assert r.status_code == 200 and r.json()["existed"] is True
    r = put_bin("/ota/reader/reader_v1.bin", img(body=b"B"))
    assert r.status_code == 409, r.text
    assert (kho / "products" / "reader" / "reader_v1.bin").read_bytes() == a  # không bị ghi đè
    assert client.delete("/ota/reader/reader_v1.bin", headers=AUTH).status_code == 200
    assert put_bin("/ota/reader/reader_v1.bin", img(body=b"B")).status_code == 200


def test_require_tag(kho, monkeypatch):
    monkeypatch.setattr(config, "OTA_REQUIRE_TAG", True)
    r = put_bin("/ota/reader/reader_v1.bin", img())
    assert r.status_code == 400 and "force" in r.json()["detail"]
    assert put_bin("/ota/reader/reader_v1.bin", img(), force="1").status_code == 200
    assert put_bin("/ota/reader/reader_v2.bin", img("reader", "v2")).status_code == 200  # có thẻ: khỏi force


# --- chọn bản: ghim > chung, lọc hw, ghim mất file -------------------------------------------

def test_loc_hw(kho):
    put_bin("/ota/rapidplus", img("rapidplus", "v2.4.6", hw="V1.3"))
    client.put("/ota/rapidplus/target/fbt_v2.4.6.bin", headers=AUTH)
    q = "/ota/check?device=RPL1&ver=v2.4.5&product=rapidplus"
    assert client.get(q + "&hw=V1.2", headers=AUTH).json() == {"update": False, "reason": "hw"}
    assert client.get(q + "&hw=v1.3", headers=AUTH).json()["update"] is True  # không phân biệt hoa/thường
    assert client.get(q, headers=AUTH).json()["update"] is True  # máy không khai hw → không lọc
    # ảnh không thẻ (hw None) → không lọc dù máy khai hw
    put_bin("/ota/rapidplus/fbt_v2.4.7.bin", img())
    client.put("/ota/rapidplus/target/fbt_v2.4.7.bin", headers=AUTH)
    assert client.get(q + "&hw=V1.2", headers=AUTH).json()["version"] == "fbt_v2.4.7.bin"


def test_ghim_thang_chung_va_ghim_mat_file(kho):
    put_bin("/ota/fbt_v1.bin", img(body=b"1"))
    put_bin("/ota/fbt_v2.bin", img(body=b"2"))
    client.put("/ota/target/fbt_v2.bin", headers=AUTH)
    client.put("/ota/target/fbt_v1.bin?device=RPLPIN&by=ky-thuat", headers=AUTH)
    assert client.get("/ota/check?device=RPLPIN", headers=AUTH).json()["version"] == "fbt_v1.bin"
    assert client.get("/ota/check?device=RPLOTHER", headers=AUTH).json()["version"] == "fbt_v2.bin"
    lst = client.get("/ota", headers=AUTH).json()
    assert lst["devices"]["RPLPIN"]["file"] == "fbt_v1.bin" and lst["devices"]["RPLPIN"]["by"] == "ky-thuat"
    # ghim mà file mất (xoá tay trên box) → KHÔNG rơi về bản chung
    (kho / "products" / "rapidplus" / "fbt_v1.bin").unlink()
    assert client.get("/ota/check?device=RPLPIN", headers=AUTH).json() == {"update": False, "reason": "pin-missing"}
    # gỡ ghim → theo bản chung
    assert client.delete("/ota/target?device=RPLPIN", headers=AUTH).status_code == 200
    assert client.get("/ota/check?device=RPLPIN", headers=AUTH).json()["version"] == "fbt_v2.bin"
    # xoá file đang là bản chung + đang được ghim → dọn cả hai
    client.put("/ota/target/fbt_v2.bin?device=RPLPIN", headers=AUTH)
    assert client.delete("/ota/rapidplus/fbt_v2.bin", headers=AUTH).status_code == 200
    cfg = json.loads((kho / "products" / "rapidplus" / "target.json").read_text(encoding="utf-8"))
    assert cfg["target"] is None and cfg["devices"] == {}


def test_bo_chung_giu_ghim_rieng(kho):
    put_bin("/ota/reader/reader_v1.bin", img(body=b"1"))
    client.put("/ota/reader/target/reader_v1.bin", headers=AUTH)
    client.put("/ota/reader/target/reader_v1.bin?device=RDRX", headers=AUTH)
    r = client.delete("/ota/reader/target", headers=AUTH).json()
    assert r["target"] is None
    assert client.get("/ota/check?device=RDRX&product=reader", headers=AUTH).json()["update"] is True
    assert client.get("/ota/check?device=RDRY&product=reader", headers=AUTH).json()["update"] is False


# --- máy cũ theo tiền tố, fw_seen, manifest tự lành, di cư -----------------------------------

def test_legacy_theo_tien_to(kho, monkeypatch):
    monkeypatch.setattr(config, "LEGACY_PRODUCT_BY_PREFIX", {"RDR": "reader", "RD": "readermax"})
    put_bin("/ota/reader/reader_v1.bin", img("reader", "v1"))
    client.put("/ota/reader/target/reader_v1.bin", headers=AUTH)
    assert ota.legacy_product_for("RDR001") == "reader"      # tiền tố dài hơn thắng
    assert ota.legacy_product_for("RD9") == "readermax"
    assert ota.legacy_product_for("RPL001") == "rapidplus"
    assert client.get("/ota/check?device=RDR001", headers=AUTH).json()["version"] == "reader_v1.bin"
    assert client.get("/ota/check?device=RPL001", headers=AUTH).json()["update"] is False


def test_fw_seen_ghi_product_hw(kho):
    client.get("/ota/check?device=RPLSEEN1&ver=v2.4.6&product=rapidplus&hw=V1.3", headers=AUTH)
    e = _fw_seen()["RPLSEEN1"]
    assert e["version"] == "v2.4.6" and e["product"] == "rapidplus" and e["hw"] == "V1.3"
    client.get("/ota/check?device=RPLSEEN2&ver=v2.4.5", headers=AUTH)  # firmware cũ: rỗng
    e2 = _fw_seen()["RPLSEEN2"]
    assert e2["product"] == "" and e2["hw"] == ""
    # hw bẩn không lọt vào file
    client.get("/ota/check?device=RPLSEEN3&ver=v2.4.6&product=rapidplus&hw=V1.3%20x", headers=AUTH)
    assert _fw_seen()["RPLSEEN3"]["hw"] == ""


def test_manifest_tu_lanh_va_anh_nam_nham_kho(kho):
    d = kho / "products" / "reader"
    d.mkdir(parents=True)
    raw = img("reader", "v3", hw="R2")
    (d / "reader_v3.bin").write_bytes(raw)  # scp tay lên box, không manifest
    lst = client.get("/ota?product=reader", headers=AUTH).json()
    assert lst["files"][0]["ver"] == "v3" and lst["files"][0]["hw"] == ["R2"]
    m = json.loads((d / "reader_v3.bin.json").read_text(encoding="utf-8"))
    assert m["source"] == "rebuilt" and m["size"] == len(raw)
    # ghi đè tay nội dung khác → size lệch → dựng lại từ bytes mới
    (d / "reader_v3.bin").write_bytes(img("reader", "v3", hw="R9", body=b"longer body here"))
    assert client.get("/ota?product=reader", headers=AUTH).json()["files"][0]["hw"] == ["R9"]
    # ảnh thẻ reader nằm trong kho rapidplus → không bao giờ được mời
    dl = kho / "products" / "rapidplus"
    dl.mkdir(parents=True)
    (dl / "fbt_v9.bin").write_bytes(img("reader", "v9"))
    client.put("/ota/rapidplus/target/fbt_v9.bin", headers=AUTH)
    assert client.get("/ota/check?device=RPL1", headers=AUTH).json() == {"update": False, "reason": "tag"}


def test_migrate_legacy(kho):
    (kho / "fbt_v2.4.4.bin").write_bytes(img(body=b"44"))
    (kho / "fbt_v2.4.5.bin").write_bytes(img(body=b"45"))
    (kho / "target.json").write_text(json.dumps({
        "target": "fbt_v2.4.5.bin",  # dạng chuỗi trần cũ
        "devices": {"RPLOLD": {"file": "fbt_v2.4.4.bin", "by": "x", "at": ""}},
    }), encoding="utf-8")
    plan = ota.migrate_legacy(dry_run=True)
    assert len(plan) == 3 and (kho / "fbt_v2.4.4.bin").is_file()  # dry-run không đụng gì
    done = ota.migrate_legacy()
    assert done == plan
    dest = kho / "products" / "rapidplus"
    assert not list(kho.glob("*.bin")) and not (kho / "target.json").exists()
    assert (dest / "fbt_v2.4.4.bin").is_file() and (dest / "target.json").is_file()
    m = json.loads((dest / "fbt_v2.4.5.bin.json").read_text(encoding="utf-8"))
    assert m["source"] == "migrated" and m["ver"] == "v2.4.5" and m["hw"] is None
    assert client.get("/ota/check?device=RPLOLD", headers=AUTH).json()["version"] == "fbt_v2.4.4.bin"
    assert client.get("/ota/check?device=RPLNEW", headers=AUTH).json()["version"] == "fbt_v2.4.5.bin"
    assert ota.migrate_legacy() == []  # idempotent
    # Trùng tên: giống nội dung → xoá gốc; khác → dời sang .conflict, kho giữ nguyên
    (kho / "fbt_v2.4.5.bin").write_bytes(img(body=b"45"))
    (kho / "fbt_v2.4.4.bin").write_bytes(img(body=b"KHAC"))
    (kho / "target.json").write_text("{}", encoding="utf-8")
    acts = ota.migrate_legacy()
    assert len(acts) == 3
    assert not (kho / "fbt_v2.4.5.bin").exists()
    assert (kho / "fbt_v2.4.4.bin.conflict").is_file()
    assert (dest / "fbt_v2.4.4.bin").read_bytes() == img(body=b"44")
    assert (kho / "target.json.migrated").is_file() and (dest / "target.json").is_file()
    assert client.get("/ota/check?device=RPLOLD", headers=AUTH).json()["version"] == "fbt_v2.4.4.bin"


def test_post_roi_vao_catchall(kho):
    """Ghi bằng PUT, không POST: catch-all ingest nuốt mọi POST → 400. Ghim luật."""
    r = client.post("/ota/reader/reader_v1.bin", content=img(), headers=AUTH)
    assert r.status_code == 400


# --- code-review 2026-09-12: 10 phát hiện → test ghim lại ------------------------------------

def test_tmp_bin_khong_phai_anh(kho):
    """`Path.glob('*.bin')` khớp cả dotfile → .tmp-*.bin (upload dở) không được lộ ra."""
    (kho / ".tmp-fbt_v9.bin").write_bytes(img(body=b"do dang"))       # ở gốc (bản cũ)
    d = kho / "products" / "reader"; d.mkdir(parents=True)
    (d / ".tmp-reader_v1.bin").write_bytes(img(body=b"do dang"))      # trong kho
    (d / "reader_v1.bin").write_bytes(img())
    assert ota.migrate_legacy() == []                                   # gốc coi như sạch
    assert (kho / ".tmp-fbt_v9.bin").is_file()                          # không bị dời
    names = [f["name"] for f in client.get("/ota?product=reader", headers=AUTH).json()["files"]]
    assert names == ["reader_v1.bin"]
    assert not (d / ".tmp-reader_v1.bin.json").exists()                 # không sinh manifest cho nó
    by = {x["product"]: x for x in client.get("/ota/products", headers=AUTH).json()["products"]}
    assert by["reader"]["files"] == 1
    # ghim vào tên dotfile / traversal cũng bị từ chối ở API
    assert client.put("/ota/reader/target/.tmp-reader_v1.bin", headers=AUTH).status_code == 400


def test_legacy_product_reserved_roi_ve_rapidplus():
    """Env OTA_LEGACY_PRODUCT trúng từ dành riêng phải rơi về rapidplus (không thì mọi route
    ghi kiểu cũ 404 vì _product('target') từ chối)."""
    assert config.valid_product("rapidplus") and config.valid_product("reader-2")
    for bad in ("target", "check", "products", "Reader", ""):
        assert not config.valid_product(bad), bad


def test_target_json_sua_tay_ten_khong_an_toan(kho):
    d = kho / "products" / "rapidplus"; d.mkdir(parents=True)
    (d / "fbt_v1.bin").write_bytes(img())
    (d / "target.json").write_text(json.dumps({
        "target": {"file": "../../x.bin"},
        "devices": {"RPLA": ".tmp-fbt_v1.bin", "RPLB": {"file": "fbt_v1.bin"}},
    }), encoding="utf-8")
    cfg = ota.read_cfg("rapidplus")
    assert cfg["target"] is None and list(cfg["devices"]) == ["RPLB"]
    assert client.get("/ota/check?device=RPLA", headers=AUTH).json() == {"update": False, "reason": "none"}
    assert client.get("/ota/check?device=RPLB", headers=AUTH).json()["version"] == "fbt_v1.bin"
    assert not (kho / "x.bin.json").exists() and not (kho.parent / "x.bin.json").exists()


def test_manifest_hw_sua_tay_duoc_chuan_hoa(kho):
    d = kho / "products" / "rapidplus"; d.mkdir(parents=True)
    raw = img(); (d / "fbt_v1.bin").write_bytes(raw)
    import hashlib
    (d / "fbt_v1.bin.json").write_text(json.dumps({
        "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "hw": "v1.3, v1.2"}), encoding="utf-8")
    client.put("/ota/rapidplus/target/fbt_v1.bin", headers=AUTH)
    q = "/ota/check?device=RPL1&product=rapidplus"
    assert client.get(q + "&hw=V1.3", headers=AUTH).json()["update"] is True   # chữ thường trong file vẫn khớp
    assert client.get(q + "&hw=V1", headers=AUTH).json() == {"update": False, "reason": "hw"}  # không còn so chuỗi con
    assert client.get(q, headers=AUTH).json()["hw"] == ["V1.3", "V1.2"]


def test_upload_khong_ten_day_lai_anh_da_co(kho):
    raw = img("reader", "v1.0.0")
    assert put_bin("/ota/reader", raw).status_code == 200
    # manifest sửa tay thiếu 'tag' → đẩy lại đúng ảnh vẫn 200 existed (không KeyError)
    mp = kho / "products" / "reader" / "reader_v1.0.0.bin.json"
    m = json.loads(mp.read_text(encoding="utf-8")); m.pop("tag")
    mp.write_text(json.dumps(m), encoding="utf-8")
    r = put_bin("/ota/reader", raw)
    assert r.status_code == 200 and r.json()["existed"] is True


def test_lifespan_khong_chet_khi_di_cu_loi(kho, monkeypatch):
    """Di cư kho lỗi (PermissionError…) không được làm server không khởi động."""
    def boom(dry_run=False):
        raise PermissionError("gia lap file do root so huu")
    monkeypatch.setattr(ota, "migrate_legacy", boom)
    from fastapi.testclient import TestClient as TC
    from app.main import app as _app
    with TC(_app) as c:  # `with` mới chạy lifespan
        assert c.get("/").json()["ok"] is True
