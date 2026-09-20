"""Kiểm "box production đang chạy code nào" KHÔNG cần SSH, KHÔNG cần token.

So `openapi.json` public của production với `app.openapi()` sinh từ code local (env tạm, không ghi
gì vào cây làm việc), in route chỉ có ở local (chưa deploy), chỉ có ở prod (local thiếu — deploy sẽ
ghi đè mất, xem gotcha 2026-09-12 trong CLAUDE.md), route chung nhưng đổi chữ ký. Kèm md5 của
`app/*.py` trong CÂY LÀM VIỆC (bản CRLF — đúng bản scp lên box) để đối chiếu với `md5sum` trên box,
vì openapi KHÔNG phản ánh đổi trong logic.py/ota.py/config.py.

Dùng (từ server/):
    python scripts/check_deploy.py                      # prod mặc định fbt.basa-luma.ts.net
    python scripts/check_deploy.py --url http://127.0.0.1:8080   # bộ test local
    python scripts/check_deploy.py --md5                # chỉ in md5 local (dán cạnh md5sum trên box)
Mã thoát: 0 = openapi giống hệt; 1 = lệch (đọc bảng); 2 = không tải được prod.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
import urllib.request
from pathlib import Path

SERVER_DIR = Path(__file__).resolve().parent.parent
DEFAULT_URL = "https://fbt.basa-luma.ts.net"


def _local_openapi() -> dict:
    """Nạp app.main với env tạm (không đụng ~/fbt_server, không cần Postgres)."""
    tmp = tempfile.mkdtemp(prefix="fbt-check-deploy-")
    for k in ("FBT_DATA_DIR", "FBT_OTA_DIR", "FBT_LOGS_DIR", "FBT_ATE_DIR", "FBT_WEB_DIR"):
        os.environ.setdefault(k, os.path.join(tmp, k.lower()))
    os.environ.setdefault("RECEIVER_TOKEN", "check")
    os.environ.setdefault("OTA_ADMIN_TOKEN", "check-admin")
    sys.path.insert(0, str(SERVER_DIR))
    from app.main import app  # noqa: WPS433 — import trễ, sau khi đặt env

    return app.openapi()


def _prod_openapi(url: str) -> dict:
    with urllib.request.urlopen(url.rstrip("/") + "/openapi.json", timeout=20) as r:
        return json.load(r)


def _ops(spec: dict) -> dict[str, dict]:
    """{'GET /devices': <operation>} — một khoá cho mỗi (method, path)."""
    out = {}
    for path, methods in spec.get("paths", {}).items():
        for m, op in methods.items():
            out[f"{m.upper():6} {path}"] = op
    return out


def _md5_lines() -> list[str]:
    lines = []
    for p in sorted((SERVER_DIR / "app").glob("*.py")):
        lines.append(f"  {hashlib.md5(p.read_bytes()).hexdigest()}  app/{p.name}")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=DEFAULT_URL, help=f"gốc server (mặc định {DEFAULT_URL})")
    ap.add_argument("--md5", action="store_true", help="chỉ in md5 app/*.py local rồi thoát")
    args = ap.parse_args()

    if args.md5:
        print("md5 app/*.py (cây làm việc, bản CRLF = bản scp lên box):")
        print("\n".join(_md5_lines()))
        return 0

    try:
        prod = _prod_openapi(args.url)
    except Exception as e:  # noqa: BLE001 — mọi lỗi mạng đều là "không tải được"
        print(f"Không tải được {args.url}/openapi.json: {e}")
        return 2
    local = _local_openapi()
    lo, po = _ops(local), _ops(prod)

    print(f"prod : {args.url}  ({len(po)} route)")
    print(f"local: {SERVER_DIR}  ({len(lo)} route)")

    only_local = sorted(set(lo) - set(po))
    only_prod = sorted(set(po) - set(lo))
    changed = sorted(
        k for k in set(lo) & set(po)
        if json.dumps(lo[k], sort_keys=True) != json.dumps(po[k], sort_keys=True)
    )
    print("\nCHỈ CÓ Ở LOCAL (chưa deploy):" if only_local else "\nChỉ có ở local: (không)")
    for k in only_local:
        print("  +", k)
    print("\n⚠️ CHỈ CÓ Ở PROD (local thiếu — deploy sẽ ghi đè MẤT, dừng lại tìm nhánh chưa merge):"
          if only_prod else "\nChỉ có ở prod: (không)")
    for k in only_prod:
        print("  -", k)
    print("\nROUTE CHUNG nhưng đổi chữ ký:" if changed else "\nRoute chung đổi chữ ký: (không)")
    for k in changed:
        print("  ~", k)

    same = json.dumps(local, sort_keys=True) == json.dumps(prod, sort_keys=True)
    print("\nopenapi giống hệt:", "CÓ" if same else "KHÔNG")
    print("Lưu ý: giống hệt CHƯA chắc đã deploy đủ — so thêm md5 logic.py/ota.py/config.py trên box:")
    print(f"  ssh engineer@fbt.basa-luma.ts.net 'md5sum ~/fbt_server/app/*.py'")
    print("\n".join(_md5_lines()))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
