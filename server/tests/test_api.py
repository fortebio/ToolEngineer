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
# setdefault: test_ota_products.py đặt cùng bộ env; module nào import app.main trước
# cũng phải ra CÙNG thư mục, không thì module sau trỏ vào thư mục app không dùng.
os.environ.setdefault("FBT_DATA_DIR", tempfile.mkdtemp(prefix="fbt_test_"))
os.environ.setdefault("FBT_OTA_DIR", tempfile.mkdtemp(prefix="fbt_ota_test_"))
os.environ.setdefault("FBT_LOGS_DIR", tempfile.mkdtemp(prefix="fbt_logs_test_"))
os.environ.setdefault("FBT_ATE_DIR", tempfile.mkdtemp(prefix="fbt_ate_test_"))
os.environ.setdefault("RECEIVER_TOKEN", "testtok")
os.environ.setdefault("FBT_DB", "dbname=__nope__ connect_timeout=1")  # DB không tồn tại -> reads 500

from fastapi.testclient import TestClient  # noqa: E402

from app.main import app  # noqa: E402

DATA = Path(os.environ["FBT_DATA_DIR"])
OTA = Path(os.environ["FBT_OTA_DIR"])
ATE = Path(os.environ["FBT_ATE_DIR"])
SAMPLE = json.loads((ROOT / "docs" / "data_sample" / "data_RPL.json").read_text(encoding="utf-8"))
SAMPLE_READER = json.loads((ROOT / "docs" / "data_sample" / "data_reader.json").read_text(encoding="utf-8"))
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


