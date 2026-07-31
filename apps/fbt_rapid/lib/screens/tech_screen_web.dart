import 'package:flutter/material.dart';

import '../util/i18n.dart';
import '../util/web_serial.dart';
import 'web_flasher_screen.dart';
import 'web_serial_console_screen.dart';
import 'web_temp_log_screen.dart';

/// Bản WEB của tab **Kỹ Thuật** (chọn qua conditional import ở home_shell) —
/// cùng mẫu segmented với `tech_screen.dart` desktop: **Log nhiệt | Đọc serial
/// | Nạp code**, chạy bằng **Web Serial API** + **esptool-js** thay
/// libserialport/esptool.exe. CHỈ hoạt động trên Chrome/Edge DESKTOP (Web
/// Serial không có trên Firefox/Safari/mobile) — trình duyệt khác hiện cảnh
/// báo. Mỗi lần kết nối người dùng phải tự chọn cổng trong hộp thoại browser.
class TechScreen extends StatefulWidget {
  const TechScreen({super.key});

  @override
  State<TechScreen> createState() => _TechScreenState();
}

class _TechScreenState extends State<TechScreen> {
  int _seg = 0; // 0 = log nhiệt, 1 = đọc serial, 2 = nạp code

  @override
  Widget build(BuildContext context) {
    if (!webSerialSupported) {
      final cs = Theme.of(context).colorScheme;
      return Scaffold(
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.usb_off, size: 48, color: cs.onSurfaceVariant),
                const SizedBox(height: 12),
                Text(
                  tr('techweb.unsupported'),
                  textAlign: TextAlign.center,
                  style: TextStyle(color: cs.onSurfaceVariant),
                ),
              ],
            ),
          ),
        ),
      );
    }
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
                // active=false → màn tự nhả cổng (Đọc serial đóng cổng giữ
                // log; Nạp code dừng theo dõi) — cùng quy ước desktop. Log
                // nhiệt CỐ TÌNH giữ đọc nền (dừng = mất mẫu).
                children: [
                  const WebTempLogScreen(),
                  WebSerialConsoleScreen(active: _seg == 1),
                  WebFlasherScreen(active: _seg == 2),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
