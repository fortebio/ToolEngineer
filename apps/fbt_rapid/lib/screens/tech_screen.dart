import 'package:flutter/material.dart';

import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'flasher_screen.dart';
import 'serial_console_screen.dart';
import 'temperature_log_screen.dart';

/// Tab **Kỹ Thuật** (nhân sự: root + nhân viên) — gộp 3 công cụ:
/// **Log nhiệt | Đọc serial | Nạp code**.
///
/// Lưu ý cổng COM: 3 màn dùng chung phần cứng COM. Mở cổng X ở "Log nhiệt"/"Đọc
/// serial" rồi sang "Nạp code" nạp cổng X sẽ báo bận → **đóng cổng trước khi nạp**.
class TechScreen extends StatefulWidget {
  const TechScreen({super.key});

  @override
  State<TechScreen> createState() => _TechScreenState();
}

class _TechScreenState extends State<TechScreen> {
  int _seg = 0; // 0 = log nhiệt, 1 = đọc serial, 2 = nạp code

  @override
  Widget build(BuildContext context) {
    return AppTabScaffold(
      title: tr('nav.tech'),
      subtitle: tr('tech.comShared'),
      index: _seg,
      onChanged: (i) => setState(() => _seg = i),
      tabs: [
        AppTab(
          icon: Icons.thermostat_outlined,
          label: tr('tech.templog'),
          // active=false khi không phải mục đang chọn → màn tự nhả cổng COM
          // (Đọc serial đóng cổng; Nạp code dừng theo dõi) tránh tranh chấp
          // phần cứng. Log nhiệt CỐ Ý giữ đọc nền để không mất mẫu.
          page: const TemperatureLogScreen(),
        ),
        AppTab(
          icon: Icons.terminal_outlined,
          label: tr('tech.serial'),
          page: SerialConsoleScreen(active: _seg == 1),
        ),
        AppTab(
          icon: Icons.memory_outlined,
          label: tr('tech.flash'),
          page: FlasherScreen(active: _seg == 2),
        ),
      ],
    );
  }
}
