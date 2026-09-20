"""lcdtool.py — tool localhost debug màn hình Rapid4P qua cổng console (USB-JTAG S3 / UART0 P4), KHÔNG cần camera.
Phương án A trong docs/plan/2026-09-21-lcd-debug-tool.md.

    python scripts/lcdtool.py COM20                      # mở http://127.0.0.1:8791/ (ảnh màn ×2 tự làm mới, 3 nút ảo,
                                                         #   chọn màn `ui N`, gõ lệnh console, log, lưu PNG, chụp bộ màn)
    python scripts/lcdtool.py COM20 --port 8800          # cổng web khác
    python scripts/lcdtool.py COM20 --shot out.png       # chụp MỘT ảnh rồi thoát (Claude: Read out.png)
    python scripts/lcdtool.py COM20 --gallery out_dir    # chạy `ui N` cho mọi màn, lưu NN_<tên>.png rồi thoát
    python scripts/lcdtool.py COM20 --cmd "btn red hold" --shot out.png   # gửi lệnh rồi chụp

Cách hoạt động: firmware lệnh `screen` in framebuffer LVGL (RGB565 logical, nén RLE, base64, CRC32 —
core/dev_console.c::cmd_screen); tool đọc cổng liên tục (mở KHÔNG DTR/RTS — kéo DTR/RTS làm chip USB-JTAG câm),
tách khối `SCR … SCR-END` khỏi log, giải mã thành PNG (OpenCV, venv IDF). Lệnh `btn green|red|white [hold|rep]`
đi qua event group như nút cơ thật và đánh thức màn; `ui <tên>` nhảy màn để xem bố cục.
Chỉ thư viện có sẵn trong venv IDF: pyserial, numpy, opencv.
"""
import argparse
import base64
import json
import os
import re
import struct
import sys
import threading
import time
import zlib
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2
import numpy as np
import serial

ANSI = re.compile(r"\x1b\[[0-9;]*m")
B64 = re.compile(r"^[A-Za-z0-9+/=]+$")
UI_NAMES = ["start", "sample", "tube", "prepare", "measuring", "result", "calib", "settings",
            "language", "wifi", "update", "threshold", "thredit", "measerr"]


