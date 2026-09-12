/// Tab **Sản xuất (ATE)** — trạm test tự động cho xưởng (chỉ nhân sự).
///
/// Ba mục con theo đúng mẫu segmented của tab Kỹ Thuật / Chăm sóc KH:
/// **Chạy trạm | Hồ sơ máy | Thống kê**.
///
/// Pha P0 của `docs/plan/ate-san-xuat.md`: nạp firmware → khai sinh (số máy +
/// tham số lô) → hồ sơ có truy vết. Các phép đo quang/nhiệt (P1–P3) sẽ thêm vào
/// `services/ate_runner.dart` mà không phải đụng màn hình.
///
/// ⚠️ Bản này kéo `dart:io` (esptool + cổng COM) — bản web ở `ate_screen_web.dart`
/// (cùng tên class + constructor) chỉ có hai mục HTTP.
library;

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
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
    // Dựng danh sách rồi kẹp chỉ số — số mục đổi theo quyền (thao tác viên
    // không có Thống kê/Tiêu chuẩn).
    final tabs = <AppTab>[
      AppTab(
        icon: Icons.play_circle_outline,
        label: tr('ate.run'),
        // KHÔNG lazy: mục Chạy trạm giữ log + kết quả của máy đang chạy; dựng
        // lại khi quay về là mất sạch (cùng lý do với 3 công cụ tab Kỹ Thuật).
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
      subtitle: tr('ate.shellHint'),
      // Kẹp chỉ số: mục Thống kê có thể biến mất giữa chừng (đổi tài khoản mà
      // không khởi động lại app) — index trỏ ra ngoài là `IndexedStack` ném.
      index: _seg.clamp(0, tabs.length - 1),
      onChanged: (i) => setState(() => _seg = i),
      tabs: tabs,
    );
  }
}
