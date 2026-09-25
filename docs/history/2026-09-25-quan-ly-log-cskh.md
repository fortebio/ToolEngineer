# 2026-09-25 — Quản lý log lỗi CSKH (server + app)

Chạm 2 phần. Chi tiết từng phần:
- Server: `server/docs/history/2026-09-25.md` — `fw` + mức dấu hiệu trong metadata, `GET /logs/stats`,
  báo Telegram khi có log mới (env `FBT_LOG_NOTIFY_*`, tắt mặc định).
- App: `apps/fbt_rapid/docs/history/2026-09-25-quan-ly-log-cskh.md` — tab Chăm sóc KH thêm **Log đã nhận**
  (hộp thư kỹ thuật + badge) và **Thống kê lỗi**; CSKH thấy trạng thái + ghi chú trả lời trong "Log đã gửi".

Kiểm: server 136 test pass; `flutter analyze` không lỗi mới; `test/device_log_entry_test.dart` pass; API
kiểm trên server local (`localtest.ps1` + 5 log mẫu). **Chưa deploy, chưa commit.**
