import 'dart:async';

import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/fbt_api.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'support_inbox_screen.dart';
import 'support_info_screen.dart';
import 'support_log_stats_screen.dart';
import 'support_troubleshoot_screen.dart';

/// Tab **Chăm sóc KH** (nhân sự: root + nhân viên) — bộ công cụ cho nhân viên
/// chăm sóc khách hàng vận hành KHÔNG cần kiến thức kỹ thuật, mẫu segmented
/// giống tab Kỹ Thuật / Quản lý máy, gồm 4 mục:
///
/// - **Thông tin máy** (`support_info_screen.dart`): tra cứu tính năng, cách đọc
///   kết quả, nút bấm, mã lỗi, mạng, OTA, sự cố thường gặp — nội dung ở
///   `data/machine_info_content.dart`.
/// - **Xử lý sự cố** (`support_troubleshoot_screen.dart`): cắm USB → Kết nối →
///   log máy tự hiện kèm "dấu hiệu" đọc hiểu được → **Gửi log về kỹ thuật**
///   (Engineer Server `PUT /devices/{id}/logs`).
/// - **Log đã nhận** (`support_inbox_screen.dart`, 2026-09-25): hộp thư của kỹ
///   thuật — mọi log CSKH gửi về, Nhận xử lý → Đã xử lý kèm ghi chú trả lời.
/// - **Thống kê lỗi** (`support_log_stats_screen.dart`): dấu hiệu hay gặp, máy
///   gửi nhiều log, theo firmware, theo ngày (`GET /logs/stats`).
///
/// Vì sao KHÔNG nhét vào tab Kỹ Thuật: bên đó là ba công cụ kỹ thuật (đa cổng,
/// HEX, nạp code) — đúng thứ nhân viên CSKH không cần và dễ bấm nhầm. Tab này
/// chỉ có việc của họ, một cổng, một nút gửi.
///
/// Chạy cả desktop lẫn web (mục Xử lý sự cố dùng `util/serial_link.dart` tự
/// chọn libserialport / Web Serial). Không cần bản `_web` riêng.
class SupportScreen extends StatefulWidget {
  final AppSettings settings;

  /// Mục Xử lý sự cố tự nhả cổng COM (giữ log) khi tab/mục này không còn được
  /// xem — nó đọc `TickerMode` do HomeShell + AppTabScaffold bọc, không cần cờ.
  const SupportScreen({super.key, required this.settings});

  @override
  State<SupportScreen> createState() => _SupportScreenState();
}

class _SupportScreenState extends State<SupportScreen> {
  // 0 = thông tin máy, 1 = xử lý sự cố, 2 = log đã nhận, 3 = thống kê lỗi
  int _seg = 0;

  /// Số log CSKH `new` (badge trên mục Log đã nhận). Poll nhẹ 90 s KHI TAB Chăm
  /// sóc KH đang được xem (TickerMode của HomeShell); mục hộp thư tự báo lại mỗi
  /// lần nạp nên số khớp ngay sau khi kỹ thuật đổi trạng thái.
  int _newLogs = 0;
  bool _visible = false;
  Timer? _timer;
  final _deviceFilter = ValueNotifier<String>('');

  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final v = TickerMode.valuesOf(context).enabled;
    if (v == _visible) return;
    _visible = v;
    _timer?.cancel();
    if (v) {
      Future.microtask(_pollNew);
      _timer = Timer.periodic(const Duration(seconds: 90), (_) => _pollNew());
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    _deviceFilter.dispose();
    super.dispose();
  }

  Future<void> _pollNew() async {
    try {
      final r = await _api.listAllLogs(status: 'new', limit: 1);
      _setNew(r.counts['new'] ?? 0);
    } catch (_) {
      // Badge là tiện ích: server cũ (405) / token cũ (401) / mất mạng → im lặng.
    }
  }

  void _setNew(int n) {
    if (mounted && n != _newLogs) setState(() => _newLogs = n);
  }

  String _subtitle() => switch (_seg) {
        0 => tr('sp.infoHint'),
        1 => tr('sp.troubleHint'),
        2 => tr('lg.hint'),
        _ => tr('ls.hint'),
      };

  @override
  Widget build(BuildContext context) {
    return AppTabScaffold(
      title: tr('nav.support'),
      subtitle: _subtitle(),
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
          page: SupportTroubleshootScreen(settings: widget.settings),
        ),
        AppTab(
          icon: Icons.inbox_outlined,
          label: tr('lg.tab'),
          badge: _newLogs,
          page: SupportInboxScreen(
            settings: widget.settings,
            onNewCount: _setNew,
            deviceFilter: _deviceFilter,
          ),
        ),
        AppTab(
          icon: Icons.insights_outlined,
          label: tr('ls.tab'),
          page: SupportLogStatsScreen(
            settings: widget.settings,
            onOpenDevice: (d) {
              setState(() => _seg = 2);
              // Đặt sau khi đổi mục: gán cùng giá trị thì ValueNotifier im lặng.
              _deviceFilter.value = '';
              _deviceFilter.value = d;
            },
          ),
        ),
      ],
    );
  }
}
