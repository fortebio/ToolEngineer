"""Smoke test API bằng TestClient — không cần Postgres (DB lỗi → db:false, file vẫn ghi).

Chạy: python tests/test_api.py  hoặc  pytest tests/test_api.py
Cần: fastapi, httpx (trong requirements.txt).
"""
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.environ["FBT_DATA_DIR"] = tempfile.mkdtemp(prefix="fbt_test_")
os.environ["FBT_OTA_DIR"] = tempfile.mkdtemp(prefix="fbt_ota_test_")
os.environ["RECEIVER_TOKEN"] = "testtok"
os.environ.setdefault("FBT_DB", "dbname=__nope__ connect_timeout=1")  # DB không tồn tại -> reads 500

from fastapi.testclient import TestClient  # noqa: E402

from app.main import app  # noqa: E402

DATA = Path(os.environ["FBT_DATA_DIR"])
OTA = Path(os.environ["FBT_OTA_DIR"])
SAMPLE = json.loads((ROOT / "docs" / "data_sample" / "data_RPL.json").read_text(encoding="utf-8"))
client = TestClient(app, raise_server_exceptions=False)
AUTH = {"Authorization": "Bearer testtok"}


def test_root_khong_405():
    r = client.get("/")
    assert r.status_code == 200 and r.json()["ok"] is True


def test_ingest_can_token():
    assert client.post("/", json=SAMPLE).status_code == 401


def test_ingest_ghi_file_truoc():
    r = client.post("/duong/dan/bat-ky", json=SAMPLE, headers=AUTH)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["ok"] is True and body["db"] is False and body["file"].startswith("RPL02013_")
    files = list(DATA.glob("*.json"))
    assert len(files) >= 1
    assert json.loads(files[0].read_text(encoding="utf-8")) == SAMPLE  # giữ nguyên nội dung gốc


def test_retry_khong_sinh_file_rac():
    before = len(list(DATA.glob("*.json")))
    r1 = client.post("/", json=SAMPLE, headers=AUTH)
    r2 = client.post("/", json=SAMPLE, headers=AUTH)
    assert r1.json()["file"] == r2.json()["file"]
    assert len(list(DATA.glob("*.json"))) == before  # cùng nội dung -> cùng tên, không thêm file


def test_json_hong_400():
    r = client.post("/", content=b"{oops", headers={**AUTH, "Content-Type": "application/json"})
    assert r.status_code == 400 and "invalid json" in r.text


def test_validate_400():
    assert client.post("/", json={"x": 1}, headers=AUTH).status_code == 400
    assert client.post("/", json=dict(SAMPLE, CT_value=[1.0] * 9), headers=AUTH).status_code == 400


def test_chunked_411():
    def chunks():
        yield json.dumps(SAMPLE).encode()
    r = client.post("/", content=chunks(), headers=AUTH)
    assert r.status_code == 411, f"chunked phải bị chặn 411, got {r.status_code}"


def test_path_traversal_lam_sach():
    r = client.post("/", json=dict(SAMPLE, id_device="../../etc/passwd"), headers=AUTH)
    assert r.status_code == 200
    assert not (DATA.parent / "etc").exists()


def test_sessions_ngay_sai_400():
    # from/to sai định dạng -> 400 ngay ở API (không tới DB), kể cả tháng ngoài khoảng
    assert client.get("/sessions?from=garbage", headers=AUTH).status_code == 400
    assert client.get("/sessions?to=2026-13-01", headers=AUTH).status_code == 400


def test_login_can_token_va_route_rieng():
    # /login CẦN token API (cửa chung); thiếu token -> 401
    assert client.post("/login", json={"username": "x", "password": "y"}).status_code == 401
    # có token + thiếu field -> 422 (route riêng dùng pydantic, không bị catch-all nuốt thành 400)
    assert client.post("/login", json={}, headers=AUTH).status_code == 422


def test_auth_khong_can_token_va_luon_200():
    # /auth KHÔNG gate Bearer (web public không nhúng token được) — mọi action tự gate
    # bằng mật khẩu. LUÔN HTTP 200 + cờ ok (hợp đồng Apps Script — app đọc `ok`).
    r = client.post("/auth", content=b"{oops", headers={"Content-Type": "text/plain"})
    assert r.status_code == 200 and r.json()["ok"] is False
    r = client.post("/auth", json={"action": "bay-nhay"})
    assert r.status_code == 200 and r.json()["ok"] is False
    # DB không có -> vẫn 200 {ok:false} (không 500 làm app hiện "HTTP 500")
    r = client.post("/auth", json={"action": "login", "username": "x", "password": "y"})
    assert r.status_code == 200 and r.json()["ok"] is False
    # KHÔNG bị catch-all ingest nuốt (catch-all yêu cầu token -> 401, và trả {file:...})
    assert "file" not in r.json()
    # Action admin không mật khẩu đúng -> không lộ gì
    r = client.post("/auth", json={"action": "listUsers", "adminUser": "x", "adminPassword": "y"})
    assert r.status_code == 200 and r.json()["ok"] is False


def test_reads_can_token_va_db():
    assert client.get("/devices").status_code == 401          # thiếu token
    assert client.get("/devices", headers=AUTH).status_code == 500   # có token, DB không có -> 500


def test_ota_tai_file_bin_va_chan_path_ban():
    (OTA / "firmware.bin").write_bytes(b"firmware")
    assert client.get("/ota/firmware.bin").status_code == 401
    r = client.get("/ota/firmware.bin", headers=AUTH)
    assert r.status_code == 200
    assert r.content == b"firmware"
    assert r.headers["content-type"] == "application/octet-stream"
    assert client.get("/ota/../../note.md", headers=AUTH).status_code == 404
    assert client.get("/ota/fw.txt", headers=AUTH).status_code == 404


if __name__ == "__main__":
    ran = 0
    for _name, _fn in sorted(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            _fn()
            ran += 1
            print(f"ok {_name}")
    print(f"{ran} tests passed — {len(list(DATA.glob('*.json')))} file trong {DATA}")
