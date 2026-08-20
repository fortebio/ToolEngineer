"""FastAPI: nhận POST JSON từ thiết bị + API đọc cho app Flutter.

Chạy:  uvicorn app.main:app --host 0.0.0.0 --port 8080   # systemd fbt-receiver
Docs:  https://<domain>/docs                              # Swagger tự sinh

Nguyên tắc: file JSON trong DATA_DIR là NGUỒN CHÂN LÝ — /ingest ghi file trước,
INSERT vào Postgres sau; DB lỗi thì chỉ log, scripts/reconcile.py nạp bù sau.
"""
import hashlib
import json
import os
import re
import threading
import time
from datetime import datetime, timezone

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel

from app import auth as accounts, config, db, ratelimit
from app.logic import canonical_sha256, check_auth, payload_time, safe_name, validate, verify_password

app = FastAPI(title="FBT Home Server", version="1.0")
config.DATA_DIR.mkdir(parents=True, exist_ok=True)  # tạo thư mục lưu file 1 lần lúc khởi động
config.OTA_DIR.mkdir(parents=True, exist_ok=True)


def auth(authorization: str = Header("")):
    """Cửa chung: token thiết bị (chính + cửa sổ xoay) HOẶC token admin OTA.

    Token admin qua được mọi route vì nó là quyền LỚN HƠN, không phải quyền khác — nhờ vậy
    admin chỉ cần dán một token duy nhất vào app là đọc lẫn ghi đều chạy.
    Mọi danh sách rỗng -> any() False -> vẫn fail-closed y như trước (check_auth từ chối
    token rỗng, nên OTA_ADMIN_TOKEN chưa đặt là vô hại).
    """
    if not any(check_auth(authorization, t)
               for t in (*config.TOKENS, config.OTA_ADMIN_TOKEN)):
        raise HTTPException(401, "unauthorized")


def ota_admin(authorization: str = Header("")):
    """Gác 4 route GHI của OTA: upload .bin, chọn/huỷ target, xoá .bin.

    CHƯA đặt OTA_ADMIN_TOKEN -> rơi về `auth()`, tức hành vi CŨ y nguyên. Đây là chủ ý: deploy
    code này một mình không được làm gãy app đang dùng token thiết bị. Bật lên bằng cách đặt
    biến môi trường, SAU khi admin đã có token mới trong Cài đặt của app.

    Bật rồi thì token thiết bị (thứ nằm trong mọi .bin và thứ `/auth` phát cho mọi tài khoản)
    chỉ còn mở được `GET /ota/check` + tải `.bin` — đọc, không ghi.
    """
    if not config.OTA_ADMIN_TOKEN:
        auth(authorization)
        return
    if not check_auth(authorization, config.OTA_ADMIN_TOKEN):
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


@app.get("/whoami")
def whoami(request: Request):
    """Server NHÌN THẤY client là ai — để kiểm `ratelimit` khoá theo đúng IP hay không.

    Vì sao cần: sau Tailscale Funnel, request tới app từ localhost. Nếu tailscaled KHÔNG
    gắn `X-Forwarded-For` thì mọi khách công khai dồn chung khoá `127.0.0.1` và ngưỡng
    theo IP thành ngưỡng toàn cục. KHÔNG đo được bằng máy trong tailnet (MagicDNS phân
    giải thẳng về IP tailnet, không đi qua Funnel) → mở link này bằng 4G/tắt Tailscale.

    Công khai (không Bearer) và chỉ nói cho người gọi biết IP của CHÍNH HỌ — không lộ gì.
    Xoá được sau khi đã xác minh xong.
    """
    return {
        "peer": request.client.host if request.client else None,  # ai nối thẳng tới app
        "x_forwarded_for": request.headers.get("x-forwarded-for"),
        "ratelimit_key": ratelimit.client_ip(
            request.headers, request.client.host if request.client else ""
        ),
    }


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

    # Chống dò mật khẩu: chặn theo (ip, username) trước khi đụng DB/scrypt.
    ip = ratelimit.client_ip(request.headers, request.client.host if request.client else "")
    who = str(body.get("username") or body.get("adminUser") or "")
    now = time.time()
    if ratelimit.blocked(ip, who, now):
        print(f"ratelimit: chan ip={ip} user={who!r}", flush=True)
        return {"ok": False, "error": "Thử sai quá nhiều lần. Đợi vài phút rồi thử lại."}

    try:
        result = accounts.dispatch(body)
    except Exception as e:  # DB down... → app hiện thông báo thay vì HTTP 500
        print(f"auth action failed: {e}", flush=True)
        return {"ok": False, "error": "Lỗi máy chủ, vui lòng thử lại"}

    # Chỉ đếm lần THẤT BẠI → user đăng nhập đúng không bao giờ chạm giới hạn.
    if result.get("ok"):
        ratelimit.reset(ip, who)
    else:
        ratelimit.record_failure(ip, who, now)
    return result


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
    file_ok = True
    try:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    except OSError as e:  # ổ đầy/không ghi được -> vẫn cố vào DB, không làm gãy request
        print(f"file write failed ({name}): {e}", flush=True)
        file_ok = False

    try:
        # received_at = thời điểm đo trong payload ('time'); không có -> now(). posted_at = now().
        sid = db.insert_session(data, received_at=payload_time(data))
        db_ok = True
    except Exception as e:  # DB down → file vẫn còn, reconcile.py nạp bù
        print(f"db insert failed ({name}): {e}", flush=True)
        sid, db_ok = None, False

    # MỘT đường ghi sống là còn cứu được (file → reconcile.py nạp bù; DB → có bản ghi).
    # CẢ HAI chết thì phiên đo BIẾN MẤT — phải báo lỗi để firmware gửi lại, tuyệt đối
    # không trả {ok:true} (bug cũ: thiết bị tưởng xong, không retry, mất hẳn dữ liệu).
    if not file_ok and not db_ok:
        print(f"LOST {name} ({len(raw)} bytes): ghi file VÀ insert DB đều fail", flush=True)
        raise HTTPException(500, "luu that bai ca file lan DB - gui lai")

    print(f"saved {name} ({len(raw)} bytes, db={'ok' if db_ok else 'FAIL'}, "
          f"file={'ok' if file_ok else 'FAIL'})", flush=True)
    return {"ok": True, "file": name, "db": db_ok, "id": sid}


