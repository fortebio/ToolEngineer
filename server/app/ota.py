"""Kho firmware OTA tách THEO SẢN PHẨM — file, không DB (2026-09-11, giai đoạn 0 của
docs/plan/ota-nhieu-san-pham.md ở repo app).

Bố cục trên đĩa:
    OTA_DIR/
      products/<product>/<file>.bin          ảnh firmware
      products/<product>/<file>.bin.json     manifest {product, ver, hw[], size, sha256, tag, by, at, note}
      products/<product>/target.json         {target, devices{id: pin}} — mỗi sản phẩm MỘT file
      devices.json                           MÁY → KHO gán tay {id: {product, group, note, by, at}}
      fw_seen.json, fw_log.json              version máy tự khai (vẫn ở main.py)

Vì sao tách: trước đây kho phẳng + MỘT bản chung cho cả server, nên đặt target cho dòng
máy thứ hai là 109 máy RapidPlus cũng bị mời nạp ảnh đó ở lượt poll 6 h tới. Giờ `/ota/check`
của máy sản phẩm A không bao giờ đọc target.json của sản phẩm B.

Máy thuộc kho nào — BỐN nguồn, thứ tự cố định ([product_for], 2026-09-18,
docs/plan/ota-quan-ly-may-nhieu-san-pham.md §2.1):
    (1) máy TỰ KHAI `?product=`            (firmware ≥ v2.4.6, rapid4p)   — luôn thắng
  > (2) GÁN TAY `devices.json`              (người vận hành: "RPL00123 là rapidplus-a")
  > (3) tiền tố mã máy                      `OTA_LEGACY_PRODUCT_BY_PREFIX`
  > (4) `config.LEGACY_PRODUCT`
(2) là mảnh cho fleet Rapid+ hôm nay: 109 máy cùng tiền tố RPL, không tự khai, nhưng biến thể
`a` (SHAPE_RULE_NEGATIVE) phải nhận bản `a` chứ không phải bản AT của kho chung. File cũ ở gốc
OTA_DIR được [migrate_legacy] dời vào kho legacy lúc khởi động. Đọc `config.OTA_DIR`/
`LEGACY_PRODUCT` QUA HÀM chứ không chụp lúc import, để test đổi thư mục được.

Không import fastapi: lỗi trả về bằng [OtaError(status, detail)], route ở main.py đổi sang
HTTPException. Không import app.main (vòng).
"""
import hashlib
import json
import os
import re
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path

from app import config
from app.logic import (expected_bin_name, image_tag, norm_version, product_key, safe_name,
                       ver_from_name)

# Trần SỐ MÁY ở mọi file theo-máy của kho (devices.json ở đây, fw_seen/fw_log ở main.py):
# `device` là query param tuỳ ý và mỗi lượt ghi đọc+ghi TRỌN file trong lock một-worker.
# Fleet thật ~109; 500 dư mấy lần mà file vẫn cỡ KB.
MAX_DEVICES = 500
# Mã máy an toàn — đúng bộ ký tự firmware lọc (updateOTA.cpp urlSafe) và main.py _ID_OK.
_ID_RE = re.compile(r"[A-Za-z0-9._-]{1,32}")


class OtaError(Exception):
    """Lỗi nghiệp vụ của kho: `status` HTTP + `detail` tiếng Việt cho app hiện."""

    def __init__(self, status: int, detail: str):
        super().__init__(detail)
        self.status = status
        self.detail = detail


# --- đường dẫn ---------------------------------------------------------------------------

def root() -> Path:
    return config.OTA_DIR


def products_root() -> Path:
    return root() / "products"


def product_dir(product: str) -> Path:
    return products_root() / product


def legacy_product_for(device: str = "") -> str:
    """Sản phẩm cho máy KHÔNG tự khai: tiền tố mã máy dài nhất khớp
    `OTA_LEGACY_PRODUCT_BY_PREFIX`, không có thì `LEGACY_PRODUCT`."""
    best, hit = "", config.LEGACY_PRODUCT
    for pre, prod in config.LEGACY_PRODUCT_BY_PREFIX.items():
        if device and device.startswith(pre) and len(pre) > len(best):
            best, hit = pre, prod
    return hit


