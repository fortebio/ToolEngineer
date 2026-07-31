#!/usr/bin/env python3
"""Nhận POST JSON từ thiết bị, lưu mỗi request thành 1 file .json trong ~/fbt_server/data_plus/.

Chạy:   python3 receiver.py            # lắng nghe cổng 8080
Test:   python3 receiver.py --selftest # kiểm tra logic, không mở mạng
"""
import hmac
import json
import os
import re
import sys
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DATA_DIR = Path.home() / "fbt_server" / "data_plus"
PORT = 8080
MAX_BODY = 16 * 1024 * 1024  # 16MB: chặn payload khổng lồ nuốt hết RAM
TOKEN = os.environ.get("RECEIVER_TOKEN", "")  # đặt biến này khi mở ra Internet (Funnel)


def safe_name(s: str) -> str:
    """Chỉ giữ ký tự an toàn cho tên file — chống path traversal (../, /...)."""
    return re.sub(r"[^A-Za-z0-9_.-]", "_", s).strip("._")[:64] or "unknown"


def check_auth(auth_header: str, token: str) -> bool:
    """Cho phép nếu chưa đặt token (chỉ LAN), hoặc header khớp 'Bearer <token>'."""
    if not token:
        return True
    return hmac.compare_digest(auth_header, f"Bearer {token}")


class Handler(BaseHTTPRequestHandler):
    def _reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if not check_auth(self.headers.get("Authorization", ""), TOKEN):
            return self._reply(401, {"ok": False, "error": "unauthorized"})

        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            return self._reply(400, {"ok": False, "error": "missing/too large Content-Length"})

        raw = self.rfile.read(length)
        try:
            data = json.loads(raw)
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            return self._reply(400, {"ok": False, "error": f"invalid json: {e}"})

        device = safe_name(str(data.get("id_device", "unknown")))
        ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        path = DATA_DIR / f"{device}_{ts}.json"
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        except OSError as e:
            return self._reply(500, {"ok": False, "error": f"write failed: {e}"})

        print(f"saved {path.name} ({length} bytes)", flush=True)
        return self._reply(200, {"ok": True, "file": path.name})

    def log_message(self, *args):  # tắt log HTTP mặc định ồn ào
        pass


def selftest():
    assert safe_name("RPL02013") == "RPL02013"
    for bad in ("../../etc/passwd", "a/b", "..", "/", ""):
        n = safe_name(bad)
        assert "/" not in n and ".." not in n and n, f"unsafe name from {bad!r}: {n!r}"
    assert check_auth("", "") is True               # chưa đặt token -> mở (LAN)
    assert check_auth("Bearer s3cret", "s3cret") is True
    assert check_auth("Bearer wrong", "s3cret") is False
    assert check_auth("", "s3cret") is False        # có token mà thiếu header -> chặn
    print("selftest ok")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    else:
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        print(f"listening on 0.0.0.0:{PORT} -> {DATA_DIR}", flush=True)
        ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