@app.get("/devices", dependencies=[Depends(auth)])
def devices():
    """Danh sách thiết bị + số phiên + lần gửi cuối + version firmware.

    `version` = bản máy TỰ BÁO ở `/ota/check?ver=` nếu có; không có thì rơi về version
    của phiên đo gần nhất (firmware cũ chưa biết báo). Xem [_fw_report].
    """
    seen = _fw_seen()
    rows = db.list_devices()
    for r in rows:
        # `_fw_entry`: fw_seen.json sửa tay được như target.json, một mục méo không được
        # phép làm 500 cả bảng "Trạng thái máy". `_fw_newer`: mục tự khai chỉ thắng khi nó
        # MỚI HƠN phiên đo gần nhất — máy hạ bản xuống firmware cũ thì ngừng gửi `?ver=`
        # và mục cũ phải tự nhường lại, không thì cột này kẹt ở bản máy không còn chạy.
        e = _fw_entry(seen.get(r["id_device"]))
        if e and _fw_newer(e["at"], r.get("last_seen")):
            r["version"] = e["version"]
    return rows


@app.get("/devices/{device}/fw-log", dependencies=[Depends(auth)])
def device_fw_log(device: str):
    """Các mốc ĐỔI version máy tự khai — **mới nhất trước**.

    Đây là ngày cập nhật THẬT (máy khai lúc khởi động, vài giây sau khi nạp xong), khác với
    lịch sử app suy từ `sessions.version` vốn chỉ biết "lần đo đầu tiên báo bản đó".

    ⚠️ Chỉ có dữ liệu **từ lúc firmware ≥ v2.4.5 và server này lên**. Phần trước đó không tồn
    tại ở đâu cả — app phải suy từ phiên đo và nói rõ là suy đoán.
    """
    entries = _fw_log().get(device)
    if not isinstance(entries, list):
        return []
    # `how` chỉ có ở mốc máy TỰ KHAI vừa nạp xong (`?updated=1`). Thiếu nó = mốc suy từ
    # "version đổi giữa hai lượt poll" — vẫn là ngày thật, nhưng chỉ chặn được tới lượt
    # poll gần nhất. Hai độ tin cậy khác nhau nên app phải phân biệt được.
    out = []
    for e in entries:
        if not (isinstance(e, dict) and e.get("version")):
            continue
        row = {"version": e["version"], "at": str(e.get("at") or "")}
        if e.get("how"):
            row["how"] = e["how"]
        out.append(row)
    return list(reversed(out))


