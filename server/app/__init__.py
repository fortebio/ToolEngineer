"""FBT Home Server — nhận dữ liệu thiết bị + API đọc cho app Flutter.

Chạy service:  uvicorn app.main:app --host 0.0.0.0 --port 8080
(re-export `app` để `uvicorn app:app` cũ vẫn chạy được.)
"""
from app.main import app

__version__ = "1.0"
__all__ = ["app"]
