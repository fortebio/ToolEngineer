"""Ống chuẩn hiệu chuẩn quang (FAM) — pha dung dịch, đo thô, xếp hạng tổ hợp, bộ ống.

Thay cho quy trình Google Sheet + Apps Script bàn giao 2026-09 (KHAI'S HANDOVER): kỹ sư pha
dãy FAM 1000 → 300/200/100 nM bằng EDTA pH 8, chia 25 µL × 10 ống mỗi nồng độ (+ 10 ống 0 nM),
đọc số thô từng ống ở MỘT khe của một máy Rapid+ tham chiếu, rồi thử mọi tổ hợp (1 ống/nồng
độ) — tổ hợp có R² và slope tốt thành một BỘ 4 ống (300/200/100/0) bỏ túi zip cất tủ lạnh, sau
này cấp cho máy để chạy `eSensorcalib` (firmware khớp 4 điểm này ra slope/intercept từng khe).

Kho = file JSON dưới `config.CALIB_DIR` (cùng nguyên tắc với ate/ và ota/: không thêm bảng DB):

    batches/<id>.json   lô pha: nguyên liệu, các bước pha (dự kiến + thực tế), số đo thô
    sets/<id>.json      bộ ống: 4 ống đã chọn + hồi quy + vòng đời (tủ lạnh → cấp máy → hết)
    limits.json         ngưỡng PASS có version (r2_min, slope_min/max, shelf_days)
    history.jsonl       nhật ký append-only mọi thao tác ghi

Hàm thuần (không đụng đĩa) nằm trên; kho file nằm dưới và đọc `config` qua HÀM để test vá
được `config.CALIB_DIR` sau import (như ota.py). `CalibError` → HTTPException ở main.py.
"""
from __future__ import annotations

import itertools
import json
import os
import re
import threading
from datetime import datetime, timedelta, timezone
from pathlib import Path

from app import config

# ---------------------------------------------------------------------------------------
# Hằng số / mặc định
# ---------------------------------------------------------------------------------------

# Nồng độ 4 điểm firmware Rapid+ dùng ở eSensorcalib (docs/architecture/04 §"Hiệu chuẩn").
DEFAULT_CONCENTRATIONS = [300, 200, 100, 0]
DEFAULT_TUBES_PER_CONC = 10
DEFAULT_ALIQUOT_UL = 25.0
# Bàn giao ghi "52mM" nhưng hệ số pha 52× ra 1000 nM chỉ đúng khi stock là 52 µM (52 000 nM)
# → lấy con số KHỚP VỚI BẢNG PHA, ghi chú lệch đơn vị trong docs/plan/calib-ong-chuan.md.
DEFAULT_STOCK_NM = 52_000.0
DEFAULT_WORKING_NM = 1000.0     # dung dịch trung gian pha từ stock
DEFAULT_WORKING_TOTAL_UL = 520.0
DEFAULT_TARGET_TOTAL_UL = 300.0

# Ngưỡng PASS mặc định khi chưa ai đặt limits.json. WI gốc (DxD Hub "Optical Calibration for
# Beta Prototype with Fluorescein" 18/07/2024, §4) chấp nhận MÁY khi R² > 0,95 và LOD < 20 nM
# với LOD = 3,3·SD(10 lần đo blank)/slope (template LOD_template_Fluo_240717). Chọn ỐNG CHUẨN
# thì siết R² hơn (0,995 — sheet bàn giao PASS ở 0,998) vì bộ ống là vật chuẩn cho cả fleet;
# lod_max giữ 20 nM như WI. slope_min/max = 0 = không giới hạn (số thô phụ thuộc máy tham
# chiếu; đặt sau khi có thống kê). shelf_days 90: WI đo được tín hiệu tăng ~20 %/12 tháng do
# bay hơi, dùng ngoài hiện trường còn ngắn hơn.
DEFAULT_LIMITS = {
    "version": "mặc định",
    "r2_min": 0.995,
    "lod_max": 20.0,
    "slope_min": 0.0,
    "slope_max": 0.0,
    "shelf_days": 90,
}
LOD_K = 3.3  # SNR 3,3 theo template LOD của WI

BATCH_STATUSES = ("prep", "measure", "ranked", "closed")
SET_STATUSES = ("stored", "issued", "used", "discarded")
# Chuyển trạng thái bộ ống được phép: cất tủ → cấp máy/huỷ; đã cấp → dùng hết/huỷ/thu hồi.
SET_TRANSITIONS = {
    "stored": ("issued", "discarded"),
    "issued": ("used", "discarded", "stored"),
    "used": (),
    "discarded": (),
}
MAX_COMBOS = 200_000       # 10 ống × 4 nồng độ = 10⁴; trần này chặn lô cấu hình lạ
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.\-]{0,63}$")
_LOCK = threading.Lock()   # mọi thao tác ghi kho (đọc-sửa-ghi file JSON) đi qua đây


class CalibError(Exception):
    def __init__(self, status: int, detail: str):
        super().__init__(detail)
        self.status = status
        self.detail = detail


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


# ---------------------------------------------------------------------------------------
# Hàm thuần: pha loãng
# ---------------------------------------------------------------------------------------

