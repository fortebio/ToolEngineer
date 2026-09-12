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
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import FileResponse
from pydantic import BaseModel

from app import auth as accounts, config, db, monitor as monitor_mod, ota, ratelimit
from app.logic import (DEFAULT_ATE_LIMITS, ate_limits_conflict, ate_match,
                       ate_meta, ate_stats,
                       canonical_sha256, check_auth, normalize_ate_record,
                       payload_time, product_key, safe_name, validate,
                       validate_ate_limits, validate_ate_record, verify_password)

@asynccontextmanager
async def _lifespan(_app: FastAPI):
    # Kho OTA phẳng cũ (trước 2026-09-11) → products/<LEGACY_PRODUCT>/. Tự chạy, idempotent;
    # quên bước này là /ota/check của cả fleet thấy kho trống — xem ota.migrate_legacy.
    # Ở LIFESPAN chứ không phải lúc import: `app/__init__.py` import module này, nên mọi
    # script (`scripts/migrate_ota.py --dry-run`, manage_users…) và test import `app.*`
    # đều chạy qua đây — di cư lúc import là "--dry-run" cũng dời file thật (đã dính).
    # BỌC try/except: đây là việc DỌN KHO, không được phép làm uvicorn không lên — startup fail
    # là mất luôn `/ingest` của cả fleet (109 máy) vì một file .bin do root scp lên mà
    # `engineer` không os.replace được. Kêu to trong journal rồi vẫn phục vụ.
    try:
        for act in ota.migrate_legacy():
            print(f"ota migrate: {act}", flush=True)
    except Exception as e:  # noqa: BLE001 — cố ý bắt tất cả
        print(f"ota migrate FAILED (kho OTA giữ nguyên, server vẫn chạy): {e!r}", flush=True)
    yield


app = FastAPI(title="FBT Home Server", version="1.0", lifespan=_lifespan)
config.DATA_DIR.mkdir(parents=True, exist_ok=True)  # tạo thư mục lưu file 1 lần lúc khởi động
config.OTA_DIR.mkdir(parents=True, exist_ok=True)
config.LOGS_DIR.mkdir(parents=True, exist_ok=True)  # log máy CSKH gửi lên (xem cuối file)
config.ATE_DIR.mkdir(parents=True, exist_ok=True)  # hồ sơ trạm ATE (xem cuối file)


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


# Route của nhánh `ota-rollout-docs-tests` (commit 8bc627d, 2026-08-28) — đã chạy trên box từ
# 28/08 cùng bản web có tab Giám sát; deploy 2026-09-12 từ `main` lỡ ghi đè mất nên ghép lại.
@app.get("/monitor", dependencies=[Depends(ota_admin)])
def monitor(flow: bool = Query(True, description="False = bỏ phần đếm phiên đo (vòng vẽ realtime)")):
    """Số liệu giám sát cho tab **Giám sát** của app: dịch vụ, tài nguyên box, luồng dữ liệu.

    `def` chứ KHÔNG `async def`: hàm này đọc `/proc` và **chặn** ở truy vấn
    Postgres. FastAPI đẩy handler `def` sang threadpool, còn `async def` thì chạy
    THẲNG trên event loop — unit chạy `--workers 1` nên một lần bấm Làm mới lúc
    DB chậm sẽ treo cả server (kể cả POST của thiết bị).

    Gác bằng **`ota_admin`** (token nhân sự), KHÔNG phải `auth()`: token thiết bị
    nằm trong 4 KB đầu mọi file `.bin`, mà `disk.free` cộng với `/ingest` 16 MB
    mỗi POST là công thức làm đầy đĩa — đĩa đầy thì mất dữ liệu đo (xem docstring
    `app/monitor.py`). `OTA_ADMIN_TOKEN` chưa đặt thì `ota_admin` rơi về `auth()`,
    nên deploy file này một mình không đổi hành vi của box hiện tại.

    Server chỉ phân biệt nhân sự/khách hàng (root và admin dùng CHUNG token), nên
    "chỉ root" vẫn là gác ở giao diện app.
    """
    return monitor_mod.snapshot(with_flow=flow)


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
        # Sản phẩm/PCB máy TỰ KHAI (firmware ≥ v2.4.6). Rỗng = chưa khai → app hiện
        # "cũ → <legacy>" chứ không hiện trống; `product_effective` là kho mà /ota/check
        # THẬT SỰ tra cho máy này, để app đối chiếu tiến độ đúng kho.
        r["product"] = e["product"] if e else ""
        r["hw"] = e["hw"] if e else ""
        r["product_effective"] = r["product"] or ota.legacy_product_for(r["id_device"])
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
# Kho .bin TÁCH THEO SẢN PHẨM ở config.OTA_DIR/products/<product>/ (mỗi sản phẩm một
# `target.json`; ảnh + manifest cạnh nhau) — toàn bộ logic kho nằm ở app/ota.py, route ở
# đây chỉ đổi OtaError → HTTPException. Máy không khai `?product=` (fleet ≤ v2.4.5) đi vào
# kho `LEGACY_PRODUCT`; các route KHÔNG có đoạn `{product}` là đường lùi cho app/firmware cũ.
#
# ⚠️ DÙNG PUT/DELETE cho mọi thao tác ghi, KHÔNG POST: route `POST /{_path:path}`
# catch-all ingest ở trên NUỐT mọi POST → `POST /ota/...` sẽ bị hiểu là payload
# thiết bị và trả 400. PUT/DELETE không dính catch-all nên khỏi lo thứ tự đăng ký.
# Riêng các GET path CỐ ĐỊNH (/ota, /ota/check, /ota/products) phải đứng TRƯỚC
# `/ota/{filename}` và `/ota/{product}/{filename}`; đoạn cố định `target` cũng phải
# đứng trước đoạn động cùng hình dạng (`/ota/target/{f}` vs `/ota/{product}/{f}`) —
# và `product_key` từ chối luôn ba từ `check|products|target` để không có sản phẩm nào
# trùng path cố định.


