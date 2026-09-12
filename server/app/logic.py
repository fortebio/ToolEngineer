"""Hàm thuần + tiện ích băm — không phụ thuộc fastapi/psycopg. Test được độc lập."""
import hashlib
import hmac
import json
import os
import re
from datetime import datetime, timedelta, timezone

from app.config import ARRAY_FIELDS

_VN = timezone(timedelta(hours=7))
# Thời điểm đo nằm trong TÊN FILE. Khớp cả 2 kiểu phân tách giờ: ':' (gốc Drive) và '_' (tải trên Windows).
_ISO_RE = re.compile(r"(\d{4})-(\d{2})-(\d{2})T(\d{2})[_:](\d{2})[_:](\d{2})(?:\.\d+)?Z")
_ALT_RE = re.compile(r"(\d{2})-(\d{2})-(\d{4})[ _](\d{2})[_:](\d{2})[_:](\d{2})")


def parse_ts(name: str):
    """Đọc thời điểm đo từ chuỗi (tên file hoặc trường 'time'); None nếu không khớp/không hợp lệ."""
    try:
        m = _ISO_RE.search(name)
        if m:
            y, mo, d, h, mi, s = map(int, m.groups())
            return datetime(y, mo, d, h, mi, s, tzinfo=timezone.utc)  # có Z -> UTC
        m = _ALT_RE.search(name)
        if m:
            d, mo, y, h, mi, s = map(int, m.groups())  # DD-MM-YYYY, không Z -> giờ VN
            return datetime(y, mo, d, h, mi, s, tzinfo=_VN)
    except ValueError:  # regex khớp nhưng ngày/giờ ngoài khoảng (tháng 45, giờ 25...) -> coi như không có
        return None
    return None


def payload_time(data) -> datetime | None:
    """Thời điểm đo từ trường 'time' trong payload (vd '06-08-2025 17:14:27', giờ VN).
    None nếu không có / 'N/A' / không parse được — caller lùi về tên file rồi now()."""
    if not isinstance(data, dict):
        return None
    return parse_ts(str(data.get("time") or ""))


def safe_name(s: str) -> str:
    """Chỉ giữ ký tự an toàn cho tên file — chống path traversal (../, /...)."""
    return re.sub(r"[^A-Za-z0-9_.-]", "_", s).strip("._")[:64] or "unknown"


def check_auth(auth_header: str, token: str) -> bool:
    """Chỉ cho qua nếu header khớp 'Bearer <token>'.

    fail-CLOSED: chưa đặt token -> TỪ CHỐI hết. Server chạy public qua Funnel nên không được
    mở toang khi env token trống/lỗi cấu hình (thà 401 toàn bộ để phát hiện, còn hơn lộ dữ liệu).
    """
    if not token:
        return False
    # so sánh dạng bytes: header có ký tự non-ASCII cũng không làm compare_digest ném TypeError
    return hmac.compare_digest(auth_header.encode("utf-8", "ignore"), f"Bearer {token}".encode("utf-8"))


def validate(data) -> str | None:
    """Trả thông báo lỗi, None nếu hợp lệ. Chỉ chặn rác hiển nhiên, không ép đủ schema RPL."""
    if not isinstance(data, dict) or not str(data.get("id_device", "")).strip():
        return "missing id_device"
    for f in ARRAY_FIELDS:
        if f in data and not (isinstance(data[f], list) and len(data[f]) == 10):
            return f"{f} must be a list of 10 items"
    return None


def hash_password(password: str, salt: bytes | None = None) -> str:
    """Băm mật khẩu (scrypt, có muối). Lưu dạng 'scrypt$<salt_hex>$<hash_hex>' — không lưu thô."""
    salt = salt or os.urandom(16)
    dk = hashlib.scrypt(password.encode("utf-8"), salt=salt, n=16384, r=8, p=1, dklen=32)
    return f"scrypt${salt.hex()}${dk.hex()}"


