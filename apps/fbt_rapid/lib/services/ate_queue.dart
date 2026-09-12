/// Facade **hàng đợi hồ sơ ATE** theo nền tảng: desktop ghi FILE
/// (`ate_queue_io.dart`), web ghi **localStorage** (`ate_queue_web.dart`).
///
/// API dùng chung (màn Chạy trạm chỉ cần ba hàm này):
/// - `AteQueue.save(record)` — ghi hồ sơ TRƯỚC khi thử gửi.
/// - `AteQueue.pendingCount()` — còn bao nhiêu hồ sơ chưa lên server.
/// - `AteQueue.flush(api)` — gửi lại; trả `(sent, left, rejected, error)`.
///
/// Khác nhau CỐ Ý: bản web **lược log thô** khi xếp hàng (localStorage ~5MB,
/// mà log esptool + UART của một máy có thể vài trăm KB) và mất khi người dùng
/// xoá dữ liệu duyệt — hai điều phải nói với xưởng nếu trạm chạy trên web.
library;

export 'ate_queue_io.dart' if (dart.library.html) 'ate_queue_web.dart';
