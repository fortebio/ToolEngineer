"""Số liệu giám sát server cho app (tab **Giám sát**).

Chỉ dùng **thư viện chuẩn** + psycopg đã có — cố ý KHÔNG thêm `psutil`: mọi thứ
cần đều nằm trong `/proc` và `os.statvfs` trên Debian của box, thêm một
dependency cho ba con số là không đáng.

`/monitor` gác bằng **`ota_admin`**, tức chỉ token NHÂN SỰ (`OTA_ADMIN_TOKEN`,
`auth.api_token_for` phát khi root/admin đăng nhập). KHÔNG dùng `auth()`: token
thiết bị qua được `auth()`, mà token đó nằm trong **4 KB đầu MỌI file `.bin`** —
ai có một bản firmware là đọc được `disk.free`, rồi dùng chính token ấy POST
`/ingest` (tới 16 MB mỗi lần) cho tới khi đĩa đầy, mà đĩa đầy nghĩa là MẤT dữ
liệu đo.

Server chỉ phân biệt được **nhân sự / khách hàng** — root và admin dùng chung một
token nên "chỉ root" vẫn là gác ở giao diện app. Vì vậy vẫn giữ nguyên tắc: chỉ
trả số liệu vận hành, KHÔNG trả đường dẫn, biến môi trường, tên file hay bất kỳ
thứ gì giúp người có token đi xa hơn.

Mọi mục bọc try/except riêng: chạy trên Windows lúc dev (không có `/proc`,
không có `os.getloadavg`) hay Postgres chết thì mục đó trả `null`, các mục còn
lại vẫn ra số. Một phần hỏng không được làm chết cả trang giám sát.
"""
import os
import time
from pathlib import Path
from datetime import datetime, timedelta, timezone

from app import config, db

# Mốc khởi động TIẾN TRÌNH — module import một lần lúc uvicorn lên, nên hiệu số
# này chính là uptime của service (systemd restart = tiến trình mới = về 0).
_STARTED = time.time()


def _service() -> dict:
    return {
        "started_at": datetime.fromtimestamp(_STARTED, timezone.utc).isoformat(),
        "uptime_sec": int(time.time() - _STARTED),
    }


def _mem() -> dict | None:
    """RAM từ `/proc/meminfo` (kB -> byte). None nếu không phải Linux."""
    try:
        vals = {}
        with open("/proc/meminfo", encoding="ascii") as f:
            for line in f:
                key, _, rest = line.partition(":")
                parts = rest.split()
                if parts:
                    vals[key] = int(parts[0]) * 1024
        total = vals.get("MemTotal", 0)
        # MemAvailable là con số ĐÚNG để nói "còn dùng được bao nhiêu" — MemFree
        # bỏ qua cache có thể thu hồi nên luôn trông như sắp hết RAM.
        avail = vals.get("MemAvailable", 0)
        if not total:
            return None
        return {"total": total, "available": avail,
                "used_pct": round((total - avail) * 100 / total, 1)}
    except Exception:
        return None


def _disk() -> dict | None:
    """Đĩa chứa DATA_DIR — chỗ này đầy là mất dữ liệu đo, không phải chậm đi."""
    try:
        st = os.statvfs(config.DATA_DIR)
        total = st.f_frsize * st.f_blocks
        free = st.f_frsize * st.f_bavail
        if not total:
            return None
        return {"total": total, "free": free,
                "used_pct": round((total - free) * 100 / total, 1)}
    except Exception:
        return None


def _cpu() -> dict | None:
    try:
        one, five, fifteen = os.getloadavg()
        cores = os.cpu_count() or 1
        return {
            "cores": cores,
            "load1": round(one, 2),
            "load5": round(five, 2),
            "load15": round(fifteen, 2),
            # Load chia số nhân mới so sánh được giữa các máy: 4.0 trên box 4
            # nhân là đầy tải, trên box 1 nhân là quá tải gấp 4.
            "load1_pct": round(one * 100 / cores, 1),
        }
    except Exception:
        return None


def _temp() -> dict | None:
    """Cảm biến NÓNG NHẤT đọc được, từ `/sys/class/thermal` (milli°C -> °C).

    Đọc thẳng sysfs: KHÔNG cần root, không cần cài `lm-sensors`. Lấy MAX vì câu
    hỏi của người vận hành là "có chỗ nào đang nóng không", chứ không phải "CPU
    bao nhiêu độ" — máy để 24/7 trong phòng không điều hoà thì chip wifi hay
    chipset nóng cũng đáng biết như CPU.

    Bỏ giá trị ngoài khoảng 0–200 °C: vùng kiểu `INT3400` là cảm biến ảo của
    ACPI, luôn trả một hằng số vô nghĩa (đo được trên box: 20 °C cố định).

    KHÔNG có nhiệt độ Ổ ĐĨA: ổ trong box là SATA nên phải qua SMART, mà
    `smartctl` chưa cài và đọc SMART cần root — service chạy bằng user
    `engineer`. Muốn có thì phải `apt install smartmontools` + một dòng sudoers.
    """
    best = None
    try:
        for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
            try:
                milli = int((zone / "temp").read_text().strip())
                name = (zone / "type").read_text().strip()
            except (OSError, ValueError):
                continue
            c = milli / 1000
            if not (0 < c < 200):
                continue
            if best is None or c > best["c"]:
                best = {"c": round(c, 1), "sensor": name}
    except Exception:
        return None
    return best


