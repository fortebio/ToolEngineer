import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/history_store.dart' show kLocalHistoryEnabled;
import '../services/session_store.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'cloud_devices_screen.dart';
import 'history_screen.dart';

/// Tab "Lịch sử": xem Cloud (admin có dải chọn nguồn Google | RAPID ERP |
/// Engineer Server). Mục File JSON đã CHUYỂN sang tab **Thư Mục**
/// (`folder_screen.dart` → `json_files_screen.dart`).
///
/// Dùng `AppTabScaffold` như mọi tab khác (đại tu 2026-08-19). Trước đó dải chọn
/// nguồn nằm **trên** tiêu đề — vì tiêu đề khi ấy là AppBar của `CloudDevicesScreen`
/// bên trong, nên màn mở ra bằng ba nút không ai giới thiệu.
class HistoryCombinedScreen extends StatefulWidget {
  final AppSettings settings;
  const HistoryCombinedScreen({super.key, required this.settings});

  @override
  State<HistoryCombinedScreen> createState() => _HistoryCombinedScreenState();
}

class _HistoryCombinedScreenState extends State<HistoryCombinedScreen> {
  // Công tắc nằm ở `history_store.dart` — MỘT chỗ gác cả nơi đọc lẫn nơi ghi.
  // Có riêng một cờ ở đây thì tắt được mục đọc mà nút "Đồng bộ về máy" vẫn sống.
  static const bool _showLocal = kLocalHistoryEnabled;

  int _seg = 0; // chỉ số trong danh sách mục đang hiện

  /// Nguồn cloud cho từng mục. Chỉ admin (`canWrite`) được chọn nguồn — khách
  /// hàng luôn xem Google, nên danh sách rút còn một mục và `AppTabScaffold`
  /// tự giấu dải chọn.
  ///
  /// THỨ TỰ = mức độ dùng: Engineer Server đứng đầu (mục mặc định khi mở tab),
  /// Cloud Google lùi về cuối.
  List<CloudSource> get _sources => SessionStore.canWriteClinical
      ? const [CloudSource.engineer, CloudSource.rapidErp, CloudSource.google]
      : const [CloudSource.google];

  static const _sourceIcons = {
    CloudSource.google: Icons.cloud_outlined,
    CloudSource.rapidErp: Icons.api_outlined,
    CloudSource.engineer: Icons.dns_outlined,
  };

  static const _sourceKeys = {
    CloudSource.google: 'history.cloudGoogle',
    CloudSource.rapidErp: 'history.cloudRapid',
    CloudSource.engineer: 'history.cloudEngineer',
  };

  Widget _cloudPage(CloudSource src) {
    final s = widget.settings;
    return CloudDevicesScreen(
      // ValueKey gồm nguồn + danh sách mã máy → đổi nguồn/sửa danh sách thì
      // remount + nạp lại (_api là late final).
      key: ValueKey('cloud_${src.name}'
          '_${s.cloudUrlFor(src)}'
          '_${s.rapidErpDeviceIds.hashCode}'),
      settings: s,
      source: src,
    );
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;

    // Mục "Cục bộ" bật lại → quay về cặp Cục bộ | Cloud (nguồn cloud khi đó
    // cố định Google, y như trước đại tu).
    final tabs = _showLocal
        ? [
            AppTab(
              icon: Icons.smartphone_outlined,
              label: tr('history.local'),
              page: HistoryScreen(
                  key: ValueKey('hist_${s.deviceIp}'), settings: s),
            ),
            AppTab(
              icon: Icons.cloud_outlined,
              label: tr('history.cloud'),
              page: _cloudPage(CloudSource.google),
            ),
          ]
        : [
            for (final src in _sources)
              AppTab(
                icon: _sourceIcons[src]!,
                label: tr(_sourceKeys[src]!),
                page: _cloudPage(src),
              ),
          ];

    final i = _seg.clamp(0, tabs.length - 1);
    return AppTabScaffold(
      title: tr('nav.history'),
      subtitle: tr('history.hint'),
      // Mỗi nguồn tự mở kết nối riêng → dựng cả ba là ba request tới ba server
      // ngay khi mở tab, hai trong đó không ai xem.
      lazy: !_showLocal,
      index: i,
      onChanged: (v) => setState(() => _seg = v),
      tabs: tabs,
    );
  }
}
