"""Kho firmware OTA tách THEO SẢN PHẨM — file, không DB (2026-09-11, giai đoạn 0 của
docs/plan/ota-nhieu-san-pham.md ở repo app).

Bố cục trên đĩa:
    OTA_DIR/
      products/<product>/<file>.bin          ảnh firmware
      products/<product>/<file>.bin.json     manifest {product, ver, hw[], size, sha256, tag, by, at, note}
      products/<product>/target.json         {target, devices{id: pin}} — mỗi sản phẩm MỘT file
      fw_seen.json, fw_log.json              version máy tự khai (vẫn ở main.py)

Vì sao tách: trước đây kho phẳng + MỘT bản chung cho cả server, nên đặt target cho dòng
máy thứ hai là 109 máy RapidPlus cũng bị mời nạp ảnh đó ở lượt poll 6 h tới. Giờ `/ota/check`
của máy sản phẩm A không bao giờ đọc target.json của sản phẩm B.

Máy KHÔNG khai `?product=` (toàn bộ fleet ≤ v2.4.5) → `config.LEGACY_PRODUCT`
([legacy_product_for]); file cũ ở gốc OTA_DIR được [migrate_legacy] dời vào kho đó lúc khởi
động. Đọc `config.OTA_DIR`/`LEGACY_PRODUCT` QUA HÀM chứ không chụp lúc import, để test đổi
thư mục được.

Không import fastapi: lỗi trả về bằng [OtaError(status, detail)], route ở main.py đổi sang
HTTPException. Không import app.main (vòng).
"""
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from app import config
from app.logic import (expected_bin_name, parse_image_tags, product_key, safe_name,
                       ver_from_name)


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


def bin_name(filename: str) -> str:
    """Tên file .bin an toàn (chặn path traversal); sai → 400."""
    name = safe_name(filename)
    if name != filename or not name.lower().endswith(".bin"):
        raise OtaError(400, "tên file phải là .bin hợp lệ")
    return name


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
        return {"file": v, "by": "", "at": ""} if v else None
    if isinstance(v, dict) and isinstance(v.get("file"), str) and v["file"]:
        return {"file": v["file"], "by": str(v.get("by") or ""), "at": str(v.get("at") or "")}
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


def build_manifest(product: str, name: str, raw: bytes, *, by: str = "", note: str = "",
                   at: str = "", source: str = "upload") -> dict:
    """Manifest từ BYTES: có thẻ nhúng thì ver/hw lấy từ thẻ, không thì ver suy từ tên và
    hw = None (= không biết = KHÔNG lọc, đúng hành vi trước khi có manifest)."""
    tags = parse_image_tags(raw)
    tag = tags[0] if len(tags) == 1 else None
    return {
        "product": product,
        "ver": (tag["ver"] if tag and tag["ver"] else ver_from_name(name)),
        "hw": (tag["hw"] or None) if tag else None,
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "tag": tag["raw"] if tag else None,
        # product THẺ khai — khác thư mục chứa nó là ảnh nằm nhầm kho (scp tay); resolve từ chối.
        "tag_product": tag["product"] if tag else None,
        "by": by[:64],
        "note": note[:200],
        "at": at or _now(),
        "source": source,
    }


def read_manifest(product: str, name: str) -> dict | None:
    """Manifest của một ảnh; None nếu .bin không tồn tại.

    Manifest thiếu hoặc `size` lệch với file (ảnh scp thẳng lên box, hoặc ghi đè tay) →
    DỰNG LẠI từ bytes rồi ghi xuống — kho tự lành, không ai phải chạy lệnh gì. Ghi hỏng
    (đĩa chỉ đọc) thì vẫn trả manifest vừa dựng: đường đọc không được phép 500.
    """
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
        m.setdefault("hw", None)
        return m
    m = build_manifest(product, name, path.read_bytes(), source="rebuilt",
                       at=datetime.fromtimestamp(st.st_mtime, timezone.utc)
                       .isoformat(timespec="seconds"))
    try:
        _atomic_json(mp, m)
    except OSError:
        pass
    return m


# --- chọn bản cho một máy ------------------------------------------------------------------

def resolve(product: str, device: str = "", hw: str = "") -> tuple[dict | None, str]:
    """Bản firmware dành cho [device] của [product] → `({file, manifest, pinned}, "")`,
    hoặc `(None, reason)` với reason ∈ none | pin-missing | hw | tag.

    Ghim theo TỪNG MÁY thắng bản chung. Ghim mà file đã mất → KHÔNG rơi về bản chung
    (rơi về là đẩy đúng cái máy vừa được cố ý giữ lại đi lên phía trước). `hw` chỉ lọc khi
    CẢ máy có khai LẪN manifest có danh sách — thiếu một bên là không biết = không lọc.
    """
    cfg = read_cfg(product)
    pin = cfg["devices"].get(device) if device else None
    entry = pin or cfg["target"]
    if not entry:
        return None, "none"
    m = read_manifest(product, entry["file"])
    if m is None:
        return None, "pin-missing" if pin else "none"
    if m.get("tag_product") and m["tag_product"] != product:
        return None, "tag"
    if hw and m.get("hw") and hw.upper() not in m["hw"]:
        return None, "hw"
    return {"file": entry["file"], "manifest": m, "pinned": bool(pin)}, ""


