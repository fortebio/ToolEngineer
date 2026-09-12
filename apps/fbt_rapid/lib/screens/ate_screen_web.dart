/// Bản **WEB** của tab Sản xuất (ATE) — chọn qua conditional import ở
/// `home_shell.dart`, cùng tên class + constructor với bản desktop.
///
/// Từ 2026-09-08 web có **đủ cả Chạy trạm**: nạp bằng **esptool-js** + **Web
/// Serial** (`services/ate_station_web.dart`), firmware lấy từ **kho OTA của
/// server**. Mục đó chỉ hiện khi trình duyệt thật sự chạy được
/// ([ateStationAvailable] = Chrome/Edge desktop + bundle `esptool.js`) — bày một
/// mục mà bấm vào chỉ để nhận lỗi thì tệ hơn không bày.
///
/// Hai giới hạn của bản web, màn tự nói ra chứ không giấu:
/// - Bước nạp **chưa đối chiếu lại được** nội dung flash (esptool-js không có
///   `verify_flash`) → FW-01 ghi `info` chứ không phải `pass`.
/// - Gọi `http://<ip>` tới máy bị trình duyệt chặn khi app chạy HTTPS →
///   OPT-01 tự lùi về đường UART.
library;

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/ate_station.dart' show ateStationAvailable;
import '../services/session_store.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'ate_limits_screen.dart';
import 'ate_profile_screen.dart';
import 'ate_run_screen.dart';
import 'ate_stats_screen.dart';

class AteScreen extends StatefulWidget {
  final AppSettings settings;
  const AteScreen({super.key, required this.settings});

  @override
  State<AteScreen> createState() => _AteScreenState();
}

class _AteScreenState extends State<AteScreen> {
  int _seg = 0;

  @override
  Widget build(BuildContext context) {
    // Dựng danh sách rồi kẹp chỉ số: số mục đổi theo QUYỀN (thao tác viên không
    // có Thống kê/Tiêu chuẩn) lẫn theo TRÌNH DUYỆT (không Web Serial thì không
    // có Chạy trạm) — index trỏ ra ngoài là `IndexedStack` ném.
    final tabs = <AppTab>[
      if (ateStationAvailable && SessionStore.canRunStation)
        AppTab(
          icon: Icons.play_circle_outline,
          label: tr('ate.run'),
          page: AteRunScreen(settings: widget.settings),
        ),
      AppTab(
        icon: Icons.badge_outlined,
        label: tr('ate.profile'),
        page: AteProfileScreen(settings: widget.settings),
      ),
      if (SessionStore.canSeeProductionStats) ...[
        AppTab(
          icon: Icons.insights_outlined,
          label: tr('ate.stats'),
          page: AteStatsScreen(settings: widget.settings),
        ),
        AppTab(
          icon: Icons.rule_outlined,
          label: tr('ate.limits2'),
          page: AteLimitsScreen(settings: widget.settings),
        ),
      ],
    ];
    return AppTabScaffold(
      title: tr('nav.ate'),
      subtitle: ateStationAvailable ? tr('ate.webNote') : tr('ate.shellHintWeb'),
      index: _seg.clamp(0, tabs.length - 1),
      onChanged: (i) => setState(() => _seg = i),
      tabs: tabs,
    );
  }
}