def dilution_step(c_from: float, c_to: float, total_ul: float) -> dict:
    """C1·V1 = C2·V2: thể tích dung dịch mẹ và đệm để có `total_ul` ở nồng độ `c_to`."""
    if c_from <= 0 or c_to <= 0 or total_ul <= 0:
        raise CalibError(400, "nồng độ và thể tích phải > 0")
    if c_to > c_from:
        raise CalibError(400, f"không pha được {c_to} từ {c_from} (đặc hơn dung dịch mẹ)")
    factor = c_from / c_to
    dye = total_ul / factor
    return {
        "factor": round(factor, 4),
        "vol_dye_ul": round(dye, 2),
        "vol_buffer_ul": round(total_ul - dye, 2),
        "total_ul": round(total_ul, 2),
    }


def plan_steps(stock_nM: float = DEFAULT_STOCK_NM,
               working_nM: float = DEFAULT_WORKING_NM,
               working_total_ul: float = DEFAULT_WORKING_TOTAL_UL,
               concentrations=None,
               target_total_ul: float = DEFAULT_TARGET_TOTAL_UL,
               tubes_per_conc: int = DEFAULT_TUBES_PER_CONC,
               aliquot_ul: float = DEFAULT_ALIQUOT_UL) -> list[dict]:
    """Danh sách bước của một lô pha theo đúng bàn giao — mỗi bước có số DỰ KIẾN, kỹ sư
    ghi số THỰC TẾ + giờ làm vào (`actual_*`, `done_at`, `by`) khi thực hiện."""
    concs = [float(c) for c in (concentrations or DEFAULT_CONCENTRATIONS)]
    steps: list[dict] = [
        {"code": "prep.stock_out", "name": "Lấy ống FAM khỏi tủ lạnh — TRÁNH ánh nắng trực tiếp"},
        {"code": "prep.spin", "name": "Ly tâm ống stock 30 giây"},
        {"code": "prep.buffer", "name": "Chia EDTA pH 8.0 vào các ống 1,5 mL theo bảng pha"},
    ]
    if working_nM and stock_nM > working_nM:
        d = dilution_step(stock_nM, working_nM, working_total_ul)
        steps.append({"code": f"dil.{working_nM:g}", "name": f"Pha {working_nM:g} nM từ stock",
                      "target_nM": working_nM, "from_nM": stock_nM, **d})
        parent = working_nM
    else:
        parent = stock_nM
    for c in concs:
        if c <= 0:
            continue
        d = dilution_step(parent, c, target_total_ul)
        steps.append({"code": f"dil.{c:g}", "name": f"Pha {c:g} nM từ {parent:g} nM (ly tâm sau khi pha)",
                      "target_nM": c, "from_nM": parent, **d})
    need_ul = tubes_per_conc * aliquot_ul
    for c in concs:
        if c > 0 and need_ul > target_total_ul:
            raise CalibError(400, f"{tubes_per_conc} ống × {aliquot_ul:g} µL = {need_ul:g} µL "
                                  f"> {target_total_ul:g} µL pha được ở {c:g} nM")
    steps.append({"code": "aliquot",
                  "name": f"Chia {aliquot_ul:g} µL × {tubes_per_conc} ống 0,5 mL cho MỖI nồng độ "
                          f"({', '.join(f'{c:g}' for c in concs)} nM), đánh số 1–{tubes_per_conc}",
                  "tubes_per_conc": tubes_per_conc, "aliquot_ul": aliquot_ul})
    steps.append({"code": "seal",
                  "name": "Bọc parafilm nắp ống, cho vào hộp kín tránh sáng, giữ 4–10 °C (WI: bay hơi "
                          "làm tín hiệu tăng ~20 %/12 tháng, ánh sáng làm tẩy quang)"})
    steps.append({"code": "measure",
                  "name": "Đọc số thô từng ống ở MỘT khe của máy tham chiếu: vortex/ly tâm cho dịch "
                          "xuống đáy, đặt ống cùng hướng mũi, đóng nắp nếu phòng sáng; USB 115200, "
                          "gửi `0` → `raw,calibrated` (Serial Debug Assistant)"})
    for s in steps:
        s.setdefault("done_at", "")
        s.setdefault("by", "")
    return steps


def template(**kw) -> dict:
    """Bộ khung một lô mới (nguyên liệu + bước pha) — app hiện cho kỹ sư sửa trước khi tạo."""
    concs = [float(c) for c in kw.get("concentrations") or DEFAULT_CONCENTRATIONS]
    tubes = int(kw.get("tubes_per_conc") or DEFAULT_TUBES_PER_CONC)
    aliquot = float(kw.get("aliquot_ul") or DEFAULT_ALIQUOT_UL)
    stock_nM = float(kw.get("stock_nM") or DEFAULT_STOCK_NM)
    return {
        # WI: thuốc thử phải NIST-traceable (ThermoFisher F36915) — lô khác nguồn là lệch chuẩn.
        "stock": {"name": "Fluorescein NIST-traceable", "supplier": "ThermoFisher",
                  "catalog": "F36915", "lot": "", "expiry": "", "conc_nM": stock_nM, "note": ""},
        "buffer": {"name": "EDTA pH 8.0", "lot": "", "expiry": ""},
        "concentrations": concs,
        "tubes_per_conc": tubes,
        "aliquot_ul": aliquot,
        "steps": plan_steps(stock_nM=stock_nM, concentrations=concs, tubes_per_conc=tubes,
                            aliquot_ul=aliquot,
                            working_nM=float(kw.get("working_nM") or DEFAULT_WORKING_NM),
                            working_total_ul=float(kw.get("working_total_ul")
                                                   or DEFAULT_WORKING_TOTAL_UL),
                            target_total_ul=float(kw.get("target_total_ul")
                                                  or DEFAULT_TARGET_TOTAL_UL)),
    }