# --- OTA firmware (tab "Quản lý máy" của app) --------------------------------
# Kho .bin nằm ở config.OTA_DIR; bản đang CHỌN để nạp ghi trong `target.json`
# (file, KHÔNG bảng DB — khỏi migration + GRANT, đúng nguyên tắc "file là nguồn
# chân lý" của service này).
#
# ⚠️ DÙNG PUT/DELETE cho mọi thao tác ghi, KHÔNG POST: route `POST /{_path:path}`
# catch-all ingest ở trên NUỐT mọi POST → `POST /ota/...` sẽ bị hiểu là payload
# thiết bị và trả 400. PUT/DELETE không dính catch-all nên khỏi lo thứ tự đăng ký.
# Riêng các GET path CỐ ĐỊNH (/ota, /ota/check) phải đứng TRƯỚC `/ota/{filename}`
# nếu không "check" sẽ khớp vào {filename}.

_TARGET_FILE = config.OTA_DIR / "target.json"


def _pin_entry(v) -> dict | None:
    """Một mục ghim, chấp nhận CẢ HAI dạng đã từng ghi ra `target.json`.

    Dạng đầu (2026-08-18 sáng) là chuỗi tên file trần; dạng hiện tại là
    `{"file": ..., "by": ..., "at": ...}`. File này sửa tay được và tồn tại trên box từ
    trước, nên đọc phòng thủ ở đúng một chỗ thay vì rải `isinstance` khắp nơi.

    ⚠️ `by` là do CLIENT tự khai — token admin không mang danh tính nào để server đối
    chiếu. Nó là **ghi chú vận hành**, không phải nhật ký kiểm toán; đừng dùng nó để quy
    trách nhiệm.
    """
    if isinstance(v, str):
        return {"file": v, "by": "", "at": ""} if v else None
    if isinstance(v, dict) and isinstance(v.get("file"), str) and v["file"]:
        return {"file": v["file"], "by": str(v.get("by") or ""), "at": str(v.get("at") or "")}
    return None


