/// Facade **trạm ATE theo nền tảng** — desktop dùng esptool + `flutter_libserialport`
/// (`ate_station_io.dart`), web dùng **esptool-js + Web Serial**
/// (`ate_station_web.dart`).
///
/// Cùng khuôn với `util/platform_files.dart` và `util/serial_link.dart`: màn
/// **Chạy trạm** import file NÀY, không import bản `_io`/`_web` — nhờ vậy chỉ có
/// MỘT màn hình cho cả hai nền tảng. Repo đã trả giá cho lối làm ngược lại ở tab
/// Kỹ Thuật: hai bản song song là hai chỗ phải sửa mỗi lần, và chúng đã lệch nhau.
///
/// API chung (hai bản cùng khai):
/// - `AteStationImpl({port, baud, esptoolPath?, fetchBin?})` — hiện thực `AteStation`.
/// - `ateCanListPorts` · `ateListPorts()` · `ateRequestPort()`.
///
/// Khác nhau CỐ Ý giữa hai bản (màn hình phải nói cho người dùng biết):
/// - Web chỉ chạy trên **Chrome/Edge desktop** (Web Serial).
/// - Web lấy firmware từ **kho OTA của server** (`fetchBin`) chứ không phải file
///   trên máy — mở lại tab là mất đường dẫn, mà bản đang chốt thì server luôn giữ.
/// - Web gọi `http://<ip>` tới máy bị trình duyệt chặn (mixed content) khi app
///   chạy HTTPS → bước OPT-01 tự lùi về đường UART.
library;

export 'ate_station_io.dart'
    if (dart.library.html) 'ate_station_web.dart';
