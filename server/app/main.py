"""FastAPI: nhận POST JSON từ thiết bị + API đọc cho app Flutter.

Chạy:  uvicorn app.main:app --host 0.0.0.0 --port 8080   # systemd fbt-receiver
Docs:  https://<domain>/docs                              # Swagger tự sinh

Nguyên tắc: file JSON trong DATA_DIR là NGUỒN CHÂN LÝ — /ingest ghi file trước,
INSERT vào Postgres sau; DB lỗi thì chỉ log, scripts/reconcile.py nạp bù sau.
"""
import json
from datetime import datetime, timezone

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel

from app import auth as accounts, config, db
from app.logic import canonical_sha256, check_auth, payload_time, safe_name, validate, verify_password

app = FastAPI(title="FBT Home Server", version="1.0")
config.DATA_DIR.mkdir(parents=True, exist_ok=True)  # tạo thư mục lưu file 1 lần lúc khởi động
config.OTA_DIR.mkdir(parents=True, exist_ok=True)


def auth(authorization: str = Header("")):
    if not check_auth(authorization, config.TOKEN):
        raise HTTPException(401, "unauthorized")


def _check_date(value: str | None, field: str):
    """Chặn from/to sai định dạng ngay ở API (400) thay vì để SQL cast lỗi (500)."""
    if value is not None:
        try:
            datetime.strptime(value, "%Y-%m-%d")
        except ValueError:
            raise HTTPException(400, f"{field} phải dạng YYYY-MM-DD")


@app.get("/")
def root():
    """Cho nút 'test connection' và người mở base URL — khỏi 405 khó hiểu."""
    return {"ok": True, "service": "FBT Home Server", "docs": "/docs"}


class LoginIn(BaseModel):
    username: str
    password: str


# Đăng nhập app Flutter — ĐẶT TRƯỚC route catch-all bên dưới để không bị nuốt.
# Cần token API (cửa chung) + username/password (danh tính). Trả thông tin tài khoản (KHÔNG kèm mật khẩu).
@app.post("/login", dependencies=[Depends(auth)])
def login(body: LoginIn):
    u = db.get_user(body.username)
    if u is None or not u["active"] or not verify_password(body.password, u["password"]):
        raise HTTPException(401, "sai tài khoản hoặc mật khẩu, hoặc tài khoản bị khoá")
    return {"ok": True, "username": u["username"], "role": u["role"], "name": u["name"],
            "ids": u["ids"], "active": u["active"], "email": u["email"],
            "fw_version": u["fw_version"], "date_create": u["date_create"]}


# Tài khoản app (thay Apps Script userAuth.js) — ĐẶT TRƯỚC catch-all. Body JSON
# {action: login|changePassword|changeEmail|listUsers|saveUser|deleteUser, ...};
# app gửi Content-Type text/plain nên đọc body thô, KHÔNG dùng Pydantic.
# LUÔN trả HTTP 200 {ok:...} (app đọc cờ `ok`, không đọc status — hợp đồng Apps Script).
# KHÔNG gate Bearer: app web public không nhúng token được (lộ trong JS); mọi action
# đã tự gate bằng mật khẩu (login/changeX: mật khẩu user; action admin: mật khẩu root)
# — đúng mô hình Apps Script /exec công khai trước đây. Login OK sẽ trả kèm apiToken.
@app.post("/auth")
async def auth_actions(request: Request):
    try:
        body = json.loads(await request.body())
        if not isinstance(body, dict):
            return {"ok": False, "error": "Body không hợp lệ"}
    except (json.JSONDecodeError, UnicodeDecodeError):
        return {"ok": False, "error": "JSON không hợp lệ"}
    try:
        return accounts.dispatch(body)
    except Exception as e:  # DB down... → app hiện thông báo thay vì HTTP 500
        print(f"auth action failed: {e}", flush=True)
        return {"ok": False, "error": "Lỗi máy chủ, vui lòng thử lại"}


