/// Máy này có dùng được **công cụ cổng nối tiếp** (Log nhiệt · Đọc serial · Nạp
/// code) không.
///
/// Cùng khuôn conditional-import với `platform_files.dart`: bản `_io` cho
/// desktop, bản `_web` cho trình duyệt.
///
/// Vì sao cần: tab **Kỹ Thuật** vô dụng ở nơi không có cổng nối tiếp — trình
/// duyệt **điện thoại** không có Web Serial API. Bày một tab mà bấm vào chỉ ra
/// màn báo lỗi thì tệ hơn là không bày.
///
/// ⚠️ Gác theo **khả năng của nền tảng**, KHÔNG theo bề rộng màn hình: cửa sổ
/// desktop kéo hẹp vẫn phải giữ tab Kỹ Thuật.
library;

export 'serial_support_io.dart'
    if (dart.library.html) 'serial_support_web.dart';
