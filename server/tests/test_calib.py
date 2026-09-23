"""Ống chuẩn hiệu chuẩn (app/calib.py + route /calib/*) — hàm thuần + TestClient, không cần
Postgres. Mỗi test một CALIB_DIR riêng (fixture `kho` vá `config.CALIB_DIR`; calib.py đọc
config qua hàm nên vá được sau import).

Số liệu mẫu lấy từ sheet "RAPIDPlus calibration tubes" (bàn giao 2026-09): 3 ống × 4 nồng độ,
tổ hợp tốt nhất phải là 300/3 · 200/2 · 100/3 · 0/3 với slope 3,033 và R² 0,99998.

Chạy: pytest tests/test_calib.py -q
"""
import os
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.environ.setdefault("FBT_DATA_DIR", tempfile.mkdtemp(prefix="fbt_test_"))
os.environ.setdefault("FBT_OTA_DIR", tempfile.mkdtemp(prefix="fbt_ota_test_"))
os.environ.setdefault("FBT_LOGS_DIR", tempfile.mkdtemp(prefix="fbt_logs_test_"))
os.environ.setdefault("FBT_ATE_DIR", tempfile.mkdtemp(prefix="fbt_ate_test_"))
os.environ.setdefault("FBT_CALIB_DIR", tempfile.mkdtemp(prefix="fbt_calib_test_"))
os.environ.setdefault("RECEIVER_TOKEN", "testtok")
os.environ.setdefault("FBT_DB", "dbname=__nope__ connect_timeout=1")

from fastapi.testclient import TestClient  # noqa: E402

from app import calib, config  # noqa: E402
from app.main import app  # noqa: E402

client = TestClient(app, raise_server_exceptions=False)
AUTH = {"Authorization": "Bearer testtok"}
ADMIN = {"Authorization": "Bearer admintok"}

# Sheet mẫu: cột 300/200/100/0, hàng ống 1..3
SHEET = {
    "300": {"1": 1193, "2": 1179, "3": 1213},
    "200": {"1": 905, "2": 907, "3": 924},
    "100": {"1": 598, "2": 620, "3": 607},
    "0": {"1": 312, "2": 297, "3": 302},
}


@pytest.fixture
def kho(monkeypatch, tmp_path):
    monkeypatch.setattr(config, "CALIB_DIR", tmp_path)
    return tmp_path


# --- hàm thuần --------------------------------------------------------------------------

def test_bang_pha_khop_ban_giao():
    steps = calib.plan_steps()
    d = {s["code"]: s for s in steps}
    # 1000 nM từ stock 52 µM: 10 µL + 510 µL = 520 µL (hệ số 52)
    assert d["dil.1000"]["vol_dye_ul"] == 10 and d["dil.1000"]["vol_buffer_ul"] == 510
    assert d["dil.1000"]["factor"] == 52
    # 300/200/100 nM từ 1000 nM, mỗi 300 µL: 90+210, 60+240, 30+270
    assert (d["dil.300"]["vol_dye_ul"], d["dil.300"]["vol_buffer_ul"]) == (90, 210)
    assert (d["dil.200"]["vol_dye_ul"], d["dil.200"]["vol_buffer_ul"]) == (60, 240)
    assert (d["dil.100"]["vol_dye_ul"], d["dil.100"]["vol_buffer_ul"]) == (30, 270)
    assert d["dil.300"]["factor"] == 3.3333
    # thứ tự: chuẩn bị → pha → chia ống → đo
    codes = [s["code"] for s in steps]
    assert codes[:3] == ["prep.stock_out", "prep.spin", "prep.buffer"]
    assert codes[-3:] == ["aliquot", "seal", "measure"]
    assert all(s["done_at"] == "" for s in steps)


def test_pha_loang_chan_sai():
    with pytest.raises(calib.CalibError):
        calib.dilution_step(100, 300, 300)           # đặc hơn dung dịch mẹ
    with pytest.raises(calib.CalibError):
        calib.plan_steps(tubes_per_conc=20, aliquot_ul=25)  # 500 µL > 300 µL pha được


def test_hoi_quy_khop_sheet():
    fit = calib.linear_fit([300, 200, 100, 0], [1213, 907, 607, 302])
    assert fit["slope"] == 3.033 and fit["intercept"] == 302.3
    assert abs(fit["r2"] - 0.999986) < 1e-6
    assert calib.linear_fit([1, 1], [2, 3]) is None      # x trùng nhau
    assert calib.linear_fit([1, 2], [5, 5])["r2"] == 0.0   # y phẳng: SStot = 0


