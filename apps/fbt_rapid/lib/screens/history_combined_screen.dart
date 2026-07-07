import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/session_store.dart';
import '../util/i18n.dart';
import 'cloud_devices_screen.dart';
import 'history_screen.dart';

/// Tab "Lịch sử" GỘP: nút gạt **Cục bộ | Cloud** ở trên, bên dưới là màn tương
/// ứng (mỗi màn giữ nguyên AppBar/hành động riêng). Mặc định xem Cloud.
class HistoryCombinedScreen extends StatefulWidget {
  final AppSettings settings;
  const HistoryCombinedScreen({super.key, required this.settings});

  @override
  State<HistoryCombinedScreen> createState() => _HistoryCombinedScreenState();
}

class _HistoryCombinedScreenState extends State<HistoryCombinedScreen> {
  // Tạm ẩn mục "Cục bộ" → chỉ hiện Cloud. Bật lại: đổi thành true.
  final bool _showLocal = false;

  int _seg = 1; // 0 = cục bộ, 1 = cloud

  // Nguồn cloud đang xem: Google (mặc định) hoặc Server riêng (chỉ admin chọn).
  CloudSource _cloudSource = CloudSource.google;

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    // Đã tạm ẩn Cục bộ → hiển thị thẳng màn Cloud.
    if (!_showLocal) {
      // Nút gạt chọn nguồn (Google | RAPID ERP) dành cho admin (canWrite).
      // KHÔNG yêu cầu nhập key: admin luôn thấy; chưa có key thì gọi API RAPID
      // ERP có thể trả 401 (server tự quyết có bắt buộc key hay không).
      final showSourcePicker = SessionStore.canWrite;
      return Scaffold(
        body: SafeArea(
          bottom: false,
          child: Column(
            children: [
              if (showSourcePicker)
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
                  child: SegmentedButton<CloudSource>(
                    showSelectedIcon: false,
                    segments: [
                      ButtonSegment(
                        value: CloudSource.google,
                        icon: const Icon(Icons.cloud_outlined),
                        label: Text(tr('history.cloudGoogle')),
                      ),
                      ButtonSegment(
                        value: CloudSource.rapidErp,
                        icon: const Icon(Icons.api_outlined),
                        label: Text(tr('history.cloudRapid')),
                      ),
                    ],
                    selected: {_cloudSource},
                    onSelectionChanged: (sel) =>
                        setState(() => _cloudSource = sel.first),
                  ),
                ),
              Expanded(
                child: CloudDevicesScreen(
                  // ValueKey gồm nguồn + danh sách mã máy → đổi nguồn/sửa danh
                  // sách thì remount + nạp lại (_api là late final).
                  key: ValueKey('cloud_${_cloudSource.name}'
                      '_${s.cloudUrlFor(_cloudSource)}'
                      '_${s.rapidErpDeviceIds.hashCode}'),
                  settings: s,
                  source: _cloudSource,
                ),
              ),
            ],
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
                    icon: const Icon(Icons.smartphone_outlined),
                    label: Text(tr('history.local')),
                  ),
                  ButtonSegment(
                    value: 1,
                    icon: const Icon(Icons.cloud_outlined),
                    label: Text(tr('history.cloud')),
                  ),
                ],
                selected: {_seg},
                onSelectionChanged: (sel) => setState(() => _seg = sel.first),
              ),
            ),
            Expanded(
              child: IndexedStack(
                index: _seg,
                children: [
                  HistoryScreen(
                      key: ValueKey('hist_${s.deviceIp}'), settings: s),
                  CloudDevicesScreen(
                      key: ValueKey('cloud_${s.cloudApiUrl}'), settings: s),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