def target_file(product: str) -> str | None:
    """Tên bản CHUNG đang chọn (None nếu chưa chọn hoặc file đã mất) — cho danh sách."""
    hit, _ = resolve(product)
    return hit["file"] if hit and not hit["pinned"] else None


# --- thao tác kho ------------------------------------------------------------------------------

def upload(product: str, name: str | None, raw: bytes, *, force: bool = False,
           by: str = "", note: str = "") -> dict:
    """Ghi một ảnh vào kho [product]. Trả manifest (+ `name`, `existed`).

    Quy tắc, theo thứ tự:
      1. Ảnh chứa ≥ 2 thẻ KHÁC nhau → 400 (ảnh dị dạng).
      2. Có thẻ: `product` thẻ phải = kho; TÊN DO SERVER ĐẶT từ thẻ ([expected_bin_name]);
         client có gửi tên thì phải trùng — tên nói một version, thẻ nói version khác là
         đúng cái vụ "hai ảnh một tên" cần chặn.
      3. Không thẻ: phải có tên; `OTA_REQUIRE_TAG` bật thì cần `force=1`.
      4. Tên đã có trong kho: cùng sha256 → idempotent (không ghi lại); khác → **409**
         (một tên = một nội dung; muốn thay thì xoá trước — thao tác có chủ ý, có dấu vết).
    """
    if not raw:
        raise OtaError(400, "file rỗng")
    tags = parse_image_tags(raw)
    if len(tags) > 1:
        raise OtaError(400, "ảnh chứa nhiều thẻ nhận dạng khác nhau — ảnh dị dạng, không nhận")
    tag = tags[0] if tags else None
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
        if config.OTA_REQUIRE_TAG and not force:
            raise OtaError(400, "ảnh không có thẻ nhận dạng nhúng (firmware < v2.4.6?) — thêm ?force=1 nếu chắc chắn")
    d = product_dir(product)
    path = d / name
    if path.is_file():
        old = read_manifest(product, name) or {}
        new_sha = hashlib.sha256(raw).hexdigest()
        if old.get("sha256") == new_sha:
            return {**old, "name": name, "existed": True}
        raise OtaError(409, f"{name} đã có trong kho {product} với nội dung KHÁC — "
                            "một tên = một nội dung; xoá bản cũ trước nếu thật sự muốn thay")
    d.mkdir(parents=True, exist_ok=True)
    # Ghi file tạm rồi ĐỔI TÊN: `GET /ota/…` đang stream 2.4 MB cho một máy giữ inode cũ tới
    # khi xong, không bao giờ thấy nửa file.
    tmp = d / f".tmp-{name}"
    tmp.write_bytes(raw)
    os.replace(tmp, path)
    m = build_manifest(product, name, raw, by=by, note=note)
    _atomic_json(manifest_path(product, name), m)
    return {**m, "name": name, "existed": False}


def set_target(product: str, name: str, *, device: str = "", by: str = "") -> None:
    if not (product_dir(product) / name).is_file():
        raise OtaError(404, "firmware not found")
    cfg = read_cfg(product)
    entry = {"file": name, "by": by[:64], "at": _now()}
    if device:
        cfg["devices"][safe_name(device)] = entry
    else:
        cfg["target"] = entry
    save_cfg(product, cfg)


def clear_target(product: str, *, device: str = "") -> dict:
    """Huỷ chọn. Không `device` → bỏ bản CHUNG **và giữ nguyên mọi ghim riêng**;
    có `device` → chỉ gỡ ghim máy đó. Trả cfg sau khi ghi."""
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
    cfg = read_cfg(product)
    if (cfg["target"] or {}).get("file") == name:
        cfg["target"] = None
    cfg["devices"] = {k: e for k, e in cfg["devices"].items() if e["file"] != name}
    save_cfg(product, cfg)


def listing(product: str) -> dict:
    """Nội dung kho một sản phẩm — hình dạng `GET /ota` cũ + `product` + trường manifest."""
    d = product_dir(product)
    files = sorted((p for p in d.glob("*.bin") if p.is_file()),
                   key=lambda p: p.stat().st_mtime, reverse=True) if d.is_dir() else []
    cfg = read_cfg(product)
    tgt = cfg["target"] or {}
    out = []
    for p in files:
        m = read_manifest(product, p.name) or {}
        out.append({
            "name": p.name, "size": p.stat().st_size,
            "modified": datetime.fromtimestamp(p.stat().st_mtime, timezone.utc),
            "ver": m.get("ver", ""), "hw": m.get("hw"), "sha256": m.get("sha256", ""),
            "tag": bool(m.get("tag")), "by": m.get("by", ""), "note": m.get("note", ""),
        })
    return {
        "product": product,
        "target": target_file(product),
        "target_by": tgt.get("by", ""),
        "target_at": tgt.get("at", ""),
        "devices": cfg["devices"],
        "files": out,
    }


def products_summary() -> list[dict]:
    out = []
    for key in list_products():
        d = product_dir(key)
        n = sum(1 for p in d.glob("*.bin") if p.is_file()) if d.is_dir() else 0
        cfg = read_cfg(key)
        out.append({"product": key, "files": n, "target": target_file(key),
                    "pinned": len(cfg["devices"]), "legacy": key == config.LEGACY_PRODUCT})
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
    bins = sorted(p for p in r.glob("*.bin") if p.is_file())
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
