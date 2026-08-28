"""FBT Home Server — nhận dữ liệu thiết bị + API đọc cho app Flutter.

Chạy service:  uvicorn app.main:app --host 0.0.0.0 --port 8080
(re-export `app` để `uvicorn app:app` cũ vẫn chạy được.)
"""
__version__ = "1.0"
__all__ = ["app"]


def __getattr__(name):
    """Nạp TRỄ `app` — `uvicorn app:app` vẫn chạy, nhưng import `app.logic` /
    `app.monitor` thì KHÔNG còn kéo theo FastAPI.

    Trước đây dòng `from app.main import app` nằm thẳng ở đây, nên Python nạp
    `app/__init__.py` là nạp luôn cả FastAPI + pydantic **và** chạy
    `config.DATA_DIR.mkdir()` như tác dụng phụ của việc import. Hệ quả:
    `python tests/test_logic.py` — file tự nhận là "không cần DB/mạng" — chết
    ngay ở dòng import trên máy chưa cài đủ (đã gặp thật: pydantic-core lệch
    version), và chỉ chạy `scripts/manage_users.py list` cũng tự tạo thư mục dữ
    liệu trên máy dev.
    """
    if name == "app":
        from app.main import app
        return app
    raise AttributeError(f"module 'app' has no attribute {name!r}")