# --- devices.json: máy → kho GÁN TAY -------------------------------------------------------

def _devices_file() -> Path:
    return root() / "devices.json"


def _dev_entry(v) -> dict | None:
    """Một mục devices.json, đọc PHÒNG THỦ (file sửa tay được như target.json): thiếu/méo
    `product` → bỏ mục, không được làm `/ota/check` hay `/devices` 500 vì một dòng gõ sai."""
    if not isinstance(v, dict) or not product_key(v.get("product")):
        return None
    return {"product": v["product"], "group": str(v.get("group") or "")[:32],
            "note": str(v.get("note") or "")[:200], "by": str(v.get("by") or "")[:64],
            "at": str(v.get("at") or "")}


def read_devices() -> dict[str, dict]:
    """{id: {product, group, note, by, at}} — rỗng nếu chưa gán máy nào / file hỏng."""
    try:
        data = json.loads(_devices_file().read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(data, dict):
        return {}
    return {k: e for k, v in data.items()
            if isinstance(k, str) and _ID_RE.fullmatch(k) and (e := _dev_entry(v)) is not None}


def _save_devices(devs: dict) -> None:
    root().mkdir(parents=True, exist_ok=True)
    _atomic_json(_devices_file(), devs)


def product_for(device: str = "", declared: str = "", devices: dict | None = None) -> tuple[str, bool]:
    """Kho `/ota/check` THẬT SỰ tra cho [device] → `(product, conflict)`.

    Thứ tự: máy tự khai [declared] > gán tay devices.json > tiền tố > legacy. `conflict` = máy
    tự khai KHÁC bản gán tay: có người nạp tay một bản khác hồ sơ — thông tin đúng để app tô
    cảnh báo, không phải nhiễu. [devices] truyền vào khi gọi cho nhiều máy (đọc file một lần).
    """
    devs = read_devices() if devices is None else devices
    assigned = (devs.get(device) or {}).get("product", "") if device else ""
    if declared:
        return declared, bool(assigned and assigned != declared)
    return (assigned or legacy_product_for(device)), False


def assign_product(ids: list[str], product: str, *, by: str = "", note: str = "") -> dict:
    """Gán các máy vào kho [product] (ghi đè gán cũ, GIỮ `group`). Trả {id: mục} đã ghi.
    Mã máy sai bộ ký tự → 400; vượt trần MAX_DEVICES → 400 (không ghi nửa chừng)."""
    ids = [str(i).strip() for i in ids if str(i).strip()]
    if not ids:
        raise OtaError(400, "thiếu mã máy")
    bad = [i for i in ids if not _ID_RE.fullmatch(i)]
    if bad:
        raise OtaError(400, f"mã máy không hợp lệ: {', '.join(bad[:5])}")
    if not product_key(product):
        raise OtaError(400, "khoá sản phẩm không hợp lệ")
    now = _now()
    with _CFG_LOCK:
        devs = read_devices()
        if len(set(devs) | set(ids)) > MAX_DEVICES:
            raise OtaError(400, f"vượt trần {MAX_DEVICES} máy gán tay")
        out = {}
        for i in ids:
            old = devs.get(i) or {}
            devs[i] = out[i] = {"product": product, "group": old.get("group", ""),
                                "note": note[:200] or old.get("note", ""), "by": by[:64], "at": now}
        _save_devices(devs)
    return out


def unassign_product(device: str) -> bool:
    """Bỏ gán → máy về tiền tố/legacy. Trả True nếu có mục để bỏ."""
    with _CFG_LOCK:
        devs = read_devices()
        had = devs.pop(device, None) is not None
        if had:
            _save_devices(devs)
    return had
# `group` trong mục devices.json để sẵn cho ghim theo nhóm/lô (B4 của plan) — chưa có API.


def list_products() -> list[str]:
    """Mọi khoá sản phẩm có thư mục + LUÔN có sản phẩm kế thừa (dù kho còn trống) để
    app không bao giờ thấy danh sách rỗng trên một server đang phục vụ 109 máy."""
    keys = {config.LEGACY_PRODUCT}
    pr = products_root()
    if pr.is_dir():
        for p in pr.iterdir():
            if p.is_dir() and product_key(p.name):
                keys.add(p.name)
    return sorted(keys)


def is_bin_name(name) -> bool:
    """Tên file .bin an toàn: đúng bộ ký tự của safe_name, đuôi .bin, KHÔNG bắt đầu bằng `.`
    (file tạm `.tmp-<name>` của upload dở dang cũng là *.bin)."""
    return (isinstance(name, str) and bool(name) and safe_name(name) == name
            and name.lower().endswith(".bin") and not name.startswith("."))


def bin_name(filename: str) -> str:
    """Tên file .bin an toàn (chặn path traversal); sai → 400."""
    if not is_bin_name(filename):
        raise OtaError(400, "tên file phải là .bin hợp lệ")
    return filename


def _bins(d: Path) -> list[Path]:
    """Các ảnh trong một kho. ⚠️ `Path.glob('*.bin')` KHỚP CẢ dotfile (khác glob của shell) →
    phải lọc `.tmp-*.bin` (upload bị ngắt) ra, không thì nó được liệt kê/di cư như ảnh thật."""
    if not d.is_dir():
        return []
    return [p for p in d.glob("*.bin") if p.is_file() and not p.name.startswith(".")]


# Mọi read-modify-write của target.json đi qua khoá này: set/clear/delete chạy trong threadpool
# (route `def`), hai request ghi cùng lúc mà không khoá là một ghim biến mất không báo gì.
# Lock trong tiến trình đủ vì service chạy MỘT worker (cùng giả định với _FW_LOCK ở main.py).
_CFG_LOCK = threading.Lock()
# Khoá riêng cho upload: giữ trong lúc ghi 2,4 MB (vài ms trên SSD) — không kẹp chung
# _CFG_LOCK để `/ota/check` của fleet đang set_target/… không phải đợi một upload.
_UPLOAD_LOCK = threading.Lock()


def _atomic_json(path: Path, obj) -> None:
    """Ghi JSON NGUYÊN TỬ: `/ota/check` của cả fleet đọc các file này trên mọi request,
    ghi đè tại chỗ bị đọc trúng giữa chừng trả JSON cụt → máy coi như 'không có bản nào'."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


# --- target.json (mỗi sản phẩm một file) -------------------------------------------------

def pin_entry(v) -> dict | None:
    """Một mục ghim, chấp nhận CẢ HAI dạng đã từng ghi ra `target.json`.

    Dạng đầu (2026-08-18 sáng) là chuỗi tên file trần; dạng hiện tại là
    `{"file": ..., "by": ..., "at": ...}`. File này sửa tay được và tồn tại trên box từ
    trước, nên đọc phòng thủ ở đúng một chỗ thay vì rải `isinstance` khắp nơi.

    ⚠️ `by` là do CLIENT tự khai — token admin không mang danh tính nào để server đối
    chiếu. Nó là **ghi chú vận hành**, không phải nhật ký kiểm toán.
    """
    if isinstance(v, str):
        return {"file": v, "by": "", "at": ""} if is_bin_name(v) else None
    if isinstance(v, dict) and is_bin_name(v.get("file")):
        return {"file": v["file"], "by": str(v.get("by") or ""), "at": str(v.get("at") or "")}
    # Tên không an toàn (`../x.bin`, `.tmp-x.bin`): bỏ mục — đọc/ghi manifest theo nó là ra ngoài kho.
    return None


def read_cfg(product: str) -> dict:
    """Nội dung target.json của [product]: {"target": pin|None, "devices": {<id>: pin}}."""
    try:
        cfg = json.loads((product_dir(product) / "target.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        cfg = {}
    if not isinstance(cfg, dict):
        cfg = {}
    devs = cfg.get("devices")
    devs = devs if isinstance(devs, dict) else {}
    return {
        "target": pin_entry(cfg.get("target")),
        "devices": {k: e for k, v in devs.items() if (e := pin_entry(v)) is not None},
    }


def save_cfg(product: str, cfg: dict) -> None:
    d = product_dir(product)
    d.mkdir(parents=True, exist_ok=True)
    _atomic_json(d / "target.json", {"target": cfg.get("target"),
                                     "devices": cfg.get("devices") or {}})


# --- manifest (mỗi .bin một .json cạnh nó) -----------------------------------------------

def manifest_path(product: str, name: str) -> Path:
    return product_dir(product) / f"{name}.json"


def _tag_products(product: str) -> set[str]:
    """Các khoá kho mà `esp_app_desc_t.project_name` được coi là thẻ: kho đích + mọi kho đã
    có (để ảnh rapid4p tải nhầm vào rapidplus vẫn bị 400 "nhầm kho"). Xem logic.image_tag."""
    return {product, *list_products()}


def build_manifest(product: str, name: str, raw: bytes, *, by: str = "", note: str = "",
                   at: str = "", source: str = "upload") -> dict:
    """Manifest từ BYTES: có thẻ (FBTIMG1 nhúng, hoặc `esp_app_desc_t` của ảnh ESP-IDF —
    logic.image_tag) thì ver/hw lấy từ thẻ, không thì ver suy từ tên và hw = None (= không
    biết = KHÔNG lọc, đúng hành vi trước khi có manifest). `md5` để `GET /ota/{p}/{f}` trả
    header `x-MD5` mà không băm lại 2,4 MB mỗi lượt máy tải."""
    try:
        tag, src = image_tag(raw, _tag_products(product))
    except ValueError:
        tag, src = None, None  # ảnh dị dạng: upload() đã chặn; dựng lại từ đĩa thì coi như không thẻ
    return {
        "product": product,
        "ver": (tag["ver"] if tag and tag["ver"] else ver_from_name(name)),
        "hw": (tag["hw"] or None) if tag else None,
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "md5": hashlib.md5(raw).hexdigest(),
        "tag": tag["raw"] if tag else None,
        "tag_source": src,
        # product THẺ khai — khác thư mục chứa nó là ảnh nằm nhầm kho (scp tay); resolve từ chối.
        "tag_product": tag["product"] if tag else None,
        "by": by[:64],
        "note": note[:200],
        "at": at or _now(),
        "source": source,
    }


def _norm_hw(v) -> list[str] | None:
    if isinstance(v, str):
        v = v.split(",")
    if not isinstance(v, list):
        return None
    out = [str(h).strip().upper() for h in v if str(h).strip()]
    return out or None


def read_manifest(product: str, name: str, cache: dict | None = None) -> dict | None:
    """Manifest của một ảnh; None nếu .bin không tồn tại.

    Manifest thiếu hoặc `size` lệch với file (ảnh scp thẳng lên box, hoặc ghi đè tay) →
    DỰNG LẠI từ bytes rồi ghi xuống — kho tự lành, không ai phải chạy lệnh gì. Ghi hỏng
    (đĩa chỉ đọc) thì vẫn trả manifest vừa dựng: đường đọc không được phép 500.

    [cache] (dict, sống trong MỘT request): `/devices` gọi resolve cho 100+ máy mà kho chỉ có
    vài ảnh — đọc mỗi manifest một lần thay vì mỗi máy một lần.
    """
    key = ("m", product, name)
    if cache is not None and key in cache:
        return cache[key]
    m = _read_manifest(product, name)
    if cache is not None:
        cache[key] = m
    return m


def _read_manifest(product: str, name: str) -> dict | None:
    path = product_dir(product) / name
    if not path.is_file():
        return None
    st = path.stat()
    mp = manifest_path(product, name)
    try:
        m = json.loads(mp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        m = None
    if isinstance(m, dict) and m.get("size") == st.st_size and isinstance(m.get("sha256"), str):
        m.setdefault("product", product)
        m.setdefault("ver", ver_from_name(name))
        # hw sửa tay có thể là chuỗi hay chữ thường → ép về đúng dạng build_manifest sinh
        # (list HOA hoặc None), không thì `in` thành so chuỗi con / lệch hoa-thường.
        m["hw"] = _norm_hw(m.get("hw"))
        return m
    m = build_manifest(product, name, path.read_bytes(), source="rebuilt",
                       at=datetime.fromtimestamp(st.st_mtime, timezone.utc)
                       .isoformat(timespec="seconds"))
    try:
        _atomic_json(mp, m)
    except OSError:
        pass
    return m


def md5_for(product: str, name: str) -> str | None:
    """MD5 cho header `x-MD5` khi máy tải ảnh. Manifest cũ (trước 2026-09-18) không có `md5`
    → tính một lần rồi ghi bổ sung; ghi hỏng thì vẫn trả — đường tải không được phép 500."""
    m = read_manifest(product, name)
    if m is None:
        return None
    if isinstance(m.get("md5"), str) and len(m["md5"]) == 32:
        return m["md5"]
    m["md5"] = hashlib.md5((product_dir(product) / name).read_bytes()).hexdigest()
    try:
        _atomic_json(manifest_path(product, name), m)
    except OSError:
        pass
    return m["md5"]


# --- chọn bản cho một máy ------------------------------------------------------------------

def cached_cfg(product: str, cache: dict | None = None) -> dict:
    key = ("cfg", product)
    if cache is not None and key in cache:
        return cache[key]
    cfg = read_cfg(product)
    if cache is not None:
        cache[key] = cfg
    return cfg


def resolve(product: str, device: str = "", hw: str = "",
            cache: dict | None = None) -> tuple[dict | None, str]:
    """Bản firmware dành cho [device] của [product] → `({file, manifest, pinned}, "")`,
    hoặc `(None, reason)` với reason ∈ none | pin-missing | hw | tag.

    Ghim theo TỪNG MÁY thắng bản chung. Ghim mà file đã mất → KHÔNG rơi về bản chung
    (rơi về là đẩy đúng cái máy vừa được cố ý giữ lại đi lên phía trước). `hw` chỉ lọc khi
    CẢ máy có khai LẪN manifest có danh sách — thiếu một bên là không biết = không lọc.
    [cache]: xem read_manifest — `/devices` gọi hàm này cho từng máy.
    """
    cfg = cached_cfg(product, cache)
    pin = cfg["devices"].get(device) if device else None
    entry = pin or cfg["target"]
    if not entry:
        return None, "none"
    m = read_manifest(product, entry["file"], cache)
    if m is None:
        return None, "pin-missing" if pin else "none"
    if m.get("tag_product") and m["tag_product"] != product:
        return None, "tag"
    if hw and m.get("hw") and hw.upper() not in m["hw"]:
        return None, "hw"
    return {"file": entry["file"], "manifest": m, "pinned": bool(pin)}, ""


# Trạng thái OTA của MỘT máy — quyết định ở ĐÚNG MỘT chỗ cho app/CSV/web/feed ERP:
#   none     kho chưa có bản cho máy (none | pin-missing | tag)
#   skipped  bị lọc PCB (reason hw)
#   on       version máy == manifest.ver (so giữ hậu tố — logic.norm_version)
#   offered  đã được mời ĐÚNG bản này ở lượt poll gần nhất, chưa nạp (Rapid+: chờ người bấm RED)
#   waiting  chưa được mời bản này (chưa poll từ lúc đặt target, hoặc lần cuối được mời bản khác)
#   unknown  máy chưa báo version
OTA_STATES = ("none", "skipped", "on", "offered", "waiting", "unknown")


def device_status(product: str, device: str, *, version: str = "", hw: str = "",
                  offered: dict | None = None, cache: dict | None = None) -> dict:
    """Khối `ota` của một dòng `/devices`: {target, ver, pinned, reason, state, offered_at}."""
    hit, reason = resolve(product, device, hw, cache)
    out = {"target": None, "ver": "", "pinned": False, "reason": reason, "state": "none",
           "offered_at": ""}
    if hit is None:
        out["state"] = "skipped" if reason == "hw" else "none"
        return out
    out.update(target=hit["file"], ver=hit["manifest"].get("ver", ""), pinned=hit["pinned"])
    if not str(version or "").strip():
        out["state"] = "unknown"
    elif norm_version(version) == norm_version(out["ver"]):
        out["state"] = "on"
    elif offered and offered.get("file") == hit["file"]:
        out["state"] = "offered"
        out["offered_at"] = str(offered.get("at") or "")
    else:
        out["state"] = "waiting"
    return out


def target_file(product: str) -> str | None:
    """Tên bản CHUNG đang chọn (None nếu chưa chọn hoặc file đã mất) — cho danh sách."""
    hit, _ = resolve(product)
    return hit["file"] if hit and not hit["pinned"] else None


# --- thao tác kho ------------------------------------------------------------------------------

def upload(product: str, name: str | None, raw: bytes, *, force: bool = False,
           by: str = "", note: str = "") -> dict:
    """Ghi một ảnh vào kho [product]. Trả manifest (+ `name`, `existed`).

    Quy tắc, theo thứ tự:
      1. Ảnh chứa ≥ 2 thẻ FBTIMG1 KHÁC nhau → 400 (ảnh dị dạng).
      2. Có thẻ (FBTIMG1, hoặc `esp_app_desc_t` của ảnh ESP-IDF): `product` thẻ phải = kho;
         TÊN DO SERVER ĐẶT từ thẻ ([expected_bin_name]); client có gửi tên thì phải trùng —
         tên nói một version, thẻ nói version khác là đúng cái vụ "hai ảnh một tên" cần chặn.
      3. Không thẻ: phải có tên; kho bị `OTA_REQUIRE_TAG` ([config.require_tag]) thì cần `force=1`.
      4. Tên đã có trong kho: cùng sha256 → idempotent (không ghi lại); khác → **409**
         (một tên = một nội dung; muốn thay thì xoá trước — thao tác có chủ ý, có dấu vết).
         Kiểm + ghi nằm trong CÙNG khoá `_UPLOAD_LOCK` và file tạm có tên DUY NHẤT: hai PUT
         cùng tên đồng thời từng lách 409 (cả hai qua `is_file()` = False, chung `.tmp-<name>`
         → Windows 500 PermissionError, Linux bản sau đè — kiểm thực tế 2026-09-18).
    """
    if not raw:
        raise OtaError(400, "file rỗng")
    try:
        tag, _src = image_tag(raw, _tag_products(product))
    except ValueError:
        raise OtaError(400, "ảnh chứa nhiều thẻ nhận dạng khác nhau — ảnh dị dạng, không nhận")
    if tag:
        if tag["product"] is None:
            raise OtaError(400, "thẻ nhận dạng trong ảnh có product không hợp lệ")
        if tag["product"] != product:
            raise OtaError(400, f"ảnh tự khai product {tag['product']!r}, không phải {product!r}"
                                " — tải vào đúng kho sản phẩm")
        if not tag["ver"]:
            raise OtaError(400, "thẻ nhận dạng trong ảnh thiếu ver")
        expected = expected_bin_name(product, tag["ver"], config.LEGACY_PRODUCT)
        if safe_name(expected) != expected:
            raise OtaError(400, f"ver trong thẻ ({tag['ver']!r}) chứa ký tự không dùng được cho tên file")
        if name and name != expected:
            raise OtaError(400, f"ảnh tự khai {tag['ver']} → tên file phải là {expected} (không phải {name})")
        name = expected
    else:
        if not name:
            raise OtaError(400, "ảnh không có thẻ nhận dạng nhúng — phải đặt tên file (PUT /ota/<product>/<file>.bin)")
        if config.require_tag(product) and not force:
            raise OtaError(400, f"kho {product} bắt buộc ảnh có thẻ nhận dạng (FBTIMG1 hoặc esp_app_desc) — "
                                "thêm ?force=1 nếu chắc chắn")
    d = product_dir(product)
    path = d / name
    with _UPLOAD_LOCK:
        if path.is_file():
            old = read_manifest(product, name) or {}
            new_sha = hashlib.sha256(raw).hexdigest()
            if old.get("sha256") == new_sha:
                return {**old, "name": name, "existed": True}
            raise OtaError(409, f"{name} đã có trong kho {product} với nội dung KHÁC — "
                                "một tên = một nội dung; xoá bản cũ trước nếu thật sự muốn thay")
        # Ghi file tạm rồi ĐỔI TÊN: `GET /ota/…` đang stream 2.4 MB cho một máy giữ inode cũ tới
        # khi xong, không bao giờ thấy nửa file. Tên tạm bắt đầu bằng `.` để _bins()/migrate bỏ qua.
        tmp = d / f".tmp-{name}.{os.getpid()}.{uuid.uuid4().hex[:8]}"
        try:
            d.mkdir(parents=True, exist_ok=True)
            tmp.write_bytes(raw)
            os.replace(tmp, path)
            m = build_manifest(product, name, raw, by=by, note=note)
            _atomic_json(manifest_path(product, name), m)
        except OSError as e:
            tmp.unlink(missing_ok=True)
            # Đĩa đầy / mất quyền ghi: trả lỗi CÓ CHỮ cho app, không phải traceback 500 trần.
            raise OtaError(507 if getattr(e, "errno", 0) == 28 else 500,
                           f"không ghi được {name} vào kho {product}: {e.strerror or e}")
    return {**m, "name": name, "existed": False}


def set_target(product: str, name: str, *, device: str = "", by: str = "") -> None:
    if not (product_dir(product) / name).is_file():
        raise OtaError(404, "firmware not found")
    entry = {"file": name, "by": by[:64], "at": _now()}
    with _CFG_LOCK:
        cfg = read_cfg(product)
        if device:
            cfg["devices"][safe_name(device)] = entry
        else:
            cfg["target"] = entry
        save_cfg(product, cfg)


def clear_target(product: str, *, device: str = "") -> dict:
    """Huỷ chọn. Không `device` → bỏ bản CHUNG **và giữ nguyên mọi ghim riêng**;
    có `device` → chỉ gỡ ghim máy đó. Trả cfg sau khi ghi."""
    with _CFG_LOCK:
        cfg = read_cfg(product)
        if device:
            cfg["devices"].pop(safe_name(device), None)
        else:
            cfg["target"] = None
        save_cfg(product, cfg)
    return cfg


def delete_bin(product: str, name: str) -> None:
    """Xoá ảnh + manifest, và dọn MỌI lựa chọn trỏ tới nó trong kho [product] — để trạng
    thái "ghim vào file không còn tồn tại" không thể phát sinh qua API."""
    d = product_dir(product)
    (d / name).unlink(missing_ok=True)
    manifest_path(product, name).unlink(missing_ok=True)
    with _CFG_LOCK:
        cfg = read_cfg(product)
        if (cfg["target"] or {}).get("file") == name:
            cfg["target"] = None
        cfg["devices"] = {k: e for k, e in cfg["devices"].items() if e["file"] != name}
        save_cfg(product, cfg)


def listing(product: str, effective: dict[str, str] | None = None) -> dict:
    """Nội dung kho một sản phẩm — hình dạng `GET /ota` cũ + `product` + trường manifest.

    [effective] = {id_device: kho máy đó THẬT SỰ tra} (main.py dựng từ fw_seen ∪ devices.json).
    Ghim của máy mà kho thật ≠ [product] được đánh `stale: true` — máy đã đổi kho (tự khai
    hoặc gán tay) nên ghim này không ai đọc; app có số để dọn. Máy chưa từng thấy → không
    kết luận, không đánh.
    """
    d = product_dir(product)
    files = sorted(_bins(d), key=lambda p: p.stat().st_mtime, reverse=True)
    cfg = read_cfg(product)
    tgt = cfg["target"] or {}
    out = []
    for p in files:
        m = read_manifest(product, p.name) or {}
        out.append({
            "name": p.name, "size": p.stat().st_size,
            "modified": datetime.fromtimestamp(p.stat().st_mtime, timezone.utc),
            "ver": m.get("ver", ""), "hw": m.get("hw"), "sha256": m.get("sha256", ""),
            "md5": m.get("md5", ""), "tag": bool(m.get("tag")),
            "tag_source": m.get("tag_source"), "by": m.get("by", ""), "note": m.get("note", ""),
        })
    eff = effective or {}
    devices = {k: {**e, "stale": bool(eff.get(k) and eff[k] != product)}
               for k, e in cfg["devices"].items()}
    return {
        "product": product,
        "target": target_file(product),
        "target_by": tgt.get("by", ""),
        "target_at": tgt.get("at", ""),
        "devices": devices,
        "files": out,
    }


def products_summary(effective: dict[str, str] | None = None) -> list[dict]:
    """Mỗi kho: số ảnh, bản chung, số ghim, `devices` = số máy THẬT SỰ tra kho này (0 = đặt
    target ở đây là im lặng — đúng bẫy kho `rapidplus-a` hôm nay), `stale_pins` = ghim của máy
    đã sang kho khác."""
    eff = effective or {}
    per: dict[str, int] = {}
    for prod in eff.values():
        per[prod] = per.get(prod, 0) + 1
    out = []
    for key in sorted(set(list_products()) | set(per)):
        n = len(_bins(product_dir(key)))
        cfg = read_cfg(key)
        stale = sum(1 for dev in cfg["devices"] if eff.get(dev) and eff[dev] != key)
        out.append({"product": key, "files": n, "target": target_file(key),
                    "pinned": len(cfg["devices"]), "stale_pins": stale,
                    "devices": per.get(key, 0), "legacy": key == config.LEGACY_PRODUCT})
    return out


# --- di cư kho phẳng cũ ----------------------------------------------------------------------

def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def migrate_legacy(dry_run: bool = False) -> list[str]:
    """Dời `.bin` + `target.json` ở GỐC OTA_DIR (kho phẳng trước 2026-09-11) vào
    `products/<LEGACY_PRODUCT>/`. Idempotent: gốc sạch thì không làm gì. Trả danh sách
    hành động (đã làm, hoặc SẼ làm khi `dry_run`).

    Chạy TỰ ĐỘNG lúc khởi động (main.py) chứ không đợi ai gõ lệnh: deploy mà quên di cư
    là `/ota/check` của cả fleet thấy kho trống và im lặng — đúng kiểu hỏng khó thấy nhất.
    `scripts/migrate_ota.py --dry-run` chỉ để xem trước.

    Trùng tên với file ĐÃ CÓ trong kho: cùng nội dung → xoá bản gốc; khác nội dung → giữ
    bản trong kho (đi qua API, mới hơn), dời bản gốc thành `<tên>.conflict` để người
    xử lý tay — không tự chọn hộ.
    """
    r = root()
    legacy = config.LEGACY_PRODUCT
    dest = product_dir(legacy)
    actions: list[str] = []
    bins = sorted(_bins(r))  # bỏ `.tmp-*.bin` dở dang ở gốc — không phải ảnh
    tj = r / "target.json"
    if not bins and not tj.is_file():
        return actions
    if not dry_run:
        dest.mkdir(parents=True, exist_ok=True)
    for p in bins:
        t = dest / p.name
        if t.is_file():
            if _sha(p) == _sha(t):
                actions.append(f"{p.name}: kho {legacy} đã có bản giống hệt → xoá bản ở gốc")
                if not dry_run:
                    p.unlink()
            else:
                aside = r / f"{p.name}.conflict"
                actions.append(f"{p.name}: kho {legacy} ĐÃ CÓ bản KHÁC nội dung → giữ bản trong kho, "
                               f"dời bản ở gốc thành {aside.name} (xử lý tay)")
                if not dry_run:
                    os.replace(p, aside)
            continue
        actions.append(f"{p.name} → products/{legacy}/")
        if not dry_run:
            os.replace(p, t)
            m = read_manifest(legacy, p.name)  # sinh manifest ngay, khỏi dựng lại ở request đầu
            if m and m.get("source") == "rebuilt":
                m["source"] = "migrated"
                try:
                    _atomic_json(manifest_path(legacy, p.name), m)
                except OSError:
                    pass
    if tj.is_file():
        dt = dest / "target.json"
        if dt.is_file():
            aside = r / "target.json.migrated"
            actions.append(f"target.json: kho {legacy} đã có target.json riêng → giữ kho, "
                           f"dời bản ở gốc thành {aside.name}")
            if not dry_run:
                os.replace(tj, aside)
        else:
            actions.append(f"target.json → products/{legacy}/target.json")
            if not dry_run:
                os.replace(tj, dt)
    return actions
