import 'package:flutter/material.dart';

import '../util/i18n.dart';
import 'flasher_screen.dart';
import 'serial_console_screen.dart';
import 'temperature_log_screen.dart';

/// Tab **Kỹ Thuật** (nhân sự: root + nhân viên) — gộp 3 công cụ kỹ thuật:
/// **Log nhiệt | Đọc serial | Nạp code**. Nút gạt ở trên, mỗi màn giữ nguyên
/// AppBar/hành động riêng (mẫu giống tab Lịch sử gộp).
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
    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
              child: SegmentedButton<int>(
                showSelectedIcon: false,
                segments: [
                  ButtonSegment(
                    value: 0,
                    icon: const Icon(Icons.thermostat_outlined),
                    label: Text(tr('tech.templog')),
                  ),
                  ButtonSegment(
                    value: 1,
                    icon: const Icon(Icons.terminal_outlined),
                    label: Text(tr('tech.serial')),
                  ),
                  ButtonSegment(
                    value: 2,
                    icon: const Icon(Icons.memory_outlined),
                    label: Text(tr('tech.flash')),
                  ),
                ],
                selected: {_seg},
                onSelectionChanged: (sel) => setState(() => _seg = sel.first),
              ),
            ),
            Expanded(
              child: IndexedStack(
                index: _seg,
                // active=false khi không phải segment đang chọn → màn tự nhả cổng
                // COM (Đọc serial đóng cổng; Nạp code dừng theo dõi) tránh tranh
                // chấp phần cứng. Log nhiệt giữ đọc nền (không mất dữ liệu mẫu).
                children: [
                  const TemperatureLogScreen(),
                  SerialConsoleScreen(active: _seg == 1),
                  FlasherScreen(active: _seg == 2),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
