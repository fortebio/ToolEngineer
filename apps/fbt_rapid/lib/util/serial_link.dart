/// Kết nối serial **một cổng, tối giản** cho màn **Xử lý sự cố** (tab Chăm sóc
/// KH) — dùng chung MỘT màn hình cho desktop lẫn web.
///
/// Cùng khuôn conditional-export với `platform_files.dart` / `serial_support.dart`:
/// desktop đi `flutter_libserialport` (`serial_link_io.dart`), web đi Web Serial
/// API (`serial_link_web.dart`). Màn hình import file NÀY, không import bản
/// `_io`/`_web` — build web mới không kéo dart:ffi vào.
///
/// Vì sao không tái dùng `SerialConsoleScreen`: màn đó là công cụ kỹ thuật (đa
/// cổng, HEX, ending, gửi lệnh tự do) và có HAI bản desktop/web viết tách. Nhân
/// viên CSKH cần đúng ba việc: kết nối → thấy log → gửi về. Gói ba việc đó sau
/// một giao diện nhỏ thì màn CSKH viết MỘT lần chạy cả hai nền tảng.
///
/// API (giống nhau ở cả hai bản):
/// - `serialLinkAvailable`  : nền tảng có cổng serial không (web điện thoại: không).
/// - `serialLinkCanListPorts`: desktop liệt kê được cổng; web thì trình duyệt
///   bắt người dùng tự chọn trong hộp thoại → `false`, bỏ ô chọn cổng.
/// - `listSerialLinkPorts()` : danh sách cổng USB-serial (rỗng trên web).
/// - `openSerialLink(name:, baud:)`: mở cổng; web bỏ qua `name` và hiện hộp
///   thoại, trả `null` nếu người dùng Hủy. Lỗi → `SerialLinkException`.
library;

export 'serial_link_types.dart';
export 'serial_link_io.dart' if (dart.library.html) 'serial_link_web.dart';