def verify_password(password: str, stored: str) -> bool:
    """So khớp mật khẩu với chuỗi băm đã lưu (timing-safe).

    Hỗ trợ 2 định dạng: 'scrypt$<salt>$<hash>' (server tạo) và
    'sha256$<salt>$<hash>' (di cư từ Google Sheet userAuth.js —
    hash = sha256(salt + password), giữ nguyên để user cũ khỏi đổi mật khẩu;
    đổi mật khẩu trên server sẽ tự nâng lên scrypt).
    """
    try:
        algo, salt_hex, hash_hex = stored.split("$")
        if algo == "scrypt":
            dk = hashlib.scrypt(password.encode("utf-8"), salt=bytes.fromhex(salt_hex),
                                n=16384, r=8, p=1, dklen=32)
            computed = dk.hex()
        elif algo == "sha256":  # legacy Sheet: salt nối chuỗi, không phải bytes
            computed = hashlib.sha256((salt_hex + password).encode("utf-8")).hexdigest()
        else:
            return False
    except (ValueError, AttributeError):
        return False
    return hmac.compare_digest(computed, hash_hex)


def parse_ids(raw) -> list[str]:
    """Chuẩn hoá danh sách mã máy (khớp parseIds_ của userAuth.js): nhận list
    hoặc chuỗi tách bởi phẩy/chấm phẩy/khoảng trắng; trim, bỏ rỗng, dedupe, giữ '*'."""
    parts = raw if isinstance(raw, list) else re.split(r"[\s,;]+", str(raw or ""))
    out, seen = [], set()
    for p in parts:
        token = str(p).strip()
        if token and token not in seen:
            seen.add(token)
            out.append(token)
    return out


def parse_active(v) -> bool:
    """TRUE/FALSE robust (khớp parseActive_ userAuth.js): trống/lạ = active."""
    if isinstance(v, bool):
        return v
    s = str(v or "").strip().lower()
    return s not in ("false", "0", "no", "n")


def canonical_sha256(data: dict) -> bytes:
    """Hash nội dung đã chuẩn hóa (sort_keys, compact) — độc lập cách format file/POST."""
    canon = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canon.encode("utf-8")).digest()


def parse_amplification(payload: dict) -> list[dict]:
    """10 chuỗi CSV '150,153,...' → mảng số sẵn cho fl_chart vẽ."""
    ct = payload.get("CT_value") or []
    res = payload.get("result") or []
    slots = []
    for i, csv in enumerate(payload.get("amplification") or []):
        pts = []
        for x in str(csv).split(","):
            x = x.strip()
            if not x:
                continue
            try:
                pts.append(float(x))
            except ValueError:  # token không phải số -> bỏ qua, không làm hỏng cả endpoint
                pass
        slots.append({
            "slot": i + 1,
            "ct_value": ct[i] if i < len(ct) else None,
            "result": res[i] if i < len(res) else None,
            "points": pts,
        })
    return slots


# ---------- Trạm ATE (hồ sơ nghiệm thu máy tại xưởng) ----------
# Hồ sơ do app (tab "Sản xuất") đẩy lên sau khi một máy chạy hết kịch bản nạp +
# khai sinh. Server chỉ NHẬN và TRA CỨU — việc chấm PASS/FAIL nằm ở app, đúng
# nguyên tắc của kế hoạch: ngưỡng là dữ liệu có version, đổi ngưỡng không phải
# sửa server (docs/plan/ate-san-xuat.md §4, §7.2).

ATE_VERDICTS = ("pass", "fail", "aborted")
ATE_STEP_VERDICTS = ("pass", "fail", "skip", "info")

