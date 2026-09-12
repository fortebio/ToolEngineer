"""Cấu hình đọc từ biến môi trường (systemd EnvironmentFile /etc/fbt-receiver.env)."""
import os
import re
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

# Thu muc chua firmware .bin cho OTA. Từ 2026-09-11 kho tách THEO SẢN PHẨM:
# `OTA_DIR/products/<product>/` (mỗi sản phẩm một `target.json` riêng) — xem app/ota.py.
OTA_DIR = Path(os.environ.get("FBT_OTA_DIR", str(Path.home() / "fbt_server" / "ota")))

# Sản phẩm dành cho máy KHÔNG tự khai `?product=` ở `/ota/check` — tức TOÀN BỘ fleet
# firmware ≤ v2.4.5 đang chạy hôm nay (RapidPlus). Đây là đường bắt buộc, không phải
# tiện ích: đường sửa từ xa duy nhất tới 109 máy sau NAT đi qua chính request đó, nên
# server không được đòi firmware mới rồi mới trả lời. File .bin + target.json cũ ở gốc
# OTA_DIR được tự dời vào products/<LEGACY_PRODUCT>/ lúc khởi động (ota.migrate_legacy).
# Khoá sản phẩm: [a-z0-9-]{1,24}; sai cú pháp thì rơi về "rapidplus" thay vì làm gãy
# /ota/check của cả fleet vì một dòng env gõ nhầm.
_LEGACY = os.environ.get("OTA_LEGACY_PRODUCT", "rapidplus").strip().lower()
LEGACY_PRODUCT = _LEGACY if re.fullmatch(r"[a-z0-9-]{1,24}", _LEGACY) else "rapidplus"

# Tuỳ chọn: máy cũ KHÔNG khai product nhưng mã máy có tiền tố nhận ra được → sản phẩm
# theo tiền tố, vd "RPL=rapidplus,RDR=reader". Tiền tố dài hơn thắng. Không khớp gì →
# LEGACY_PRODUCT. Để trống = tắt (mọi máy cũ đều là LEGACY_PRODUCT).
LEGACY_PRODUCT_BY_PREFIX: dict[str, str] = {}
for _item in os.environ.get("OTA_LEGACY_PRODUCT_BY_PREFIX", "").split(","):
    if "=" in _item:
        _pre, _prod = (s.strip() for s in _item.split("=", 1))
        if _pre and re.fullmatch(r"[a-z0-9-]{1,24}", _prod.lower()):
            LEGACY_PRODUCT_BY_PREFIX[_pre] = _prod.lower()

# Bắt buộc ảnh .bin tải lên phải có THẺ NHẬN DẠNG nhúng (`FBTIMG1;product=…;ver=…;hw=…;;`,
# firmware ≥ v2.4.6 mới có). Mặc định TẮT (giai đoạn 0: mọi ảnh đang có đều chưa có thẻ);
# bật ("1"/"true") khi fleet đã lên bản có thẻ — ảnh không thẻ chỉ còn lên được với `?force=1`.
OTA_REQUIRE_TAG = os.environ.get("OTA_REQUIRE_TAG", "").strip().lower() in ("1", "true", "yes")

# Thư mục chứa LOG MÁY nhân viên CSKH gửi lên từ app (tab "Chăm sóc KH" › Xử lý sự cố):
# mỗi lần gửi = 1 file JSON `<device>_<UTC>_<hash>.json` {device, received_at, by, note, text…}.
# File, KHÔNG bảng DB — cùng nguyên tắc "file là nguồn chân lý" với data_plus/ và ota/.
LOGS_DIR = Path(os.environ.get("FBT_LOGS_DIR", str(Path.home() / "fbt_server" / "logs")))

# Log UART một lần gửi tối đa 4M ký tự (app tự cắt ~200K; trần này chỉ chặn kẻ phá).
MAX_LOG_TEXT = 4 * 1024 * 1024

# Các mảng theo-slot của dữ liệu RPL: nếu có mặt thì phải đủ 10 phần tử
ARRAY_FIELDS = ("CT_value", "result", "record_out", "amplification")

# Thư mục hồ sơ TRẠM ATE (app: tab "Sản xuất"): mỗi máy qua trạm = 1 file JSON
# `<sn>_<started_at UTC>_<hash>.json` {sn, station, operator, verdict, steps[]…}.
# File, KHÔNG bảng DB — cùng nguyên tắc "file là nguồn chân lý" với data_plus/,
# ota/ và logs/: thêm bảng là thêm migration + GRANT trên box production, mà hồ
# sơ ATE ghi mỗi máy một lần (sản lượng nghìn/năm) nên đọc bằng glob vẫn thoải mái.
# Kế hoạch gốc (docs/plan/ate-san-xuat.md §7.2) vẽ bảng `ate_records`; chuyển sang
# DB khi số hồ sơ đủ lớn để glob thành nút cổ chai — hợp đồng REST không đổi.
ATE_DIR = Path(os.environ.get("FBT_ATE_DIR", str(Path.home() / "fbt_server" / "ate")))