def _product(product: str) -> str:
    """Khoá sản phẩm trong path: sai cú pháp/từ dành riêng → 404 (không có kho đó)."""
    key = product_key(product)
    if key is None:
        raise HTTPException(404, "product not found")
    return key


def _ota_call(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except ota.OtaError as e:
        raise HTTPException(e.status, e.detail) from None


@app.get("/ota", dependencies=[Depends(auth)])
def ota_list(product: str = ""):
    """Kho của MỘT sản phẩm: firmware đã tải lên + bản chung + các máy ghim riêng.

    Không `?product=` → kho `LEGACY_PRODUCT` (app cũ gọi y như trước, thấy đúng kho của
    fleet đang chạy). Mọi sản phẩm: `GET /ota/products`.
    """
    key = _product(product) if product else config.LEGACY_PRODUCT
    return ota.listing(key)


@app.get("/ota/products", dependencies=[Depends(auth)])
def ota_products():
    """Danh sách khoá sản phẩm + số ảnh + bản chung + số máy ghim. `legacy` đánh dấu kho
    mà máy không tự khai product sẽ rơi vào."""
    return {"legacy": config.LEGACY_PRODUCT, "products": ota.products_summary()}


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
        return {"version": v, "at": "", "product": "", "hw": ""} if v else None
    if isinstance(v, dict) and isinstance(v.get("version"), str) and v["version"]:
        # product/hw: máy tự khai từ firmware ≥ v2.4.6 (`?product=&hw=`); rỗng = chưa khai.
        return {"version": v["version"], "at": str(v.get("at") or ""),
                "product": str(v.get("product") or ""), "hw": str(v.get("hw") or "")}
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


def _fw_report(device: str, ver: str, updated: bool = False,
               product: str = "", hw: str = "") -> None:
    """Ghi version máy vừa khai — và ghi thêm MỘT MỐC vào nhật ký nếu version ĐỔI.

    Thiếu/bẩn -> BỎ QUA IM LẶNG, không 400: firmware trước v2.4.5 không gửi `ver`, và một
    lượt check của nó vẫn phải nạp được bản mới — đường sửa từ xa DUY NHẤT tới máy ngoài
    hiện trường đi qua chính request này.

    `product`/`hw` (firmware ≥ v2.4.6 tự khai) chỉ đi vào `fw_seen.json` cho `/devices`
    hiện cột Sản phẩm/PCB — KHÔNG vào nhật ký mốc (giữ file đó nhỏ). Đã được caller lọc.

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
            seen[device] = {"version": ver, "at": now, "product": product, "hw": hw}
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
def ota_check(request: Request, device: str = "", ver: str = "", updated: str = "",
              product: str = "", hw: str = ""):
    """**Thiết bị** gọi định kỳ để biết có bản mới không.

    `?device=<id>` — firmware v2.4.4 gửi sẵn từ bản đầu. Có ghim riêng cho máy đó thì trả
    bản ghim, không thì trả bản chung. Trả `{update:false, reason}` khi không có bản nào.

    `?ver=<bản đang chạy>` — firmware v2.4.5+ khai luôn version của nó, server ghi lại cho
    `/devices` (xem [_fw_report]). Ghi TRƯỚC khi xét có bản mới hay không: `{update:false}`
    là lượt PHỔ BIẾN NHẤT (fleet đã lên đúng bản) và cũng là lượt xác nhận nó đã lên.

    `?product=<khoá>&hw=<PCB>` — firmware v2.4.6+ tự khai nó là sản phẩm gì, PCB nào. Có
    `product` → tra ĐÚNG kho đó (sai cú pháp → fail-closed `reason:"product"`, không đoán
    hộ); không có → kho `LEGACY_PRODUCT` ([ota.legacy_product_for]) — toàn bộ fleet ≤ v2.4.5.
    `hw` chỉ lọc khi manifest của ảnh có danh sách PCB (ảnh có thẻ nhúng).

    So sánh version là việc của FIRMWARE: nó biết mình đang chạy gì, server chỉ lưu file.
    `version` GIỮ = TÊN FILE (firmware ≤ v2.4.5 so đúng chuỗi `fbt_<ver>.bin`); `ver` là
    version thật từ manifest cho firmware mới. `sha256` kèm theo cho app/người xem;
    firmware dùng header `x-MD5` của `/ota/{product}/{file}`.
    """
    # `?updated=1` = máy vừa NẠP XONG một bản và khởi động lại vào nó (firmware chốt cờ này
    # trong NVS ngay khi `httpUpdate` trả OK). Khác hẳn suy từ "version đổi": nó thấy được cả
    # lần nạp LẠI CÙNG một bản, và phân biệt "vừa cập nhật" với "vừa mất điện bật lại".
    key = product_key(product) if product else None
    hw_ok = hw if _ID_OK.fullmatch(hw) else ""
    _fw_report(device, ver, updated=updated == "1", product=key or "", hw=hw_ok)
    if product and key is None:
        return {"update": False, "reason": "product"}
    key = key or ota.legacy_product_for(device)
    hit, reason = ota.resolve(key, device, hw_ok)
    if hit is None:
        return {"update": False, "reason": reason}
    m = hit["manifest"]
    return {
        "update": True,
        "version": hit["file"],
        "ver": m.get("ver", ""),
        "product": key,
        "hw": m.get("hw"),
        "size": m.get("size"),
        "sha256": m.get("sha256"),
        "url": str(request.url.replace(path=f"/ota/{key}/{hit['file']}", query="")),
    }


# --- route GHI kiểu cũ (không đoạn {product}) = kho LEGACY_PRODUCT ------------------------
# App đang phát hành gọi đúng các URL này; giữ nguyên để deploy server một mình không làm
# gãy gì. Khai báo TRƯỚC các route `/ota/{product}/…` cùng hình dạng.

@app.put("/ota/target/{filename}", dependencies=[Depends(ota_admin)])
def ota_set_target_legacy(filename: str, device: str = "", by: str = ""):
    """Chọn bản firmware sẽ nạp ở lần `/ota/check` tới — kho `LEGACY_PRODUCT`.
    Không `?device=` → bản CHUNG; có → **ghim riêng máy đó**, thắng bản chung."""
    return ota_set_target(config.LEGACY_PRODUCT, filename, device=device, by=by)


@app.delete("/ota/target", dependencies=[Depends(ota_admin)])
def ota_clear_target_legacy(device: str = ""):
    return ota_clear_target(config.LEGACY_PRODUCT, device=device)


@app.put("/ota/{name}", dependencies=[Depends(ota_admin)])
async def ota_upload_legacy(name: str, request: Request, force: str = "", by: str = "",
                            note: str = ""):
    """Hai việc trên cùng một hình dạng path, phân biệt bằng đuôi `.bin`:
    * `PUT /ota/<file>.bin` — upload kiểu cũ vào kho `LEGACY_PRODUCT` (app đang phát hành).
    * `PUT /ota/<product>` — upload KHÔNG TÊN: server đặt tên từ thẻ nhúng trong ảnh
      (ảnh không thẻ → 400, phải dùng dạng có tên)."""
    if name.lower().endswith(".bin"):
        return await ota_upload(config.LEGACY_PRODUCT, name, request, force=force, by=by, note=note)
    key = _product(name)
    raw = await _ota_body(request)
    m = await _ota_store(key, None, raw, force=force == "1", by=by, note=note)
    return {"ok": True, "product": key, "name": m["name"], "size": m["size"],
            "ver": m["ver"], "hw": m["hw"], "sha256": m["sha256"], "existed": m["existed"]}


@app.delete("/ota/{filename}", dependencies=[Depends(ota_admin)])
def ota_delete_legacy(filename: str):
    return ota_delete(config.LEGACY_PRODUCT, filename)


@app.get("/ota/{filename}", dependencies=[Depends(auth)])
def ota_legacy(filename: str):
    """Tải `.bin` kiểu cũ (`/ota/<file>`) — tìm trong kho `LEGACY_PRODUCT`. URL mà
    `/ota/check` trả ra giờ có đoạn product, nhưng app cũ tải về bằng đường này."""
    return ota_download(config.LEGACY_PRODUCT, filename)


# --- route theo sản phẩm -------------------------------------------------------------------

async def _ota_store(key: str, name: str | None, raw: bytes, **kw) -> dict:
    """`ota.upload` trong THREADPOOL: băm sha256 + quét thẻ + ghi 2.4 MB (và đọc lại ảnh cũ
    khi trùng tên) là việc chặn — handler này `async def` (để `await request.body()`), chạy
    thẳng là treo event loop của worker duy nhất, tức treo cả POST /ingest của máy."""
    m = await run_in_threadpool(lambda: _ota_call(ota.upload, key, name, raw, **kw))
    if not m["existed"]:
        # `tag` có thể vắng ở manifest cũ/sửa tay (read_manifest chỉ đòi size+sha256) → .get
        print(f"ota upload {key}/{m['name']} ({m['size']} bytes, tag={bool(m.get('tag'))})",
              flush=True)
    return m


async def _ota_body(request: Request) -> bytes:
    """Body = NGUYÊN bytes file .bin. Cố tình KHÔNG dùng multipart/UploadFile để khỏi phải
    cài thêm `python-multipart` trên box (requirements.txt chỉ có fastapi/uvicorn/psycopg)."""
    cl = request.headers.get("content-length")
    if not (cl and cl.isdigit()):
        raise HTTPException(411, "content-length required")
    if int(cl) > config.MAX_BODY:
        raise HTTPException(413, "body too large")
    return await request.body()


@app.put("/ota/{product}/target/{filename}", dependencies=[Depends(ota_admin)])
def ota_set_target(product: str, filename: str, device: str = "", by: str = ""):
    """Chọn bản firmware của kho [product] sẽ nạp ở lần `/ota/check` tới.
    Không `?device=` → bản CHUNG của sản phẩm đó; có `?device=<id>` → **ghim riêng máy đó**."""
    key = _product(product)
    name = _ota_call(ota.bin_name, filename)
    _ota_call(ota.set_target, key, name, device=device, by=by)
    return {"ok": True, "product": key, "target": name, "device": device or None}


@app.delete("/ota/{product}/target", dependencies=[Depends(ota_admin)])
def ota_clear_target(product: str, device: str = ""):
    """Huỷ chọn. Không `?device=` → bỏ bản CHUNG **và giữ nguyên mọi ghim riêng**
    (bỏ chọn cho fleet không được âm thầm thả các máy đang bị giữ lại).
    Có `?device=<id>` → chỉ gỡ ghim của máy đó, nó quay về theo bản chung."""
    key = _product(product)
    cfg = ota.clear_target(key, device=device)
    return {"ok": True, "product": key, "target": (cfg["target"] or {}).get("file"),
            "device": device or None}


@app.put("/ota/{product}/{filename}", dependencies=[Depends(ota_admin)])
async def ota_upload(product: str, filename: str, request: Request, force: str = "",
                     by: str = "", note: str = ""):
    """Tải firmware lên kho [product]: body = NGUYÊN bytes file .bin.

    Ảnh có thẻ nhúng thì `product` thẻ phải = kho và TÊN phải = tên server đặt từ thẻ
    (`fbt_v<ver>.bin` cho kho kế thừa, `<product>_v<ver>.bin` cho kho khác). Cùng tên đã
    có: cùng nội dung → 200 `existed:true`; khác nội dung → **409** (xoá trước rồi tải).
    Xem [ota.upload].
    """
    key = _product(product)
    name = _ota_call(ota.bin_name, filename)
    raw = await _ota_body(request)
    m = await _ota_store(key, name, raw, force=force == "1", by=by, note=note)
    return {"ok": True, "product": key, "name": name, "size": m["size"], "ver": m["ver"],
            "hw": m["hw"], "sha256": m["sha256"], "existed": m["existed"]}


@app.delete("/ota/{product}/{filename}", dependencies=[Depends(ota_admin)])
def ota_delete(product: str, filename: str):
    """Xoá 1 bản firmware của kho [product], và dọn MỌI lựa chọn trỏ tới nó — bản chung lẫn
    ghim từng máy — để trạng thái "ghim vào file không còn tồn tại" không thể phát sinh qua
    API (trạng thái đó im lặng: máy chỉ đơn giản không bao giờ được mời cập nhật)."""
    key = _product(product)
    name = _ota_call(ota.bin_name, filename)
    ota.delete_bin(key, name)
    return {"ok": True, "product": key}


@app.get("/ota/{product}/{filename}", dependencies=[Depends(auth)])
def ota_download(product: str, filename: str):
    """Tải firmware .bin cho thiết bị OTA (URL mà `/ota/check` trả ra).

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
    key = product_key(product)
    name = safe_name(filename)
    if key is None or name != filename or not name.lower().endswith(".bin"):
        raise HTTPException(404, "firmware not found")
    path = ota.product_dir(key) / name
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


# --- Log máy do nhân viên CSKH gửi (app: tab "Chăm sóc KH" › Xử lý sự cố) -----
# Nhân viên cắm USB vào máy, app đọc log UART rồi PUT lên đây kèm mã máy + mô tả
# sự cố; kỹ thuật mở lại bằng GET. Lưu FILE trong config.LOGS_DIR (không bảng DB —
# khỏi migration/GRANT, cùng nguyên tắc với data_plus/ và ota/).
#
# ⚠️ PUT chứ không POST: `POST /{_path:path}` catch-all ingest ở trên nuốt mọi POST
# → `POST /devices/x/logs` bị hiểu là payload thiết bị và trả 400. Xem test
# `test_log_post_roi_vao_catchall`.
#
# Tên file: `<device>_<UTC %Y%m%d_%H%M%S>_<sha256(text)[:8]>.json` — xếp theo tên là
# xếp theo thời gian; app bấm Gửi hai lần cùng log trong cùng giây vẫn ra hai file
# khác nhau khi text khác, trùng hệt thì ghi đè (vô hại).

_LOG_FILE_RE = re.compile(r"^[A-Za-z0-9_.-]{1,160}\.json$")


def _log_file(name: str) -> str:
    """Tên file log hợp lệ, hoặc 400. KHÔNG dùng safe_name: nó cắt 64 ký tự, còn tên
    ở đây = device(≤64) + dấu thời gian + hash nên dài hơn; đổi tên là 404 sai."""
    if not _LOG_FILE_RE.match(name) or ".." in name:
        raise HTTPException(400, "tên file log không hợp lệ")
    return name


def _log_meta(path) -> dict | None:
    """Metadata một file log (KHÔNG kèm `text`) — file hỏng/sửa tay méo → None, bỏ qua
    thay vì làm 500 cả danh sách."""
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(doc, dict):
        return None
    text = doc.get("text")
    findings = doc.get("findings")
    return {
        "file": path.name,
        "device": str(doc.get("device") or ""),
        "received_at": str(doc.get("received_at") or ""),
        "captured_at": str(doc.get("captured_at") or ""),
        "by": str(doc.get("by") or ""),
        "note": str(doc.get("note") or ""),
        "port": str(doc.get("port") or ""),
        "app": str(doc.get("app") or ""),
        "size": len(text) if isinstance(text, str) else 0,
        "findings": len(findings) if isinstance(findings, list) else 0,
    }


@app.put("/devices/{device}/logs", dependencies=[Depends(auth)])
async def device_log_upload(device: str, request: Request):
    """Nhận một bản log UART của máy `device`. Body JSON:
    `{text (bắt buộc), by, note, port, baud, captured_at, app, findings[]}`.

    Mọi trường ngoài `text` là CLIENT TỰ KHAI (token không mang danh tính) — ghi chú
    vận hành, không phải kiểm toán. Trả `{ok, file, size}`.
    """
    cl = request.headers.get("content-length")
    if not (cl and cl.isdigit()):
        raise HTTPException(411, "content-length required")
    if int(cl) > config.MAX_BODY:
        raise HTTPException(413, "body too large")
    raw = await request.body()
    try:
        data = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise HTTPException(400, f"invalid json: {e}")
    if not isinstance(data, dict):
        raise HTTPException(400, "body phải là JSON object")
    text = data.get("text")
    if not isinstance(text, str) or not text.strip():
        raise HTTPException(400, "thiếu `text` (log rỗng)")
    if len(text) > config.MAX_LOG_TEXT:
        raise HTTPException(413, "log quá dài")

    dev = safe_name(device)
    now = datetime.now(timezone.utc)
    findings = data.get("findings")
    doc = {
        "device": dev,
        "received_at": now.isoformat(),
        "captured_at": str(data.get("captured_at") or "")[:40],
        "by": str(data.get("by") or "")[:64],
        "note": str(data.get("note") or "")[:2000],
        "port": str(data.get("port") or "")[:64],
        "baud": data.get("baud") if isinstance(data.get("baud"), int) else None,
        "app": str(data.get("app") or "")[:64],
        "findings": findings if isinstance(findings, list) else [],
        "text": text,
    }
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:8]
    name = f"{dev}_{now:%Y%m%d_%H%M%S}_{digest}.json"
    # Ghi tạm rồi đổi tên (nguyên tử) — giống ota_upload: GET đang đọc dở không thấy nửa file.
    tmp = config.LOGS_DIR / f".tmp-{name}"
    tmp.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(tmp, config.LOGS_DIR / name)
    print(f"device log {name} ({len(text)} chars, by={doc['by']!r})", flush=True)
    return {"ok": True, "file": name, "size": len(text)}