class Device:
    """Một luồng đọc cổng: gom log, bắt khối SCR, giữ ảnh PNG mới nhất."""

    def __init__(self, port):
        self.port = port
        self.ser = None
        self.log = deque(maxlen=600)
        self.png = None            # bytes PNG mới nhất
        self.png_time = 0.0
        self.frame_id = 0
        self.frame_wh = (0, 0)
        self.err = ""
        self._lock = threading.Lock()
        self._scr = None           # đang gom khối SCR: dict(header, lines)
        self._buf = b""
        self._want_frame = threading.Event()
        self._last_cmd = ""
        threading.Thread(target=self._reader, daemon=True).start()

    # ---- cổng ----
    def _open(self):
        s = serial.Serial()
        s.port = self.port
        s.baudrate = 115200
        s.timeout = 0.05
        s.dtr = False
        s.rts = False
        s.open()
        return s

    def send(self, cmd):
        with self._lock:
            if self.ser:
                self.ser.write((cmd + "\r\n").encode())
                self.ser.flush()
                self._last_cmd = cmd
                self.log.append(">>> " + cmd)

    def _reader(self):
        while True:
            try:
                if self.ser is None:
                    self.ser = self._open()
                    self.log.append("[lcdtool] mo %s" % self.port)
                data = self.ser.read(4096)
                if data:
                    self._feed(data)
            except Exception as e:  # cổng biến mất khi chip reset → mở lại
                self.err = str(e)
                try:
                    if self.ser:
                        self.ser.close()
                except Exception:
                    pass
                self.ser = None
                time.sleep(0.3)

    def _feed(self, data):
        self._buf += data
        while b"\n" in self._buf:
            raw, self._buf = self._buf.split(b"\n", 1)
            line = ANSI.sub("", raw.decode("utf-8", "replace")).rstrip("\r")
            self._line(line)

    # ---- tách khối SCR ----
    def _line(self, line):
        s = line.strip()
        if s.startswith("SCR-END"):
            if self._scr:
                self._finish(s)
            self._scr = None
            return
        if s.startswith("SCR ") and "RGB565" in s:
            parts = s.split()
            try:
                self._scr = {"w": int(parts[1]), "h": int(parts[2]), "mode": parts[4], "lines": []}
            except (IndexError, ValueError):
                self._scr = None
            return
        if self._scr is not None and B64.match(s):
            self._scr["lines"].append(s)
            return
        # Bỏ dấu nhắc + echo lệnh của REPL ("r4p>  screen") cho log gọn.
        while s.startswith("r4p>"):
            s = s[4:].strip()
        if s and s != self._last_cmd:
            self.log.append(s)

    def _finish(self, end_line):
        h = self._scr
        try:
            enc = base64.b64decode("".join(h["lines"]))
            w, hh = h["w"], h["h"]
            if h["mode"] == "RLE":
                out = np.empty(w * hh, dtype=np.uint16)
                pos = 0
                i = 0
                n = len(enc)
                while i + 3 <= n and pos < w * hh:
                    cnt = enc[i]
                    px = enc[i + 1] | (enc[i + 2] << 8)
                    out[pos:pos + cnt] = px
                    pos += cnt
                    i += 3
                if pos < w * hh:
                    self.log.append("[lcdtool] RLE thieu %d px" % (w * hh - pos))
            else:
                out = np.frombuffer(enc[:w * hh * 2], dtype="<u2").copy()
            crc_dev = end_line.split()[1] if len(end_line.split()) > 1 else ""
            crc_pc = "%08x" % (zlib.crc32(out.astype("<u2").tobytes()) & 0xFFFFFFFF)
            rgb565 = out.reshape(hh, w).astype(np.uint32)   # tính ở 32 bit: nhân 255 trên uint8 bị tràn
            r = (((rgb565 >> 11) & 0x1F) * 255 // 31).astype(np.uint8)
            g = (((rgb565 >> 5) & 0x3F) * 255 // 63).astype(np.uint8)
            b = ((rgb565 & 0x1F) * 255 // 31).astype(np.uint8)
            bgr = np.dstack([b, g, r])
            ok, png = cv2.imencode(".png", bgr)
            if ok:
                self.png = png.tobytes()
                self.png_time = time.time()
                self.frame_id += 1
                self.frame_wh = (w, hh)
                self._want_frame.set()
                self.log.append("[lcdtool] khung #%d %dx%d %s crc %s" % (
                    self.frame_id, w, hh, h["mode"], "OK" if crc_dev == crc_pc else "LECH(%s/%s)" % (crc_dev, crc_pc)))
        except Exception as e:
            self.log.append("[lcdtool] giai ma SCR loi: %s" % e)

    def grab(self, timeout=6.0):
        """Gửi `screen`, chờ khung mới. Trả về bytes PNG hoặc None."""
        before = self.frame_id
        self._want_frame.clear()
        self.send("screen")
        end = time.time() + timeout
        while time.time() < end:
            if self._want_frame.wait(0.1) and self.frame_id != before:
                return self.png
        return None


PAGE = r"""<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><title>Rapid4P LCD</title>
<style>
:root{--bg:#0A1418;--panel:#10202A;--card:#152A35;--border:#1F3A46;--teal:#26C5CF;--green:#1BD1A5;--red:#EF5350;--text:#F2F8F9;--muted:#A9BCC4}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 Montserrat,Segoe UI,Arial,sans-serif}
header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:10px 16px;background:var(--panel);border-bottom:2px solid var(--teal)}
header h1{font-size:18px;margin:0;color:var(--teal)}header span{color:var(--muted);font-size:13px}
main{display:grid;grid-template-columns:1fr;gap:16px;padding:16px}
@media(min-width:1100px){main{grid-template-columns:auto 1fr}}
#scr{display:block;width:100%;max-width:640px;aspect-ratio:4/3;height:auto;image-rendering:pixelated;background:#000;border:1px solid var(--border);border-radius:8px}
.keys{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;width:100%;max-width:640px;margin-top:12px}
.keys button{height:72px;border:0;border-radius:12px;font-size:20px;font-weight:600;cursor:pointer;user-select:none;color:#06262A}
.k-g{background:var(--green)}.k-r{background:var(--red);color:#fff!important}.k-w{background:#F2F8F9}
.keys button:active{filter:brightness(.8)}
.hint{color:var(--muted);font-size:13px;margin-top:6px}
.side{display:flex;flex-direction:column;gap:12px;min-width:360px}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:12px}
.card h2{margin:0 0 8px;font-size:14px;color:var(--teal);text-transform:uppercase;letter-spacing:.04em}
.ui{display:flex;flex-wrap:wrap;gap:6px}.ui button,.row button{background:#2A3F4C;color:var(--text);border:0;border-radius:8px;padding:8px 10px;cursor:pointer}
.ui button:hover,.row button:hover{background:#3A5262}
.row{display:flex;gap:6px}.row input{flex:1;background:#0A1418;color:var(--text);border:1px solid var(--border);border-radius:8px;padding:8px}
pre{margin:0;height:300px;overflow:auto;background:#0A1418;border:1px solid var(--border);border-radius:8px;padding:8px;font:12px/1.35 Consolas,monospace;color:var(--muted);white-space:pre-wrap}
label{color:var(--muted);font-size:13px;margin-left:8px}
</style></head><body>
<header><h1>Rapid4P LCD</h1><span id="st">…</span>
 <label><input type="checkbox" id="auto" checked> tự làm mới</label>
 <label>mỗi <input id="iv" type="number" value="1.5" step="0.5" min="0.5" style="width:56px"> s</label>
 <button onclick="shot()">Lưu PNG</button><button onclick="gallery()">Chụp bộ màn</button></header>
<main>
 <section>
  <img id="scr" src="/screen.png" alt="LCD">
  <div class="keys">
   <button class="k-g" data-k="green">XANH</button>
   <button class="k-r" data-k="red">ĐỎ</button>
   <button class="k-w" data-k="white">TRẮNG</button>
  </div>
  <div class="hint">Click = nhấn · giữ chuột ≥ 1,5 s = giữ (hold) · Shift+click = lặp (rep) · phím tắt: 1 / 2 / 3, giữ = Shift+1/2/3</div>
 </section>
 <aside class="side">
  <div class="card"><h2>Màn (ui N)</h2><div class="ui" id="ui"></div></div>
  <div class="card"><h2>Lệnh console</h2><div class="row"><input id="cmd" placeholder="vd: heap · btn boot · ui confirm"><button onclick="sendCmd()">Gửi</button></div></div>
  <div class="card"><h2>Log</h2><pre id="log"></pre></div>
 </aside>
</main>
<script>
const UI=%UI_NAMES%;
const uiBox=document.getElementById('ui');
UI.forEach((n,i)=>{const b=document.createElement('button');b.textContent=i+' '+n;b.onclick=()=>post('ui '+n);uiBox.appendChild(b);});
async function post(cmd){await fetch('/cmd',{method:'POST',body:JSON.stringify({cmd})});setTimeout(refresh,700);}
function sendCmd(){const i=document.getElementById('cmd');if(i.value.trim()){post(i.value.trim());i.value='';}}
document.getElementById('cmd').addEventListener('keydown',e=>{if(e.key==='Enter')sendCmd();});
let t0=0;
document.querySelectorAll('.keys button').forEach(b=>{
  const k=b.dataset.k;
  b.addEventListener('mousedown',e=>{t0=Date.now();});
  b.addEventListener('mouseup',e=>{const held=Date.now()-t0;post('btn '+k+(e.shiftKey?' rep':(held>=1500?' hold':'')));});
});
document.addEventListener('keydown',e=>{const m={'1':'green','2':'red','3':'white','!':'green','@':'red','#':'white'};
  if(document.activeElement.tagName==='INPUT')return;
  const k=m[e.key];if(!k)return;post('btn '+k+(e.shiftKey?' hold':''));});
async function refresh(){const r=await fetch('/screen.png?t='+Date.now());if(r.ok){const b=await r.blob();document.getElementById('scr').src=URL.createObjectURL(b);}}
async function status(){const r=await fetch('/log');const j=await r.json();
  document.getElementById('log').textContent=j.log.join('\n');const p=document.getElementById('log');p.scrollTop=p.scrollHeight;
  document.getElementById('st').textContent=j.port+' · khung #'+j.frame+' '+j.wh+' · '+(j.err||'ok');}
async function shot(){const r=await fetch('/shot');alert(await r.text());}
async function gallery(){if(!confirm('Chạy ui 0..'+(UI.length-1)+' và lưu PNG?'))return;const r=await fetch('/gallery');alert(await r.text());}
async function loop(){if(document.getElementById('auto').checked){await fetch('/grab');await refresh();}await status();
  setTimeout(loop,Math.max(500,parseFloat(document.getElementById('iv').value||1.5)*1000));}
loop();
</script></body></html>"""


def make_handler(dev, out_dir):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):  # im
            pass

        def _send(self, code, ctype, body):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                self._send(200, "text/html; charset=utf-8", PAGE.replace("%UI_NAMES%", json.dumps(UI_NAMES)).encode())
            elif path == "/screen.png":
                if dev.png:
                    self._send(200, "image/png", dev.png)
                else:
                    self._send(404, "text/plain", b"chua co khung")
            elif path == "/grab":
                png = dev.grab()
                self._send(200, "text/plain", b"ok" if png else b"timeout")
            elif path == "/log":
                self._send(200, "application/json", json.dumps({
                    "log": list(dev.log)[-200:], "port": dev.port, "frame": dev.frame_id,
                    "wh": "%dx%d" % dev.frame_wh, "err": dev.err}).encode())
            elif path == "/shot":
                png = dev.grab() or dev.png
                if not png:
                    self._send(500, "text/plain", "khong co khung".encode())
                    return
                os.makedirs(out_dir, exist_ok=True)
                p = os.path.join(out_dir, time.strftime("lcd_%Y%m%d_%H%M%S.png"))
                open(p, "wb").write(png)
                self._send(200, "text/plain; charset=utf-8", ("da luu " + p).encode())
            elif path == "/gallery":
                paths = capture_gallery(dev, out_dir)
                self._send(200, "text/plain; charset=utf-8", ("\n".join(paths) or "khong chup duoc").encode())
            else:
                self._send(404, "text/plain", b"?")

        def do_POST(self):
            n = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(n)
            if self.path == "/cmd":
                try:
                    cmd = json.loads(body.decode())["cmd"]
                except Exception:
                    self._send(400, "text/plain", b"json {cmd}")
                    return
                dev.send(cmd)
                self._send(200, "text/plain", b"ok")
            else:
                self._send(404, "text/plain", b"?")
    return H


def capture_gallery(dev, out_dir, wait=1.2):
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    for i, name in enumerate(UI_NAMES):
        if name == "measuring":       # cần cảm biến; bỏ qua (xem qua nút ĐỎ ở prepare)
            continue
        dev.send("ui " + name)
        time.sleep(wait)
        png = dev.grab()
        if png:
            p = os.path.join(out_dir, "%02d_%s.png" % (i, name))
            open(p, "wb").write(png)
            paths.append(p)
    dev.send("ui start")
    return paths


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--port", dest="http", type=int, default=8791)
    ap.add_argument("--out", default="out_lcd", help="thư mục lưu PNG (mặc định out_lcd/)")
    ap.add_argument("--shot", help="chụp một ảnh vào file rồi thoát")
    ap.add_argument("--gallery", help="chụp mọi màn vào thư mục rồi thoát")
    ap.add_argument("--cmd", action="append", default=[], help="lệnh gửi trước khi chụp (lặp được)")
    ap.add_argument("--wait", type=float, default=1.2, help="chờ sau mỗi --cmd (s)")
    a = ap.parse_args()

    dev = Device(a.port)
    time.sleep(0.6)
    for c in a.cmd:
        dev.send(c)
        time.sleep(a.wait)
    if a.shot:
        png = dev.grab()
        if not png:
            print("khong nhan duoc khung (firmware co lenh `screen`? cong dung?)")
            return 1
        os.makedirs(os.path.dirname(os.path.abspath(a.shot)), exist_ok=True)
        open(a.shot, "wb").write(png)
        print("da luu", a.shot, "%dx%d" % dev.frame_wh)
        return 0
    if a.gallery:
        for p in capture_gallery(dev, a.gallery, a.wait):
            print(p)
        return 0
    srv = ThreadingHTTPServer(("127.0.0.1", a.http), make_handler(dev, a.out))
    print("Rapid4P LCD tool: http://127.0.0.1:%d/  (cong %s, Ctrl+C de thoat)" % (a.http, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