# ---------------------------------------------------------------------------------------
# Hàm thuần: hồi quy + xếp hạng tổ hợp
# ---------------------------------------------------------------------------------------

def linear_fit(xs: list[float], ys: list[float]) -> dict | None:
    """Bình phương tối thiểu y = slope·x + intercept; None nếu không khớp được (x trùng nhau,
    <2 điểm). R² = 1 − SSres/SStot; SStot = 0 (mọi y bằng nhau) → R² = 0."""
    n = len(xs)
    if n < 2 or n != len(ys):
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = my - slope * mx
    ss_res = sum((y - (slope * x + intercept)) ** 2 for x, y in zip(xs, ys))
    ss_tot = sum((y - my) ** 2 for y in ys)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0
    return {"slope": round(slope, 4), "intercept": round(intercept, 2), "r2": round(r2, 6)}


def effective_limits(doc: dict | None) -> dict:
    """Bộ ngưỡng đang áp dụng: file (nếu có) đè lên mặc định, số ép về float."""
    out = dict(DEFAULT_LIMITS)
    if isinstance(doc, dict):
        for k in ("r2_min", "lod_max", "slope_min", "slope_max", "shelf_days"):
            if doc.get(k) is not None:
                try:
                    out[k] = float(doc[k]) if k != "shelf_days" else int(doc[k])
                except (TypeError, ValueError):
                    pass
        if doc.get("version"):
            out["version"] = str(doc["version"])
    return out


def blank_stats(values) -> dict:
    """Thống kê các lần đo blank (0 nM): n, mean, sd (STDEV.S — mẫu, n−1 như template LOD),
    snr33 = 3,3·sd. sd = None khi < 2 số đo (không tính LOD được)."""
    xs = [float(v) for v in values if v is not None]
    n = len(xs)
    if n == 0:
        return {"n": 0, "mean": None, "sd": None, "snr33": None}
    mean = sum(xs) / n
    if n < 2:
        return {"n": n, "mean": round(mean, 2), "sd": None, "snr33": None}
    sd = (sum((x - mean) ** 2 for x in xs) / (n - 1)) ** 0.5
    return {"n": n, "mean": round(mean, 2), "sd": round(sd, 3), "snr33": round(LOD_K * sd, 3)}


def lod_nM(snr33, slope) -> float | None:
    """LOD [nM] = 3,3·SD(blank) / slope (template LOD của WI). None khi thiếu SD hoặc slope ≤ 0."""
    if snr33 is None or not slope or slope <= 0:
        return None
    return round(float(snr33) / float(slope), 2)


def judge(fit: dict, limits: dict) -> str:
    """PASS/FAIL một tổ hợp theo ngưỡng. slope_min/max = 0 nghĩa là không giới hạn; LOD chỉ
    xét khi tính được (`fit["lod"]` khác None) — lô chưa đủ blank thì không đánh trượt oan."""
    if fit["r2"] < float(limits.get("r2_min") or 0):
        return "FAIL"
    lod_max = float(limits.get("lod_max") or 0)
    if lod_max and fit.get("lod") is not None and fit["lod"] > lod_max:
        return "FAIL"
    lo = float(limits.get("slope_min") or 0)
    hi = float(limits.get("slope_max") or 0)
    if lo and fit["slope"] < lo:
        return "FAIL"
    if hi and fit["slope"] > hi:
        return "FAIL"
    return "PASS"


def _conc_key(c) -> str:
    """Khoá JSON của nồng độ: `300`, `0`, `12.5` — không đuôi `.0` để app/sheet đọc quen."""
    f = float(c)
    return f"{f:g}"