@app.get("/devices/{device}/logs", dependencies=[Depends(auth)])
def device_logs(device: str, limit: int = Query(50, ge=1, le=500)):
    """Các bản log đã gửi của một máy — **mới nhất trước**, KHÔNG kèm `text`
    (mở từng bản qua `GET /logs/{file}`)."""
    dev = safe_name(device)
    # Glob theo tiền tố rồi KIỂM lại field `device`: "RPL_1_*" cũng khớp file của
    # máy "RPL_1_x" — tên có dấu gạch dưới thì tiền tố không đủ để phân biệt.
    paths = sorted(config.LOGS_DIR.glob(f"{dev}_*.json"), key=lambda p: p.name, reverse=True)
    items = []
    for p in paths:
        m = _log_meta(p)
        if m and m["device"] == dev:
            items.append(m)
        if len(items) >= limit:
            break
    return {"device": dev, "items": items}


@app.get("/logs/{filename}", dependencies=[Depends(auth)])
def device_log_get(filename: str):
    """Một bản log đầy đủ (có `text`)."""
    path = config.LOGS_DIR / _log_file(filename)
    if not path.is_file():
        raise HTTPException(404, "log not found")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise HTTPException(500, "log file hỏng")


# --- Trạm ATE: hồ sơ nghiệm thu máy tại xưởng (app: tab "Sản xuất") ----------
# Một máy qua trạm = 1 file JSON trong config.ATE_DIR, tên:
#   <sn>_<started_at UTC %Y%m%d_%H%M%S>_<sha256(body)[:12]>.json
#
# Tên file mang HẾT khoá idempotent (thời điểm bắt đầu + hash nội dung client
# gửi): trạm mất mạng rồi đẩy lại hàng đợi sẽ ghi đè lên chính file đó thay vì
# sinh hồ sơ trùng — thay cho ràng buộc UNIQUE(body_sha256) của bảng `ate_records`
# trong kế hoạch (docs/plan/ate-san-xuat.md §7.2). Chỉ `received_at` (giờ server)
# đổi giữa hai lần gửi, và nó KHÔNG tham gia hash.
#
# ⚠️ PUT chứ không POST: `POST /{_path:path}` catch-all ingest ở trên nuốt mọi
# POST. Kế hoạch §7.2 đề xuất khai báo `POST /ate/records` TRƯỚC catch-all; ở đây
# chọn PUT vì nó không phụ thuộc thứ tự đăng ký route — cùng lý do với mục OTA và
# log CSKH, và một lần ai đó chuyển route lên/xuống là hồ sơ ATE lại rơi vào
# đường nhận dữ liệu đo mà không ai thấy.

