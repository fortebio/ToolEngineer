"""Quản lý MÁY trong OTA nhiều sản phẩm (docs/plan/ota-quan-ly-may-nhieu-san-pham.md, B1–B3):
devices.json gán tay, product_for, /devices hợp nhất + ota.state, offered trong fw_seen,
/ota/{product}/progress, khoá upload, thẻ esp_app_desc_t, OTA_REQUIRE_TAG theo sản phẩm,
ghim stale. TestClient, không cần Postgres (`db.list_devices` vá).

Chạy: pytest tests/test_ota_devices.py -q
"""
import hashlib
import json
import os
import struct
import sys
import tempfile
import threading
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.environ.setdefault("FBT_DATA_DIR", tempfile.mkdtemp(prefix="fbt_test_"))
os.environ.setdefault("FBT_OTA_DIR", tempfile.mkdtemp(prefix="fbt_ota_test_"))
os.environ.setdefault("FBT_LOGS_DIR", tempfile.mkdtemp(prefix="fbt_logs_test_"))
os.environ.setdefault("FBT_ATE_DIR", tempfile.mkdtemp(prefix="fbt_ate_test_"))
os.environ.setdefault("RECEIVER_TOKEN", "testtok")
os.environ.setdefault("FBT_DB", "dbname=__nope__ connect_timeout=1")

from fastapi.testclient import TestClient  # noqa: E402

from app import config, db, logic, main, ota  # noqa: E402

client = TestClient(main.app, raise_server_exceptions=False)
AUTH = {"Authorization": "Bearer testtok"}


def tag(product: str, ver: str, hw: str = "V1.1,V1.2,V1.3") -> bytes:
    return f"FBTIMG1;product={product};ver={ver};hw={hw};;".encode()


def img(product: str = "", ver: str = "", hw: str = "V1.1,V1.2,V1.3", body: bytes = b"code") -> bytes:
    t = tag(product, ver, hw) if product else b""
    return b"\xe9\x00\x00\x03" + bytes(28) + t + bytes(16) + body


def idf_img(project: str, version: str, body: bytes = b"idf") -> bytes:
    """Ảnh ESP-IDF giả: header 0x20 byte + esp_app_desc_t (magic, secure_version, reserv1[2],
    version[32], project_name[32]) + phần còn lại của struct + thân. Đúng bố cục đã đọc từ
    build/rapid4p.bin thật."""
    desc = struct.pack("<IIII", logic.APP_DESC_MAGIC, 0, 0, 0)
    desc += version.encode().ljust(32, b"\0") + project.encode().ljust(32, b"\0")
    desc += bytes(256 - len(desc))
    return b"\xe9\x00\x00\x03" + bytes(28) + desc + body


@pytest.fixture
def kho(monkeypatch, tmp_path):
    monkeypatch.setattr(config, "OTA_DIR", tmp_path)
    monkeypatch.setattr(config, "LEGACY_PRODUCT", "rapidplus")
    monkeypatch.setattr(config, "LEGACY_PRODUCT_BY_PREFIX", {"RPL": "rapidplus", "RDR": "reader"})
    monkeypatch.setattr(config, "OTA_REQUIRE_TAG", False)
    # fw_seen/fw_log của main.py chụp đường dẫn lúc import → trỏ lại vào tmp để test cô lập.
    monkeypatch.setattr(main, "_FW_FILE", tmp_path / "fw_seen.json")
    monkeypatch.setattr(main, "_FW_LOG_FILE", tmp_path / "fw_log.json")
    # Không Postgres: bảng sessions chỉ biết RPL00001 chạy v2.4.4 (chưa bao giờ tự khai).
    monkeypatch.setattr(db, "list_devices", lambda: [
        {"id_device": "RPL00001", "sessions": 3, "last_seen": None, "version": "v2.4.4"}])
    return tmp_path


def put_bin(path, raw, **q):
    return client.put(path, content=raw, params=q or None, headers=AUTH)


def check(**q):
    return client.get("/ota/check", params=q, headers=AUTH).json()


def rows():
    r = client.get("/devices", headers=AUTH)
    assert r.status_code == 200, r.text
    return {d["id_device"]: d for d in r.json()}


