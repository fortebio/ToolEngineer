#!/usr/bin/env python3
"""Ghép dump `WD:<base64>` (audio_pipeline.c, bản diag) từ log UART thành WAV 16 kHz mono
và in vài số đo cơ bản để soi đầu vào WakeNet mà không cần nghe.

    python tools/wake_dump_to_wav.py capture.log out.wav [tag]   # tag: idle | wake | uplink
"""
import base64, re, struct, sys, wave

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    log, out = sys.argv[1], sys.argv[2]
    tag = sys.argv[3] if len(sys.argv) > 3 else None
    raw = open(log, "rb").read().decode("utf-8", "ignore")
    pat = r"WD:BEGIN tag=(\w+) sr=(\d+) samples=(\d+)(.*?)WD:END"
    hits = [m for m in re.finditer(pat, raw, re.S) if tag is None or m.group(1) == tag]
    if not hits:
        sys.exit("khong thay WD:BEGIN tag=%s ... WD:END trong log" % (tag or "*"))
    m = hits[-1]
    print("tag=%s" % m.group(1))
    sr, n = int(m.group(2)), int(m.group(3))
    # Mỗi dòng "WD:" đúng 76 ký tự base64 = 57 byte. Log của task khác có thể chen
    # NGANG một dòng (tách thành mảnh "WD:abc" + "I (...) ..." + "def"). Bỏ dòng hỏng
    # là mất 57 byte (lẻ) → mọi mẫu int16 phía sau lệch 1 byte → nghe/nhìn như nhiễu
    # trắng phân bố đều (đã bị lừa 12/09). Ở đây: ghép mảnh nếu ghép được, không thì
    # chèn đúng 57 byte 0 để giữ thẳng hàng.
    chunks, bad = [], 0
    pending = ""
    for line in m.group(4).splitlines():
        line = line.rstrip("\r")
        mm = re.match(r"WD:([A-Za-z0-9+/=]*)(.*)$", line)
        if mm:
            if pending:
                chunks.append(None); bad += 1; pending = ""
            body, rest = mm.group(1), mm.group(2)
            if len(body) == 76 and not rest:
                chunks.append(body)
            elif len(body) < 76 and (rest.startswith("I (") or rest.startswith("W (") or rest.startswith("E (") or rest == ""):
                pending = body            # mảnh đầu, chờ mảnh cuối ở dòng sau
            else:
                chunks.append(None); bad += 1
        elif pending:
            tail = re.match(r"([A-Za-z0-9+/=]+)$", line)
            if tail and len(pending) + len(tail.group(1)) == 76:
                chunks.append(pending + tail.group(1))
            else:
                chunks.append(None); bad += 1
            pending = ""
    if pending:
        chunks.append(None); bad += 1
    pcm = b"".join(base64.b64decode(c) if c else bytes(57) for c in chunks)
    print("dong base64: %d, hong (chen 57 byte 0): %d" % (len(chunks), bad))
    samples = struct.unpack("<%dh" % (len(pcm) // 2), pcm[: (len(pcm) // 2) * 2])
    with wave.open(out, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm)
    print(f"{out}: {len(samples)} mau @ {sr} Hz ({len(samples)/sr:.2f} s), mong {n}")

    # --- so do co ban ---
    import math
    frame = sr // 50  # 20 ms
    rms = []
    for i in range(0, len(samples) - frame, frame):
        f = samples[i:i + frame]
        rms.append(math.sqrt(sum(x * x for x in f) / frame))
    peak = max(abs(x) for x in samples)
    print(f"peak={peak} ({20*math.log10(max(peak,1)/32768):.1f} dBFS)  rms 20ms: min={min(rms):.0f} max={max(rms):.0f}")
    loud = [i for i, r in enumerate(rms) if r >= 600]
    if loud:
        print(f"doan to (rms>=600): {loud[0]*20} ms .. {loud[-1]*20} ms, {len(loud)} khung")
    # cao do co ban (autocorrelation) tren cac khung to — giong nguoi 80..350 Hz
    f0s = []
    for i in loud[:60]:
        f = samples[i * frame:(i + 2) * frame]
        if len(f) < 2 * frame: break
        best, bestc = 0, 0.0
        e = sum(x * x for x in f) or 1
        for lag in range(sr // 350, sr // 80):
            c = sum(f[k] * f[k + lag] for k in range(0, len(f) - lag, 2)) / e
            if c > bestc: bestc, best = c, lag
        if best and bestc > 0.3:
            f0s.append(sr / best)
    if f0s:
        f0s.sort()
        print(f"F0 (cao do giong) trung vi ~{f0s[len(f0s)//2]:.0f} Hz tren {len(f0s)} khung "
              f"(nguoi lon 85–255 Hz; lech x1.5 = sai sample rate)")
    # ti le nang luong tren 4 kHz (aliasing/nhieu) bang zero-crossing tho
    zc = sum(1 for k in range(1, len(samples)) if (samples[k-1] < 0) != (samples[k] < 0))
    print(f"zero-crossing trung binh {zc / (len(samples)/sr) / 2:.0f} Hz (giong noi thuong 300–1500)")

if __name__ == "__main__":
    main()
