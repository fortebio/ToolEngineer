"""Chặn dò mật khẩu ở /auth. Hàm thuần (truyền `now` vào) nên test không cần sleep.

Vì sao cần: /auth công khai qua Tailscale Funnel, không giới hạn gì → dò mật khẩu thoải
mái. Mỗi lần thử còn tốn ~100ms CPU cho scrypt mà uvicorn chạy **1 worker**, nên dò mật
khẩu cũng đồng thời là DoS.

Thiết kế (cố ý đơn giản — 1 worker, RAM, không thêm Redis):
- CHỈ đếm lần THẤT BẠI. Đăng nhập đúng không bao giờ chạm giới hạn.
- Đếm theo 2 khoá:
  * `(ip, username)` — chặn dò 1 tài khoản. Ngưỡng thấp.
  * `(ip)`           — chặn quét nhiều tài khoản từ 1 nguồn. Ngưỡng cao hơn.
  KHÔNG đếm theo mình username: kẻ xấu sẽ khoá được tài khoản người khác (DoS).
- Bị chặn thì trả lỗi NGAY, không đụng DB/scrypt → hết tốn CPU.

⚠️ Sau Tailscale Funnel, request tới app từ localhost. Nếu tailscaled KHÔNG gắn
`X-Forwarded-For` thì mọi IP công khai dồn vào một khoá `127.0.0.1` → ngưỡng theo IP
thành ngưỡng toàn cục. Vì vậy ngưỡng theo IP để RỘNG, và khoá chính là `(ip, username)`.
Kiểm thực tế: `journalctl -u fbt-receiver | grep ratelimit` xem IP ghi ra là gì.
"""
import ipaddress
from collections import deque

WINDOW_SEC = 300  # 5 phút
MAX_PER_USER = 10  # thất bại cho 1 (ip, username)
MAX_PER_IP = 60  # thất bại cho 1 ip, mọi username
MAX_KEYS = 5000  # trần số khoá giữ trong RAM (chống phình bộ nhớ)

_fails: dict[tuple, deque] = {}


def _hits(key, now: float) -> deque:
    """Số lần thất bại của `key` còn trong cửa sổ (tự dọn cái cũ)."""
    dq = _fails.get(key)
    if dq is None:
        dq = _fails[key] = deque()
    while dq and now - dq[0] > WINDOW_SEC:
        dq.popleft()
    return dq


def _prune(now: float):
    """Xoá khoá đã hết hạn; quá trần thì bỏ luôn khoá cũ nhất."""
    for k in [k for k, dq in _fails.items() if not dq or now - dq[-1] > WINDOW_SEC]:
        _fails.pop(k, None)
    while len(_fails) > MAX_KEYS:
        _fails.pop(next(iter(_fails)), None)


def client_ip(headers, fallback: str) -> str:
    """IP thật của client: X-Forwarded-For (proxy/Funnel) → phần tử ĐẦU, không thì fallback."""
    xff = (headers.get("x-forwarded-for") or "").split(",")[0].strip()
    return xff or (fallback or "?")


def normalize_ip(ip: str) -> str:
    """Khoá đếm theo NGUỒN, không theo địa chỉ.

    IPv6 phải gom theo **/64**: nhà mạng cấp mỗi thuê bao nguyên khối /64 (2^64 địa chỉ),
    đếm theo địa chỉ đầy đủ thì kẻ dò chỉ cần đổi hậu tố là bộ đếm về 0 — vô dụng.
    Xác minh 2026-08-17: khách 4G vào bằng IPv6 thật (`2401:d800:...`), không phải IPv4.
    IPv4 giữ nguyên (/32). Chuỗi không phải IP (vd 'testclient', '?') giữ nguyên.
    """
    try:
        addr = ipaddress.ip_address(ip)
    except ValueError:
        return ip
    if addr.version == 6:
        return f"{ipaddress.ip_network(f'{addr}/64', strict=False).network_address}/64"
    return str(addr)


def _keys(ip: str, username: str):
    """(khoá theo ip+user, khoá theo ip) — đã chuẩn hoá nguồn và hạ hoa/thường username."""
    src = normalize_ip(ip)
    return (src, (username or "").strip().lower()), (src,)


def blocked(ip: str, username: str, now: float) -> bool:
    """True nếu nguồn này đang bị chặn (đã thất bại quá nhiều trong cửa sổ)."""
    user_key, ip_key = _keys(ip, username)
    if len(_hits(user_key, now)) >= MAX_PER_USER:
        return True
    return len(_hits(ip_key, now)) >= MAX_PER_IP


def record_failure(ip: str, username: str, now: float):
    """Ghi 1 lần thất bại. Gọi SAU khi biết kết quả là sai."""
    user_key, ip_key = _keys(ip, username)
    _hits(user_key, now).append(now)
    _hits(ip_key, now).append(now)
    _prune(now)


def reset(ip: str, username: str):
    """Xoá lịch sử thất bại của 1 tài khoản sau khi đăng nhập ĐÚNG.

    Chỉ xoá khoá (nguồn, username) — giữ nguyên bộ đếm theo nguồn để 1 lần đăng nhập
    đúng không rửa sạch dấu vết của cả đợt quét nhiều tài khoản.
    """
    user_key, _ = _keys(ip, username)
    _fails.pop(user_key, None)


def clear_all():
    """Chỉ dùng trong test."""
    _fails.clear()
