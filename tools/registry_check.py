#!/usr/bin/env python3
"""registry_check.py — kiểm `system/products.yaml` đối chiếu với CODE THẬT (pha 1 tiêu thụ registry).

Chạy từ gốc monorepo (hoặc bất kỳ đâu, script tự tìm gốc = thư mục cha của `tools/`):

    python tools/registry_check.py            # in bảng, exit 1 nếu có LỖI
    python tools/registry_check.py --json     # cho CI
    python tools/registry_check.py --check-tag            # thêm: tag fw/<product>/<ver> phải tồn tại
    python tools/registry_check.py --skip-firmware        # khi chưa lắp monorepo (thư mục firmware/ chưa có)

Kiểm gì (mỗi dòng dưới là một nhóm lỗi/cảnh báo có mã riêng):
  SCHEMA   validate theo system/contracts/products.schema.json SAU KHI resolve `variant_of`.
  KEY      khoá sản phẩm hợp lệ theo CHÍNH `server/app/config.py::valid_product` (nạp file trực tiếp,
           không import gói `app` — `app/__init__.py` kéo cả main.py/FastAPI).
  PREFIX   OTA_LEGACY_PRODUCT_BY_PREFIX kỳ vọng (từ các sản phẩm legacy_prefix_default: true) phải
           khớp `server/deploy/fbt-receiver.env.example`; hai sản phẩm cùng tiền tố đều true = lỗi.
  FWDIR    firmware.dir tồn tại; platformio.ini có đúng [env:…]; build_flags có mặt trong env đó.
  VERSION  version_source.regex bắt được chuỗi dạng vX.Y.Z[hậu tố]; `--check-tag` đối chiếu tag git.
  IMAGE    image_name chứa {ver}; legacy_name_locked ⇒ bắt buộc fbt_{ver}.bin.
  ARRAY    payload.array_fields của sản phẩm LEGACY_PRODUCT == server ARRAY_FIELDS.
  TODO     ô null / `# TODO` ở sản phẩm production → cảnh báo (không chặn).

Phụ thuộc: pyyaml, jsonschema (`pip install pyyaml jsonschema`). Thuần đọc, không sửa gì.
"""
from __future__ import annotations

import argparse
import configparser
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "system" / "products.yaml"
SCHEMA = ROOT / "system" / "contracts" / "products.schema.json"
SERVER_CONFIG = ROOT / "server" / "app" / "config.py"
ENV_EXAMPLE = ROOT / "server" / "deploy" / "fbt-receiver.env.example"

VERSION_RE = re.compile(r"^v?\d+\.\d+\.\d+[A-Za-z0-9.\-]*$")

# Windows console mặc định cp1252 → tiếng Việt vỡ. Ép UTF-8 khi được.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8")
    except Exception:  # pragma: no cover - console lạ
        pass


class Report:
    def __init__(self) -> None:
        self.errors: list[dict] = []
        self.warnings: list[dict] = []
        self.info: list[dict] = []

    def err(self, code: str, product: str | None, msg: str) -> None:
        self.errors.append({"code": code, "product": product, "msg": msg})

    def warn(self, code: str, product: str | None, msg: str) -> None:
        self.warnings.append({"code": code, "product": product, "msg": msg})

    def note(self, code: str, product: str | None, msg: str) -> None:
        self.info.append({"code": code, "product": product, "msg": msg})


# ------------------------------------------------------------------ nạp
def load_yaml(path: Path):
    try:
        import yaml  # type: ignore
    except ImportError:
        sys.exit("Thiếu pyyaml: pip install pyyaml jsonschema")
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_server_config():
    """Nạp server/app/config.py như một module rời (KHÔNG qua gói `app`)."""
    if not SERVER_CONFIG.exists():
        return None
    spec = importlib.util.spec_from_file_location("fbt_server_config", SERVER_CONFIG)
    mod = importlib.util.module_from_spec(spec)  # type: ignore[arg-type]
    spec.loader.exec_module(mod)  # type: ignore[union-attr]
    return mod