def rank_combinations(concentrations, readings: dict, limits: dict | None = None,
                      top: int | None = None) -> dict:
    """Thử MỌI tổ hợp 1 ống/nồng độ (như Apps Script cũ), khớp đường thẳng raw = f(nM),
    xếp R² giảm dần rồi slope giảm dần. Ống chưa có số đo bị bỏ qua (sheet cũ cho `#NUM!`
    và FAIL — vô nghĩa, không phải một tổ hợp thật).

    Trả `{combos[:top], total, pass, suggested_sets, limits}`; `suggested_sets` = chọn THAM
    LAM từ trên xuống các tổ hợp PASS KHÔNG DÙNG CHUNG ống — một ống chỉ nằm trong một túi
    zip, nên bảng xếp hạng thô (nhiều dòng dùng lại cùng ống) không phải danh sách bộ có thể
    đóng gói.
    """
    lim = effective_limits(limits)
    concs = [float(c) for c in concentrations]
    per_conc: list[list[tuple[str, float]]] = []
    for c in concs:
        tubes = readings.get(_conc_key(c)) or {}
        have = []
        for no, raw in tubes.items():
            if raw is None or raw == "":
                continue
            try:
                have.append((str(no), float(raw)))
            except (TypeError, ValueError):
                continue
        have.sort(key=lambda t: int(t[0]) if t[0].isdigit() else 0)
        per_conc.append(have)
    n_comb = 1
    for h in per_conc:
        n_comb *= len(h)
    if n_comb == 0:
        return {"combos": [], "total": 0, "pass": 0, "suggested_sets": [], "limits": lim,
                "blank": blank_stats(list((readings.get("0") or {}).values())),
                "missing": [_conc_key(c) for c, h in zip(concs, per_conc) if not h]}
    if n_comb > MAX_COMBOS:
        raise CalibError(400, f"{n_comb} tổ hợp vượt trần {MAX_COMBOS} — giảm số ống/nồng độ")
    # QC blank theo WI: SD của MỌI ống 0 nM đo trên cùng khe tham chiếu → LOD từng tổ hợp.
    blank = blank_stats(list((readings.get("0") or {}).values()))
    combos = []
    for pick in itertools.product(*per_conc):
        fit = linear_fit(concs, [raw for _, raw in pick])
        if fit is None:
            continue
        fit["lod"] = lod_nM(blank["snr33"], fit["slope"])
        combos.append({
            "tubes": {_conc_key(c): no for c, (no, _) in zip(concs, pick)},
            "raw": {_conc_key(c): raw for c, (_, raw) in zip(concs, pick)},
            **fit,
            "status": judge(fit, lim),
        })
    combos.sort(key=lambda d: (-d["r2"], -d["slope"]))
    for i, d in enumerate(combos, 1):
        d["rank"] = i
    n_pass = sum(1 for d in combos if d["status"] == "PASS")
    suggested = []
    used: set[tuple[str, str]] = set()
    for d in combos:
        if d["status"] != "PASS":
            continue
        keys = {(c, no) for c, no in d["tubes"].items()}
        if keys & used:
            continue
        used |= keys
        suggested.append(d)
    return {
        "combos": combos[:top] if top else combos,
        "total": len(combos),
        "pass": n_pass,
        "suggested_sets": suggested,
        "blank": blank,
        "limits": lim,
    }


# ---------------------------------------------------------------------------------------
# Kiểm tra dữ liệu vào
# ---------------------------------------------------------------------------------------

def valid_id(s) -> bool:
    return isinstance(s, str) and bool(ID_RE.match(s)) and ".." not in s


def _as_readings(concentrations, raw) -> dict:
    """Chuẩn hoá `readings` gửi lên: `{"300": {"1": 1193, "2": null}}` → số float, `null`/"" =
    xoá số đo đó (giữ khoá với giá trị None để hàm merge biết mà xoá)."""
    if not isinstance(raw, dict):
        raise CalibError(400, "readings phải là object {nồng_độ: {số_ống: raw}}")
    allowed = {_conc_key(c) for c in concentrations}
    out: dict[str, dict[str, float | None]] = {}
    for ck, tubes in raw.items():
        k = _conc_key(ck) if isinstance(ck, (int, float)) or re.fullmatch(r"-?\d+(\.\d+)?", str(ck)) \
            else str(ck)
        if k not in allowed:
            raise CalibError(400, f"nồng độ {ck!r} không thuộc lô ({sorted(allowed)})")
        if not isinstance(tubes, dict):
            raise CalibError(400, f"readings[{k}] phải là object {{số_ống: raw}}")
        out[k] = {}
        for no, v in tubes.items():
            sno = str(no).strip()
            if not sno.isdigit() or int(sno) < 1:
                raise CalibError(400, f"số ống {no!r} không hợp lệ (1..N)")
            if v is None or v == "":
                out[k][sno] = None
                continue
            try:
                fv = float(v)
            except (TypeError, ValueError):
                raise CalibError(400, f"raw của ống {k}/{sno} phải là số")
            if fv < 0 or fv != fv:
                raise CalibError(400, f"raw của ống {k}/{sno} phải ≥ 0")
            out[k][sno] = fv
    return out


def validate_limits(doc) -> str | None:
    if not isinstance(doc, dict):
        return "body phải là JSON object"
    if not str(doc.get("version") or "").strip():
        return "thiếu version (bộ ngưỡng bắt buộc có version)"
    if len(str(doc.get("version"))) > 64:
        return "version quá dài (tối đa 64 ký tự)"
    try:
        r2 = float(doc.get("r2_min", DEFAULT_LIMITS["r2_min"]))
        lod = float(doc.get("lod_max", DEFAULT_LIMITS["lod_max"]) or 0)
        lo = float(doc.get("slope_min", 0) or 0)
        hi = float(doc.get("slope_max", 0) or 0)
        days = int(doc.get("shelf_days", DEFAULT_LIMITS["shelf_days"]))
    except (TypeError, ValueError):
        return "r2_min/slope_min/slope_max phải là số, shelf_days là số nguyên"
    if not 0 <= r2 <= 1:
        return "r2_min phải trong [0, 1]"
    if lod < 0:
        return "lod_max phải ≥ 0 (0 = không xét LOD)"
    if lo < 0 or hi < 0 or (lo and hi and lo > hi):
        return "slope_min/slope_max phải ≥ 0 và slope_min ≤ slope_max"
    if days < 0:
        return "shelf_days phải ≥ 0"
    return None