# Bắt MỌI path POST còn lại — thiết bị gửi vào path nào cũng không gãy (receiver cũ nhận mọi path)
@app.post("/{_path:path}", dependencies=[Depends(auth)])
async def ingest(request: Request):
    # Bắt buộc Content-Length: chặn chunked stream vô hạn nuốt RAM
    cl = request.headers.get("content-length")
    if not (cl and cl.isdigit()):
        raise HTTPException(411, "content-length required")
    if int(cl) > config.MAX_BODY:
        raise HTTPException(413, "body too large")
    raw = await request.body()
    if not raw or len(raw) > config.MAX_BODY:
        raise HTTPException(400, "missing/too large body")
    try:
        data = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise HTTPException(400, f"invalid json: {e}")
    err = validate(data)
    if err:
        raise HTTPException(400, err)

    device = safe_name(str(data["id_device"]))
    # Tên file = ngày UTC + hash nội dung → thiết bị retry không sinh file rác
    name = f"{device}_{datetime.now(timezone.utc):%Y%m%d}_{canonical_sha256(data).hex()[:12]}.json"
    path = config.DATA_DIR / name
    try:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    except OSError as e:  # ổ đầy/không ghi được -> vẫn cố vào DB, không làm gãy request
        print(f"file write failed ({name}): {e}", flush=True)

    try:
        # received_at = thời điểm đo trong payload ('time'); không có -> now(). posted_at = now().
        sid = db.insert_session(data, received_at=payload_time(data))
        db_ok = True
    except Exception as e:  # DB down → file vẫn còn, reconcile.py nạp bù
        print(f"db insert failed ({name}): {e}", flush=True)
        sid, db_ok = None, False
    print(f"saved {name} ({len(raw)} bytes, db={'ok' if db_ok else 'FAIL'})", flush=True)
    return {"ok": True, "file": name, "db": db_ok, "id": sid}


@app.get("/devices", dependencies=[Depends(auth)])
def devices():
    """Danh sách thiết bị + số phiên + lần gửi cuối."""
    return db.list_devices()


@app.get("/ota/{filename}", dependencies=[Depends(auth)])
def ota(filename: str):
    """Tai firmware .bin cho thiet bi OTA."""
    name = safe_name(filename)
    if name != filename or not name.lower().endswith(".bin"):
        raise HTTPException(404, "firmware not found")
    path = config.OTA_DIR / name
    if not path.is_file():
        raise HTTPException(404, "firmware not found")
    return FileResponse(path, media_type="application/octet-stream", filename=name)


@app.get("/sessions", dependencies=[Depends(auth)])
def sessions(
    device: str | None = None,
    from_: str | None = Query(None, alias="from", description="YYYY-MM-DD"),
    to: str | None = Query(None, description="YYYY-MM-DD (bao gồm cả ngày này)"),
    page: int = Query(1, ge=1),
    limit: int = Query(20, ge=1, le=200),
):
    """Danh sách phiên đo, lọc theo thiết bị/khoảng ngày, phân trang."""
    _check_date(from_, "from")
    _check_date(to, "to")
    return db.list_sessions(device, from_, to, page, limit)


@app.get("/sessions/{sid}", dependencies=[Depends(auth)])
def session_detail(sid: int):
    """Chi tiết 1 phiên (bỏ amplification — lấy riêng ở endpoint dưới)."""
    session = db.get_session(sid)
    if session is None:
        raise HTTPException(404, "session not found")
    return session


@app.get("/sessions/{sid}/amplification", dependencies=[Depends(auth)])
def session_amplification(sid: int):
    """Đường cong khuếch đại đã parse thành mảng số — chỉ tải khi mở biểu đồ."""
    slots = db.get_amplification(sid)
    if slots is None:
        raise HTTPException(404, "session not found")
    return {"id": sid, "slots": slots}


# App WEB (Flutter build web, build với --base-href /app/) serve tĩnh CÙNG ORIGIN
# với API → không dính CORS. Mount SAU các route API (path /app không đụng ai);
# thiếu thư mục (máy dev/test) thì bỏ qua. html=True → /app/ trả index.html.
if config.WEB_DIR.is_dir():
    from fastapi.staticfiles import StaticFiles

    app.mount("/app", StaticFiles(directory=config.WEB_DIR, html=True), name="webapp")