_ATE_FILE_RE = re.compile(r"^[A-Za-z0-9_.-]{1,200}\.json$")

# Bộ ngưỡng CHUNG (khi trạm chưa khai lô) + bộ ngưỡng THEO LÔ.
#
# Tiêu chuẩn là thứ admin đặt cho TỪNG LÔ SẢN XUẤT: lô dùng linh kiện quang khác,
# hoặc chạy ở xưởng có nhiệt phòng khác, thì ngưỡng khác - ép tất cả về một bộ
# chung là hoặc chấm oan lô này, hoặc thả lỏng lô kia.
_ATE_LIMITS_FILE = config.ATE_DIR / "limits.json"
_ATE_LIMITS_DIR = config.ATE_DIR / "limits"


def _ate_batch(batch: str) -> str:
    """Mã lô hợp lệ (dùng làm tên file), hoặc 400."""
    b = batch.strip()
    if not b:
        return ""
    if len(b) > 64 or not re.fullmatch(r"[A-Za-z0-9_.\-]+", b):
        raise HTTPException(400, "mã lô chỉ gồm chữ, số, _ . - và tối đa 64 ký tự")
    return b


def _ate_limits_file(batch: str) -> Path:
    return _ATE_LIMITS_FILE if not batch else _ATE_LIMITS_DIR / f"{batch}.json"

