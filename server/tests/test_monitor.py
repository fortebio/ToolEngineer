"""Kiểm thử số liệu giám sát (không cần DB/mạng/FastAPI).

Chạy: python tests/test_monitor.py   hoặc   pytest
"""
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app import monitor


def _today(offset=0):
    # Phải neo theo CÙNG múi giờ với `_fill_days` (UTC+7). Dùng UTC ở đây thì
    # test xanh cả ngày rồi ĐỎ trong khoảng 17:00–24:00 UTC — loại test hỏng
    # theo giờ chạy, tệ hơn là không có test.
    return str((datetime.now(timezone(timedelta(hours=7))) - timedelta(days=offset)).date())


def test_fill_days_bu_ngay_rong():
    """Ngày server không nhận gì phải hiện thành cột 0, không được biến mất."""
    days = monitor._fill_days({_today(): 5}, days=7)
    assert len(days) == 7, days
    assert days[-1] == {"d": _today(), "n": 5}, days[-1]
    assert [d["n"] for d in days[:-1]] == [0] * 6, days
    # Thứ tự phải là cũ -> mới, không thì biểu đồ vẽ ngược thời gian
    assert [d["d"] for d in days] == sorted(d["d"] for d in days)


def test_fill_days_khong_co_gi():
    days = monitor._fill_days({}, days=7)
    assert len(days) == 7 and all(d["n"] == 0 for d in days)


def test_fill_days_bo_qua_ngay_ngoai_khoang():
    """Dữ liệu cũ hơn cửa sổ không được lọt vào (và không làm dài chuỗi)."""
    days = monitor._fill_days({"2020-01-01": 99, _today(1): 3}, days=7)
    assert len(days) == 7
    assert sum(d["n"] for d in days) == 3


def test_snapshot_khong_co_db_van_tra_du_muc():
    """DB chết thì `db.ok=False` nhưng các mục khác VẪN có — một phần hỏng không
    được làm trắng cả trang giám sát."""
    snap = monitor.snapshot()

    assert snap["ok"] is True
    assert set(snap) >= {"service", "db", "cpu", "mem", "disk", "flow"}
    # Máy dev Windows: không có /proc, không có psycopg -> các mục đó là None,
    # nhưng service (thuần Python) thì LUÔN phải có.
    assert snap["service"]["uptime_sec"] >= 0
    assert snap["service"]["started_at"]
    assert isinstance(snap["db"]["ok"], bool)
    if not snap["db"]["ok"]:
        # Chỉ báo LOẠI lỗi, không được lộ DSN/đường dẫn ra ngoài
        assert snap["db"]["error"] and "://" not in str(snap["db"]["error"])


def test_snapshot_khong_flow_bo_han_phan_dem():
    """Vòng vẽ realtime gọi 5 giây/lần — không được kéo theo 3 truy vấn quét bảng."""
    snap = monitor.snapshot(with_flow=False)
    assert snap["flow"] is None
    assert snap["service"]["uptime_sec"] >= 0, "phần tài nguyên vẫn phải có"
    assert snap["db"]["ok"] is True, "không đụng DB thì coi như không có lỗi DB"


def test_days_until_full():
    """1000 hàng chiếm 1 MB, tuần rồi 700 hàng -> 100 hàng/ngày -> 100 KB/ngày.
    Còn 1 GB trống -> khoảng 10737 ngày."""
    d = monitor.days_until_full(1024 ** 3, 1024 ** 2, 1000, 700)
    assert d is not None and 10000 < d < 11000, d

    # Nhanh gấp 100 lần thì thời gian còn lại chia 100
    d2 = monitor.days_until_full(1024 ** 3, 1024 ** 2, 1000, 70000)
    assert abs(d2 * 100 - d) < 1, (d, d2)


def test_days_until_full_thieu_du_lieu_thi_im():
    """Thà không nói còn hơn đoán bừa một con số người ta sẽ tin."""
    assert monitor.days_until_full(0, 1, 1, 1) is None           # không đọc được đĩa
    assert monitor.days_until_full(1024, 0, 1, 1) is None         # chưa biết cỡ bảng
    assert monitor.days_until_full(1024, 1024, 0, 1) is None      # bảng rỗng
    assert monitor.days_until_full(1024, 1024, 10, 0) is None     # tuần rồi không chạy gì
    assert monitor.days_until_full(1024, 1024, 10, None) is None


def test_temp_khong_doc_duoc_thi_None_chu_khong_no():
    """Máy dev Windows không có /sys — phải trả None, không được ném."""
    t = monitor._temp()
    assert t is None or (0 < t["c"] < 200 and t["sensor"])


if __name__ == "__main__":
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in fns:
        fn()
        print(f"ok  {fn.__name__}")
    print(f"\n{len(fns)}/{len(fns)} passed")