def test_r2_khong_lam_tron_va_sai_so_du():
    """R² là KHOÁ SẮP XẾP nên KHÔNG được làm tròn (2026-09-23).

    Bản cũ `round(r2, 6)` biến một đường gần-hoàn-hảo thành đúng `1.0` và gộp các tổ hợp
    khác nhau ở đỉnh bảng thành "hoà" giả — thứ hạng #1 khi đó do `slope` quyết định chứ
    không do độ khớp. Kèm theo: `se` (sai số dư) phải có và KHÔNG bão hoà như R².
    """
    # lệch 1 đơn vị trên dải raw ~12000 (cỡ máy thật): R² = 0,99999999 — làm tròn 6 chữ ra ĐÚNG 1.0
    fit = calib.linear_fit([300, 200, 100, 0], [12001, 9000, 6000, 3000])
    assert fit["r2"] < 1.0, "R² của đường KHÔNG hoàn hảo không được thành đúng 1.0"
    assert round(fit["r2"], 6) == 1.0, "đúng là ca mà bản cũ làm tròn thành 1.0"
    assert fit["se"] is not None and fit["se"] > 0

    # khớp hoàn hảo thật thì R² = 1.0 và se = 0
    perfect = calib.linear_fit([300, 200, 100, 0], [12000, 9000, 6000, 3000])
    assert perfect["r2"] == 1.0 and perfect["se"] == 0.0

    # 2 điểm: luôn khớp hoàn hảo, KHÔNG còn bậc tự do → se = None (đừng in 0)
    assert calib.linear_fit([0, 300], [300, 1200])["se"] is None

    # se phân biệt được hai tổ hợp mà R² nhìn như nhau ở 6 chữ số
    a = calib.linear_fit([300, 200, 100, 0], [12001, 9000, 6000, 3000])
    b = calib.linear_fit([300, 200, 100, 0], [12003, 9000, 6000, 3000])
    assert round(a["r2"], 6) == round(b["r2"], 6) == 1.0
    assert a["se"] < b["se"]


def test_xep_hang_to_hop_va_goi_y_bo():
    res = calib.rank_combinations([300, 200, 100, 0], SHEET)
    assert res["total"] == 81 and res["pass"] >= 3
    best = res["combos"][0]
    assert best["rank"] == 1 and best["tubes"] == {"300": "3", "200": "2", "100": "3", "0": "3"}
    assert best["slope"] == 3.033 and best["status"] == "PASS"
    # R² giảm dần
    r2s = [c["r2"] for c in res["combos"]]
    assert r2s == sorted(r2s, reverse=True)
    # gợi ý bộ: không dùng chung ống, tối đa 3 bộ (3 ống/nồng độ)
    sets = res["suggested_sets"]
    assert 1 <= len(sets) <= 3
    seen = set()
    for s in sets:
        for ck, no in s["tubes"].items():
            assert (ck, no) not in seen
            seen.add((ck, no))
    # top cắt bớt nhưng total giữ nguyên
    assert len(calib.rank_combinations([300, 200, 100, 0], SHEET, top=5)["combos"]) == 5


def test_xep_hang_bo_qua_ong_chua_do_va_nguong():
    part = {"300": {"1": 1193, "2": None}, "200": {"1": 905}, "100": {"1": 598}, "0": {"1": 312}}
    res = calib.rank_combinations([300, 200, 100, 0], part)
    assert res["total"] == 1
    # thiếu hẳn một nồng độ → 0 tổ hợp, báo nồng độ thiếu
    res = calib.rank_combinations([300, 200, 100, 0], {"300": {"1": 1}})
    assert res["total"] == 0 and set(res["missing"]) == {"200", "100", "0"}
    # ngưỡng chặt → FAIL
    res = calib.rank_combinations([300, 200, 100, 0], SHEET, {"r2_min": 0.99999})
    assert res["pass"] == 0 and res["suggested_sets"] == []
    # giới hạn slope
    res = calib.rank_combinations([300, 200, 100, 0], SHEET, {"r2_min": 0.9, "slope_min": 3.0})
    assert all(c["slope"] >= 3.0 for c in res["combos"] if c["status"] == "PASS")