# Bộ giới hạn MẶC ĐỊNH — trả về khi chưa ai PUT bộ nào. Cố tình mỏng: chỉ những
# ngưỡng P0 thật sự dùng được ngay, phần quang/nhiệt để trống cho tới khi đo
# golden unit (kế hoạch §6: "mọi ô cần chốt số phải điền bằng dữ liệu đo").
DEFAULT_ATE_LIMITS = {
    "version": "p1-2026-09-07",
    "note": "Bộ mặc định P0+P1. Các ngưỡng QUANG để null = CHƯA CHỐT: app sẽ chỉ "
            "GHI SỐ (verdict 'info'), không chấm đạt/hỏng. Điền bằng số đo của "
            "10-20 golden unit rồi PUT bộ mới - đặt ngưỡng theo cảm tính còn tệ "
            "hơn không có ngưỡng.",
    "sn_max_len": 9,          # char device_id[10] của firmware (§9.1)
    "boot_watch_sec": 30,     # thời gian nghe UART sau khi nạp (BOOT-01)
    "serial_baud": 115200,
    "ack_timeout_sec": 8,     # chờ máy trả lời một lệnh Serial
    "require_flash_verify": True,

    # --- P1: quang (OPT-03) - CHƯA CHỐT, chờ golden unit ---
    "bright_min": None,        # tín hiệu sáng thấp nhất chấp nhận được
    "bright_max": None,        # cao nhất (quang bão hoà / hở sáng)
    "bright_spread_pct": None,  # lệch tối đa giữa 10 kênh (%)

    # --- P1: nhiệt (TMP-01) ---
    # Hai con số lấy từ kế hoạch §6 (lệch nhiệt phòng <= 3 °C, lệch giữa kênh
    # <= 2 °C). `ambient_c` để null vì nhiệt phòng là số của TỪNG xưởng - đo rồi
    # điền, đừng đoán; thiếu nó thì app chỉ chấm phần "lệch giữa các kênh".
    "ambient_c": None,
    "temp_tol_c": 3,
    "temp_spread_c": 2,
    "temp_window_sec": 8,      # nghe UART bao lâu để lấy mẫu nhiệt
    "fan_wait_sec": 4,         # FAN-01: chờ quạt chạy rồi mới hỏi người vận hành
}


def validate_ate_record(doc) -> str | None:
    """Trả thông báo lỗi (tiếng Việt), None nếu hồ sơ hợp lệ.

    Chỉ chặn rác hiển nhiên - KHÔNG chấm lại ngưỡng: một hồ sơ FAIL vẫn phải vào
    được kho, đó chính là dữ liệu quý nhất. Cũng KHÔNG ép sn <= 9 ký tự (ràng
    buộc firmware): máy bị gõ nhầm số là một ca FAIL có thật, chặn ở đây là làm
    biến mất đúng bằng chứng cần lưu - app chấm bước ID-01 hỏng là đủ.
    """
    if not isinstance(doc, dict):
        return "body phải là JSON object"
    if not str(doc.get("sn") or "").strip():
        return "thiếu sn (số máy)"
    if len(str(doc.get("sn"))) > 64:
        return "sn quá dài (tối đa 64 ký tự)"
    if str(doc.get("verdict") or "").strip().lower() not in ATE_VERDICTS:
        return "verdict phải là pass | fail | aborted"
    if not str(doc.get("limits_ver") or "").strip():
        return "thiếu limits_ver (bộ ngưỡng đã chấm)"
    steps = doc.get("steps")
    if not isinstance(steps, list) or not steps:
        return "thiếu steps (danh sách bước đã chạy)"
    for i, s in enumerate(steps):
        if not isinstance(s, dict):
            return f"steps[{i}] phải là object"
        if not str(s.get("code") or "").strip():
            return f"steps[{i}] thiếu code"
        if str(s.get("verdict") or "").strip().lower() not in ATE_STEP_VERDICTS:
            return f"steps[{i}].verdict phải là pass | fail | skip | info"
    return None


def ate_first_fail(steps) -> str:
    """Mã bước HỎNG ĐẦU TIÊN - cột vẽ Pareto. Rỗng nếu không bước nào fail."""
    for s in steps if isinstance(steps, list) else []:
        if isinstance(s, dict) and str(s.get("verdict") or "").lower() == "fail":
            return str(s.get("code") or "")
    return ""


