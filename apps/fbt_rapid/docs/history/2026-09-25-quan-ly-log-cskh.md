# 2026-09-25 — Quản lý log lỗi CSKH: hộp thư, phản hồi, badge, thống kê

Server đã có hộp thư `/logs` + trạng thái từ 2026-09-17 (deploy 09-23) nhưng app **chưa từng có màn**
dùng nó → kỹ thuật chỉ tra được log khi biết mã máy. Làm đủ 4 việc người dùng chọn:

1. **Log đã nhận** (`lib/screens/support_inbox_screen.dart`) — mục thứ 3 tab Chăm sóc KH: lọc trạng
   thái/mã máy, phân trang "Tải thêm", hộp chi tiết Nhận xử lý → Đã xử lý kèm ghi chú, Mở lại, Xoá, Xem log thô.
2. **CSKH thấy phản hồi**: hộp "Log đã gửi" (`support_troubleshoot_screen.dart`) hiện `LogStatusChip` +
   "Kỹ thuật trả lời: …" (route theo máy vốn đã trả `status*`, chỉ app bỏ qua).
3. **Báo có log mới**: badge trên mục (`AppTab.badge` mới trong `app_tab_scaffold.dart`, `Badge` M3 bọc
   icon segment); `SupportScreen` poll `GET /logs?status=new&limit=1` 90 s khi tab đang xem. Phía server:
   báo Telegram (env, xem `server/docs/history/2026-09-25.md`).
4. **Thống kê lỗi** (`lib/screens/support_log_stats_screen.dart`) — `GET /logs/stats` mới.

Khác: `DeviceLogEntry.fromJson` + trường `fw/errors/warnings/keys/status*`; `FbtApi.listAllLogs/
setLogStatus/deleteLog/logStats`; upload gửi thêm `fw` (= `_report.version`). Khoá i18n `lg.*`, `ls.*` (vi/en).
Test `test/device_log_entry_test.dart` (package là `RapidPlusApp`, không phải `fbt_rapid`).

**CHƯA DEPLOY** (server + web).