# --- B1 ------------------------------------------------------------------------------------

def test_upload_dong_thoi_cung_ten_khac_noi_dung(kho):
    """Trước 2026-09-18: cả hai qua is_file()=False, chung .tmp-<name> → Linux 200+200 (bản
    sau đè, lách 409), Windows một bên 500 PermissionError. Giờ: đúng một 200, một 409."""
    a = img() + b"A" * 200_000
    b = img() + b"B" * 200_000
    codes = []
    go = threading.Barrier(2)

    def put(raw):
        go.wait()
        codes.append(put_bin("/ota/rapidplus/fbt_v9.9.9.bin", raw).status_code)

    ts = [threading.Thread(target=put, args=(x,)) for x in (a, b)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert sorted(codes) == [200, 409], codes
    assert not list((kho / "products" / "rapidplus").glob(".tmp-*")), "file tạm phải được dọn"


def test_devices_hop_nhat_fw_seen_va_khoi_ota(kho):
    check(device="RPL00002", ver="v2.4.5AT1")                       # chỉ poll, chưa gửi phiên đo
    check(device="R4P00001", ver="v0.1.0", product="rapid4p", hw="P4C5-43")
    d = rows()
    assert set(d) >= {"RPL00001", "RPL00002", "R4P00001"}
    assert d["RPL00002"]["sessions"] == 0 and d["RPL00002"]["last_seen"] is None
    assert d["RPL00002"]["version"] == "v2.4.5AT1"
    assert d["R4P00001"]["product_effective"] == "rapid4p" and d["R4P00001"]["hw"] == "P4C5-43"
    assert d["RPL00001"]["product_effective"] == "rapidplus"
    for r in d.values():
        assert r["ota"]["state"] in ota.OTA_STATES and "last_check" in r
    assert d["RPL00001"]["ota"]["state"] == "none"                   # kho trống


def test_x_md5_tu_manifest_va_tinh_bu_cho_manifest_cu(kho):
    raw = img("rapidplus", "v2.4.6")
    put_bin("/ota/rapidplus", raw)
    m = json.loads((kho / "products/rapidplus/fbt_v2.4.6.bin.json").read_text("utf-8"))
    assert m["md5"] == hashlib.md5(raw).hexdigest() and m["tag_source"] == "fbtimg"
    # manifest cũ (deploy trước) không có md5 → tải vẫn đúng header và ghi bù
    del m["md5"]
    (kho / "products/rapidplus/fbt_v2.4.6.bin.json").write_text(json.dumps(m), "utf-8")
    r = client.get("/ota/rapidplus/fbt_v2.4.6.bin", headers=AUTH)
    assert r.status_code == 200 and r.headers["x-md5"] == hashlib.md5(raw).hexdigest()
    m2 = json.loads((kho / "products/rapidplus/fbt_v2.4.6.bin.json").read_text("utf-8"))
    assert m2["md5"] == hashlib.md5(raw).hexdigest()


# --- B2: gán tay, product_for, state, offered -------------------------------------------------

def test_product_for_thu_tu_bon_nguon(kho):
    assert ota.product_for("RPL00009") == ("rapidplus", False)          # tiền tố
    assert ota.product_for("XX1") == ("rapidplus", False)               # legacy
    ota.assign_product(["RPL00009"], "rapidplus-a", by="root")
    assert ota.product_for("RPL00009") == ("rapidplus-a", False)        # gán tay thắng tiền tố
    assert ota.product_for("RPL00009", "rapidplus-prod") == ("rapidplus-prod", True)  # tự khai thắng + conflict
    assert ota.product_for("RPL00009", "rapidplus-a") == ("rapidplus-a", False)
    assert ota.unassign_product("RPL00009") and not ota.unassign_product("RPL00009")
    assert ota.product_for("RPL00009") == ("rapidplus", False)


def test_gan_tay_doi_kho_ota_check(kho):
    """Bẫy kho rapidplus-a: máy a1 không tự khai → rơi vào rapidplus. Gán tay là kho a sống."""
    put_bin("/ota/rapidplus-a/fbt_v2.4.6a1.bin", img())
    client.put("/ota/rapidplus-a/target/fbt_v2.4.6a1.bin", headers=AUTH)
    put_bin("/ota/rapidplus/fbt_v2.4.6AT1.bin", img())
    client.put("/ota/rapidplus/target/fbt_v2.4.6AT1.bin", headers=AUTH)
    assert check(device="RPL00007", ver="v2.4.5a1")["version"] == "fbt_v2.4.6AT1.bin"  # mời nhầm bản AT
    r = client.put("/devices/RPL00007/product", params={"product": "rapidplus-a", "by": "root"}, headers=AUTH)
    assert r.status_code == 200 and r.json()["device"]["product"] == "rapidplus-a"
    j = check(device="RPL00007", ver="v2.4.5a1")
    assert j["version"] == "fbt_v2.4.6a1.bin" and j["product"] == "rapidplus-a"
    d = rows()["RPL00007"]
    assert d["product_assigned"] == "rapidplus-a" and d["product_effective"] == "rapidplus-a"
    assert d["ota"]["target"] == "fbt_v2.4.6a1.bin" and d["ota"]["state"] == "offered"
    assert client.delete("/devices/RPL00007/product", headers=AUTH).json()["removed"] is True
    assert check(device="RPL00007", ver="v2.4.5a1")["version"] == "fbt_v2.4.6AT1.bin"


def test_gan_hang_loat_va_ma_xau(kho):
    r = client.put("/devices/product", params={"product": "rapidplus-a", "ids": "RPL1,RPL2, RPL3", "by": "x"},
                   headers=AUTH)
    assert r.status_code == 200 and sorted(r.json()["devices"]) == ["RPL1", "RPL2", "RPL3"]
    assert client.put("/devices/product", params={"product": "rapidplus-a", "ids": "../x"}, headers=AUTH).status_code == 400
    assert client.put("/devices/product", params={"product": "Reader", "ids": "RPL1"}, headers=AUTH).status_code == 404
    assert client.put("/devices/product", params={"product": "rapidplus-a", "ids": ""}, headers=AUTH).status_code == 400
    # POST bị catch-all nuốt — nhắc lại quy tắc PUT
    assert client.post("/devices/product", params={"product": "rapidplus-a", "ids": "RPL1"}, headers=AUTH).status_code != 200
    # máy gán tay chưa từng poll vẫn hiện ở /devices
    d = rows()
    assert d["RPL1"]["sessions"] == 0 and d["RPL1"]["product_effective"] == "rapidplus-a"
    assert d["RPL1"]["ota"]["state"] == "none" if not d["RPL1"]["ota"]["target"] else True


def test_devices_json_sua_tay_muc_meo_bi_bo(kho):
    (kho / "devices.json").write_text(json.dumps({
        "RPL5": {"product": "rapidplus-a"}, "RPL6": "rapidplus-a", "RPL7": {"product": "Bad Key"},
        "../x": {"product": "rapidplus-a"}}), "utf-8")
    assert set(ota.read_devices()) == {"RPL5"}
    assert rows()["RPL5"]["product_assigned"] == "rapidplus-a"


def test_ota_state_sau_ca_va_hau_to(kho):
    put_bin("/ota/rapidplus/fbt_v2.4.5AT1.bin", img())
    client.put("/ota/rapidplus/target/fbt_v2.4.5AT1.bin", headers=AUTH)
    cache = {}
    st = lambda **k: ota.device_status("rapidplus", "RPLX", cache=cache, **k)["state"]  # noqa: E731
    assert st(version="v2.4.5AT1") == "on"                       # hậu tố GIỮ: đúng bản
    assert st(version="V2.4.5at1") == "on"
    assert st(version="v2.4.5") == "waiting"                     # bản gốc không hậu tố ≠ AT1
    assert st(version="v2.4.5AT1x") == "waiting"
    assert st(version="") == "unknown"
    assert st(version="v2.4.4", offered={"file": "fbt_v2.4.5AT1.bin"}) == "offered"
    assert st(version="v2.4.4", offered={"file": "fbt_v2.4.4.bin"}) == "waiting"  # mời bản cũ hơn target mới
    assert ota.device_status("rapidplus-a", "RPLX", version="v1")["state"] == "none"
    # ghim máy vào file đã mất → none (pin-missing), không rơi về bản chung
    client.put("/ota/rapidplus/target/fbt_v2.4.5AT1.bin", params={"device": "RPLY"}, headers=AUTH)
    (kho / "products/rapidplus/fbt_v2.4.5AT1.bin").unlink()
    s = ota.device_status("rapidplus", "RPLY", version="v1")
    assert s["state"] == "none" and s["reason"] == "pin-missing"


def test_loc_hw_ra_skipped(kho):
    put_bin("/ota/rapid4p", idf_img("rapid4p", "0.2.0"))
    client.put("/ota/rapid4p/target/rapid4p_v0.2.0.bin", headers=AUTH)
    # manifest app_desc không có hw → không lọc; gắn hw tay để kiểm nhánh skipped
    mp = kho / "products/rapid4p/rapid4p_v0.2.0.bin.json"
    m = json.loads(mp.read_text("utf-8")); m["hw"] = ["P4C5-43"]; mp.write_text(json.dumps(m), "utf-8")
    assert ota.device_status("rapid4p", "R4P1", version="v0.1.0", hw="P4C5-PROD")["state"] == "skipped"
    assert ota.device_status("rapid4p", "R4P1", version="v0.1.0", hw="P4C5-43")["state"] == "waiting"


def test_offered_ghi_va_xoa_theo_luot_check(kho):
    put_bin("/ota/rapidplus/fbt_v2.4.6.bin", img())
    assert check(device="RPL00003", ver="v2.4.5AT1")["update"] is False
    assert main._fw_seen()["RPL00003"]["offered"] is None
    assert rows()["RPL00003"]["ota"]["state"] == "none"
    client.put("/ota/rapidplus/target/fbt_v2.4.6.bin", headers=AUTH)
    assert rows()["RPL00003"]["ota"]["state"] == "waiting"          # đặt target, máy chưa poll lại
    assert check(device="RPL00003", ver="v2.4.5AT1")["update"] is True
    o = main._fw_seen()["RPL00003"]["offered"]
    assert o["file"] == "fbt_v2.4.6.bin" and o["ver"] == "v2.4.6" and o["at"]
    d = rows()["RPL00003"]
    assert d["ota"]["state"] == "offered" and d["ota"]["offered_at"] == o["at"]
    # Server KHÔNG so version (việc của firmware): vẫn trả update:true, nhưng /devices thấy máy
    # đã báo đúng bản → `on`, và mốc updated=1 vào fw_log.
    assert check(device="RPL00003", ver="v2.4.6", updated="1")["update"] is True
    assert rows()["RPL00003"]["ota"]["state"] == "on"
    assert client.get("/devices/RPL00003/fw-log", headers=AUTH).json()[0]["how"] == "update"
    # máy cũ (không gửi ver) không vào fw_seen → không có gì để ghi, không lỗi
    assert check(device="RPL00004")["update"] is True
    assert "RPL00004" not in main._fw_seen()


def test_fw_seen_cu_khong_co_offered_van_doc_duoc(kho):
    main._atomic_json(main._FW_FILE, {"RPL9": {"version": "v2.4.5", "at": "2026-01-01T00:00:00+00:00"},
                                      "RPL8": "v2.4.4", "RPL7": {"version": "v1", "offered": "rác"}})
    d = rows()
    assert d["RPL9"]["version"] == "v2.4.5" and d["RPL8"]["version"] == "v2.4.4"
    assert d["RPL7"]["ota"]["state"] in ("none", "waiting")


# --- B3: thẻ esp_app_desc, REQUIRE_TAG theo sản phẩm, progress, stale ------------------------

def test_parse_app_desc_anh_that_gia_va_uu_tien_fbtimg():
    d = logic.parse_app_desc(idf_img("rapid4p", "0.1.0"))
    assert d == {"product": "rapid4p", "ver": "v0.1.0", "hw": None,
                 "raw": "esp_app_desc;project=rapid4p;version=0.1.0"}
    # Core Arduino nhúng project_name "arduino-lib-builder" — HỢP LỆ theo regex khoá, nên
    # parse_app_desc vẫn đọc; image_tag() với tập kho mới là chỗ chặn (ảnh Rapid+ không được
    # coi là "tự khai product arduino-lib-builder").
    ard = idf_img("arduino-lib-builder", "v4.4.7")
    assert logic.parse_app_desc(ard)["product"] == "arduino-lib-builder"
    assert logic.image_tag(ard, {"rapidplus", "rapid4p"}) == (None, None)
    assert logic.image_tag(ard)[1] == "app_desc"                       # None = nhận hết (chỉ test)
    assert logic.image_tag(idf_img("rapid4p", "0.1.0"), {"rapidplus"}) == (None, None)  # kho rapid4p chưa có
    assert logic.parse_app_desc(idf_img("rapid4p", "")) is None
    assert logic.parse_app_desc(img()) is None and logic.parse_app_desc(b"\xe9" * 40) is None
    both = idf_img("rapid4p", "0.1.0", body=tag("rapidplus", "v2.4.6"))
    assert logic.image_tag(both)[1] == "fbtimg" and logic.image_tag(both)[0]["product"] == "rapidplus"
    assert logic.image_tag(idf_img("rapid4p", "v0.3.0"))[0]["ver"] == "v0.3.0"


def test_anh_arduino_khong_bi_coi_la_the(kho):
    """Ảnh Rapid+/Reader build bằng core Arduino mang esp_app_desc_t project_name
    "arduino-lib-builder" → KHÔNG phải thẻ: upload có tên như trước, không 400 nhầm kho."""
    r = put_bin("/ota/rapidplus/fbt_v2.4.6AT1.bin", idf_img("arduino-lib-builder", "v4.4.7"))
    assert r.status_code == 200 and r.json()["tag"] is None and r.json()["tag_source"] is None, r.text
    assert r.json()["ver"] == "v2.4.6AT1"                                # ver suy từ tên
    assert put_bin("/ota/rapidplus", idf_img("arduino-lib-builder", "v4.4.7")).status_code == 400  # không tên → 400


def test_upload_anh_esp_idf_khong_ten(kho):
    r = put_bin("/ota/rapid4p", idf_img("rapid4p", "0.1.0"))
    assert r.status_code == 200 and r.json()["name"] == "rapid4p_v0.1.0.bin", r.text
    assert r.json()["ver"] == "v0.1.0" and r.json()["hw"] is None and r.json()["tag_source"] == "app_desc"
    assert put_bin("/ota/rapidplus", idf_img("rapid4p", "0.1.0")).status_code == 400   # nhầm kho
    assert put_bin("/ota/rapid4p/rapid4p_v0.2.0.bin", idf_img("rapid4p", "0.1.0")).status_code == 400  # tên ≠ thẻ
    lst = client.get("/ota", params={"product": "rapid4p"}, headers=AUTH).json()
    assert lst["files"][0]["tag_source"] == "app_desc" and lst["files"][0]["tag"] is True
    j = check(device="R4P1", ver="v0.0.9", product="rapid4p", hw="P4C5-43")
    client.put("/ota/rapid4p/target/rapid4p_v0.1.0.bin", headers=AUTH)
    j = check(device="R4P1", ver="v0.0.9", product="rapid4p", hw="P4C5-43")
    assert j["update"] is True and j["ver"] == "v0.1.0" and j["hw"] is None


def test_require_tag_theo_san_pham(kho, monkeypatch):
    monkeypatch.setattr(config, "OTA_REQUIRE_TAG", frozenset({"rapid4p"}))
    assert config.require_tag("rapid4p") and not config.require_tag("rapidplus")
    assert put_bin("/ota/rapid4p/rapid4p_v0.9.0.bin", img()).status_code == 400
    assert put_bin("/ota/rapid4p/rapid4p_v0.9.0.bin", img(), force="1").status_code == 200
    assert put_bin("/ota/rapidplus/fbt_v2.4.9.bin", img()).status_code == 200      # kho legacy không bắt
    monkeypatch.setattr(config, "OTA_REQUIRE_TAG", True)
    assert put_bin("/ota/rapidplus/fbt_v2.4.8.bin", img()).status_code == 400      # "1" = tất cả (cũ)


def test_require_tag_env_parse(monkeypatch):
    import importlib
    monkeypatch.setenv("OTA_REQUIRE_TAG", "rapid4p, Reader,rapidplus-prod,check")
    cfg = importlib.reload(config)
    # hạ chữ thường như LEGACY_PRODUCT ('Reader' → 'reader'); 'check' là từ dành riêng → bỏ
    assert cfg.OTA_REQUIRE_TAG == frozenset({"rapid4p", "reader", "rapidplus-prod"})
    monkeypatch.setenv("OTA_REQUIRE_TAG", "true")
    assert importlib.reload(config).OTA_REQUIRE_TAG is True
    monkeypatch.setenv("OTA_REQUIRE_TAG", "")
    assert importlib.reload(config).OTA_REQUIRE_TAG is False
    monkeypatch.delenv("OTA_REQUIRE_TAG")
    importlib.reload(config)


def test_progress_theo_kho(kho):
    put_bin("/ota/rapidplus/fbt_v2.4.6AT1.bin", img())
    client.put("/ota/rapidplus/target/fbt_v2.4.6AT1.bin", params={"by": "root"}, headers=AUTH)
    check(device="RPL00002", ver="v2.4.6AT1")                       # on
    check(device="RPL00003", ver="v2.4.5AT1")                       # offered
    ota.assign_product(["RPL00005"], "rapidplus")                   # gán tay, chưa poll → unknown
    check(device="R4P00001", ver="v0.1.0", product="rapid4p")       # kho khác, không đếm
    r = client.get("/ota/rapidplus/progress", headers=AUTH)
    assert r.status_code == 200, r.text
    j = r.json()
    assert j["target"] == "fbt_v2.4.6AT1.bin" and j["ver"] == "v2.4.6AT1" and j["target_by"] == "root"
    by = {d["id_device"]: d["state"] for d in j["devices"]}
    assert by == {"RPL00001": "waiting", "RPL00002": "on", "RPL00003": "offered", "RPL00005": "unknown"}
    assert j["counts"]["on"] == 1 and j["counts"]["offered"] == 1 and j["total"] == 4
    assert client.get("/ota/rapid4p/progress", headers=AUTH).json()["total"] == 1
    assert client.get("/ota/check/progress", headers=AUTH).status_code == 404   # từ dành riêng
    assert client.get("/ota/rapidplus/progress").status_code == 401             # cần token


def test_ghim_stale_va_dem_may_moi_kho(kho):
    put_bin("/ota/rapidplus/fbt_v2.4.6.bin", img())
    client.put("/ota/rapidplus/target/fbt_v2.4.6.bin", params={"device": "RPL00008"}, headers=AUTH)
    check(device="RPL00008", ver="v2.4.5", product="rapidplus-a")   # máy tự khai sang kho a
    check(device="RPL00002", ver="v2.4.5")
    lst = client.get("/ota", headers=AUTH).json()
    assert lst["devices"]["RPL00008"]["stale"] is True
    ps = {p["product"]: p for p in client.get("/ota/products", headers=AUTH).json()["products"]}
    assert ps["rapidplus"]["stale_pins"] == 1 and ps["rapidplus"]["devices"] == 1       # RPL00002
    assert ps["rapidplus-a"]["devices"] == 1 and ps["rapidplus-a"]["files"] == 0         # kho chưa có thư mục vẫn liệt kê
    # gán tay kèm clean=1 gỡ ghim mồ côi ở kho khác
    r = client.put("/devices/RPL00008/product", params={"product": "rapidplus-a", "clean": "1"}, headers=AUTH)
    assert r.json()["cleaned_pins"] == ["rapidplus"]
    assert "RPL00008" not in client.get("/ota", headers=AUTH).json()["devices"]


def test_route_ghi_can_ota_admin(kho, monkeypatch):
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    assert client.put("/devices/RPL1/product", params={"product": "rapidplus-a"}, headers=AUTH).status_code == 401
    assert client.put("/devices/RPL1/product", params={"product": "rapidplus-a"},
                      headers={"Authorization": "Bearer admintok"}).status_code == 200
    assert client.delete("/devices/RPL1/product", headers=AUTH).status_code == 401