def _ota_cfg() -> dict:
    """Nội dung target.json: {"target": {file,by,at}|None, "devices": {<id>: {file,by,at}}}.

    `target` đi qua CÙNG [_pin_entry] với ghim: bản chung cũng phải nhớ ai đặt (trước
    đây nó là chuỗi tên file trần -> không ai trả lời được "ai đẩy bản này cho 109 máy").
    Dạng chuỗi cũ vẫn đọc được, `by` rỗng.
    """
    try:
        cfg = json.loads(_TARGET_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        cfg = {}
    if not isinstance(cfg, dict):
        cfg = {}
    devs = cfg.get("devices")
    devs = devs if isinstance(devs, dict) else {}
    cfg["devices"] = {
        k: e for k, v in devs.items() if (e := _pin_entry(v)) is not None
    }
    cfg["target"] = _pin_entry(cfg.get("target"))
    return cfg


def _ota_save(cfg: dict) -> None:
    """Ghi NGUYÊN TỬ: `/ota/check` đọc file này trên mọi request của 109 máy, và một lần
    ghi đè tại chỗ bị đọc trúng giữa chừng sẽ trả JSON cụt -> thiết bị coi như 'không có
    bản nào' và im lặng bỏ qua lượt đó."""
    cfg = {"target": cfg.get("target"), "devices": cfg.get("devices") or {}}
    tmp = _TARGET_FILE.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(cfg, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, _TARGET_FILE)


def _ota_target(device: str = "") -> str | None:
    """Bản firmware dành cho `device`; None nếu không có bản nào để nạp.

    Ghim theo TỪNG MÁY thắng bản chung. Ba lý do tồn tại: thử bản mới trên 1 máy trước khi
    mở cho cả fleet, giữ một máy ở bản cũ, và giao bản riêng cho một khách.

    Ghim mà file đã mất -> **KHÔNG rơi về bản chung**, trả None. Rơi về bản chung sẽ đẩy
    đúng cái máy vừa được cố ý giữ lại đi lên phía trước — hỏng theo chiều nguy hiểm. (Trên
    thực tế trạng thái đó không xảy ra qua API: xoá một .bin sẽ gỡ luôn mọi ghim tới nó.)
    """
    cfg = _ota_cfg()
    entry = (cfg["devices"].get(device) if device else None) or cfg["target"]
    if not entry:
        return None
    return entry["file"] if (config.OTA_DIR / entry["file"]).is_file() else None


def _ota_name(filename: str) -> str:
    """Tên file .bin an toàn (chặn path traversal); sai → 400."""
    name = safe_name(filename)
    if name != filename or not name.lower().endswith(".bin"):
        raise HTTPException(400, "tên file phải là .bin hợp lệ")
    return name


@app.get("/ota", dependencies=[Depends(auth)])
def ota_list():
    """Danh sách firmware đã tải lên + bản chung + các máy được ghim riêng."""
    files = sorted(
        (p for p in config.OTA_DIR.glob("*.bin") if p.is_file()),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    cfg = _ota_cfg()
    tgt = cfg["target"] or {}
    return {
        "target": _ota_target(),
        # Ai đặt bản CHUNG + lúc nào — app hiện ở dải trạng thái, như cột "Người thiết
        # lập" của ghim. Client tự khai, xem _pin_entry.
        "target_by": tgt.get("by", ""),
        "target_at": tgt.get("at", ""),
        "devices": cfg["devices"],   # {id_device: tên .bin} — app hiện cột "Bản ghim"
        "files": [
            {"name": p.name, "size": p.stat().st_size,
             "modified": datetime.fromtimestamp(p.stat().st_mtime, timezone.utc)}
            for p in files
        ],
    }


# --- Version firmware do MÁY TỰ BÁO ------------------------------------------
# `sessions.version` chỉ nói máy chạy gì LÚC ĐO GẦN NHẤT: nạp xong mà chưa ai chạy mẫu thì
# bảng "Trạng thái máy" vẫn hiện bản CŨ — đúng lúc cần biết đợt OTA tới đâu thì nó mù. Nên
# từ v2.4.5 firmware gửi kèm `?ver=<bản đang chạy>` trong CHÍNH lượt `/ota/check` nó đã gọi
# (lúc khởi động = ngay sau khi nạp xong, rồi mỗi 6 h) — không thêm endpoint, không thêm
# request, và máy chưa bao giờ update cũng tự khai.
#
# FILE chứ không phải bảng Postgres: DDL trên box bắt buộc `sudo -u postgres` (service chạy
# role `engineer`, không CREATE TABLE được) nên thêm bảng = thêm một bước tay lúc deploy, cho
# đúng thứ `target.json` ngay trên đã làm bằng file.
_FW_FILE = config.OTA_DIR / "fw_seen.json"
# Chuỗi định danh AN TOÀN — đúng bộ ký tự firmware lọc trước khi ghép query (updateOTA.cpp
# urlSafe), nên bên nào chặn cũng ra cùng một kết quả.
_ID_OK = re.compile(r"[A-Za-z0-9._-]{1,32}")
# Read-modify-write của 109 máy: uvicorn chạy endpoint sync trong threadpool nên hai máy báo
# cùng lúc là mất một bản ghi. Lock trong tiến trình đủ vì service chạy MỘT worker (cùng giả
# định ratelimit.py đang dựa vào). ponytail: thêm worker thì phải chuyển sang bảng DB.
_FW_LOCK = threading.Lock()
# Nhật ký các mốc ĐỔI version (file riêng, không nằm trong đường nóng của `/devices`).
_FW_LOG_FILE = config.OTA_DIR / "fw_log.json"
_FW_LOG_MAX = 50
# Trần SỐ MÁY được ghi nhật ký. Fleet thật 109 (đếm được: 95 máy đã từng gửi phiên đo).
# 500 là dư gấp mấy lần mà vẫn giữ file ở cỡ KB — xem lý do ở `_fw_report`.
_FW_MAX_DEVICES = 500


def _atomic_json(path, obj) -> None:
    """Ghi JSON NGUYÊN TỬ. `/devices` và `/ota/check` đọc các file này trên mọi request của
    109 máy — ghi đè tại chỗ bị đọc trúng giữa chừng trả JSON cụt, mất dữ liệu của CẢ fleet
    chứ không riêng máy đang ghi."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)


def _fw_read(path):
    """Đọc một file nhật ký. `{}` = CHƯA CÓ file. `None` = CÓ file nhưng hỏng.

    Phân biệt hai ca đó là bắt buộc, không phải cầu kỳ: `_fw_report` ghi đè TOÀN BỘ file,
    nên coi "hỏng" là "rỗng" biến một dấu phẩy lệch (sửa tay trên box — chính ca mà
    `_fw_entry` sinh ra để phòng — hoặc mất điện giữa lúc ghi) thành XOÁ SẠCH mốc cập nhật
    của cả 109 máy. Đúng thứ mà thứ tự "ghi log trước seen" được dựng ra để bảo vệ, và
    cũng là "cái không dựng lại được" mà CLAUDE.md nói tới.
    """
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def _fw_seen() -> dict:
    """{id_device: {"version": ..., "at": ISO}} — rỗng nếu chưa máy nào báo.

    Đường ĐỌC-ĐỂ-HIỂN-THỊ: file hỏng chỉ là thiếu dữ liệu, không được làm `/devices` 500.
    Đường GHI thì gọi thẳng `_fw_read` để còn phân biệt được hỏng với chưa-có.
    """
    return _fw_read(_FW_FILE) or {}


def _fw_entry(v) -> dict | None:
    """Một mục fw_seen, đọc PHÒNG THỦ như `_pin_entry` của target.json.

    Cần vì `fw_seen.json` **không có API nào sửa** — đường duy nhất để dọn một mục là ssh
    vào box gõ tay, và dạng gọn `{"<id>": "v2.4.4"}` là nhầm lẫn rất dễ xảy ra (đúng là dạng
    cũ của `target.json`). Không kiểm thì `.get("version")` trên một `str` ném AttributeError
    → `GET /devices` trả 500 → **sập cả bảng "Trạng thái máy"**, tức một mục gõ sai làm mù
    toàn bộ fleet. Chuỗi trần vẫn hiểu được nên nhận luôn, khỏi bắt người ta gõ lại.
    """
    if isinstance(v, str):
        return {"version": v, "at": ""} if v else None
    if isinstance(v, dict) and isinstance(v.get("version"), str) and v["version"]:
        return {"version": v["version"], "at": str(v.get("at") or "")}
    return None


def _fw_newer(at: str, last_seen) -> bool:
    """Bản máy TỰ KHAI có mới hơn phiên đo gần nhất không.

    Vì sao phải so chứ không đè thẳng: chỉ firmware **≥ v2.4.5** gửi `?ver=`. Máy hạ xuống
    bản cũ hơn (nạp tay, hoặc nhận đúng một target cũ còn armed) thì **ngừng gửi** — mục cũ
    nằm lại vĩnh viễn và không API nào gỡ. Đè vô điều kiện khi đó nghĩa là cột "Firmware"
    kẹt ở bản máy KHÔNG còn chạy, mâu thuẫn với chính hộp "Lịch sử cập nhật" ngay cạnh nó,
    và bảng tiến độ đếm nhầm máy đã-đúng-bản thành chưa-lên.

    Lý do đè vẫn đứng khi mục tự khai TƯƠI HƠN — đúng ca nó sinh ra để giải quyết (nạp xong
    chưa ai chạy mẫu). Nên điều kiện là "mới hơn", không phải "có là thắng".

    ⚠️ `last_seen` = `max(received_at)`, mà `received_at` lấy từ trường `time` trong payload
    = **đồng hồ MÁY**; `at` là đồng hồ server. Máy lệch giờ nặng thì phép so này lệch theo —
    đổi lại nó không cần thêm cột hay query nào. Mục méo/thiếu `at` → cho đè (giữ hành vi cũ,
    thà hiện bản tự khai còn hơn 500).
    """
    if not last_seen:
        return True
    try:
        return datetime.fromisoformat(str(at)) >= last_seen
    except (TypeError, ValueError):
        return True


def _fw_log() -> dict:
    """{id_device: [{"version","at"}, ...]} — CŨ → MỚI. Rỗng nếu chưa có gì."""
    return _fw_read(_FW_LOG_FILE) or {}


def _fw_report(device: str, ver: str, updated: bool = False) -> None:
    """Ghi version máy vừa khai — và ghi thêm MỘT MỐC vào nhật ký nếu version ĐỔI.

    Thiếu/bẩn -> BỎ QUA IM LẶNG, không 400: firmware trước v2.4.5 không gửi `ver`, và một
    lượt check của nó vẫn phải nạp được bản mới — đường sửa từ xa DUY NHẤT tới máy ngoài
    hiện trường đi qua chính request này.

    **Hai file, cố ý:**
      * `fw_seen.json` — chỉ bản HIỆN TẠI. `/devices` đọc nó trên mọi lần mở bảng, giữ nhỏ.
      * `fw_log.json` — các mốc ĐỔI bản. Chỉ đọc khi có người mở hộp "Lịch sử cập nhật".

    **Nhật ký ghi TRƯỚC**: chết giữa hai lần ghi thì lượt check sau ghi lại `seen` (version
    không đổi nên nhật ký không thêm trùng). Ghi `seen` trước mà chết thì mốc đó mất VĨNH
    VIỄN — lần sau version đã bằng nhau, không còn gì để phát hiện là đã đổi.

    Vì sao mốc này đáng giá hơn cách suy từ `sessions.version`: máy khai ngay lúc **khởi
    động**, tức vài giây sau khi nạp xong. Suy từ phiên đo chỉ biết "lần đo đầu tiên báo bản
    đó" — muộn hơn hàng ngày, và máy nạp xong không chạy mẫu thì không bao giờ có mốc nào.
    """
    if not (_ID_OK.fullmatch(device) and _ID_OK.fullmatch(ver)):
        return
    now = datetime.now(timezone.utc).isoformat()
    try:
        with _FW_LOCK:
            log = _fw_read(_FW_LOG_FILE)
            seen = _fw_read(_FW_FILE)
            # File hỏng -> KHÔNG ghi đè. Ghi đè lên `{}` là xoá sổ cả 109 máy bằng đúng
            # một lượt check của một máy bất kỳ.
            if log is None or seen is None:
                print(f"fw report bo qua ({device}): file nhat ky hong, khong ghi de",
                      flush=True)
                return
            # Trần SỐ MÁY. `_FW_LOG_MAX` chỉ chặn số mốc TRÊN MỖI máy, không chặn số máy —
            # mà `device` là query param tuỳ ý và `/ota/check` KHÔNG qua ratelimit. Ai cầm
            # token thiết bị (nằm trong 4 KB đầu MỌI file .bin) bơm mã máy ngẫu nhiên là
            # hai file này phình vô hạn, mà mỗi lượt check lại đọc+ghi TRỌN file trong
            # `_FW_LOCK` của tiến trình một-worker => O(n²) làm nghẹn chính đường sửa từ xa
            # duy nhất tới 109 máy sau NAT. Thà từ chối ghi thống kê còn hơn mất đường đó.
            if device not in log and len(log) >= _FW_MAX_DEVICES:
                print(f"fw report tu choi ({device}): da {len(log)} may, vuot tran "
                      f"{_FW_MAX_DEVICES}", flush=True)
                return
            entries = log.get(device)
            if not isinstance(entries, list):
                entries = []
            entries = [e for e in entries if isinstance(e, dict) and e.get("version")]
            # `updated` THẮNG phép so version: máy tự khai nó vừa nạp xong thì đó là một lần
            # cập nhật, kể cả nạp lại đúng bản cũ — mà phép so "version có đổi không" mù hẳn
            # với ca đó. Chủ dự án yêu cầu đúng điều này: "cập nhật bao nhiêu lần thì lưu vào
            # lịch sử bấy nhiêu".
            if updated or not entries or entries[-1].get("version") != ver:
                e = {"version": ver, "at": now}
                # `how` để app biết dòng nào là mốc XÁC NHẬN (máy tự khai vừa nạp) và dòng nào
                # chỉ là suy từ version đổi giữa hai lượt poll. Thiếu trường này thì hai thứ
                # trông y hệt nhau, mà độ tin cậy của chúng khác hẳn.
                if updated:
                    e["how"] = "update"
                entries.append(e)
                # Trần mỗi máy: nhật ký này không có ai dọn, mà một máy hỏng bật/tắt liên
                # tục với hai bản luân phiên sẽ thêm mốc mãi. 50 lần đổi bản là nhiều hơn
                # mọi máy thật sẽ trải qua; chạm trần thì bỏ mốc CŨ NHẤT.
                log[device] = entries[-_FW_LOG_MAX:]
                _atomic_json(_FW_LOG_FILE, log)
            seen[device] = {"version": ver, "at": now}
            _atomic_json(_FW_FILE, seen)
    except Exception as e:
        # ĐĨA ĐẦY / MẤT QUYỀN GHI KHÔNG ĐƯỢC PHÉP GIẾT `/ota/check`.
        # Chính docstring trên nói request này là đường sửa từ xa DUY NHẤT tới 109 máy
        # ngoài hiện trường — để lỗi ghi nhật ký ném lên là đổi một tính năng THỐNG KÊ
        # lấy toàn bộ khả năng cứu fleet. Cùng lý do `ingest` vẫn trả 200 khi một đường
        # chết (xem `LOST` phía trên).
        # Nhưng KÊU TO trong journal: hỏng im lặng ở đây thì bảng lịch sử cứ rỗng dần mà
        # không ai biết vì sao. `journalctl -u fbt-receiver | grep "fw report"`.
        print(f"fw report failed ({device} {ver}): {e}", flush=True)


@app.get("/ota/check", dependencies=[Depends(auth)])
def ota_check(request: Request, device: str = "", ver: str = "", updated: str = ""):
    """**Thiết bị** gọi định kỳ để biết có bản mới không.

    `?device=<id>` — firmware v2.4.4 gửi sẵn từ bản đầu. Có ghim riêng cho máy đó thì trả
    bản ghim, không thì trả bản chung. Trả `{update:false}` khi không có bản nào.

    `?ver=<bản đang chạy>` — firmware v2.4.5+ khai luôn version của nó, server ghi lại cho
    `/devices` (xem [_fw_report]). Ghi TRƯỚC khi xét có bản mới hay không: `{update:false}`
    là lượt PHỔ BIẾN NHẤT (fleet đã lên đúng bản) và cũng là lượt xác nhận nó đã lên.

    So sánh version là việc của FIRMWARE: nó biết mình đang chạy gì, server chỉ lưu file.
    `sha256` kèm theo cho app/người xem; firmware dùng header `x-MD5` của `/ota/{file}`.
    """
    # `?updated=1` = máy vừa NẠP XONG một bản và khởi động lại vào nó (firmware chốt cờ này
    # trong NVS ngay khi `httpUpdate` trả OK). Khác hẳn suy từ "version đổi": nó thấy được cả
    # lần nạp LẠI CÙNG một bản, và phân biệt "vừa cập nhật" với "vừa mất điện bật lại".
    _fw_report(device, ver, updated=updated == "1")
    name = _ota_target(device)
    if name is None:
        return {"update": False}
    path = config.OTA_DIR / name
    return {
        "update": True,
        "version": name,
        "size": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "url": str(request.url.replace(path=f"/ota/{name}", query="")),
    }


@app.put("/ota/target/{filename}", dependencies=[Depends(ota_admin)])
def ota_set_target(filename: str, device: str = "", by: str = ""):
    """Chọn bản firmware sẽ nạp ở lần `/ota/check` tới.

    Không có `?device=` → đặt bản CHUNG cho cả fleet.
    Có `?device=<id>` → **ghim riêng máy đó**, thắng bản chung.
    """
    name = _ota_name(filename)
    if not (config.OTA_DIR / name).is_file():
        raise HTTPException(404, "firmware not found")
    cfg = _ota_cfg()
    entry = {
        "file": name,
        "by": by[:64],  # client tự khai, xem _pin_entry
        "at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    if device:
        cfg["devices"][safe_name(device)] = entry
    else:
        cfg["target"] = entry
    _ota_save(cfg)
    return {"ok": True, "target": name, "device": device or None}


@app.delete("/ota/target", dependencies=[Depends(ota_admin)])
def ota_clear_target(device: str = ""):
    """Huỷ chọn. Không có `?device=` → bỏ bản CHUNG **và giữ nguyên mọi ghim riêng**
    (bỏ chọn cho fleet không được âm thầm thả các máy đang bị giữ lại).
    Có `?device=<id>` → chỉ gỡ ghim của máy đó, nó quay về theo bản chung."""
    cfg = _ota_cfg()
    if device:
        cfg["devices"].pop(safe_name(device), None)
    else:
        cfg["target"] = None
    _ota_save(cfg)
    return {"ok": True, "target": (cfg["target"] or {}).get("file"), "device": device or None}


@app.put("/ota/{filename}", dependencies=[Depends(ota_admin)])
async def ota_upload(filename: str, request: Request):
    """Tải firmware lên: body = NGUYÊN bytes file .bin.

    Cố tình KHÔNG dùng multipart/UploadFile để khỏi phải cài thêm
    `python-multipart` trên box (requirements.txt hiện chỉ có fastapi/uvicorn/psycopg).
    """
    name = _ota_name(filename)
    cl = request.headers.get("content-length")
    if not (cl and cl.isdigit()):
        raise HTTPException(411, "content-length required")
    if int(cl) > config.MAX_BODY:
        raise HTTPException(413, "body too large")
    raw = await request.body()
    if not raw:
        raise HTTPException(400, "file rỗng")
    # Ghi ra file tạm rồi ĐỔI TÊN, không ghi đè tại chỗ. `write_bytes` truncate file về 0 rồi
    # ghi lại, mà `GET /ota/{file}` đọc lại file đó suốt cả stream 2.4 MB — đẩy bản mới trùng
    # tên trong lúc một máy đang tải sẽ ghép nửa bản cũ với nửa bản mới. `os.replace` trong
    # CÙNG thư mục là nguyên tử trên ext4: máy đang tải giữ inode cũ tới khi xong.
    tmp = config.OTA_DIR / f".tmp-{name}"
    tmp.write_bytes(raw)
    os.replace(tmp, config.OTA_DIR / name)
    print(f"ota upload {name} ({len(raw)} bytes)", flush=True)
    return {"ok": True, "name": name, "size": len(raw)}


@app.delete("/ota/{filename}", dependencies=[Depends(ota_admin)])
def ota_delete(filename: str):
    """Xoá 1 bản firmware, và dọn MỌI lựa chọn trỏ tới nó — bản chung lẫn ghim từng máy.

    Dọn ghim ở đây là chủ ý: nó làm cho trạng thái "máy bị ghim vào một file không còn tồn
    tại" **không thể phát sinh qua API**. Trạng thái đó im lặng (máy chỉ đơn giản không bao
    giờ được mời cập nhật) nên rất khó phát hiện, mà nguyên nhân lại nằm ở một thao tác xoá
    xảy ra từ lâu, ở màn hình khác.
    """
    name = _ota_name(filename)
    (config.OTA_DIR / name).unlink(missing_ok=True)
    cfg = _ota_cfg()
    if (cfg["target"] or {}).get("file") == name:
        cfg["target"] = None
    cfg["devices"] = {d: e for d, e in cfg["devices"].items() if e["file"] != name}
    _ota_save(cfg)
    return {"ok": True}


@app.get("/ota/{filename}", dependencies=[Depends(auth)])
def ota(filename: str):
    """Tai firmware .bin cho thiet bi OTA.

    Header `x-MD5`: thư viện `HTTPUpdate` của ESP32 TỰ ĐỌC header này và gọi
    `Update.setMD5()` (HTTPUpdate.cpp:223,344) → firmware được kiểm toàn vẹn
    ảnh MÀ KHÔNG cần thêm dòng code nào. (`sha256` ở `/ota/check` vẫn giữ cho
    app/người xem.)

    ⚠️ **TÊN phải là `x-MD5`** — `HTTPUpdate` tra cứu đúng chuỗi đó, đổi sang
    `Content-MD5` là mất sạch phép kiểm mà không báo gì. Nhưng **HOA/THƯỜNG thì
    KHÔNG quan trọng**: Starlette gửi ra dây là `x-md5` (responses.py chuẩn hoá
    về chữ thường) và ESP32 so khớp không phân biệt hoa thường
    (HTTPClient.cpp:1305). → Khi kiểm bằng curl phải **grep -i**, thấy vắng mặt
    thì đừng đi sửa hoa/thường, đó không phải nguyên nhân.

    ⚠️ Và **`curl -I` (HEAD) trả 405 trên server này** — FastAPI `@app.get` không
    tự nhận HEAD. Kiểm bằng `curl -s -o /dev/null -D -`, đừng dùng `-I`.
    """
    name = safe_name(filename)
    if name != filename or not name.lower().endswith(".bin"):
        raise HTTPException(404, "firmware not found")
    path = config.OTA_DIR / name
    if not path.is_file():
        raise HTTPException(404, "firmware not found")
    return FileResponse(
        path, media_type="application/octet-stream", filename=name,
        headers={"x-MD5": hashlib.md5(path.read_bytes()).hexdigest()},
    )


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


@app.get("/sessions/{sid}/errors", dependencies=[Depends(auth)])
def session_errors(sid: int):
    """Lỗi cảm biến máy báo về TRONG lần đo này — `[{at, session_id, slot, code, message}]`.

    Firmware gửi lỗi bằng POST RIÊNG (`method:"error"`) chứ không nhét vào payload kết quả,
    nên nó nằm trong `sessions` như một hàng riêng và phải GHÉP theo thời gian. Luật ghép +
    lý do: [db.session_errors].

    Rỗng là kết quả BÌNH THƯỜNG (máy chạy sạch), không phải thiếu dữ liệu — app đừng hiện
    khung rỗng. 404 chỉ khi không có phiên `sid`.

    ⚠️ **Hầu hết fleet chưa gửi lỗi về đây**: trước firmware 2026-08-05 đường lỗi chỉ tới
    Google Sheet, và gần cả 109 máy vẫn chạy bản cũ hơn. Đo được 2026-08-20: **1 bản ghi lỗi
    trên 3706 phiên**. Endpoint này đúng nhưng sẽ im lặng tới khi fleet lên bản mới.
    """
    rows = db.session_errors(sid)
    if rows is None:
        raise HTTPException(404, "session not found")
    return {"id": sid, "errors": rows}


# App WEB (Flutter build web, build với --base-href /app/) serve tĩnh CÙNG ORIGIN
# với API → không dính CORS. Mount SAU các route API (path /app không đụng ai);
# thiếu thư mục (máy dev/test) thì bỏ qua. html=True → /app/ trả index.html.
if config.WEB_DIR.is_dir():
    from fastapi.staticfiles import StaticFiles

    app.mount("/app", StaticFiles(directory=config.WEB_DIR, html=True), name="webapp")