def test_blank_va_lod_theo_template_wi():
    # Template LOD: SNR3.3 = STDEV.S(blank)*3.3, LOD = SNR3.3/slope. Blank mẫu trong WI:
    # 0,2,5,3,2,4,5,6,8,10 → mean 4,5; SD mẫu 2,991 → SNR 9,87 (ảnh template ghi 9,87);
    # slope 0,973 → LOD 10,1 (template ghi 10,1).
    b = calib.blank_stats([0, 2, 5, 3, 2, 4, 5, 6, 8, 10])
    assert b["n"] == 10 and b["mean"] == 4.5 and abs(b["sd"] - 2.991) < 0.001
    assert abs(b["snr33"] - 9.87) < 0.01
    assert abs(calib.lod_nM(b["snr33"], 0.973) - 10.14) < 0.01
    assert calib.blank_stats([5])["sd"] is None and calib.lod_nM(None, 3) is None
    assert calib.lod_nM(9.99, 0) is None
    # Sheet bàn giao: 3 blank 312/297/302 → SD 7,64 → SNR 25,2; slope 3,033 → LOD 8,3 nM (PASS)
    res = calib.rank_combinations([300, 200, 100, 0], SHEET)
    assert res["blank"]["n"] == 3 and abs(res["blank"]["sd"] - 7.638) < 0.001
    assert abs(res["combos"][0]["lod"] - 8.31) < 0.01 and res["combos"][0]["status"] == "PASS"
    # lod_max chặt → FAIL dù R² tốt; lod_max = 0 → không xét
    res = calib.rank_combinations([300, 200, 100, 0], SHEET, {"lod_max": 5})
    assert res["pass"] == 0
    res = calib.rank_combinations([300, 200, 100, 0], SHEET, {"lod_max": 0})
    assert res["pass"] > 0
    # blank chỉ 1 ống → lod None, không đánh trượt
    one = {**SHEET, "0": {"1": 312}}
    res = calib.rank_combinations([300, 200, 100, 0], one, {"lod_max": 1})
    assert res["combos"][0]["lod"] is None and res["pass"] > 0


def test_validate_limits():
    assert calib.validate_limits({"version": "v1"}) is None
    assert "version" in calib.validate_limits({})
    assert "r2_min" in calib.validate_limits({"version": "v1", "r2_min": 1.5})
    assert "lod_max" in calib.validate_limits({"version": "v1", "lod_max": -1})
    assert "slope" in calib.validate_limits({"version": "v1", "slope_min": 5, "slope_max": 2})
    assert calib.limits_conflict({"version": "v1", "r2_min": 0.99},
                                 {"version": "v1", "r2_min": 0.98})
    assert calib.limits_conflict({"version": "v1", "r2_min": 0.99},
                                 {"version": "v1", "r2_min": 0.99, "updated_by": "x"}) is None


# --- API --------------------------------------------------------------------------------

def test_calib_can_token(monkeypatch):
    assert client.get("/calib/batches").status_code == 401
    # Đặt OTA_ADMIN_TOKEN qua monkeypatch (KHÔNG setdefault env ở đầu file: test_ota_products
    # chạy chung sẽ thấy token admin bật và mọi PUT bằng token máy rớt 401).
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    assert client.put("/calib/limits", json={"version": "x"}, headers=AUTH).status_code == 401


def test_template_mac_dinh_va_tham_so(kho):
    r = client.get("/calib/template", headers=AUTH)
    assert r.status_code == 200
    t = r.json()
    assert t["concentrations"] == [300, 200, 100, 0] and t["tubes_per_conc"] == 10
    assert t["stock"]["conc_nM"] == 52000 and t["stock"]["catalog"] == "F36915"
    r = client.get("/calib/template", params={"stock_nM": 26000}, headers=AUTH)
    d = {s["code"]: s for s in r.json()["steps"]}
    assert d["dil.1000"]["factor"] == 26