# Cache tóm tắt hồ sơ theo (mtime, size) — `/ate/stats` và `/ate/records` đều
# phải quét CẢ kho để sắp theo thời gian (tên file bắt đầu bằng số máy nên xếp
# theo tên KHÔNG phải xếp theo thời gian). Đọc lại vài nghìn file JSON mỗi lần
# mở màn Thống kê là phí; file không đổi thì tóm tắt cũng không đổi.
_ATE_CACHE: dict[str, tuple[float, int, dict]] = {}


def _ate_file(name: str) -> str:
    """Tên file hồ sơ hợp lệ, hoặc 400. KHÔNG dùng safe_name (nó cắt 64 ký tự,
    còn tên ở đây = sn + dấu thời gian + hash nên dài hơn → đổi tên = 404 sai)."""
    if not _ATE_FILE_RE.match(name) or ".." in name:
        raise HTTPException(400, "tên file hồ sơ không hợp lệ")
    return name


def _ate_stamp(value: str) -> str:
    """`started_at` ISO → `%Y%m%d_%H%M%S` UTC cho tên file; hỏng/thiếu → giờ hiện
    tại. Tên file phải xếp được theo thời gian nên không được để rỗng."""
    try:
        dt = datetime.fromisoformat(str(value).strip().replace("Z", "+00:00"))
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return f"{dt.astimezone(timezone.utc):%Y%m%d_%H%M%S}"
    except (ValueError, TypeError):
        return f"{datetime.now(timezone.utc):%Y%m%d_%H%M%S}"


