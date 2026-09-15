/// NỘI DUNG tab **Chăm sóc KH › Thông tin máy** — tài liệu cho **nhân viên chăm
/// sóc khách hàng** hiểu sản phẩm Forte Rapid+ và cách vận hành, viết bằng
/// ngôn ngữ đời thường. Thứ tự mục = thứ tự một nhân viên mới nên đọc:
/// sản phẩm → quy trình một lần xét nghiệm → đọc kết quả → nút/màn hình →
/// WiFi & app → cập nhật → câu hỏi khách hay gọi → kịch bản tiếp nhận → từ
/// điển. Phần kỹ thuật (cổng USB, lệnh serial) gom vào MỘT mục cuối, đánh dấu
/// `advanced` → màn hình thu gọn, chỉ mở khi kỹ thuật yêu cầu.
///
/// Đây là **nội dung** (không phải chuỗi giao diện) nên viết thẳng tiếng Việt ở
/// MỘT chỗ này, không đi qua `tr()`: người sửa nội dung là người biết máy,
/// không phải người biết i18n. Chrome màn hình (tiêu đề tab, ô tìm…) vẫn `tr()`.
///
/// NGUỒN: code app (`test_result.dart`, `temp_types.dart`, `fbt_api.dart`,
/// `manager_machine_screen.dart`), `docs/`, bảng khảo sát firmware v2.4.4 trong
/// `docs/plan/ate-san-xuat.md`, và JSON `ParaRead` đọc từ máy thật RPL01015
/// (2026-09-04: ly giải 600 s @ 82 °C, khuếch đại 90 vòng × 20 s @ 65.8 °C).
/// KHÔNG bịa thông số; chỗ chưa có số chính thức thì chỉ về màn hình máy /
/// hướng dẫn kit. Thêm/sửa mục = sửa danh sách [kMachineInfoSections].
library;

import 'package:flutter/material.dart';

/// Một dòng tra cứu: nhãn đậm + nội dung (có thể nhiều dòng, xuống dòng bằng \n).
class InfoItem {
  final String label;
  final String text;
  const InfoItem(this.label, this.text);
}

/// Phần "đặc biệt" gắn thêm vào cuối một mục (widget tương tác nhỏ).
enum InfoExtra { none, errorCodeDecoder }

/// Một mục lớn (một thẻ trên màn hình).
class InfoSection {
  final String id;
  final IconData icon;
  final String title;

  /// Nhãn ngắn cho chip "nhảy tới" ở đầu màn (rỗng = không có chip).
  final String jump;
  final String intro; // 1–2 câu tóm tắt, hiện ngay dưới tiêu đề
  final List<InfoItem> items;
  final InfoExtra extra;

  /// Mục KỸ THUẬT — màn hình thu gọn mặc định, nhân viên CSKH không phải đọc.
  final bool advanced;

  const InfoSection({
    required this.id,
    required this.icon,
    required this.title,
    this.jump = '',
    required this.intro,
    required this.items,
    this.extra = InfoExtra.none,
    this.advanced = false,
  });
}

