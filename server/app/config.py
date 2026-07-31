"""Cấu hình đọc từ biến môi trường (systemd EnvironmentFile /etc/fbt-receiver.env)."""
import os
from pathlib import Path

# Thư mục lưu file JSON gốc (nguồn chân lý)
DATA_DIR = Path(os.environ.get("FBT_DATA_DIR", str(Path.home() / "fbt_server" / "data_plus")))

# 16MB: chặn payload khổng lồ nuốt hết RAM
MAX_BODY = 16 * 1024 * 1024

# Bearer token; rỗng = chỉ cho phép trong LAN/tailnet (không public)
TOKEN = os.environ.get("RECEIVER_TOKEN", "")

# Chuỗi kết nối Postgres — peer auth qua unix socket, không nhúng mật khẩu trong code
DB = os.environ.get("FBT_DB", "dbname=mydb connect_timeout=5")

# Thư mục chứa app WEB (Flutter build web) — có thì serve tĩnh ở /app, không có thì bỏ qua
WEB_DIR = Path(os.environ.get("FBT_WEB_DIR", str(Path.home() / "fbt_server" / "web")))

# Thu muc chua firmware .bin cho OTA
OTA_DIR = Path(os.environ.get("FBT_OTA_DIR", str(Path.home() / "fbt_server" / "ota")))

# Các mảng theo-slot của dữ liệu RPL: nếu có mặt thì phải đủ 10 phần tử
ARRAY_FIELDS = ("CT_value", "result", "record_out", "amplification")
