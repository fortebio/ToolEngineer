"""Cấu hình đọc từ biến môi trường (systemd EnvironmentFile /etc/fbt-receiver.env)."""
import os
from pathlib import Path

# Thư mục lưu file JSON gốc (nguồn chân lý)
DATA_DIR = Path(os.environ.get("FBT_DATA_DIR", str(Path.home() / "fbt_server" / "data_plus")))

# 16MB: chặn payload khổng lồ nuốt hết RAM
MAX_BODY = 16 * 1024 * 1024

# Bearer token CHÍNH — cái `/auth` phát cho app và cái được biên dịch vào firmware mới.
# Rỗng = fail-closed, từ chối hết (check_auth). Server chạy public nên không được mở toang
# khi env lỗi cấu hình.
TOKEN = os.environ.get("RECEIVER_TOKEN", "")

# Token CŨ còn được chấp nhận trong lúc xoay (phân tách bằng dấu phẩy). Mặc định RỖNG.
#
# Vì sao phải có: firmware v2.4.4 dùng CHÍNH token này cho `/ota/check` và tải `.bin`, tức là
# cho ĐƯỜNG SỬA TỪ XA DUY NHẤT tới 109 máy sau NAT. Nếu chỉ có một token thì đổi nó là 401
# đồng thời cả `/ingest` lẫn `/ota/*` — phá đúng đường sửa vào đúng lúc cần nó nhất, và lấy
# lại được chỉ bằng cách đi tới từng máy. Trước v2.4.4 thì OTA đi qua URL GitHub không xác
# thực nên xoay token vẫn cứu được từ xa; giờ thì không.
#
# Thứ tự an toàn (mỗi bước xác minh trước khi đi tiếp):
#   1. RECEIVER_TOKEN=<mới>, RECEIVER_TOKENS_OLD=<cũ>  -> restart. Cả fleet vẫn chạy bằng <cũ>.
#   2. Build + phát hành firmware nhúng <mới>, OTA cả fleet.
#   3. `GET /devices` cho tới khi MỌI máy đã báo về (last_seen mới) = đã lên <mới>.
#   4. Xoá RECEIVER_TOKENS_OLD -> restart. <cũ> chết hẳn.
# Máy nào offline suốt bước 3 sẽ kẹt ở <cũ> — đừng làm bước 4 khi còn máy chưa báo về.
#
# CỐ Ý tách làm hai biến chứ không phải một danh sách: một danh sách thì "cái nào là chính"
# phụ thuộc thứ tự, mà `/auth` phát đúng một token cho app — đảo thứ tự là âm thầm phát token
# sắp bị khai tử. Ở đây RECEIVER_TOKEN luôn là cái được phát, không cần đọc thứ tự để biết.
TOKENS_OLD = [t.strip() for t in os.environ.get("RECEIVER_TOKENS_OLD", "").split(",") if t.strip()]

# Mọi token được chấp nhận. Deploy code mà KHÔNG đặt RECEIVER_TOKENS_OLD -> đúng bằng [TOKEN],
# tức hành vi không đổi một chút nào; biến mới chỉ "bật" khi thật sự đang xoay.
TOKENS = [t for t in [TOKEN, *TOKENS_OLD] if t]

# Token RIÊNG cho 4 route GHI của OTA (upload .bin, chọn/huỷ target, xoá .bin).
# RỖNG = TẮT, các route ghi dùng chung TOKENS y như trước -> deploy code một mình KHÔNG đổi
# hành vi gì và app không gãy. Chỉ "bật" khi đặt biến môi trường.
#
# Vì sao cần: TOKEN nằm ở 4 KB đầu MỌI file .bin (`strings` là ra), và `POST /auth` trả đúng
# token đó cho MỌI tài khoản đăng nhập được, KHÔNG phân biệt vai trò (auth.py không kiểm role).
# Từ firmware v2.4.4 token đó còn mở luôn quyền GHI firmware -> ai có một tài khoản app, hoặc
# một file .bin, đều upload được ảnh tuỳ ý và arm nó cho cả 109 máy. Không có ký số, và `x-MD5`
# do chính server tính trên bytes của kẻ tấn công nên không đỡ được gì. Trước v2.4.4 cùng một
# rò rỉ chỉ cho phép giả kết quả xét nghiệm. (Giảm nhẹ duy nhất: cài đặt vẫn cần một cú bấm ĐỎ
# của con người trên từng máy.)
#
# Token này ĐƯỢC CHẤP NHẬN Ở MỌI ROUTE, không chỉ route ghi — nó là quyền lớn hơn, không phải
# quyền khác. Nhờ vậy app KHÔNG phải sửa một dòng code nào: admin chỉ dán token này vào ô
# "Engineer token" trong Cài đặt thay cho token thiết bị, rồi cả đọc lẫn ghi đều chạy.
OTA_ADMIN_TOKEN = os.environ.get("OTA_ADMIN_TOKEN", "")

# Chuỗi kết nối Postgres — peer auth qua unix socket, không nhúng mật khẩu trong code
DB = os.environ.get("FBT_DB", "dbname=mydb connect_timeout=5")

# Thư mục chứa app WEB (Flutter build web) — có thì serve tĩnh ở /app, không có thì bỏ qua
WEB_DIR = Path(os.environ.get("FBT_WEB_DIR", str(Path.home() / "fbt_server" / "web")))

# Thu muc chua firmware .bin cho OTA
OTA_DIR = Path(os.environ.get("FBT_OTA_DIR", str(Path.home() / "fbt_server" / "ota")))

# Các mảng theo-slot của dữ liệu RPL: nếu có mặt thì phải đủ 10 phần tử
ARRAY_FIELDS = ("CT_value", "result", "record_out", "amplification")