const List<InfoSection> kMachineInfoSections = [
  InfoSection(
    id: 'product',
    icon: Icons.science_outlined,
    title: 'Sản phẩm: Forte Rapid+ là gì',
    jump: 'Sản phẩm',
    intro:
        'Máy xét nghiệm sinh học phân tử nhanh đặt tại phòng khám / phòng xét nghiệm. '
        'Người dùng đặt ống mẫu vào, máy tự làm phần còn lại và báo kết quả.',
    items: [
      InfoItem('Dùng để làm gì',
          'Phát hiện tác nhân gây bệnh (vi khuẩn, virus…) trong mẫu bệnh phẩm bằng '
              'cách khuếch đại vật liệu di truyền, cho kết quả Dương tính / Âm tính '
              'cho từng ống mẫu ngay tại chỗ — không cần gửi mẫu về phòng thí nghiệm '
              'trung tâm.'),
      InfoItem('Ai dùng',
          'Nhân viên y tế / kỹ thuật viên xét nghiệm tại cơ sở của khách. Kết quả '
              'đọc trên màn hình máy và trên app FBT_RAPID này (tab Lịch sử).'),
      InfoItem('Chạy được bao nhiêu mẫu',
          '10 ống mẫu một lần. Máy và app gọi mỗi vị trí là một "slot" (kênh).'),
      InfoItem('Cần gì để hoạt động',
          'Nguồn điện ổn định (adapter đi kèm máy) và WiFi để gửi kết quả lên hệ '
              'thống. Không có WiFi máy vẫn chạy và hiện kết quả trên màn hình, nhưng '
              'app sẽ không thấy.'),
      InfoItem('Mã máy — thứ phải hỏi đầu tiên',
          'Mỗi máy có mã dạng "RPL" + 5 số (ví dụ RPL02013) in trên tem máy. Tra '
              'lịch sử, cấp quyền xem, gửi log — tất cả bắt đầu từ mã này.'),
      InfoItem('Bộ kit hoá chất',
          'Máy dùng kèm bộ kit của Forte Biotech. Cách chuẩn bị mẫu và ý nghĩa kết '
              'quả theo hướng dẫn sử dụng của từng kit — nhân viên CSKH không tự '
              'diễn giải kết quả y khoa.'),
      InfoItem('Website', 'https://fortebiotech.com'),
    ],
  ),
  InfoSection(
    id: 'workflow',
    icon: Icons.play_circle_outline,
    title: 'Một lần xét nghiệm diễn ra thế nào',
    jump: 'Quy trình',
    intro: 'Nắm 5 bước này để biết khách đang ở đâu khi gọi hỗ trợ.',
    items: [
      InfoItem('1. Bật máy, chờ sẵn sàng',
          'Máy tự làm nóng và tự kiểm tra; màn hình báo khi sẵn sàng. Nếu đã cấu '
              'hình WiFi, lúc này máy đã nối mạng.'),
      InfoItem('2. Đặt ống mẫu',
          'Khách chuẩn bị mẫu theo hướng dẫn kit rồi đặt vào các slot (tối đa 10). '
              'Slot không dùng để trống, không ảnh hưởng gì.'),
      InfoItem('3. Bấm chạy',
          'Bấm nút theo chỉ dẫn trên màn hình. Máy bắt đầu đếm giờ.'),
      InfoItem('4. Máy tự chạy hai giai đoạn',
          'Ly giải: làm nóng mẫu (khoảng 82 °C) để tách vật liệu di truyền — '
              'khoảng 10 phút. Khuếch đại: giữ ấm (khoảng 66 °C) và đo phát quang '
              'mỗi 20 giây — khoảng 30 phút. Đây là cấu hình mặc định đọc từ máy; '
              'kit khác có thể khác, đồng hồ trên màn hình máy là chuẩn.'),
      InfoItem('5. Kết quả',
          'Màn hình máy hiện kết quả từng slot; máy tự gửi kết quả và đồ thị lên '
              'hệ thống. Vài phút sau app FBT_RAPID (tab Lịch sử) xem được.'),
      InfoItem('Trong lúc chạy KHÔNG nên',
          'Rút điện, mở nắp, tắt WiFi hay bấm nút lung tung. Mất điện giữa chừng '
              'là mất lần chạy đó, phải làm lại với mẫu mới.'),
    ],
  ),
  InfoSection(
    id: 'results',
    icon: Icons.fact_check_outlined,
    title: 'Đọc kết quả cho khách',
    jump: 'Kết quả',
    intro:
        'Máy và app dùng chung 4 ký hiệu. Nhân viên CSKH giải thích ký hiệu, '
        'không kết luận y khoa — phần đó theo hướng dẫn kit và bác sĩ.',
    items: [
      InfoItem('Dương tính (P)', 'Phát hiện tác nhân trong mẫu. App tô nổi bật.'),
      InfoItem('Âm tính (N)', 'Không phát hiện.'),
      InfoItem('Dương tính nhẹ (S)',
          'Có tín hiệu nhưng yếu hoặc xuất hiện muộn. Kit hướng dẫn cách xử lý '
              '(thường là làm lại hoặc kết hợp lâm sàng).'),
      InfoItem('Lỗi (E)',
          'Slot đó không cho kết quả được (ống mẫu, cảm biến hoặc quy trình có '
              'vấn đề). Nhiều slot cùng E, hoặc cùng một slot E lặp lại → xem mục '
              '"Câu hỏi khách thường gọi".'),
      InfoItem('Số CT là gì',
          'Số vòng đo mà máy bắt đầu thấy tín hiệu. Chỉ có ở mẫu dương tính; số '
              'càng nhỏ tín hiệu càng mạnh. Khách hỏi ý nghĩa cụ thể → theo hướng '
              'dẫn kit.'),
      InfoItem('Đồ thị trong app',
          'Mỗi ống mẫu một đường; đường đi lên rõ = có khuếch đại. Kỹ thuật dùng '
              'để kiểm chất lượng; khách không bắt buộc phải đọc.'),
      InfoItem('Dấu hỏi (?) trong danh sách',
          'Hệ thống chưa có trường kết quả của lần chạy đó. Mở chi tiết vẫn đọc '
              'được — không phải lỗi máy.'),
    ],
  ),
  InfoSection(
    id: 'buttons',
    icon: Icons.touch_app_outlined,
    title: 'Nút bấm & màn hình máy',
    jump: 'Nút & màn hình',
    intro: 'Ba nút màu và một màn hình là toàn bộ giao diện trên máy.',
    items: [
      InfoItem('Nút ĐỎ · XANH LÁ · TRẮNG',
          'Máy hướng dẫn trên màn hình nên bấm nút nào ở mỗi bước. Khi hỗ trợ qua '
              'điện thoại, hỏi "màn hình đang ghi gì" trước khi hỏi "đã bấm nút nào".'),
      InfoItem('Nút ĐỎ khi có bản cập nhật',
          'Máy có phần mềm mới sẽ HỎI trên màn hình; khách bấm ĐỎ để đồng ý cài. '
              'Không bấm thì máy vẫn chạy bản cũ — lý do số 1 của "đã cập nhật mà '
              'máy chưa lên bản mới".'),
      InfoItem('Màn hình hiện mã lỗi 4 số',
          'Máy tự phát hiện lỗi cảm biến và hiện mã. Ghi lại đúng 4 số (chụp ảnh '
              'càng tốt) và gửi kèm ticket. Ô bên dưới tách mã thành 4 phần để kỹ '
              'thuật đọc nhanh — CSKH không cần hiểu từng phần.'),
    ],
    extra: InfoExtra.errorCodeDecoder,
  ),
  InfoSection(
    id: 'network',
    icon: Icons.wifi_outlined,
    title: 'WiFi, app và hệ thống',
    jump: 'WiFi & app',
    intro: 'Máy cần WiFi để kết quả lên app. Phần lớn ca "không thấy kết quả" nằm ở đây.',
    items: [
      InfoItem('Máy nối mạng thế nào',
          'Máy dùng WiFi của khách (nhập tên và mật khẩu WiFi ngay trên máy). Không '
              'nối được, máy tự phát một WiFi riêng để khách vào cấu hình lại.'),
      InfoItem('Kết quả đi đâu',
          'Máy gửi lên hệ thống của Forte (có bản dự phòng trên Google Sheet). App '
              'FBT_RAPID đọc từ đó — nên xem được ở bất kỳ đâu, không cần đứng cạnh máy.'),
      InfoItem('Kiểm tra máy có "sống" không',
          'Tab Quản lý máy › Trạng thái máy: chấm xanh = có gửi dữ liệu trong 24 giờ; '
              'xám = im lặng. Cột "Lần gửi cuối" là lần gần nhất máy liên lạc.'),
      InfoItem('Xem tình trạng máy ngay tại chỗ khách',
          'Máy có trang web nội bộ: điện thoại/máy tính cùng WiFi mở trình duyệt, '
              'gõ địa chỉ IP hiện trên màn hình máy — thấy mã máy, nhiệt độ, trạng '
              'thái, lỗi. Tiện khi hướng dẫn khách qua điện thoại.'),
      InfoItem('Khách đổi WiFi / đổi mật khẩu',
          'Máy sẽ không lên mạng nữa — cần cấu hình lại WiFi trên máy. Kết quả chạy '
              'trong lúc mất mạng chỉ có trên màn hình máy.'),
    ],
  ),
  InfoSection(
    id: 'update',
    icon: Icons.system_update_alt_outlined,
    title: 'Cập nhật phần mềm máy',
    jump: 'Cập nhật',
    intro: 'Máy tự tải bản mới qua WiFi; khách chỉ cần bấm ĐỎ xác nhận.',
    items: [
      InfoItem('Máy nào tự cập nhật được',
          'Máy chạy phần mềm (firmware) từ bản 2.4.0 trở lên. Máy cũ hơn phải có kỹ '
              'thuật đến nạp bằng cáp.'),
      InfoItem('Xem máy đang chạy bản nào',
          'Tab Quản lý máy › Trạng thái máy, cột Firmware.'),
      InfoItem('Khách hỏi "khi nào máy tôi được cập nhật"',
          'Kỹ thuật chọn bản trên hệ thống; máy nhận ở lần liên lạc kế tiếp (cần '
              'WiFi) và hỏi trên màn hình; khách bấm ĐỎ, chờ vài phút, không rút điện.'),
      InfoItem('Đã cập nhật mà chưa thấy đổi',
          'Kiểm theo thứ tự: (1) khách đã bấm ĐỎ chưa; (2) máy có mạng không; (3) bản '
              'cũ có từ 2.4.0 chưa; (4) cột Firmware chỉ đổi sau khi máy gửi dữ liệu '
              'lần kế tiếp.'),
    ],
  ),
  InfoSection(
    id: 'faq',
    icon: Icons.support_agent_outlined,
    title: 'Câu hỏi khách thường gọi',
    jump: 'Sự cố thường gặp',
    intro:
        'Mỗi mục có ba phần: hỏi khách gì, khách tự thử gì, khi nào chuyển kỹ thuật. '
        'Mọi ca đều cần mã máy + mô tả.',
    items: [
      InfoItem('Máy tự tắt / tự khởi động lại giữa chừng',
          'Hỏi: có dùng adapter đi kèm không? cắm chung ổ với tủ lạnh, máy lạnh? có '
              'lặp lại không?\n'
              'Khách tự thử: đổi ổ cắm, dùng đúng adapter, bỏ dây nối dài.\n'
              'Chuyển kỹ thuật: vẫn lặp → lấy nhật ký máy (mục Xử lý sự cố); nhật ký '
              'thường có chữ "Brownout" = nguồn yếu.'),
      InfoItem('Máy đứng hình, bấm nút không phản hồi',
          'Khách tự thử: rút điện 10 giây, cắm lại, chờ màn hình lên.\n'
              'Chuyển kỹ thuật: lặp nhiều lần trong ngày → lấy nhật ký ngay sau khi '
              'bật máy (có lý do khởi động lại).'),
      InfoItem('Chạy xong không thấy kết quả trên app',
          'Hỏi: màn hình máy có hiện kết quả không? WiFi nhà có đổi mật khẩu hay đổi '
              'router không? máy có biểu tượng WiFi không?\n'
              'Kiểm: Quản lý máy › Trạng thái máy › Lần gửi cuối.\n'
              'Khách tự thử: cấu hình lại WiFi trên máy.\n'
              'Chuyển kỹ thuật: máy có mạng mà vẫn không lên → có thể lỗi hệ thống, '
              'báo ngay.'),
      InfoItem('Một hoặc nhiều slot ra E (lỗi) liên tục',
          'Hỏi: cùng slot mỗi lần hay ngẫu nhiên? ống mẫu đóng nắp kỹ chưa?\n'
              'Khách tự thử: lau sạch slot, chạy lại ở slot khác.\n'
              'Chuyển kỹ thuật: cùng một slot lỗi lặp → cảm biến quang; kèm mã lỗi '
              'trên màn hình nếu có.'),
      InfoItem('Màn hình hiện mã lỗi 4 số',
          'Ghi mã, chụp ảnh, hỏi lúc nào hiện (khởi động hay đang chạy). Chuyển kỹ '
              'thuật kèm mã — CSKH không tự xử.'),
      InfoItem('Nhiệt độ trên máy hiện -127 hoặc số lạ',
          'Cảm biến nhiệt mất kết nối. Không tự sửa được — chuyển kỹ thuật; khuyên '
              'khách KHÔNG chạy mẫu tiếp cho tới khi sửa.'),
      InfoItem('Máy không nhận bản cập nhật',
          'Xem mục "Cập nhật phần mềm máy" — gần như luôn là chưa bấm ĐỎ hoặc mất mạng.'),
      InfoItem('Không thấy cổng COM khi cắm cáp lấy nhật ký',
          'Dùng cáp có dây dữ liệu (cáp sạc rẻ thường chỉ sạc), thử cổng USB khác, '
              'cài driver CH340/CP210x. App chỉ hiện cổng USB thật, không hiện cổng '
              'Bluetooth.'),
    ],
  ),
  InfoSection(
    id: 'ticket',
    icon: Icons.assignment_turned_in_outlined,
    title: 'Kịch bản tiếp nhận & chuyển kỹ thuật',
    jump: 'Kịch bản',
    intro: 'Làm đúng thứ tự này thì kỹ thuật xử lý trong một lần, không phải gọi lại khách.',
    items: [
      InfoItem('1. Lấy mã máy', 'RPL + 5 số trên tem máy (hoặc trên màn hình máy).'),
      InfoItem('2. Ghi hiện tượng bằng lời khách',
          'Khách thấy gì, màn hình ghi gì, xảy ra lúc nào (khởi động / đang chạy / '
              'xong), lặp bao nhiêu lần, từ khi nào.'),
      InfoItem('3. Hướng dẫn thử nhanh',
          'Nguồn (đúng adapter, đổi ổ cắm), WiFi (còn nối không), khởi động lại máy '
              'khi máy đang rảnh.'),
      InfoItem('4. Lấy nhật ký máy nếu có laptop',
          'Mục Xử lý sự cố: cắm cáp USB → Kết nối → Gửi log về kỹ thuật (kèm mã máy '
              '+ mô tả). Không có mạng thì Lưu file rồi gửi email.'),
      InfoItem('5. Tạo ticket',
          'Bắt buộc: mã máy, mô tả, nhật ký (nếu lấy được). Nên có: ảnh màn hình / '
              'mã lỗi, bản firmware (Trạng thái máy), lần gửi cuối, adapter đang dùng.'),
      InfoItem('Câu mở đầu nên dùng',
          '"Anh/chị cho em xin mã máy trên tem, rồi mô tả giúp em màn hình máy đang '
              'hiện gì" — hỏi trước, hướng dẫn thao tác sau.'),
    ],
  ),
  InfoSection(
    id: 'glossary',
    icon: Icons.menu_book_outlined,
    title: 'Từ điển thuật ngữ',
    jump: 'Từ điển',
    intro: 'Giải thích ngắn các chữ hay gặp trong app, trên máy và trong nhật ký.',
    items: [
      InfoItem('Slot / kênh', 'Một vị trí đặt ống mẫu. 10 slot = 10 mẫu một lần.'),
      InfoItem('CT', 'Số vòng đo mà máy bắt đầu thấy tín hiệu — chỉ có ở mẫu dương tính.'),
      InfoItem('Firmware',
          'Phần mềm chạy bên trong máy (khác app trên máy tính). Có số bản như 2.4.5.'),
      InfoItem('OTA', 'Cập nhật firmware qua mạng WiFi, không cần cáp.'),
      InfoItem('Log (nhật ký máy)',
          'Dòng chữ máy liên tục in ra kể lại nó đang làm gì. Kỹ thuật đọc để tìm '
              'nguyên nhân; CSKH chỉ cần lấy và gửi.'),
      InfoItem('Cổng COM / cáp USB',
          'Cắm cáp USB máy vào laptop, Windows tạo một "cổng COM" (ví dụ COM13) để '
              'app nói chuyện với máy.'),
      InfoItem('Brownout', 'Điện áp tụt làm máy tự reset. Gần như luôn do nguồn / adapter.'),
      InfoItem('Reset / khởi động lại',
          'Máy tắt rồi bật lại. Một lần sau cập nhật là bình thường; lặp liên tục là có vấn đề.'),
      InfoItem('Cảm biến nhiệt / -127',
          'Máy có 6 cảm biến nhiệt (3 đáy, 2 nắp, 1 môi trường). -127 là giá trị máy '
              'in khi không đọc được cảm biến.'),
      InfoItem('WiFi STA / AP',
          'STA = máy nối vào WiFi của khách. AP = máy tự phát WiFi riêng để cấu hình.'),
      InfoItem('Engineer Server / hệ thống',
          'Máy chủ của Forte nhận dữ liệu từ mọi máy; app đọc lịch sử từ đây.'),
      InfoItem('Ticket', 'Phiếu chuyển kỹ thuật — mã máy + mô tả + nhật ký.'),
    ],
  ),
  InfoSection(
    id: 'technical',
    icon: Icons.build_outlined,
    title: 'Dành cho kỹ thuật: cổng USB & lệnh máy',
    intro: 'Chỉ cần khi kỹ thuật yêu cầu. Nhân viên CSKH không phải đọc mục này.',
    advanced: true,
    items: [
      InfoItem('Thông số cổng',
          '115200 baud, 8N1. Windows nhận thành cổng COM (driver CP210x / CH340).'),
      InfoItem('Lệnh gửi qua cổng (kèm xuống dòng)',
          'ParaRead = in toàn bộ tham số máy (JSON) · M = in hệ số hiệu chuẩn · '
              'TemperatureOutput = bật/tắt in 6 kênh nhiệt · Res = khởi động lại · '
              'Fan On = bật quạt · Buzzer … = còi. Mục Xử lý sự cố đã có nút cho 4 lệnh đầu.'),
      InfoItem('Lệnh KHÔNG được gửi',
          'Lệnh "P" (đặt PWM LED) làm firmware chờ vô hạn — chỉ dùng sau khi sửa firmware.'),
      InfoItem('Máy tự reset khi cắm cáp?',
          'Cổng USB có mạch auto-reset (DTR/RTS). App ghim hai chân này tắt khi mở '
              'cổng nên máy KHÔNG reset. Nếu vẫn reset khi gửi lệnh thì là firmware '
              'tự khởi động lại theo lệnh.'),
      InfoItem('6 kênh nhiệt',
          'Lysis · Amp1 · Amp2 (đáy) · Hotlid1 · Hotlid2 (nắp) · Ambient. Xem thời '
              'gian thực ở tab Kỹ Thuật › Log nhiệt. Máy in dòng TimePT/TimeRB/TimeRT.'),
      InfoItem('Trang web nội bộ của máy',
          'Trang chủ: mã máy, mạng, nhiệt độ, trạng thái. Có trang lỗi 10 slot, trang '
              'cấu hình, và bấm nút từ xa (đỏ / xanh / trắng).'),
      InfoItem('Mã lỗi 4 số',
          'module × 1000 + loại × 100 + bước × 10 + slot (bảng ý nghĩa trong firmware).'),
    ],
  ),
];

/// Tách mã lỗi 4 chữ số trên màn TFT theo công thức firmware
/// `module*1000 + type*100 + step*10 + slot` (xem `SensorError` trong
/// `fbt_api.dart`). Trả `null` nếu không phải 4 chữ số.
///
/// Ý NGHĨA từng con số (module nào, loại lỗi nào) nằm trong bảng của firmware
/// (`errorCheck.cpp`) — app KHÔNG có bảng đó nên chỉ tách số, không diễn giải.
({int module, int type, int step, int slot})? decodeErrorCode(String raw) {
  final s = raw.trim();
  if (!RegExp(r'^\d{4}$').hasMatch(s)) return null;
  final code = int.parse(s);
  return (
    module: code ~/ 1000,
    type: (code ~/ 100) % 10,
    step: (code ~/ 10) % 10,
    slot: code % 10,
  );
}