def days_until_full(free_bytes, db_bytes, total_rows, rows_7d) -> float | None:
    """Còn bao nhiêu NGÀY nữa đầy đĩa, theo tốc độ ghi của 7 ngày gần nhất.

    `bytes/hàng × hàng/ngày` — không cần bảng lịch sử dung lượng nào.

    ponytail: chỉ tính phần bảng `sessions` phình ra. File JSON trong `data_plus`
    cũng lớn dần với tốc độ cùng bậc, nên con số này là **cận trên lạc quan** —
    đủ để phân biệt "còn hàng tháng" với "còn vài ngày", ĐỪNG dùng để hẹn giờ.
    Cần chính xác thì phải ghi dung lượng theo ngày rồi hồi quy trên đó.

    Trả None khi chưa đủ dữ liệu để nói gì (tuần vừa rồi không có phiên nào,
    bảng rỗng, không đọc được đĩa) — thà không nói còn hơn đoán bừa một con số
    người ta sẽ tin.
    """
    if not free_bytes or not db_bytes or not total_rows or not rows_7d:
        return None
    per_row = db_bytes / total_rows
    per_day = per_row * rows_7d / 7
    if per_day <= 0:
        return None
    return round(free_bytes / per_day, 1)


def _fill_days(by_day: dict, days: int = 7) -> list[dict]:
    """Chuỗi `days` ngày LIÊN TỤC tới hôm nay, ngày không có phiên nào = 0.

    Trả về list có thứ tự thay vì map thưa: thiếu ngày thì biểu đồ vẽ các cột sát
    nhau và đọc thành "ngày nào cũng có dữ liệu", giấu mất đúng cái mà người xem
    giám sát đang tìm — ngày server không nhận được gì.
    """
    # Neo theo GIỜ VIỆT NAM cho khớp `db.monitor_flow` (nhóm ngày bằng
    # `AT TIME ZONE 'Asia/Ho_Chi_Minh'`). Lệch múi giữa hai bên là cột cuối luôn
    # rỗng. Dùng offset cứng +7 thay vì tên vùng: Việt Nam không có giờ mùa hè
    # nên hai cách cho cùng kết quả, mà offset thì không cần tzdata của Python.
    today = datetime.now(timezone(timedelta(hours=7))).date()
    out = []
    for i in range(days - 1, -1, -1):
        d = str(today - timedelta(days=i))
        out.append({"d": d, "n": int(by_day.get(d, 0))})
    return out


def snapshot(with_flow: bool = True) -> dict:
    """Ảnh chụp trạng thái server ngay lúc gọi.

    `with_flow=False` bỏ phần đếm phiên đo — app gọi kiểu này cho vòng vẽ biểu đồ
    realtime (vài giây một lần). Tài nguyên đọc từ `/proc` gần như miễn phí, còn
    `monitor_flow` là 3 truy vấn gom nhóm quét toàn bảng `sessions` — bảng đó chỉ
    dài thêm mãi, mà số phiên đo trong 7 ngày thì không đổi sau mỗi 5 giây.
    """
    flow, db_ok, db_error = None, False, None
    try:
        if with_flow:
            flow = db.monitor_flow()
            flow["by_day"] = _fill_days(flow.get("by_day") or {})
        db_ok = True
    except Exception as e:
        # Nói LOẠI lỗi thôi, không kèm chuỗi kết nối/DSN — xem ghi chú đầu file.
        db_error = type(e).__name__

    disk = _disk()
    # Ước tính "đầy sau bao lâu" cần CẢ hai phía: chỗ trống (đĩa) và tốc độ
    # phình (DB). Thiếu một bên thì bỏ hẳn field, đừng gửi 0 — 0 ngày đọc thành
    # "đầy tới nơi rồi".
    if disk and flow:
        d = days_until_full(disk.get("free"), flow.get("db_bytes"),
                            flow.get("total"), flow.get("last7d"))
        if d is not None:
            disk["days_left"] = d

    return {
        "ok": True,
        "service": _service(),
        "db": {"ok": db_ok, "error": db_error},
        "cpu": _cpu(),
        "mem": _mem(),
        "disk": disk,
        "temp": _temp(),
        "flow": flow,
    }