def normalize_ate_record(doc: dict, received_at: str) -> dict:
    """Hồ sơ đã chuẩn hoá để ghi file. Cắt trần các trường tự khai, tự suy
    fail_code nếu client không gửi (Pareto không được phụ thuộc client nhớ gửi)."""
    steps = [s for s in doc.get("steps", []) if isinstance(s, dict)]
    verdict = str(doc.get("verdict")).strip().lower()
    return {
        "sn": str(doc.get("sn")).strip()[:64],
        # Lô sản xuất: khoá để tra "lô này chấm theo tiêu chuẩn nào" và để lọc
        # FPY theo lô. Rỗng = trạm chưa khai lô (hợp lệ, nhưng mất khả năng đó).
        "batch": str(doc.get("batch") or "").strip()[:64],
        "mac": str(doc.get("mac") or "")[:32],
        "station": str(doc.get("station") or "")[:64],
        "operator": str(doc.get("operator") or "")[:64],
        "fw_version": str(doc.get("fw_version") or "")[:32],
        # sha256 của .bin đã nạp - chuỗi version KHÔNG đủ để truy vết (§7.1: từng
        # có hai image khác nhau cùng mang tên v2.4.5).
        "fw_sha256": str(doc.get("fw_sha256") or "")[:64],
        "pcb_version": str(doc.get("pcb_version") or "")[:32],
        "limits_ver": str(doc.get("limits_ver")).strip()[:64],
        "started_at": str(doc.get("started_at") or "")[:40],
        "finished_at": str(doc.get("finished_at") or "")[:40],
        "received_at": received_at,
        "verdict": verdict,
        "fail_code": str(doc.get("fail_code") or ate_first_fail(steps))[:32],
        "note": str(doc.get("note") or "")[:2000],
        "app": str(doc.get("app") or "")[:64],
        "calib": doc.get("calib") if isinstance(doc.get("calib"), dict) else None,
        "steps": steps,
    }


def ate_meta(doc: dict, filename: str) -> dict:
    """Bản tóm tắt cho danh sách - KHÔNG kèm steps (mở từng hồ sơ mới có).

    steps là phần nặng nhất (log thô của từng bước); nhét vào danh sách 50 hồ sơ
    là vài MB cho một màn chỉ hiện mỗi dòng một chữ PASS/FAIL.
    """
    steps = doc.get("steps") if isinstance(doc.get("steps"), list) else []
    return {
        "id": filename,
        **{k: doc.get(k) for k in (
            "sn", "batch", "mac", "station", "operator", "fw_version", "fw_sha256",
            "pcb_version", "limits_ver", "started_at", "finished_at",
            "received_at", "verdict", "fail_code", "note", "app")},
        "steps_total": len(steps),
        "steps_failed": sum(
            1 for s in steps
            if isinstance(s, dict) and str(s.get("verdict") or "").lower() == "fail"),
    }


def ate_match(meta: dict, sn: str = "", from_: str = "", to: str = "",
              verdict: str = "", batch: str = "") -> bool:
    """Lọc một hồ sơ theo tham số query. So ngày trên chuỗi ISO (started_at) chứ
    không parse: hồ sơ nào thiếu/lệch định dạng thì rơi ra ngoài bộ lọc ngày thay
    vì làm 500 cả danh sách."""
    if sn and str(meta.get("sn") or "").lower() != sn.lower():
        return False
    if verdict and str(meta.get("verdict") or "").lower() != verdict.lower():
        return False
    if batch and str(meta.get("batch") or "").lower() != batch.lower():
        return False
    day = str(meta.get("started_at") or meta.get("received_at") or "")[:10]
    if from_ and (not day or day < from_):
        return False
    if to and (not day or day > to):
        return False
    return True