def resolve_variants(products: dict, rep: Report) -> dict:
    """Kế thừa NÔNG: con lấy mọi khoá cấp 1 của cha rồi ghi đè bằng khoá của mình.
    `variant_of` giữ lại để biết nguồn gốc; cấm chuỗi kế thừa 2 tầng (đơn giản, dễ đọc)."""
    out: dict = {}
    for key, p in products.items():
        if not isinstance(p, dict):
            rep.err("SCHEMA", key, "sản phẩm phải là map")
            continue
        parent = p.get("variant_of")
        if parent is None:
            out[key] = dict(p)
            continue
        base = products.get(parent)
        if not isinstance(base, dict):
            rep.err("SCHEMA", key, f"variant_of trỏ tới sản phẩm không tồn tại: {parent!r}")
            out[key] = dict(p)
            continue
        if base.get("variant_of"):
            rep.err("SCHEMA", key, f"variant_of {parent!r} lại là biến thể — chỉ cho phép 1 tầng")
        merged = dict(base)
        merged.update(p)
        out[key] = merged
    return out


# ------------------------------------------------------------------ các bước kiểm
def check_schema(doc: dict, rep: Report) -> None:
    try:
        import jsonschema  # type: ignore
    except ImportError:
        sys.exit("Thiếu jsonschema: pip install pyyaml jsonschema")
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema)
    for e in sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path)):
        path = "/".join(str(x) for x in e.absolute_path) or "<gốc>"
        prod = None
        parts = list(e.absolute_path)
        if len(parts) >= 2 and parts[0] == "products":
            prod = str(parts[1])
        rep.err("SCHEMA", prod, f"{path}: {e.message}")


def check_keys(products: dict, cfg, rep: Report) -> None:
    if cfg is None:
        rep.warn("KEY", None, f"không thấy {SERVER_CONFIG.relative_to(ROOT)} — bỏ qua đối chiếu valid_product")
        return
    for key in products:
        if not cfg.valid_product(key):
            rep.err("KEY", key, "khoá không qua server/app/config.py::valid_product "
                                f"(regex {cfg.PRODUCT_RE.pattern!r}, dành riêng {sorted(cfg.PRODUCT_RESERVED)})")


def expected_prefix_map(products: dict, rep: Report) -> dict[str, str]:
    m: dict[str, str] = {}
    for key, p in products.items():
        if not p.get("legacy_prefix_default"):
            continue
        pre = p.get("id_prefix")
        if not pre:
            rep.err("PREFIX", key, "legacy_prefix_default: true nhưng thiếu id_prefix")
            continue
        if pre in m:
            rep.err("PREFIX", key, f"tiền tố {pre} đã được {m[pre]!r} nhận legacy_prefix_default — chỉ MỘT sản phẩm/tiền tố")
            continue
        m[pre] = key
    return m


def check_prefix_env(expected: dict[str, str], cfg, rep: Report) -> None:
    exp_str = ",".join(f"{k}={v}" for k, v in sorted(expected.items()))
    rep.note("PREFIX", None, f"OTA_LEGACY_PRODUCT_BY_PREFIX kỳ vọng: {exp_str}")
    if not ENV_EXAMPLE.exists():
        rep.err("PREFIX", None, f"thiếu {ENV_EXAMPLE.relative_to(ROOT)} — tạo file example để đối chiếu")
        return
    found = None
    for line in ENV_EXAMPLE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("OTA_LEGACY_PRODUCT_BY_PREFIX="):
            found = line.split("=", 1)[1].strip().strip('"').strip("'")
            break
    if found is None:
        rep.err("PREFIX", None, "env.example không có dòng OTA_LEGACY_PRODUCT_BY_PREFIX=")
        return
    got = {}
    for item in found.split(","):
        if "=" in item:
            k, v = (s.strip() for s in item.split("=", 1))
            if k:
                got[k] = v.lower()
    if got != expected:
        rep.err("PREFIX", None, f"env.example có {found!r} ≠ registry kỳ vọng {exp_str!r}")
    if cfg is not None and cfg.LEGACY_PRODUCT not in expected.values():
        rep.warn("PREFIX", None, f"server LEGACY_PRODUCT={cfg.LEGACY_PRODUCT!r} không phải sản phẩm nào có legacy_prefix_default")