def _ate_metas() -> list[dict]:
    """Tóm tắt MỌI hồ sơ trong kho, mới nhất trước. File hỏng/sửa tay méo → bỏ
    qua (một file rác không được làm 500 cả màn thống kê)."""
    out = []
    seen = set()
    for p in config.ATE_DIR.glob("*.json"):
        if p.name == _ATE_LIMITS_FILE.name:
            continue
        seen.add(p.name)
        try:
            st = p.stat()
        except OSError:
            continue
        hit = _ATE_CACHE.get(p.name)
        if hit and hit[0] == st.st_mtime and hit[1] == st.st_size:
            out.append(hit[2])
            continue
        doc = _ate_doc(p)
        if doc is None:
            continue
        meta = ate_meta(doc, p.name)
        _ATE_CACHE[p.name] = (st.st_mtime, st.st_size, meta)
        out.append(meta)
    for gone in set(_ATE_CACHE) - seen:  # file bị xoá tay → đừng giữ trong cache
        _ATE_CACHE.pop(gone, None)
    out.sort(key=lambda m: str(m.get("started_at") or m.get("received_at") or ""),
             reverse=True)
    return out


def _ate_doc(path) -> dict | None:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return doc if isinstance(doc, dict) else None


@app.put("/ate/records", dependencies=[Depends(auth)])
async def ate_record_put(request: Request):
    """Trạm đẩy một hồ sơ nghiệm thu. Body JSON:
    `{sn, verdict, limits_ver, steps[], started_at, finished_at, station, operator,
    fw_version, fw_sha256, pcb_version, mac, calib, note, app}`.

    Trả `{ok, id, file, sn, verdict}` — `id` chính là tên file, dùng cho
    `GET /ate/records/{id}`. Gửi lại cùng nội dung = cùng `id` (idempotent).
    """
    cl = request.headers.get("content-length")
    if not (cl and cl.isdigit()):
        raise HTTPException(411, "content-length required")
    if int(cl) > config.MAX_BODY:
        raise HTTPException(413, "body too large")
    raw = await request.body()
    try:
        data = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise HTTPException(400, f"invalid json: {e}")
    err = validate_ate_record(data)
    if err:
        raise HTTPException(400, err)

    now = datetime.now(timezone.utc)
    doc = normalize_ate_record(data, now.isoformat())
    name = (f"{safe_name(doc['sn'])}_{_ate_stamp(doc['started_at'])}"
            f"_{canonical_sha256(data).hex()[:12]}.json")
    # Ghi tạm rồi đổi tên (nguyên tử) — GET đang đọc dở không thấy nửa file.
    tmp = config.ATE_DIR / f".tmp-{name}"
    tmp.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(tmp, config.ATE_DIR / name)
    _ATE_CACHE.pop(name, None)
    print(f"ate record {name} verdict={doc['verdict']} by={doc['operator']!r}", flush=True)
    return {"ok": True, "id": name, "file": name, "sn": doc["sn"], "verdict": doc["verdict"]}