def ate_stats(metas: list) -> dict:
    """FPY + Pareto + sản lượng theo ngày, tính từ danh sách tóm tắt hồ sơ.

    **FPY (First Pass Yield) = số máy ĐẠT NGAY LẦN THỬ ĐẦU / số máy đã thử.** Máy
    phải chạy lại 3 lần mới PASS thì KHÔNG tính là first-pass - đó chính là con số
    kế hoạch muốn theo dõi (§1), khác hẳn "tỉ lệ hồ sơ PASS" vốn đẹp lên khi thao
    tác viên bấm chạy lại nhiều lần.

    Hồ sơ aborted (người bấm dừng, mất điện...) KHÔNG tính là một lần thử: nó
    không nói gì về chất lượng máy.
    """
    per_sn: dict[str, list] = {}
    pareto: dict[str, int] = {}
    by_day: dict[str, dict] = {}
    counts = {"pass": 0, "fail": 0, "aborted": 0}
    for m in metas:
        v = str(m.get("verdict") or "").lower()
        if v in counts:
            counts[v] += 1
        day = str(m.get("started_at") or m.get("received_at") or "")[:10] or "?"
        d = by_day.setdefault(day, {"day": day, "pass": 0, "fail": 0, "aborted": 0})
        if v in d:
            d[v] += 1
        if v == "fail":
            code = str(m.get("fail_code") or "?")
            pareto[code] = pareto.get(code, 0) + 1
        if v in ("pass", "fail"):
            per_sn.setdefault(str(m.get("sn") or ""), []).append(m)

    first_pass = tried = 0
    for _sn, items in per_sn.items():
        # Sắp theo thời gian BẮT ĐẦU: file được liệt kê mới-nhất-trước, mà "lần
        # thử đầu" phải là bản ghi CŨ NHẤT.
        items.sort(key=lambda m: str(m.get("started_at") or m.get("received_at") or ""))
        tried += 1
        if str(items[0].get("verdict") or "").lower() == "pass":
            first_pass += 1

    return {
        "total": len(metas),
        **counts,
        "machines": tried,
        "first_pass": first_pass,
        "fpy": round(first_pass / tried, 4) if tried else None,
        "pareto": [{"code": c, "count": n}
                   for c, n in sorted(pareto.items(), key=lambda kv: (-kv[1], kv[0]))],
        "by_day": [by_day[d] for d in sorted(by_day)],
    }


def validate_ate_limits(doc) -> str | None:
    """Bộ ngưỡng phải có version - mọi hồ sơ ghi kèm limits_ver, không có version
    thì không truy được máy đã bị chấm theo ngưỡng nào (§4.3)."""
    if not isinstance(doc, dict):
        return "body phải là JSON object"
    if not str(doc.get("version") or "").strip():
        return "thiếu version (bộ ngưỡng bắt buộc có version)"
    if len(str(doc.get("version"))) > 64:
        return "version quá dài (tối đa 64 ký tự)"
    return None


# Các trường server tự gắn khi lưu - KHÔNG tính vào so sánh nội dung.
_ATE_LIMITS_META = ("updated_at", "updated_by", "batch", "source")


def ate_limits_conflict(old, new) -> str | None:
    """Trả lỗi nếu [new] dùng LẠI `version` của [old] nhưng nội dung khác.

    Vì sao chặn: hồ sơ nghiệm thu chỉ ghi `limits_ver`. Cho phép cùng một chuỗi
    version mang hai nội dung khác nhau là làm câu hỏi "máy này bị chấm theo
    ngưỡng nào" thành không trả lời được - đúng bài học đã trả giá với firmware
    (`fbt_v2.4.5.bin` từng là hai image khác nhau, xem docs/plan §7.1). Sửa
    ngưỡng thì đổi version, một dòng chữ, và mọi hồ sơ cũ vẫn tra ngược được.
    """
    if not isinstance(old, dict) or not isinstance(new, dict):
        return None
    if str(old.get("version") or "") != str(new.get("version") or ""):
        return None
    strip = lambda d: {k: v for k, v in d.items() if k not in _ATE_LIMITS_META}
    if strip(old) != strip(new):
        return (f"version {new.get('version')!r} đã dùng cho một bộ ngưỡng KHÁC - "
                "đổi version rồi lưu lại (hồ sơ cũ chỉ ghi chuỗi version này)")
    return None


# --- OTA nhiều sản phẩm (docs/plan/ota-nhieu-san-pham.md ở repo app) -----------------
# Khoá sản phẩm: chữ thường + số + gạch ngang, gộp luôn biến thể (`rapidplus`, `rapidplus-a`,
# `reader`). Ba từ dành riêng vì chúng là ĐOẠN PATH cố định của /ota/*: một sản phẩm tên
# "target" sẽ làm `PUT /ota/target/<file>` (đặt bản chung kiểu cũ) và `PUT /ota/<product>/<file>`
# (upload kiểu mới) trỏ vào cùng một URL.
PRODUCT_RE = re.compile(r"[a-z0-9-]{1,24}")
PRODUCT_RESERVED = frozenset({"check", "products", "target"})