_LIMITS_META = ("updated_at", "updated_by", "source")


def limits_conflict(old, new) -> str | None:
    """Cùng luật với /ate/limits: MỘT version = MỘT nội dung (bộ ống chỉ ghi `limits_ver`)."""
    if not isinstance(old, dict) or not isinstance(new, dict):
        return None
    if str(old.get("version") or "") != str(new.get("version") or ""):
        return None
    strip = lambda d: {k: v for k, v in d.items() if k not in _LIMITS_META}
    if strip(old) != strip(new):
        return (f"version {new.get('version')!r} đã dùng cho một bộ ngưỡng KHÁC — "
                "đổi version rồi lưu lại")
    return None


# ---------------------------------------------------------------------------------------
# Kho file
# ---------------------------------------------------------------------------------------

def _root() -> Path:
    return Path(config.CALIB_DIR)


def _batches_dir() -> Path:
    return _root() / "batches"


def _sets_dir() -> Path:
    return _root() / "sets"


def _limits_file() -> Path:
    return _root() / "limits.json"


def _read(path: Path) -> dict | None:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return doc if isinstance(doc, dict) else None


def _write(path: Path, doc: dict) -> None:
    """Ghi tạm rồi os.replace — GET đang đọc dở không thấy nửa file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.parent / f".tmp-{path.name}"
    tmp.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(tmp, path)


def _log(kind: str, **fields) -> None:
    """Nhật ký append-only — pha/đo/cấp phát là việc phải giải trình được; lỗi ghi log
    không được làm hỏng thao tác chính."""
    rec = {"at": _now(), "kind": kind, **fields}
    try:
        _root().mkdir(parents=True, exist_ok=True)
        with (_root() / "history.jsonl").open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    except OSError as e:
        print(f"calib history failed: {e}", flush=True)


def _by(by) -> str:
    return str(by or "")[:64]


# --- ngưỡng ---------------------------------------------------------------------------

def read_limits() -> dict:
    doc = _read(_limits_file()) if _limits_file().is_file() else None
    out = effective_limits(doc)
    out["source"] = "file" if doc else "mặc định"
    if doc:
        out["updated_at"] = doc.get("updated_at", "")
        out["updated_by"] = doc.get("updated_by", "")
    return out


def write_limits(data: dict, by: str = "") -> dict:
    err = validate_limits(data)
    if err:
        raise CalibError(400, err)
    with _LOCK:
        old = _read(_limits_file()) if _limits_file().is_file() else None
        clash = limits_conflict(old, data)
        if clash:
            raise CalibError(400, clash)
        doc = {
            "version": str(data["version"]).strip(),
            "r2_min": float(data.get("r2_min", DEFAULT_LIMITS["r2_min"])),
            "lod_max": float(data.get("lod_max", DEFAULT_LIMITS["lod_max"]) or 0),
            "slope_min": float(data.get("slope_min", 0) or 0),
            "slope_max": float(data.get("slope_max", 0) or 0),
            "shelf_days": int(data.get("shelf_days", DEFAULT_LIMITS["shelf_days"])),
            "updated_at": _now(),
            "updated_by": _by(by),
        }
        _write(_limits_file(), doc)
    _log("limits", version=doc["version"], by=doc["updated_by"])
    return {"ok": True, "version": doc["version"]}


# --- lô pha ---------------------------------------------------------------------------

def _new_batch_id() -> str:
    """`CB<yymmdd>-<nn>` — tăng dần trong ngày, quét kho để không trùng (gọi trong _LOCK).
    Ngày theo giờ ĐỊA PHƯƠNG của box (nhãn in lên túi zip phải khớp ngày kỹ sư pha); các dấu
    thời gian bên trong tài liệu vẫn là UTC ISO như mọi kho khác."""
    stamp = datetime.now().astimezone().strftime("%y%m%d")
    prefix = f"CB{stamp}-"
    n = 0
    if _batches_dir().is_dir():
        for p in _batches_dir().glob(f"{prefix}*.json"):
            tail = p.stem[len(prefix):]
            if tail.isdigit():
                n = max(n, int(tail))
    return f"{prefix}{n + 1:02d}"


def batch_path(bid: str) -> Path:
    if not valid_id(bid):
        raise CalibError(400, "mã lô không hợp lệ")
    return _batches_dir() / f"{bid}.json"


def read_batch(bid: str) -> dict:
    p = batch_path(bid)
    doc = _read(p) if p.is_file() else None
    if doc is None:
        raise CalibError(404, "không có lô này")
    return doc


def batch_meta(doc: dict) -> dict:
    """Dòng tóm tắt cho bảng danh sách lô (không kèm steps/readings)."""
    readings = doc.get("readings") or {}
    n_read = sum(1 for tubes in readings.values() if isinstance(tubes, dict)
                 for v in tubes.values() if v is not None)
    total = len(doc.get("concentrations") or []) * int(doc.get("tubes_per_conc") or 0)
    steps = doc.get("steps") or []
    return {
        "id": doc.get("id", ""),
        "status": doc.get("status", ""),
        "created_at": doc.get("created_at", ""),
        "created_by": doc.get("created_by", ""),
        "updated_at": doc.get("updated_at", ""),
        "stock_lot": (doc.get("stock") or {}).get("lot", ""),
        "reader": (doc.get("reader") or {}).get("device", ""),
        "steps_done": sum(1 for s in steps if s.get("done_at")),
        "steps_total": len(steps),
        "readings_done": n_read,
        "readings_total": total,
        "sets": int(doc.get("n_sets") or 0),
        "note": doc.get("note", ""),
    }


def list_batches(status: str = "") -> list[dict]:
    out = []
    if _batches_dir().is_dir():
        for p in _batches_dir().glob("*.json"):
            if p.name.startswith("."):
                continue
            doc = _read(p)
            if doc is None:
                continue
            if status and doc.get("status") != status:
                continue
            out.append(batch_meta(doc))
    out.sort(key=lambda m: m["created_at"], reverse=True)
    return out


def create_batch(data: dict | None, by: str = "") -> dict:
    """Tạo lô mới từ template, đè các trường người dùng gửi (stock, buffer, concentrations,
    tubes_per_conc, aliquot_ul, note, steps). Trả tài liệu lô."""
    data = data if isinstance(data, dict) else {}
    kw = {}
    for k in ("concentrations", "tubes_per_conc", "aliquot_ul", "working_nM",
              "working_total_ul", "target_total_ul"):
        if data.get(k) is not None:
            kw[k] = data[k]
    stock = data.get("stock") if isinstance(data.get("stock"), dict) else {}
    if stock.get("conc_nM") is not None:
        kw["stock_nM"] = stock["conc_nM"]
    try:
        tpl = template(**kw)
    except (TypeError, ValueError):
        raise CalibError(400, "concentrations/tubes_per_conc/aliquot_ul/conc_nM phải là số")
    concs = tpl["concentrations"]
    if len(concs) < 2 or len(set(concs)) != len(concs) or any(c < 0 for c in concs):
        raise CalibError(400, "concentrations phải ≥ 2 giá trị khác nhau, không âm")
    if not 1 <= tpl["tubes_per_conc"] <= 50:
        raise CalibError(400, "tubes_per_conc phải trong 1..50")
    doc = {
        "id": "",
        "status": "prep",
        "created_at": _now(),
        "created_by": _by(by),
        "updated_at": "",
        "stock": {**tpl["stock"], **{k: v for k, v in stock.items() if k in tpl["stock"]}},
        "buffer": {**tpl["buffer"], **{k: v for k, v in (data.get("buffer") or {}).items()
                                        if k in tpl["buffer"]}},
        "concentrations": concs,
        "tubes_per_conc": tpl["tubes_per_conc"],
        "aliquot_ul": tpl["aliquot_ul"],
        "steps": data.get("steps") if isinstance(data.get("steps"), list) else tpl["steps"],
        "reader": {"device": "", "slot": None, "fw": "", "note": ""},
        "readings": {_conc_key(c): {} for c in concs},
        "n_sets": 0,
        "ranked_at": "",
        "limits_ver": "",
        "note": str(data.get("note") or "")[:2000],
    }
    with _LOCK:
        doc["id"] = _new_batch_id()
        _write(batch_path(doc["id"]), doc)
    _log("batch.create", id=doc["id"], by=doc["created_by"])
    return doc


_STEP_FIELDS = ("done_at", "by", "actual_dye_ul", "actual_buffer_ul", "actual_total_ul", "note")


def update_batch(bid: str, data: dict, by: str = "") -> dict:
    """Cập nhật một phần: `stock`, `buffer`, `reader`, `note`, `status`, `steps` (gộp theo
    `code`, chỉ các trường thực-tế), `readings` (gộp từng ống; null = xoá). Lô `closed` chỉ
    còn đổi được `status`."""
    if not isinstance(data, dict):
        raise CalibError(400, "body phải là JSON object")
    with _LOCK:
        doc = read_batch(bid)
        if doc.get("status") == "closed" and set(data) - {"status"}:
            raise CalibError(409, "lô đã đóng — mở lại (status) trước khi sửa")
        changed = []
        if "status" in data:
            st = str(data["status"])
            if st not in BATCH_STATUSES:
                raise CalibError(400, f"status phải là một trong {BATCH_STATUSES}")
            doc["status"] = st
            changed.append("status")
        for key in ("stock", "buffer", "reader"):
            if key in data:
                if not isinstance(data[key], dict):
                    raise CalibError(400, f"{key} phải là object")
                cur = doc.get(key) or {}
                for k, v in data[key].items():
                    if k in cur or (key == "reader" and k in ("device", "slot", "fw", "note")):
                        if k == "conc_nM" and v not in (None, ""):
                            try:
                                v = float(v)
                            except (TypeError, ValueError):
                                raise CalibError(400, "stock.conc_nM phải là số")
                        cur[k] = v if not isinstance(v, str) else v[:200]
                doc[key] = cur
                changed.append(key)
        if "note" in data:
            doc["note"] = str(data["note"] or "")[:2000]
            changed.append("note")
        if "steps" in data:
            if not isinstance(data["steps"], list):
                raise CalibError(400, "steps phải là mảng [{code, done_at, by, actual_*…}]")
            by_code = {s.get("code"): s for s in doc.get("steps") or []}
            for st in data["steps"]:
                if not isinstance(st, dict) or st.get("code") not in by_code:
                    raise CalibError(400, f"bước {st.get('code') if isinstance(st, dict) else st!r} "
                                          "không có trong lô")
                tgt = by_code[st["code"]]
                for f in _STEP_FIELDS:
                    if f in st:
                        v = st[f]
                        if f.startswith("actual_") and v not in (None, ""):
                            try:
                                v = float(v)
                            except (TypeError, ValueError):
                                raise CalibError(400, f"{f} của bước {st['code']} phải là số")
                        tgt[f] = v if not isinstance(v, str) else v[:200]
            changed.append("steps")
        if "readings" in data:
            new = _as_readings(doc["concentrations"], data["readings"])
            cur = doc.setdefault("readings", {})
            n_max = int(doc.get("tubes_per_conc") or 0)
            for ck, tubes in new.items():
                slot = cur.setdefault(ck, {})
                for no, v in tubes.items():
                    if int(no) > n_max:
                        raise CalibError(400, f"số ống {no} vượt {n_max} ống/nồng độ của lô")
                    if v is None:
                        slot.pop(no, None)
                    else:
                        slot[no] = v
            # có số đo → lô ít nhất đang ở bước đo (không kéo lùi lô đã xếp hạng)
            if doc.get("status") == "prep":
                doc["status"] = "measure"
            changed.append("readings")
        doc["updated_at"] = _now()
        doc["updated_by"] = _by(by)
        _write(batch_path(bid), doc)
    _log("batch.update", id=bid, by=_by(by), fields=changed)
    return doc


def delete_batch(bid: str, by: str = "") -> dict:
    """Xoá lô tạo nhầm — chỉ khi chưa có bộ ống nào (bộ ống là hồ sơ cấp phát, không xoá)."""
    with _LOCK:
        doc = read_batch(bid)
        if int(doc.get("n_sets") or 0) > 0 or any(True for _ in _sets_of(bid)):
            raise CalibError(409, "lô đã có bộ ống — không xoá được")
        batch_path(bid).unlink()
    _log("batch.delete", id=bid, by=_by(by))
    return {"ok": True, "id": bid}


def rank_batch(bid: str, top: int | None = 50) -> dict:
    doc = read_batch(bid)
    lim = read_limits()
    res = rank_combinations(doc["concentrations"], doc.get("readings") or {}, lim, top)
    res["limits"] = lim  # kèm source/updated_* để app hiện "đang chấm theo bộ nào"
    used = set()
    for s in _sets_of(bid):
        for ck, no in (s.get("tubes") or {}).items():
            used.add((ck, str(no)))
    # Ống đã nằm trong bộ thì không gợi ý lại (túi zip đã đóng).
    if used:
        res["suggested_sets"] = [d for d in res["suggested_sets"]
                                 if not any((ck, str(no)) in used for ck, no in d["tubes"].items())]
    res["batch"] = bid
    res["tubes_in_sets"] = sorted(f"{ck}/{no}" for ck, no in used)
    return res


# --- bộ ống ---------------------------------------------------------------------------

def set_path(sid: str) -> Path:
    if not valid_id(sid):
        raise CalibError(400, "mã bộ không hợp lệ")
    return _sets_dir() / f"{sid}.json"


def read_set(sid: str) -> dict:
    p = set_path(sid)
    doc = _read(p) if p.is_file() else None
    if doc is None:
        raise CalibError(404, "không có bộ ống này")
    return doc


def _sets_of(bid: str):
    if not _sets_dir().is_dir():
        return
    for p in _sets_dir().glob(f"{bid}-S*.json"):
        if p.name.startswith("."):
            continue
        doc = _read(p)
        if doc and doc.get("batch") == bid:
            yield doc


def list_sets(status: str = "", device: str = "", batch: str = "") -> list[dict]:
    out = []
    if _sets_dir().is_dir():
        for p in _sets_dir().glob("*.json"):
            if p.name.startswith("."):
                continue
            doc = _read(p)
            if doc is None:
                continue
            if status and doc.get("status") != status:
                continue
            if batch and doc.get("batch") != batch:
                continue
            if device and str(doc.get("device") or "").lower() != device.lower():
                continue
            out.append({k: doc.get(k) for k in ("id", "batch", "rank", "status", "tubes", "raw",
                                                  "slope", "intercept", "r2", "lod", "verdict",
                                                  "created_at",
                                                  "created_by", "expires_at", "device",
                                                  "updated_at", "limits_ver")})
    out.sort(key=lambda m: (m.get("created_at") or "", m.get("id") or ""), reverse=True)
    return out


def create_sets(bid: str, data: dict, by: str = "") -> dict:
    """Đóng gói các tổ hợp đã chọn thành bộ ống. Server TÍNH LẠI hồi quy từ số đo của lô (không
    tin số app gửi), chặn ống đã nằm trong bộ khác của lô và ống trùng giữa các bộ trong cùng
    lần gửi. Body `{combos: [{tubes: {"300": "3", …}}, …], expires_days?}`."""
    if not isinstance(data, dict) or not isinstance(data.get("combos"), list) or not data["combos"]:
        raise CalibError(400, "body phải có combos: [{tubes: {nồng_độ: số_ống}}]")
    if len(data["combos"]) > 100:
        raise CalibError(400, "tối đa 100 bộ mỗi lần")
    with _LOCK:
        doc = read_batch(bid)
        if doc.get("status") == "closed":
            raise CalibError(409, "lô đã đóng")
        lim = read_limits()
        concs = doc["concentrations"]
        readings = doc.get("readings") or {}
        blank = blank_stats(list((readings.get("0") or {}).values()))
        used: set[tuple[str, str]] = set()
        n = 0
        for s in _sets_of(bid):
            n = max(n, int(str(s.get("id", "")).rsplit("-S", 1)[-1] or 0))
            for ck, no in (s.get("tubes") or {}).items():
                used.add((ck, str(no)))
        days = data.get("expires_days")
        try:
            days = int(days) if days not in (None, "") else int(lim.get("shelf_days") or 0)
        except (TypeError, ValueError):
            raise CalibError(400, "expires_days phải là số nguyên")
        now = datetime.now(timezone.utc)
        expires = (now + timedelta(days=days)).date().isoformat() if days > 0 else ""
        new_docs = []
        for combo in data["combos"]:
            tubes = combo.get("tubes") if isinstance(combo, dict) else None
            if not isinstance(tubes, dict):
                raise CalibError(400, "mỗi combo phải có tubes: {nồng_độ: số_ống}")
            picked_x, picked_y, tmap, rmap = [], [], {}, {}
            for c in concs:
                ck = _conc_key(c)
                no = str(tubes.get(ck, tubes.get(c, ""))).strip()
                if not no.isdigit():
                    raise CalibError(400, f"combo thiếu ống cho {ck} nM")
                raw = (readings.get(ck) or {}).get(no)
                if raw is None:
                    raise CalibError(400, f"ống {ck}/{no} chưa có số đo")
                if (ck, no) in used:
                    raise CalibError(409, f"ống {ck}/{no} đã nằm trong một bộ khác")
                used.add((ck, no))
                picked_x.append(float(c))
                picked_y.append(float(raw))
                tmap[ck] = no
                rmap[ck] = float(raw)
            fit = linear_fit(picked_x, picked_y)
            if fit is None:
                raise CalibError(400, "không khớp được đường thẳng cho combo")
            fit["lod"] = lod_nM(blank["snr33"], fit["slope"])
            n += 1
            sid = f"{bid}-S{n:02d}"
            sdoc = {
                "id": sid,
                "batch": bid,
                "rank": combo.get("rank") if isinstance(combo.get("rank"), int) else None,
                "tubes": tmap,
                "raw": rmap,
                **fit,
                "blank": blank,
                "verdict": judge(fit, lim),
                "limits_ver": lim["version"],
                "status": "stored",
                "device": "",
                "created_at": now.isoformat(),
                "created_by": _by(by),
                "updated_at": now.isoformat(),
                "expires_at": expires,
                "note": str(combo.get("note") or "")[:500],
                "history": [{"at": now.isoformat(), "by": _by(by), "status": "stored",
                             "device": "", "note": "đóng gói"}],
            }
            new_docs.append(sdoc)
        for sdoc in new_docs:
            _write(set_path(sdoc["id"]), sdoc)
        doc["n_sets"] = int(doc.get("n_sets") or 0) + len(new_docs)
        doc["status"] = "ranked"
        doc["ranked_at"] = now.isoformat()
        doc["limits_ver"] = lim["version"]
        doc["updated_at"] = now.isoformat()
        doc["updated_by"] = _by(by)
        _write(batch_path(bid), doc)
    for sdoc in new_docs:
        _log("set.create", id=sdoc["id"], batch=bid, by=_by(by), r2=sdoc["r2"],
             verdict=sdoc["verdict"])
    return {"ok": True, "batch": bid, "sets": [s["id"] for s in new_docs],
            "items": new_docs}


def update_set(sid: str, status: str, device: str = "", by: str = "", note: str = "") -> dict:
    """Đổi trạng thái bộ ống theo SET_TRANSITIONS. `issued` bắt buộc có `device` (số máy nhận);
    mọi lần đổi ghi vào `history` của bộ + nhật ký chung."""
    st = str(status or "").strip()
    if st not in SET_STATUSES:
        raise CalibError(400, f"status phải là một trong {SET_STATUSES}")
    dev = str(device or "").strip()[:64]
    if st == "issued" and not dev:
        raise CalibError(400, "cấp phát phải ghi số máy nhận (device)")
    with _LOCK:
        doc = read_set(sid)
        cur = doc.get("status", "stored")
        if st not in SET_TRANSITIONS.get(cur, ()):
            raise CalibError(409, f"không chuyển được {cur} → {st}")
        now = _now()
        doc["status"] = st
        doc["device"] = dev if st == "issued" else (doc.get("device", "") if st == "used" else "")
        doc["updated_at"] = now
        doc.setdefault("history", []).append({"at": now, "by": _by(by), "status": st,
                                               "device": doc["device"],
                                               "note": str(note or "")[:500]})
        _write(set_path(sid), doc)
    _log("set.status", id=sid, status=st, device=doc["device"], by=_by(by))
    return doc