@app.get("/ate/records", dependencies=[Depends(auth)])
def ate_records(
    sn: str = "",
    batch: str = "",
    verdict: str = "",
    from_: str = Query(None, alias="from"),
    to: str = Query(None),
    page: int = Query(1, ge=1),
    limit: int = Query(50, ge=1, le=500),
):
    """Tra cứu hồ sơ (mới nhất trước), phân trang như `/sessions`. KHÔNG kèm
    `steps` — mở từng hồ sơ bằng `GET /ate/records/{id}`."""
    _check_date(from_, "from")
    _check_date(to, "to")
    items = [m for m in _ate_metas()
             if ate_match(m, sn.strip(), from_ or "", to or "", verdict.strip(),
                          batch.strip())]
    off = (page - 1) * limit
    return {"total": len(items), "page": page, "limit": limit,
            "items": items[off:off + limit]}


@app.get("/ate/stats", dependencies=[Depends(auth)])
def ate_stats_api(from_: str = Query(None, alias="from"), to: str = Query(None),
                  batch: str = ""):
    """FPY, sản lượng theo ngày, Pareto mã bước hỏng. Xem `logic.ate_stats` để
    biết FPY tính theo LẦN THỬ ĐẦU của mỗi máy, không phải tỉ lệ hồ sơ PASS."""
    _check_date(from_, "from")
    _check_date(to, "to")
    items = [m for m in _ate_metas()
             if ate_match(m, "", from_ or "", to or "", "", batch.strip())]
    return ate_stats(items)


@app.get("/ate/limits", dependencies=[Depends(auth)])
def ate_limits_get(batch: str = ""):
    """Bộ ngưỡng ÁP DỤNG cho một lô. Thứ tự lùi: bộ của lô → bộ chung → mặc định.

    Trả thêm `batch` + `source` (`batch` | `chung` | `mặc định`) để trạm hiện rõ
    đang chấm theo bộ nào — "tưởng đang chấm theo tiêu chuẩn của lô mà thật ra
    đang chạy bộ mặc định" là kiểu nhầm không ai phát hiện ra từ màn hình.
    """
    b = _ate_batch(batch)
    doc = _ate_doc(_ate_limits_file(b)) if b and _ate_limits_file(b).is_file() else None
    source = "batch"
    if doc is None:
        doc = _ate_doc(_ATE_LIMITS_FILE) if _ATE_LIMITS_FILE.is_file() else None
        source = "chung"
    if doc is None:
        doc, source = dict(DEFAULT_ATE_LIMITS), "mặc định"
    return {**doc, "batch": b, "source": source}