def product_key(s) -> str | None:
    """Khoá sản phẩm hợp lệ, hoặc None. KHÔNG tự hạ hoa/thường: firmware gửi `Reader` là
    firmware gửi sai, và /ota/check phải fail-closed với nó chứ không đoán hộ."""
    s = str(s or "")
    if not PRODUCT_RE.fullmatch(s) or s in PRODUCT_RESERVED:
        return None
    return s


# Thẻ nhận dạng NHÚNG trong ảnh firmware, do build sinh ra (firmware ≥ v2.4.6):
#   FBTIMG1;product=rapidplus;ver=v2.4.6;hw=V1.1,V1.2,V1.3;;
# Kết thúc bằng `;;`. Server quét bytes lúc upload để tự sinh manifest — người tải lên không
# gõ version nữa, nên không còn "hai ảnh một tên".
IMAGE_TAG_MAGIC = b"FBTIMG1;"
_IMAGE_TAG_MAX = 160


def parse_image_tags(raw: bytes) -> list[dict]:
    """Mọi thẻ nhận dạng đọc được trong [raw] (đã bỏ trùng, giữ thứ tự gặp).

    Mỗi thẻ: `{"product", "ver", "hw": [..], "raw"}` — `product` ĐÃ qua [product_key] (None
    nếu sai cú pháp: caller từ chối ảnh, đừng đoán); `hw` viết HOA để so khớp với
    `PCB_version` firmware ("V1.3"). Thẻ cụt (không thấy `;;` trong 160 byte) bị bỏ qua.
    Trả nhiều hơn một thẻ KHÁC NHAU = ảnh dị dạng, caller từ chối.
    """
    out: list[dict] = []
    seen: set[str] = set()
    start = 0
    while True:
        i = raw.find(IMAGE_TAG_MAGIC, start)
        if i < 0:
            break
        start = i + len(IMAGE_TAG_MAGIC)
        chunk = raw[start:start + _IMAGE_TAG_MAX]
        end = chunk.find(b";;")
        if end < 0:
            continue
        body = chunk[:end].decode("ascii", "replace")
        if body in seen:
            continue
        seen.add(body)
        fields: dict[str, str] = {}
        for part in body.split(";"):
            if "=" in part:
                k, v = part.split("=", 1)
                fields[k.strip()] = v.strip()
        hw = [h.strip().upper() for h in fields.get("hw", "").split(",") if h.strip()]
        out.append({
            "product": product_key(fields.get("product", "")),
            "ver": fields.get("ver", "").strip(),
            "hw": hw,
            "raw": IMAGE_TAG_MAGIC.decode() + body + ";;",
        })
    return out


_BIN_VER_RE = re.compile(r"^[a-z0-9-]+_[vV](?P<ver>[0-9][A-Za-z0-9._-]*?)\.bin$")


def ver_from_name(name: str) -> str:
    """Version suy từ TÊN file theo quy ước `<prefix>_v<ver>.bin` → `v<ver>`; rỗng nếu tên
    không mang version (`firmware.bin`). Luôn trả chữ `v` thường ở đầu để khớp chuỗi
    `FirmwareVer` firmware khai ("v2.4.5AT")."""
    m = _BIN_VER_RE.match(str(name or ""))
    return "v" + m.group("ver") if m else ""


def expected_bin_name(product: str, ver: str, legacy_product: str) -> str:
    """Tên file server ĐẶT cho một ảnh có thẻ.

    Sản phẩm kế thừa (fleet cũ) GIỮ `fbt_v<ver>.bin` — firmware ≤ v2.4.5 so đúng chuỗi
    `"fbt_" + FirmwareVer + ".bin"`, đổi quy ước là cả fleet "You have the lasted version"
    mãi mãi. Sản phẩm khác: `<product>_v<ver>.bin`. Chữ `v`/`V` đầu của ver bị bỏ một lần
    rồi thêm lại `v` thường: thẻ ghi `ver=V2.3.1` hay `2.3.1` đều ra một tên.
    """
    body = str(ver or "").strip()
    if body[:1] in ("v", "V"):
        body = body[1:]
    prefix = "fbt" if product == legacy_product else product
    return f"{prefix}_v{body}.bin"