def read_pio_envs(ini: Path) -> dict[str, dict[str, str]]:
    cp = configparser.ConfigParser(interpolation=None, strict=False, allow_no_value=True)
    cp.optionxform = str  # giữ hoa/thường
    cp.read(ini, encoding="utf-8")
    envs: dict[str, dict[str, str]] = {}
    for sec in cp.sections():
        if sec.startswith("env:"):
            envs[sec[4:]] = {k: (v or "") for k, v in cp.items(sec)}
    return envs


def check_firmware(key: str, p: dict, rep: Report, check_tag: bool) -> str | None:
    fw = p.get("firmware")
    if p.get("status") == "planned" or fw is None:
        return None
    d = ROOT / fw["dir"]
    if not d.is_dir():
        rep.err("FWDIR", key, f"thư mục {fw['dir']} không tồn tại")
        return None
    ini = d / "platformio.ini"
    if not ini.exists():
        rep.err("FWDIR", key, f"{fw['dir']}/platformio.ini không tồn tại")
    else:
        envs = read_pio_envs(ini)
        for e in fw.get("envs", []):
            if e not in envs:
                rep.err("FWDIR", key, f"platformio.ini không có [env:{e}] (có: {sorted(envs)})")
                continue
            flags = envs[e].get("build_flags", "")
            for flag in fw.get("build_flags", []) or []:
                if f"-D{flag}" not in flags:
                    rep.err("FWDIR", key, f"[env:{e}] build_flags thiếu -D{flag}")
    # version
    vs = fw["version_source"]
    vfile = d / vs["file"]
    ver = None
    if not vfile.exists():
        rep.err("VERSION", key, f"{fw['dir']}/{vs['file']} không tồn tại")
    else:
        try:
            rx = re.compile(vs["regex"])
        except re.error as ex:
            rep.err("VERSION", key, f"regex lỗi: {ex}")
            return None
        if rx.groups != 1:
            rep.err("VERSION", key, f"regex phải có ĐÚNG 1 nhóm bắt (đang có {rx.groups})")
            return None
        m = rx.search(vfile.read_text(encoding="utf-8", errors="replace"))
        if not m:
            rep.err("VERSION", key, f"regex không bắt được gì trong {vs['file']}")
        else:
            ver = m.group(1)
            if not VERSION_RE.match(ver):
                rep.err("VERSION", key, f"chuỗi bắt được {ver!r} không giống version (vX.Y.Z[hậu tố])")
    # image
    img = fw["image_name"]
    if "{ver}" not in img:
        rep.err("IMAGE", key, f"image_name {img!r} thiếu {{ver}}")
    if fw.get("legacy_name_locked") and img != "fbt_{ver}.bin":
        rep.err("IMAGE", key, f"legacy_name_locked nhưng image_name={img!r} ≠ 'fbt_{{ver}}.bin' (fleet ≤ v2.4.5 so nguyên chuỗi)")
    if ver:
        rep.note("IMAGE", key, f"ảnh kỳ vọng: {img.format(ver=ver)}")
    # tag
    if check_tag and ver:
        tag = f"{fw['tag_prefix']}{ver}"
        try:
            out = subprocess.run(["git", "-C", str(ROOT), "tag", "-l", tag], capture_output=True, text=True, check=True).stdout.strip()
        except Exception as ex:  # git thiếu / không phải repo
            rep.warn("VERSION", key, f"không chạy được git tag: {ex}")
            out = tag
        if out != tag:
            rep.err("VERSION", key, f"thiếu tag {tag} (version trong {vs['file']} là {ver}, chưa được tag)")
    return ver


def check_array_fields(products: dict, cfg, rep: Report) -> None:
    if cfg is None:
        return
    key = cfg.LEGACY_PRODUCT
    p = products.get(key)
    if not p:
        rep.warn("ARRAY", None, f"registry không có sản phẩm {key!r} (= server LEGACY_PRODUCT)")
        return
    af = (p.get("payload") or {}).get("array_fields")
    if af is None:
        rep.err("ARRAY", key, "thiếu payload.array_fields (server có ARRAY_FIELDS → registry phải khai)")
        return
    if tuple(af) != tuple(cfg.ARRAY_FIELDS):
        rep.err("ARRAY", key, f"payload.array_fields {list(af)} ≠ server ARRAY_FIELDS {list(cfg.ARRAY_FIELDS)}")
    slots = (p.get("channels") or {}).get("optical_slots")
    if slots != 10:
        rep.err("ARRAY", key, f"channels.optical_slots={slots!r} nhưng server logic.validate còn ghim 10 phần tử (pha 2 mới đọc registry)")