@app.get("/ate/limits/list", dependencies=[Depends(auth)])
def ate_limits_list():
    """Các lô đã có bộ ngưỡng riêng + bộ chung — cho màn Tiêu chuẩn của admin."""
    items = []
    if _ATE_LIMITS_FILE.is_file():
        d = _ate_doc(_ATE_LIMITS_FILE) or {}
        items.append({"batch": "", "version": d.get("version", ""),
                      "updated_at": d.get("updated_at", ""),
                      "updated_by": d.get("updated_by", "")})
    if _ATE_LIMITS_DIR.is_dir():
        for p in sorted(_ATE_LIMITS_DIR.glob("*.json")):
            d = _ate_doc(p) or {}
            items.append({"batch": p.stem, "version": d.get("version", ""),
                          "updated_at": d.get("updated_at", ""),
                          "updated_by": d.get("updated_by", "")})
    return {"items": items}


@app.put("/ate/limits", dependencies=[Depends(ota_admin)])
async def ate_limits_put(request: Request, by: str = "", batch: str = ""):
    """Đặt bộ ngưỡng mới. Gác bằng token admin OTA — quyền lớn nhất server có
    (nó KHÔNG biết vai trò tài khoản); kế hoạch §7.2 muốn "chỉ root", và trong
    app chỉ root mới thấy nút này. Đổi ngưỡng là hành động có chủ đích: mọi hồ sơ
    ghi kèm `limits_ver`, nên máy đã nghiệm thu vẫn tra lại được nó bị chấm theo
    bộ nào."""
    raw = await request.body()
    try:
        data = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise HTTPException(400, f"invalid json: {e}")
    err = validate_ate_limits(data)
    if err:
        raise HTTPException(400, err)
    b = _ate_batch(batch)
    path = _ate_limits_file(b)
    # MỘT version = MỘT nội dung. Sửa ngưỡng thì đổi version, không thì hồ sơ cũ
    # (chỉ ghi `limits_ver`) mất khả năng truy ngược.
    old = _ate_doc(path) if path.is_file() else None
    clash = ate_limits_conflict(old, data)
    if clash:
        raise HTTPException(400, clash)
    data["updated_at"] = datetime.now(timezone.utc).isoformat()
    data["updated_by"] = str(by or "")[:64]
    data["batch"] = b
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.parent / f".tmp-{path.name}"
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(tmp, path)
    # Nhật ký append-only: đổi ngưỡng là hành động phải giải trình được.
    try:
        with (config.ATE_DIR / "limits_history.jsonl").open("a", encoding="utf-8") as f:
            f.write(json.dumps(data, ensure_ascii=False) + "\n")
    except OSError as e:
        print(f"ate limits history failed: {e}", flush=True)
    print(f"ate limits[{b or 'chung'}] -> {data['version']} by={data['updated_by']!r}",
          flush=True)
    return {"ok": True, "version": data["version"], "batch": b}


@app.get("/ate/sn/{sn}", dependencies=[Depends(auth)])
def ate_by_sn(sn: str):
    """**Hồ sơ khai sinh** của một máy + mọi lần test lại.

    `birth` = lần PASS ĐẦU TIÊN (máy ra xưởng theo bản ghi nào), `records` = tất
    cả các lần, mới nhất trước. Máy phải chạy lại 3 lần mới đạt thì cả 3 đều ở
    đây — retry cũng là dữ liệu.
    """
    key = sn.strip()
    items = [m for m in _ate_metas() if str(m.get("sn") or "").lower() == key.lower()]
    passes = [m for m in items if str(m.get("verdict") or "").lower() == "pass"]
    return {
        "sn": key,
        "records": items,
        "attempts": len(items),
        "birth": passes[-1] if passes else None,  # items mới→cũ nên PASS cũ nhất ở cuối
    }


@app.get("/ate/records/{filename}", dependencies=[Depends(auth)])
def ate_record_get(filename: str):
    """Một hồ sơ đầy đủ (có `steps`, kèm log thô từng bước)."""
    path = config.ATE_DIR / _ate_file(filename)
    if not path.is_file():
        raise HTTPException(404, "ate record not found")
    doc = _ate_doc(path)
    if doc is None:
        raise HTTPException(500, "file hồ sơ hỏng")
    return doc


# App WEB (Flutter build web, build với --base-href /app/) serve tĩnh CÙNG ORIGIN
# với API → không dính CORS. Mount SAU các route API (path /app không đụng ai);
# thiếu thư mục (máy dev/test) thì bỏ qua. html=True → /app/ trả index.html.
if config.WEB_DIR.is_dir():
    from fastapi.staticfiles import StaticFiles

    app.mount("/app", StaticFiles(directory=config.WEB_DIR, html=True), name="webapp")