def test_luong_day_du_lo_den_bo_ong(kho):
    # 1. tạo lô
    r = client.put("/calib/batches", params={"by": "khai"},
                   json={"stock": {"lot": "FAM-2409"}, "note": "lô thử"}, headers=AUTH)
    assert r.status_code == 200, r.text
    b = r.json()
    bid = b["id"]
    assert bid.startswith("CB") and bid.endswith("-01") and b["status"] == "prep"
    assert b["stock"]["lot"] == "FAM-2409" and b["created_by"] == "khai"
    assert (kho / "batches" / f"{bid}.json").is_file()
    # lô thứ hai trong ngày tăng số
    assert client.put("/calib/batches", headers=AUTH).json()["id"].endswith("-02")
    lst = client.get("/calib/batches", headers=AUTH).json()["items"]
    assert [m["id"] for m in lst][-1] == bid and lst[-1]["readings_total"] == 40

    # 2. ghi bước pha thực tế
    r = client.put(f"/calib/batches/{bid}", params={"by": "khai"}, headers=AUTH,
                   json={"steps": [{"code": "dil.1000", "done_at": "2026-09-21T02:00:00Z",
                                    "actual_dye_ul": "10.2"}],
                         "reader": {"device": "RPL0001", "slot": 3}})
    assert r.status_code == 200, r.text
    st = {s["code"]: s for s in r.json()["steps"]}
    assert st["dil.1000"]["actual_dye_ul"] == 10.2 and st["dil.1000"]["done_at"]
    assert r.json()["reader"] == {"device": "RPL0001", "slot": 3, "fw": "", "note": ""}
    # bước lạ → 400
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"steps": [{"code": "xx"}]}).status_code == 400

    # 3. số đo: gộp từng ống, null = xoá, sai nồng độ/số ống → 400
    r = client.put(f"/calib/batches/{bid}", headers=AUTH, json={"readings": SHEET})
    assert r.status_code == 200 and r.json()["status"] == "measure"
    r = client.put(f"/calib/batches/{bid}", headers=AUTH,
                   json={"readings": {"300": {"4": 1200, "1": None}}})
    assert r.json()["readings"]["300"] == {"2": 1179, "3": 1213, "4": 1200}
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"readings": {"500": {"1": 1}}}).status_code == 400
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"readings": {"300": {"11": 1}}}).status_code == 400
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"readings": {"300": {"1": "abc"}}}).status_code == 400
    client.put(f"/calib/batches/{bid}", headers=AUTH,
               json={"readings": {"300": {"1": 1193, "4": None}}})

    # 4. xếp hạng
    r = client.get(f"/calib/batches/{bid}/rank", params={"top": 10}, headers=AUTH)
    assert r.status_code == 200, r.text
    rk = r.json()
    assert rk["total"] == 81 and len(rk["combos"]) == 10
    assert rk["combos"][0]["tubes"] == {"300": "3", "200": "2", "100": "3", "0": "3"}
    assert rk["limits"]["source"] == "mặc định" and rk["tubes_in_sets"] == []

    # 5. đóng gói 2 bộ gợi ý đầu
    picks = rk["suggested_sets"][:2]
    r = client.put(f"/calib/batches/{bid}/sets", params={"by": "khai"}, headers=AUTH,
                   json={"combos": [{"tubes": p["tubes"], "rank": p["rank"]} for p in picks]})
    assert r.status_code == 200, r.text
    ids = r.json()["sets"]
    assert ids == [f"{bid}-S01", f"{bid}-S02"]
    s1 = r.json()["items"][0]
    assert s1["slope"] == 3.033 and s1["status"] == "stored" and s1["verdict"] == "PASS"
    assert s1["lod"] is not None and s1["blank"]["n"] == 3
    assert s1["expires_at"]  # shelf_days mặc định 90
    # ống đã đóng bộ → không đóng lại được, và không còn trong gợi ý
    r = client.put(f"/calib/batches/{bid}/sets", headers=AUTH,
                   json={"combos": [{"tubes": picks[0]["tubes"]}]})
    assert r.status_code == 409
    rk2 = client.get(f"/calib/batches/{bid}/rank", headers=AUTH).json()
    assert "300/3" in rk2["tubes_in_sets"]
    assert all(s["tubes"] != picks[0]["tubes"] for s in rk2["suggested_sets"])
    b = client.get(f"/calib/batches/{bid}", headers=AUTH).json()
    assert b["status"] == "ranked" and b["n_sets"] == 2 and len(b["sets"]) == 2
    # lô có bộ → không xoá được
    assert client.delete(f"/calib/batches/{bid}", headers=AUTH).status_code == 409

    # 6. vòng đời bộ ống
    sid = ids[0]
    assert client.put(f"/calib/sets/{sid}", params={"status": "issued"},
                      headers=AUTH).status_code == 400          # thiếu device
    r = client.put(f"/calib/sets/{sid}", params={"status": "issued", "device": "RPL0042",
                                                  "by": "khai"}, headers=AUTH)
    assert r.status_code == 200 and r.json()["device"] == "RPL0042"
    assert client.get("/calib/sets", params={"device": "rpl0042"},
                      headers=AUTH).json()["items"][0]["id"] == sid
    assert client.get("/calib/sets", params={"status": "stored"},
                      headers=AUTH).json()["items"][0]["id"] == ids[1]
    r = client.put(f"/calib/sets/{sid}", params={"status": "used"}, headers=AUTH)
    assert r.status_code == 200 and r.json()["device"] == "RPL0042"
    assert len(r.json()["history"]) == 3
    # used là trạng thái cuối
    assert client.put(f"/calib/sets/{sid}", params={"status": "stored"},
                      headers=AUTH).status_code == 409
    assert client.put(f"/calib/sets/{sid}", params={"status": "bay"},
                      headers=AUTH).status_code == 400
    assert client.get("/calib/sets/khong-co", headers=AUTH).status_code == 404

    # 7. đóng lô → không sửa được nữa trừ status
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"status": "closed"}).status_code == 200
    assert client.put(f"/calib/batches/{bid}", headers=AUTH,
                      json={"note": "x"}).status_code == 409
    # nhật ký có đủ loại thao tác
    log = (kho / "history.jsonl").read_text(encoding="utf-8")
    for kind in ("batch.create", "batch.update", "set.create", "set.status"):
        assert kind in log