def check_todo(key: str, p: dict, raw_text: str, rep: Report) -> None:
    if p.get("status") not in ("production", "dev"):
        return
    ch = p.get("channels")
    if ch is None or ch.get("optical_slots") is None or ch.get("temp_channels") is None:
        rep.warn("TODO", key, "channels chưa chốt (null) — app/firmware không sinh được spec cho sản phẩm này")
    contract = (p.get("payload") or {}).get("contract")
    if contract is None:
        rep.warn("TODO", key, "payload.contract chưa có — chưa kiểm được payload thiết bị theo schema")
    elif not (ROOT / contract).exists():
        rep.warn("TODO", key, f"payload.contract trỏ {contract} nhưng file chưa tồn tại (P2 viết)")
    if p.get("id_prefix") is None:
        rep.warn("TODO", key, "id_prefix chưa có")


# ------------------------------------------------------------------ main
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true", help="in JSON cho CI")
    ap.add_argument("--check-tag", action="store_true", help="đòi tag fw/<product>/<ver> tồn tại")
    ap.add_argument("--skip-firmware", action="store_true", help="bỏ kiểm thư mục firmware (trước khi lắp monorepo)")
    ap.add_argument("--registry", type=Path, default=REGISTRY)
    args = ap.parse_args(argv)

    rep = Report()
    raw_text = args.registry.read_text(encoding="utf-8")
    doc = load_yaml(args.registry)
    if not isinstance(doc, dict) or "products" not in doc:
        print("products.yaml không có khoá products", file=sys.stderr)
        return 2
    n_todo = sum(1 for ln in raw_text.splitlines() if "# TODO" in ln)
    if n_todo:
        rep.note("TODO", None, f"{n_todo} dòng đánh dấu # TODO trong registry (số liệu chờ xác nhận)")

    products = resolve_variants(doc["products"], rep)
    resolved = dict(doc)
    resolved["products"] = products
    check_schema(resolved, rep)

    cfg = load_server_config()
    check_keys(products, cfg, rep)
    expected = expected_prefix_map(products, rep)
    check_prefix_env(expected, cfg, rep)
    check_array_fields(products, cfg, rep)

    rows = []
    for key, p in products.items():
        ver = None
        if not args.skip_firmware:
            ver = check_firmware(key, p, rep, args.check_tag)
        check_todo(key, p, raw_text, rep)
        fw = p.get("firmware") or {}
        rows.append({
            "product": key,
            "status": p.get("status"),
            "prefix": p.get("id_prefix"),
            "legacy_default": bool(p.get("legacy_prefix_default")),
            "variant_of": p.get("variant_of"),
            "dir": fw.get("dir"),
            "envs": fw.get("envs"),
            "version": ver,
            "image": (fw.get("image_name") or "").format(ver=ver) if ver else fw.get("image_name"),
        })

    ok = not rep.errors
    if args.json:
        print(json.dumps({"ok": ok, "products": rows, "errors": rep.errors, "warnings": rep.warnings, "info": rep.info},
                         ensure_ascii=False, indent=2))
        return 0 if ok else 1

    # bảng
    hdr = f"{'product':16} {'status':11} {'prefix':7} {'legacy':6} {'version':12} {'ảnh kỳ vọng':26} dir"
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        print(f"{r['product']:16} {str(r['status']):11} {str(r['prefix'] or '-'):7} {('*' if r['legacy_default'] else ''):6} "
              f"{str(r['version'] or '-'):12} {str(r['image'] or '-'):26} {r['dir'] or '-'}")
    print()
    for it in rep.info:
        print(f"  ℹ {it['code']:8} {(it['product'] or '-'):16} {it['msg']}")
    for it in rep.warnings:
        print(f"  ⚠ {it['code']:8} {(it['product'] or '-'):16} {it['msg']}")
    for it in rep.errors:
        print(f"  ✖ {it['code']:8} {(it['product'] or '-'):16} {it['msg']}")
    print()
    print(f"{'ĐẠT' if ok else 'LỖI'}: {len(rep.errors)} lỗi, {len(rep.warnings)} cảnh báo, {len(products)} sản phẩm")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
