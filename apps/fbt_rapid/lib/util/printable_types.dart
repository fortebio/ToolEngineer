/// Kết quả của một lệnh in (xem `printable.dart`). Dùng chung cho bản desktop
/// và web — nơi gọi chỉ đọc kết quả để báo đúng việc người dùng phải làm tiếp.
class PrintOutcome {
  /// Trang in đã mở sẵn (tab mới trên web, trình duyệt mặc định trên desktop)
  /// và tự bật hộp thoại in → người dùng không phải làm gì thêm.
  final bool opened;

  /// Đường dẫn/tên file đã ghi ('' nếu không ghi ra file — web mở thẳng tab).
  final String path;

  /// Web: pop-up bị chặn nên đã TẢI file xuống thay vì mở tab. Người dùng phải
  /// tự mở file rồi Ctrl+P.
  final bool downloaded;

  const PrintOutcome({this.opened = false, this.path = '', this.downloaded = false});
}
