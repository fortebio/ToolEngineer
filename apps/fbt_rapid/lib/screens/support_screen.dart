import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'support_info_screen.dart';
import 'support_troubleshoot_screen.dart';

/// Tab **Chăm sóc KH** (nhân sự: root + nhân viên) — bộ công cụ cho nhân viên
/// chăm sóc khách hàng vận hành KHÔNG cần kiến thức kỹ thuật, mẫu segmented
/// giống tab Kỹ Thuật / Quản lý máy, gồm 2 mục:
///
/// - **Thông tin máy** (`support_info_screen.dart`): tra cứu tính năng, cách đọc
///   kết quả, nút bấm, mã lỗi, mạng, OTA, sự cố thường gặp — nội dung ở
///   `data/machine_info_content.dart`.
/// - **Xử lý sự cố** (`support_troubleshoot_screen.dart`): cắm USB → Kết nối →
///   log máy tự hiện kèm "dấu hiệu" đọc hiểu được → **Gửi log về kỹ thuật**
///   (Engineer Server `PUT /devices/{id}/logs`).
///
/// Vì sao KHÔNG nhét vào tab Kỹ Thuật: bên đó là ba công cụ kỹ thuật (đa cổng,
/// HEX, nạp code) — đúng thứ nhân viên CSKH không cần và dễ bấm nhầm. Tab này
/// chỉ có việc của họ, một cổng, một nút gửi.
///
/// Chạy cả desktop lẫn web (mục Xử lý sự cố dùng `util/serial_link.dart` tự
/// chọn libserialport / Web Serial). Không cần bản `_web` riêng.
class SupportScreen extends StatefulWidget {
  final AppSettings settings;

  /// `false` khi tab này KHÔNG phải tab đang xem → mục Xử lý sự cố tự nhả cổng
  /// COM (giữ log) cho tab Kỹ Thuật, vì hai bên dùng chung phần cứng COM.
  final bool active;

  const SupportScreen({super.key, required this.settings, this.active = true});

  @override
  State<SupportScreen> createState() => _SupportScreenState();
}

class _SupportScreenState extends State<SupportScreen> {
  int _seg = 0; // 0 = thông tin máy, 1 = xử lý sự cố

  @override
  Widget build(BuildContext context) {
    return AppTabScaffold(
      title: tr('nav.support'),
      subtitle: _seg == 0 ? tr('sp.infoHint') : tr('sp.troubleHint'),
      index: _seg,
      onChanged: (i) => setState(() => _seg = i),
      tabs: [
        AppTab(
          icon: Icons.menu_book_outlined,
          label: tr('sp.info'),
          page: const SupportInfoScreen(),
        ),
        AppTab(
          icon: Icons.healing_outlined,
          label: tr('sp.trouble'),
          page: SupportTroubleshootScreen(
            settings: widget.settings,
            active: widget.active && _seg == 1,
          ),
        ),
      ],
    );
  }
}