def test_xoa_lo_tao_nham(kho):
    bid = client.put("/calib/batches", headers=AUTH).json()["id"]
    assert client.delete(f"/calib/batches/{bid}", headers=AUTH).status_code == 200
    assert client.get(f"/calib/batches/{bid}", headers=AUTH).status_code == 404
    assert client.get("/calib/batches/../x", headers=AUTH).status_code in (400, 404, 405)


def test_nguong_pass_admin(kho, monkeypatch):
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    assert client.put("/calib/limits", json={"version": "v1", "r2_min": 0.99999},
                      headers=AUTH).status_code == 401
    r = client.put("/calib/limits", params={"by": "root"}, headers=ADMIN,
                   json={"version": "v1", "r2_min": 0.99999, "shelf_days": 30})
    assert r.status_code == 200
    lim = client.get("/calib/limits", headers=AUTH).json()
    assert lim["r2_min"] == 0.99999 and lim["source"] == "file" and lim["updated_by"] == "root"
    # cùng version khác nội dung → 400
    assert client.put("/calib/limits", headers=ADMIN,
                      json={"version": "v1", "r2_min": 0.99}).status_code == 400
    # ngưỡng chặt → gợi ý rỗng; bộ tạo ra vẫn ghi limits_ver + verdict
    bid = client.put("/calib/batches", headers=AUTH).json()["id"]
    client.put(f"/calib/batches/{bid}", headers=AUTH, json={"readings": SHEET})
    rk = client.get(f"/calib/batches/{bid}/rank", headers=AUTH).json()
    assert rk["pass"] == 0 and rk["suggested_sets"] == [] and rk["limits"]["version"] == "v1"
    r = client.put(f"/calib/batches/{bid}/sets", headers=AUTH,
                   json={"combos": [{"tubes": rk["combos"][0]["tubes"]}], "expires_days": 0})
    s = r.json()["items"][0]
    assert s["verdict"] == "FAIL" and s["limits_ver"] == "v1" and s["expires_at"] == ""


def test_lo_tuy_chinh_nong_do_va_so_ong(kho):
    r = client.put("/calib/batches", headers=AUTH,
                   json={"concentrations": [400, 200, 0], "tubes_per_conc": 5, "aliquot_ul": 20})
    assert r.status_code == 200, r.text
    b = r.json()
    assert b["concentrations"] == [400, 200, 0] and set(b["readings"]) == {"400", "200", "0"}
    assert any(s["code"] == "dil.400" for s in b["steps"])
    assert client.put("/calib/batches", headers=AUTH,
                      json={"concentrations": [300]}).status_code == 400
    assert client.put("/calib/batches", headers=AUTH,
                      json={"tubes_per_conc": 99}).status_code == 400