def test_ingest_reader_theo_hop_dong():
    """Reader (1 khe) gửi theo system/contracts/ingest-reader.schema.json — server nhận KHÔNG cần sửa code
    (catch-all + validate N khe với slots=1). Mẫu phải qua chính schema đó, để hợp đồng ↔ mẫu ↔ server
    không lệch nhau âm thầm; firmware có guard riêng đối chiếu code với `required` của schema."""
    import jsonschema
    schema = json.loads((ROOT.parent / "system" / "contracts" / "ingest-reader.schema.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(SAMPLE_READER)
    r = client.post("/reader/results", json=SAMPLE_READER, headers=AUTH)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["ok"] is True and body["file"].startswith("RE0012_")
    saved = json.loads((DATA / body["file"]).read_text(encoding="utf-8"))
    assert saved == SAMPLE_READER and saved["type_Upload"] == "reader_result"
    # Mảng khe sai độ dài so với slots=1 → 400 ngay, không lặng lẽ vào DB
    assert client.post("/reader/results", json=dict(SAMPLE_READER, slot_result=[805, 814, 817]), headers=AUTH).status_code == 400
    # Lần đo lỗi cảm biến (quyết định 2026-09-18: vẫn gửi) cũng phải qua schema và server
    err = dict(SAMPLE_READER, verdict="E", slot_positive=[False], readings_ok=[True, False, True], readings=[805, 0, 817])
    jsonschema.Draft202012Validator(schema).validate(err)
    assert client.post("/reader/results", json=err, headers=AUTH).status_code == 200


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
    # Kho tách theo sản phẩm (2026-09-11): đường `/ota/<file>` kiểu cũ đọc kho LEGACY_PRODUCT.
    legacy = OTA / "products" / "rapidplus"
    legacy.mkdir(parents=True, exist_ok=True)
    (legacy / "firmware.bin").write_bytes(b"firmware")
    assert client.get("/ota/firmware.bin").status_code == 401
    r = client.get("/ota/firmware.bin", headers=AUTH)
    assert r.status_code == 200
    assert r.content == b"firmware"
    assert r.headers["content-type"] == "application/octet-stream"
    # httpx chuan hoa ".." TRUOC khi gui -> request thanh GET /note.md, khop catch-all
    # POST /{path} sai method => 405. Path khong toi handler, khong lo file (xem CLAUDE.md).
    assert client.get("/ota/../../note.md", headers=AUTH).status_code in (404, 405)
    assert client.get("/ota/fw.txt", headers=AUTH).status_code == 404


# --- Log máy CSKH gửi lên (PUT /devices/{id}/logs, GET .../logs, GET /logs/{file}) ---
LOGS = Path(os.environ["FBT_LOGS_DIR"])
LOG_BODY = {
    "by": "cskh01",
    "note": "Khách báo máy tự tắt giữa chừng",
    "port": "COM5",
    "baud": 115200,
    "captured_at": "2026-09-04T10:00:00",
    "app": "FBT_RAPID",
    "findings": [{"level": "error", "key": "brownout", "count": 2}],
    "text": "rst:0x1 (POWERON_RESET)\nBrownout detector was triggered\nversion 2.4.5\n",
}


def test_log_upload_can_token():
    assert client.put("/devices/RPL02013/logs", json=LOG_BODY).status_code == 401


def test_log_post_roi_vao_catchall():
    # POST bị catch-all ingest nuốt -> 400 (payload thiết bị không hợp lệ). Đây là LÝ DO app dùng PUT.
    r = client.post("/devices/RPL02013/logs", json=LOG_BODY, headers=AUTH)
    assert r.status_code == 400


def test_log_upload_ghi_file():
    r = client.put("/devices/RPL02013/logs", json=LOG_BODY, headers=AUTH)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["ok"] is True and body["file"].startswith("RPL02013_") and body["file"].endswith(".json")
    assert body["size"] == len(LOG_BODY["text"])
    doc = json.loads((LOGS / body["file"]).read_text(encoding="utf-8"))
    assert doc["device"] == "RPL02013" and doc["text"] == LOG_BODY["text"]
    assert doc["by"] == "cskh01" and doc["note"] == LOG_BODY["note"] and doc["baud"] == 115200
    assert doc["findings"] == LOG_BODY["findings"] and doc["received_at"]
    assert not list(LOGS.glob(".tmp-*"))  # ghi tạm rồi đổi tên, không để rác


def test_log_upload_thieu_text_400():
    assert client.put("/devices/RPL02013/logs", json={"by": "x"}, headers=AUTH).status_code == 400
    assert client.put("/devices/RPL02013/logs", json={"text": "   "}, headers=AUTH).status_code == 400
    assert client.put("/devices/RPL02013/logs", json=[1, 2], headers=AUTH).status_code == 400
    r = client.put("/devices/RPL02013/logs", content=b"{oops", headers={**AUTH, "Content-Type": "application/json"})
    assert r.status_code == 400


def test_log_list_va_get():
    up = client.put("/devices/RPL09999/logs", json=dict(LOG_BODY, note="lần 1"), headers=AUTH).json()
    r = client.get("/devices/RPL09999/logs", headers=AUTH)
    assert r.status_code == 200
    body = r.json()
    assert body["device"] == "RPL09999"
    files = [i["file"] for i in body["items"]]
    assert up["file"] in files
    item = next(i for i in body["items"] if i["file"] == up["file"])
    assert item["note"] == "lần 1" and item["by"] == "cskh01" and item["size"] == len(LOG_BODY["text"])
    assert item["findings"] == 1 and "text" not in item  # danh sách KHÔNG kèm nội dung
    # máy khác không thấy log máy này
    assert up["file"] not in [i["file"] for i in client.get("/devices/RPL02013/logs", headers=AUTH).json()["items"]]
    # tải bản đầy đủ
    g = client.get(f"/logs/{up['file']}", headers=AUTH)
    assert g.status_code == 200 and g.json()["text"] == LOG_BODY["text"]
    assert client.get(f"/logs/{up['file']}").status_code == 401


def test_log_get_ten_file_xau():
    assert client.get("/logs/khong_co.json", headers=AUTH).status_code == 404
    assert client.get("/logs/..%2F..%2Fetc%2Fpasswd", headers=AUTH).status_code in (400, 404, 405)
    assert client.get("/logs/abc.txt", headers=AUTH).status_code == 400


def test_log_device_path_traversal_lam_sach():
    r = client.put("/devices/RPL02013%5C..%5C..%5Cetc/logs", json=LOG_BODY, headers=AUTH)
    # safe_name làm sạch mã máy -> file nằm TRONG LOGS_DIR, không leo thư mục
    assert r.status_code == 200, r.text
    assert r.json()["file"].startswith("RPL02013_.._.._etc_")
    assert not (LOGS.parent / "etc").exists()
    assert all(p.parent == LOGS for p in LOGS.glob("*.json"))


# --- Hộp thư log cho kỹ thuật: GET /logs, PUT /logs/{f}/status, DELETE /logs/{f} ---
# (ghép từ code chỉ có trên box 2026-09-23). Gác ota_admin → token thiết bị bị 401 khi
# OTA_ADMIN_TOKEN đặt; monkeypatch trong test, KHÔNG setdefault env (xem test_calib).
ADMIN = {"Authorization": "Bearer admintok"}


def test_log_hop_thu_gac_ota_admin(monkeypatch):
    from app import config
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    up = client.put("/devices/RPL07777/logs", json=LOG_BODY, headers=AUTH).json()
    assert client.get("/logs", headers=AUTH).status_code == 401
    assert client.put(f"/logs/{up['file']}/status", json={"status": "done"}, headers=AUTH).status_code == 401
    assert client.delete(f"/logs/{up['file']}", headers=AUTH).status_code == 401
    assert client.get("/logs", headers=ADMIN).status_code == 200
    # token thiết bị vẫn gửi + đọc theo máy được như cũ
    assert client.get(f"/logs/{up['file']}", headers=AUTH).status_code == 200


def test_log_hop_thu_loc_trang_thai_va_xoa(monkeypatch):
    from app import config
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    a = client.put("/devices/RPL08881/logs", json=dict(LOG_BODY, text="log a\n"), headers=AUTH).json()
    b = client.put("/devices/RPL08881/logs", json=dict(LOG_BODY, text="log b\n"), headers=AUTH).json()

    r = client.get("/logs", params={"device": "RPL08881"}, headers=ADMIN).json()
    assert r["total"] == 2 and r["counts"] == {"new": 2, "working": 0, "done": 0}
    assert all("text" not in i and i["status"] == "new" for i in r["items"])

    s = client.put(f"/logs/{a['file']}/status", json={"status": "done", "by": "kt01", "note": "thay nguồn"},
                   headers=ADMIN)
    assert s.status_code == 200, s.text
    assert s.json()["status"] == "done" and s.json()["status_by"] == "kt01"
    doc = json.loads((LOGS / a["file"]).read_text(encoding="utf-8"))
    assert doc["status"] == "done" and doc["text"] == "log a\n" and list(doc)[-1] == "text"

    r = client.get("/logs", params={"device": "RPL08881", "status": "new"}, headers=ADMIN).json()
    assert [i["file"] for i in r["items"]] == [b["file"]]
    assert r["counts"] == {"new": 1, "working": 0, "done": 1}  # counts trước lọc trạng thái

    assert client.put(f"/logs/{a['file']}/status", json={"status": "xyz"}, headers=ADMIN).status_code == 400
    assert client.get("/logs", params={"status": "xyz"}, headers=ADMIN).status_code == 400
    assert client.put("/logs/khong_co.json/status", json={"status": "done"}, headers=ADMIN).status_code == 404

    assert client.delete(f"/logs/{b['file']}", headers=ADMIN).json()["ok"] is True
    assert not (LOGS / b["file"]).exists()
    assert client.delete(f"/logs/{b['file']}", headers=ADMIN).status_code == 404
    assert client.get("/logs", params={"device": "RPL08881"}, headers=ADMIN).json()["total"] == 1


def test_log_meta_fw_va_dau_hieu(monkeypatch):
    """`fw` + mức dấu hiệu vào metadata (2026-09-25) — hộp thư tô màu, CSKH thấy trạng thái
    qua route theo máy (token thiết bị)."""
    from app import config
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    body = dict(LOG_BODY, text="meta fw\n", fw="2.4.5", findings=[
        {"level": "error", "key": "brownout", "count": 2},
        {"level": "warning", "key": "wifi", "count": 1},
        {"level": "info", "key": "resetPower", "count": 1},
        {"level": "??", "key": "la"}, "rác", {"level": "error"},
    ])
    up = client.put("/devices/RPL06661/logs", json=body, headers=AUTH).json()
    client.put(f"/logs/{up['file']}/status", json={"status": "working", "by": "kt02", "note": "đang xem"},
               headers=ADMIN)
    item = client.get("/devices/RPL06661/logs", headers=AUTH).json()["items"][0]
    assert item["fw"] == "2.4.5" and item["errors"] == 1 and item["warnings"] == 1
    assert item["keys"] == ["brownout", "wifi"] and item["error_keys"] == ["brownout"]
    assert item["status"] == "working" and item["status_by"] == "kt02" and item["status_note"] == "đang xem"


def test_log_stats(monkeypatch):
    from app import config
    monkeypatch.setattr(config, "OTA_ADMIN_TOKEN", "admintok")
    err = [{"level": "error", "key": "zzStatErr", "count": 1}]
    warn = [{"level": "warning", "key": "zzStatErr", "count": 1}]  # cùng khoá, mức nhẹ hơn
    client.put("/devices/RPL05551/logs", json=dict(LOG_BODY, text="s1\n", fw="9.9.1", findings=err), headers=AUTH)
    client.put("/devices/RPL05551/logs", json=dict(LOG_BODY, text="s2\n", fw="9.9.1", findings=warn), headers=AUTH)
    client.put("/devices/RPL05552/logs", json=dict(LOG_BODY, text="s3\n", fw="9.9.2", findings=[]), headers=AUTH)

    assert client.get("/logs/stats", headers=AUTH).status_code == 401  # token thiết bị không xem
    r = client.get("/logs/stats", headers=ADMIN)
    assert r.status_code == 200, r.text  # KHÔNG rơi vào GET /logs/{filename} (400)
    s = r.json()
    assert s["days"] == 90 and s["since"] and s["total"] >= 3 and s["clean"] >= 1
    assert s["device_count"] >= 2
    assert sum(s["counts"].values()) == s["total"]
    sign = next(x for x in s["signs"] if x["key"] == "zzStatErr")
    assert sign == {"key": "zzStatErr", "level": "error", "logs": 2, "devices": 1}
    dev = next(x for x in s["devices"] if x["device"] == "RPL05551")
    assert dev["logs"] == 2 and dev["with_errors"] == 1 and dev["new"] == 2 and dev["last"]
    fw = next(x for x in s["firmware"] if x["fw"] == "9.9.1")
    assert fw == {"fw": "9.9.1", "logs": 2, "with_errors": 1, "devices": 1}
    assert s["daily"] and sum(d["logs"] for d in s["daily"]) == s["total"]
    assert client.get("/logs/stats", params={"days": 0}, headers=ADMIN).json()["since"] == ""
    assert client.get("/logs/stats", params={"days": -1}, headers=ADMIN).status_code == 422


def test_log_bao_telegram(monkeypatch):
    """Có đủ 2 env → gửi tin (thread); lỗi mạng KHÔNG làm hỏng việc gửi log."""
    import threading as th
    from app import config, main
    sent = []
    done = th.Event()

    def fake_send(text):
        sent.append(text)
        done.set()
        raise OSError("mạng chặn")  # lỗi phải bị nuốt

    monkeypatch.setattr(main, "_telegram_send", fake_send)
    # Chưa bật → không gửi
    monkeypatch.setattr(config, "LOG_NOTIFY_TELEGRAM_TOKEN", "")
    monkeypatch.setattr(config, "LOG_NOTIFY_TELEGRAM_CHAT", "")
    client.put("/devices/RPL04441/logs", json=dict(LOG_BODY, text="n0\n"), headers=AUTH)
    assert sent == []

    monkeypatch.setattr(config, "LOG_NOTIFY_TELEGRAM_TOKEN", "123:abc")
    monkeypatch.setattr(config, "LOG_NOTIFY_TELEGRAM_CHAT", "-100")
    monkeypatch.setattr(config, "LOG_NOTIFY_APP_URL", "https://hub.example/app/")
    body = dict(LOG_BODY, text="n1\n", fw="2.4.6", note="máy tắt ngang",
                findings=[{"level": "error", "key": "brownout", "count": 1}])
    r = client.put("/devices/RPL04441/logs", json=body, headers=AUTH)
    assert r.status_code == 200
    assert done.wait(5)
    msg = sent[0]
    assert "RPL04441" in msg and "cskh01" in msg and "fw 2.4.6" in msg and "máy tắt ngang" in msg
    assert "🔴 brownout" in msg and r.json()["file"] in msg and "https://hub.example/app/" in msg




# --- Trạm ATE (tab "Sản xuất" của app) ---------------------------------------

ATE_REC = {
    "sn": "RPL02013",
    "station": "TRAM-01",
    "operator": "cskh",
    "fw_version": "v2.4.4",
    "fw_sha256": "a" * 64,
    "limits_ver": "p0-2026-09-07",
    "started_at": "2026-09-07T01:00:00+00:00",
    "finished_at": "2026-09-07T01:04:00+00:00",
    "verdict": "pass",
    "steps": [
        {"code": "FW-01", "name": "Nạp firmware", "verdict": "pass"},
        {"code": "ID-01", "name": "Ghi số máy", "verdict": "pass", "value": 1},
    ],
}


def test_ate_put_can_token():
    assert client.put("/ate/records", json=ATE_REC).status_code == 401


def test_ate_post_roi_vao_catchall():
    """POST /ate/records rơi vào `POST /{_path}` (ingest) -> 400. Đây là lý do
    route ghi hồ sơ dùng PUT."""
    r = client.post("/ate/records", json=ATE_REC, headers=AUTH)
    assert r.status_code == 400 and "id_device" in r.text


def test_ate_put_ghi_file_va_idempotent():
    r1 = client.put("/ate/records", json=ATE_REC, headers=AUTH)
    assert r1.status_code == 200, r1.text
    body = r1.json()
    assert body["ok"] is True and body["sn"] == "RPL02013" and body["verdict"] == "pass"
    assert body["id"].startswith("RPL02013_20260907_010000_")
    before = len(list(ATE.glob("*.json")))
    r2 = client.put("/ate/records", json=ATE_REC, headers=AUTH)   # đẩy lại y hệt
    assert r2.json()["id"] == body["id"]
    assert len(list(ATE.glob("*.json"))) == before  # ghi đè, KHÔNG sinh hồ sơ trùng
    doc = json.loads((ATE / body["id"]).read_text(encoding="utf-8"))
    assert doc["fail_code"] == "" and doc["received_at"]


def test_ate_put_body_xau_400():
    assert client.put("/ate/records", json={"sn": "X"}, headers=AUTH).status_code == 400
    assert client.put("/ate/records", json=dict(ATE_REC, verdict="ok"),
                      headers=AUTH).status_code == 400
    assert client.put("/ate/records", json=dict(ATE_REC, steps=[]),
                      headers=AUTH).status_code == 400


def test_ate_list_get_va_sn():
    fail = dict(ATE_REC, sn="RPL09999", verdict="fail",
                started_at="2026-09-07T02:00:00+00:00",
                steps=[{"code": "BOOT-01", "name": "Khởi động", "verdict": "fail",
                        "detail": "Guru Meditation"}])
    fid = client.put("/ate/records", json=fail, headers=AUTH).json()["id"]
    client.put("/ate/records", json=ATE_REC, headers=AUTH)

    r = client.get("/ate/records", headers=AUTH).json()
    assert r["total"] >= 2 and r["page"] == 1
    assert r["items"][0]["started_at"] >= r["items"][-1]["started_at"]  # mới nhất trước
    assert "steps" not in r["items"][0] and r["items"][0]["steps_total"] >= 1

    only_fail = client.get("/ate/records?verdict=fail", headers=AUTH).json()
    assert only_fail["total"] >= 1
    assert all(i["verdict"] == "fail" for i in only_fail["items"])
    assert only_fail["items"][0]["fail_code"] == "BOOT-01"   # tự suy từ steps

    by_sn = client.get("/ate/records?sn=rpl09999", headers=AUTH).json()
    assert by_sn["total"] == 1 and by_sn["items"][0]["sn"] == "RPL09999"
    assert client.get("/ate/records?from=2030-01-01", headers=AUTH).json()["total"] == 0
    assert client.get("/ate/records?from=garbage", headers=AUTH).status_code == 400

    full = client.get(f"/ate/records/{fid}", headers=AUTH).json()
    assert full["steps"][0]["detail"] == "Guru Meditation"

    sn = client.get("/ate/sn/RPL02013", headers=AUTH).json()
    assert sn["attempts"] >= 1 and sn["birth"]["verdict"] == "pass"
    assert client.get("/ate/sn/KHONG_CO", headers=AUTH).json() == {
        "sn": "KHONG_CO", "records": [], "attempts": 0, "birth": None}


def test_ate_record_ten_file_xau():
    assert client.get("/ate/records/khong_co.json", headers=AUTH).status_code == 404
    assert client.get("/ate/records/abc.txt", headers=AUTH).status_code == 400


def test_ate_stats():
    s = client.get("/ate/stats", headers=AUTH).json()
    assert s["machines"] >= 1 and 0 <= s["fpy"] <= 1
    assert isinstance(s["pareto"], list) and isinstance(s["by_day"], list)
    assert client.get("/ate/stats?to=2030-13-01", headers=AUTH).status_code == 400


def test_ate_limits_mac_dinh_va_put():
    d = client.get("/ate/limits", headers=AUTH).json()
    assert d["version"] and d["sn_max_len"] == 9      # bộ mặc định P0+P1
    # Ngưỡng quang CHƯA CHỐT phải là null (app chuyển sang chế độ chỉ ghi số),
    # không được lỡ tay điền một con số đoán.
    assert d["bright_min"] is None and d["bright_spread_pct"] is None
    assert d["temp_spread_c"] == 2 and d["ambient_c"] is None
    assert client.put("/ate/limits", json={"sn_max_len": 9},
                      headers=AUTH).status_code == 400  # thiếu version
    r = client.put("/ate/limits?by=root", json={"version": "v2", "sn_max_len": 9,
                                                "boot_watch_sec": 20}, headers=AUTH)
    assert r.status_code == 200 and r.json()["version"] == "v2"
    back = client.get("/ate/limits", headers=AUTH).json()
    assert back["version"] == "v2" and back["boot_watch_sec"] == 20
    assert back["updated_by"] == "root" and back["updated_at"]


def test_ate_limits_khong_lot_vao_danh_sach_ho_so():
    """limits.json nằm CÙNG thư mục với hồ sơ — không được đếm như một hồ sơ."""
    ids = [i["id"] for i in client.get("/ate/records", headers=AUTH).json()["items"]]
    assert "limits.json" not in ids


def test_ate_sn_path_traversal_lam_sach():
    r = client.put("/ate/records", json=dict(ATE_REC, sn="../../etc/passwd"), headers=AUTH)
    assert r.status_code == 200, r.text
    assert not (ATE.parent / "etc").exists()
    assert all(p.parent == ATE for p in ATE.glob("*.json"))



def test_ate_limits_theo_lo():
    """Tiêu chuẩn đặt THEO LÔ; lô chưa có bộ riêng thì lùi về bộ chung/mặc định."""
    r = client.put("/ate/limits?batch=L2609A&by=cskh",
                   json={"version": "L2609A-1", "bright_min": 800,
                         "bright_spread_pct": 8, "ambient_c": 28}, headers=AUTH)
    assert r.status_code == 200, r.text
    assert r.json()["batch"] == "L2609A"

    got = client.get("/ate/limits?batch=L2609A", headers=AUTH).json()
    assert got["version"] == "L2609A-1" and got["source"] == "batch"
    assert got["bright_min"] == 800 and got["batch"] == "L2609A"

    # Lô khác chưa khai → lùi về bộ chung (đã PUT ở test trước) hoặc mặc định.
    other = client.get("/ate/limits?batch=L9999Z", headers=AUTH).json()
    assert other["source"] in ("chung", "mặc định")
    assert other["batch"] == "L9999Z"          # vẫn nói rõ đang hỏi cho lô nào
    assert other.get("bright_min") is None      # KHÔNG dính ngưỡng của lô khác

    lst = client.get("/ate/limits/list", headers=AUTH).json()["items"]
    assert any(i["batch"] == "L2609A" and i["version"] == "L2609A-1" for i in lst)


def test_ate_limits_mot_version_mot_noi_dung():
    """Sửa ngưỡng mà giữ nguyên version → 400. Hồ sơ chỉ ghi `limits_ver`."""
    body = {"version": "L2609B-1", "bright_min": 500}
    assert client.put("/ate/limits?batch=L2609B", json=body, headers=AUTH).status_code == 200
    # Gửi LẠI y hệt = idempotent, không phải xung đột.
    assert client.put("/ate/limits?batch=L2609B", json=body, headers=AUTH).status_code == 200
    r = client.put("/ate/limits?batch=L2609B",
                   json={"version": "L2609B-1", "bright_min": 900}, headers=AUTH)
    assert r.status_code == 400 and "đổi version" in r.text
    # Đổi version thì lưu được.
    assert client.put("/ate/limits?batch=L2609B",
                      json={"version": "L2609B-2", "bright_min": 900},
                      headers=AUTH).status_code == 200
    assert client.get("/ate/limits?batch=L2609B", headers=AUTH).json()["bright_min"] == 900


def test_ate_ma_lo_xau_400():
    for bad in ("../../etc", "lô có dấu", "a" * 65, "x/y"):
        assert client.get(f"/ate/limits?batch={bad}", headers=AUTH).status_code == 400, bad
    # Không có file nào leo ra ngoài kho ATE (chỉ .json hồ sơ/ngưỡng + nhật ký .jsonl)
    assert all(p.suffix in (".json", ".jsonl")
               for p in ATE.glob("**/*") if p.is_file())


def test_ate_ho_so_loc_theo_lo():
    a = dict(ATE_REC, sn="RPL03001", batch="L2609A",
             started_at="2026-09-07T04:00:00+00:00")
    b = dict(ATE_REC, sn="RPL03002", batch="L2609B",
             started_at="2026-09-07T04:10:00+00:00")
    client.put("/ate/records", json=a, headers=AUTH)
    client.put("/ate/records", json=b, headers=AUTH)

    r = client.get("/ate/records?batch=L2609A", headers=AUTH).json()
    assert r["total"] >= 1
    assert all(i["batch"] == "L2609A" for i in r["items"])
    assert any(i["sn"] == "RPL03001" for i in r["items"])

    s = client.get("/ate/stats?batch=L2609B", headers=AUTH).json()
    assert s["total"] == 1 and s["machines"] == 1


if __name__ == "__main__":
    ran = 0
    for _name, _fn in sorted(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            _fn()
            ran += 1
            print(f"ok {_name}")
    print(f"{ran} tests passed — {len(list(DATA.glob('*.json')))} file trong {DATA}")


def test_web_app_no_cache_header(tmp_path, monkeypatch):
    """/app/* phải mang Cache-Control: no-cache (font icon cùng URL qua các build — Cloudflare/
    trình duyệt giữ 4 giờ là icon mới thành ô trống); API không bị gắn."""
    from starlette.testclient import TestClient as TC
    from fastapi import FastAPI
    from fastapi.staticfiles import StaticFiles
    import app.main as m
    # app thật chỉ mount /app khi WEB_DIR có lúc import → dựng app con cùng middleware để kiểm
    web = tmp_path / "web"; web.mkdir(); (web / "index.html").write_text("<html>x</html>")
    (web / "assets").mkdir(); (web / "assets" / "f.otf").write_bytes(b"font")
    sub = FastAPI()
    sub.mount("/app", StaticFiles(directory=web, html=True))
    sub.middleware("http")(m._web_no_cache)
    @sub.get("/ota/check")
    def _api():
        return {"update": False}
    c = TC(sub)
    assert c.get("/app/assets/f.otf").headers["cache-control"] == "no-cache"
    assert c.get("/app/").headers["cache-control"] == "no-cache"
    assert "cache-control" not in c.get("/ota/check").headers
    # 304 vẫn hoạt động (ETag từ StaticFiles) — no-cache chỉ bắt hỏi lại, không tải lại
    et = c.get("/app/assets/f.otf").headers["etag"]
    assert c.get("/app/assets/f.otf", headers={"If-None-Match": et}).status_code == 304
