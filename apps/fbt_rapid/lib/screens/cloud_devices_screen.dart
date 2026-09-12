import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/cloud_cache.dart';
import '../services/cloud_history_api.dart';
import '../services/rapid_erp_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../widgets/app_search_box.dart';
import '../util/format.dart';
import 'cloud_runs_screen.dart';
import 'download_device_dialog.dart';

/// Kiểu sắp xếp danh sách máy.
enum _DeviceSort { latestDesc, latestAsc, idAsc, idDesc }

extension on _DeviceSort {
  String get label {
    switch (this) {
      case _DeviceSort.latestDesc:
        return 'Mới nhất';
      case _DeviceSort.latestAsc:
        return 'Cũ nhất';
      case _DeviceSort.idAsc:
        return 'Mã máy A → Z';
      case _DeviceSort.idDesc:
        return 'Mã máy Z → A';
    }
  }
}

/// Tab "Cloud": chọn ID máy từ folder Drive (qua Apps Script doGet),
/// rồi xem lịch sử của từng máy.
class CloudDevicesScreen extends StatefulWidget {
  final AppSettings settings;
  final CloudSource source;
  const CloudDevicesScreen({
    super.key,
    required this.settings,
    this.source = CloudSource.google,
  });

  @override
  State<CloudDevicesScreen> createState() => _CloudDevicesScreenState();
}

class _CloudDevicesScreenState extends State<CloudDevicesScreen> {
  static const _ttl = Duration(minutes: 5);

  late final CloudHistoryClient _api =
      buildCloudClient(widget.settings, widget.source);
  final _cache = CloudCache();
  // Nhập mã máy thủ công — dùng cho RAPID ERP (không có endpoint liệt kê máy).
  final _manualId = TextEditingController();
  List<CloudDevice> _devices = [];
  bool _loading = false; // tải lần đầu (chưa có gì để hiện)
  bool _refreshing = false; // đang làm mới ngầm (đã có dữ liệu cũ)
  DateTime? _cachedAt; // thời điểm dữ liệu đang hiện được lấy
  String? _error;
  String _query = '';
  _DeviceSort _sort = _DeviceSort.latestDesc;

  String get _url => widget.settings.cloudUrlFor(widget.source);

  @override
  void initState() {
    super.initState();
    if (_url.isNotEmpty) _init();
  }

  @override
  void dispose() {
    _manualId.dispose();
    super.dispose();
  }

  /// Mở thẳng lịch sử của mã máy gõ tay (RAPID ERP). Vẫn tôn trọng quyền xem:
  /// `canSee` (root/allowAll thấy mọi mã; admin/khách chỉ mã được cấp).
  void _openManual() {
    final id = _manualId.text.trim();
    if (id.isEmpty) return;
    final session = SessionStore.current;
    if (session == null || !session.canSee(id)) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Tài khoản không được cấp mã máy "$id".'),
        backgroundColor: AppSemantic.of(context).warning,
      ));
      return;
    }
    FocusScope.of(context).unfocus();
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => CloudRunsScreen(
          settings: widget.settings,
          device: CloudDevice(id: id, runCount: 0),
          source: widget.source,
        ),
      ),
    );
  }

  /// Ô nhập mã máy + nút Xem (chỉ hiện cho nguồn RAPID ERP).
  Widget _manualEntryBar() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _manualId,
              textInputAction: TextInputAction.go,
              textCapitalization: TextCapitalization.characters,
              onSubmitted: (_) => _openManual(),
              decoration: const InputDecoration(
                hintText: 'Nhập mã máy rồi bấm Xem (vd RPL03010)…',
                prefixIcon: Icon(Icons.qr_code_2),
                isDense: true,
              ),
            ),
          ),
          const SizedBox(width: 8),
          FilledButton.icon(
            onPressed: _openManual,
            icon: const Icon(Icons.search),
            label: const Text('Xem'),
          ),
        ],
      ),
    );
  }

  /// Hiện cache ngay (nếu có) rồi chỉ gọi Apps Script khi cache quá hạn.
  Future<void> _init() async {
    final cached = await _cache.loadDevices(_url);
    if (!mounted) return;
    if (cached != null && cached.devices.isNotEmpty) {
      setState(() {
        _devices = cached.devices;
        _cachedAt = cached.savedAt;
      });
      if (DateTime.now().difference(cached.savedAt) < _ttl) {
        return; // còn mới → khỏi gọi Apps Script (giảm tải)
      }
    }
    await _refresh();
  }

  /// Gọi Apps Script, cập nhật + lưu cache. Giữ danh sách cũ trong lúc tải.
  /// [fresh] = true (bấm "Làm mới") → bỏ qua cache server, lấy dữ liệu mới nhất.
  Future<void> _refresh({bool fresh = false}) async {
    setState(() {
      _refreshing = true;
      if (_devices.isEmpty) _loading = true;
      _error = null;
    });
    try {
      final d = await _api.listDevices(fresh: fresh);
      if (!mounted) return;
      final now = DateTime.now();
      await _cache.saveDevices(_url, d, now);
      if (!mounted) return;
      setState(() {
        _devices = d;
        _cachedAt = now;
      });
    } catch (e) {
      if (!mounted) return;
      if (_devices.isEmpty) {
        setState(() => _error = '$e');
      } else {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: const Text('Không cập nhật được, đang xem dữ liệu đã lưu.'),
          backgroundColor: AppSemantic.of(context).warning,
        ));
      }
    } finally {
      if (mounted) {
        setState(() {
          _refreshing = false;
          _loading = false;
        });
      }
    }
  }

  int _cmpLatest(CloudDevice a, CloudDevice b) {
    final am = a.latest?.millisecondsSinceEpoch ?? 0;
    final bm = b.latest?.millisecondsSinceEpoch ?? 0;
    return am.compareTo(bm);
  }

  /// Số máy phiên này ĐƯỢC PHÉP xem, trước khi lọc theo ô tìm — là mẫu số của
  /// `<khớp>/<tổng>`. Dùng `_devices.length` ở đây là sai: nó gồm cả máy người
  /// này không có quyền thấy.
  int get _allowedCount {
    final session = SessionStore.current;
    if (session == null) return 0;
    if (session.allowAll) return _devices.length;
    return _devices.where((d) => session.canSee(d.id)).length;
  }

  /// Lọc theo **quyền** (user chỉ thấy mã máy được cấp) → ô tìm kiếm → sắp xếp.
  List<CloudDevice> get _visibleDevices {
    final session = SessionStore.current;
    // Không có phiên → KHÔNG thấy gì (fail-closed), tránh lộ hết máy lúc đang
    // đăng xuất/race. allowAll (root | ids '*') = thấy hết; còn lại lọc theo ids.
    final allowed = session == null
        ? const <CloudDevice>[]
        : session.allowAll
            ? _devices
            : _devices.where((d) => session.canSee(d.id)).toList();
    final q = _query.trim().toLowerCase();
    final list = q.isEmpty
        ? List<CloudDevice>.from(allowed)
        : allowed.where((d) => d.id.toLowerCase().contains(q)).toList();
    switch (_sort) {
      case _DeviceSort.latestDesc:
        list.sort((a, b) => _cmpLatest(b, a));
        break;
      case _DeviceSort.latestAsc:
        list.sort((a, b) => _cmpLatest(a, b));
        break;
      case _DeviceSort.idAsc:
        list.sort((a, b) => a.id.toLowerCase().compareTo(b.id.toLowerCase()));
        break;
      case _DeviceSort.idDesc:
        list.sort((a, b) => b.id.toLowerCase().compareTo(a.id.toLowerCase()));
        break;
    }
    return list;
  }

  @override
  Widget build(BuildContext context) {
    // KHÔNG Scaffold/AppBar ở đây. Màn này LUÔN nằm trong `AppTabScaffold` của
    // tab Lịch sử, vốn đã có tiêu đề + dải chọn nguồn — thêm một AppBar nữa là
    // tiêu đề thứ hai chồng lên tiêu đề thứ nhất (đúng lỗi của bản trước đại tu:
    // dải chọn nguồn bị đẩy lên TRÊN cả tiêu đề). Sắp xếp/làm mới chuyển xuống
    // cùng hàng với ô tìm.
    return Column(
      children: [
        // RAPID ERP không liệt kê máy → cho gõ mã máy để xem thẳng.
        if (widget.source == CloudSource.rapidErp) _manualEntryBar(),
        Expanded(child: _buildBody()),
      ],
    );
  }

  /// Nút sắp xếp + làm mới, đặt trong khe `trailing` của ô tìm.
  List<Widget> get _toolbar => [
        const SizedBox(width: 6),
        PopupMenuButton<_DeviceSort>(
          tooltip: tr('common.sort'),
          icon: const Icon(Icons.sort),
          position: PopupMenuPosition.under,
          onSelected: (s) => setState(() => _sort = s),
          itemBuilder: (_) => _DeviceSort.values
              .map((s) => CheckedPopupMenuItem<_DeviceSort>(
                    value: s,
                    checked: _sort == s,
                    child: Text(s.label),
                  ))
              .toList(),
        ),
        IconButton(
          tooltip: tr('common.refresh'),
          onPressed: _refreshing ? null : () => _refresh(fresh: true),
          icon: const Icon(Icons.refresh),
        ),
      ];

  Widget _buildBody() {
    if (_url.isEmpty) {
      final String hint;
      switch (widget.source) {
        case CloudSource.rapidErp:
          hint = 'Chưa cấu hình RAPID ERP.\n\n'
              'Vào Cài đặt → "RAPID ERP" để nhập URL + API key.';
          break;
        case CloudSource.engineer:
          hint = 'Chưa cấu hình Engineer Server.\n\n'
              'Vào Cài đặt → "Engineer Server" để nhập URL + token.';
          break;
        case CloudSource.google:
          hint = 'Chưa cấu hình URL cloud.\n\n'
              'Vào Cài đặt để dán URL Apps Script /exec.';
          break;
      }
      return _CloudHint(icon: Icons.cloud_off_outlined, text: hint);
    }
    if (_loading && _devices.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (_error != null && _devices.isEmpty) {
      return _CloudHint(
        icon: Icons.error_outline,
        text: 'Lỗi tải danh sách máy:\n\n$_error',
        onRetry: () => _refresh(fresh: true),
      );
    }

    // RAPID ERP không có endpoint liệt kê máy → danh sách suy từ mã máy được
    // cấp trong phiên. Không có mã cụ thể (hoặc chỉ "*") → hướng dẫn rõ.
    if (widget.source == CloudSource.rapidErp &&
        _devices.isEmpty &&
        !_refreshing) {
      return const _CloudHint(
        icon: Icons.devices_other_outlined,
        text: 'Chưa có máy nào để hiển thị.\n\n'
            'Admin: dán danh sách mã máy ở Cài đặt → RAPID ERP để hiện đầy đủ. '
            'Hoặc gõ mã máy vào ô phía trên rồi bấm "Xem" (vd RPL03010).',
      );
    }

    final list = _visibleDevices;
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(4, 0, 4, 8),
          child: AppSearchBox(
            hint: 'Tìm mã máy…',
            count: _query.trim().isEmpty
                ? null
                : '${list.length}/$_allowedCount',
            onChanged: (v) => setState(() => _query = v),
            trailing: _toolbar,
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(4, 0, 4, 8),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  '${list.length} máy · sắp xếp: ${_sort.label}'
                  '${_cachedAt != null ? ' · cập nhật ${formatTime(_cachedAt!)}' : ''}'
                  '${_refreshing ? ' · đang làm mới…' : ''}',
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
            ],
          ),
        ),
        if (_refreshing) const LinearProgressIndicator(minHeight: 2),
        Expanded(
          child: list.isEmpty
              ? const Center(child: Text('Không có máy nào khớp.'))
              : RefreshIndicator(
                  onRefresh: () => _refresh(fresh: true),
                  child: ListView.separated(
                    padding: const EdgeInsets.fromLTRB(4, 0, 4, 12),
                    itemCount: list.length,
                    separatorBuilder: (_, __) => const SizedBox(height: 8),
                    itemBuilder: (context, i) {
                      final d = list[i];
                      final cs = Theme.of(context).colorScheme;
                      final tt = Theme.of(context).textTheme;
                      // Thẻ theo recipe design system: AppCard + hover nhấc 2px
                      // (bản Flutter của `.card-hover`), vào màn theo stagger.
                      return AppFadeIn(
                        index: i,
                        child: AppCard(
                          hover: true,
                          padding: const EdgeInsets.symmetric(
                              horizontal: 14, vertical: 12),
                          onTap: () => Navigator.push(
                            context,
                            MaterialPageRoute(
                              builder: (_) => CloudRunsScreen(
                                settings: widget.settings,
                                device: d,
                                source: widget.source,
                              ),
                            ),
                          ),
                          child: Row(
                            children: [
                              // Huy hiệu thiết bị: ô bo góc tông màu thương hiệu.
                              Container(
                                width: 42,
                                height: 42,
                                decoration: BoxDecoration(
                                  color: cs.primaryContainer,
                                  borderRadius:
                                      BorderRadius.circular(AppRadius.base),
                                ),
                                child: Icon(Icons.memory,
                                    color: cs.onPrimaryContainer, size: 22),
                              ),
                              const SizedBox(width: 14),
                              Expanded(
                                child: Column(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                    // Mã máy = mã định danh → mono cho dễ đối chiếu.
                                    Text(d.id,
                                        style: tt.titleMedium?.copyWith(
                                          fontFamily: 'JetBrains Mono',
                                          fontWeight: FontWeight.w600,
                                        )),
                                    const SizedBox(height: 2),
                                    // Khổ điện thoại: cả cụm này dài ~50 ký tự trong
                                    // ~240px nên nó tự xuống dòng GIỮA câu ("· mới
                                    // nhất / 19/08/2026 14:12"), đọc như chữ tràn ra
                                    // ngoài thẻ. Chia sẵn 2 dòng theo Ý NGHĨA: máy
                                    // chạy gì / lần cuối khi nào. Cửa sổ rộng vẫn một
                                    // dòng như cũ.
                                    for (final line in [
                                      '${d.runCount} lần chạy'
                                          '${d.version.isNotEmpty ? ' · FW ${d.version}' : ''}'
                                          '${!isMobileWidth(context) && d.latest != null ? ' · mới nhất ${formatDateTime(d.latest!)}' : ''}',
                                      if (isMobileWidth(context) && d.latest != null)
                                        'mới nhất ${formatDateTime(d.latest!)}',
                                    ])
                                      Text(
                                        line,
                                        maxLines: 2,
                                        overflow: TextOverflow.ellipsis,
                                        // Số liệu canh cột đều (tabular) → không nhảy chữ.
                                        style: tt.bodySmall?.copyWith(
                                          color: cs.onSurfaceVariant,
                                          fontFeatures: const [
                                            FontFeature.tabularFigures()
                                          ],
                                        ),
                                      ),
                                  ],
                                ),
                              ),
                              // Tải toàn bộ dữ liệu của máy này. Đặt TRONG thẻ nhưng
                              // là IconButton riêng nên bấm vào đây không kích hoạt
                              // onTap của thẻ (không nhảy sang màn lần chạy).
                              IconButton(
                                tooltip: tr('dl.tooltip'),
                                icon: const Icon(Icons.download_outlined),
                                onPressed: () => showDialog(
                                  context: context,
                                  barrierDismissible: false,
                                  builder: (_) => DownloadDeviceDialog(
                                    settings: widget.settings,
                                    source: widget.source,
                                    device: d,
                                    // Nhịp đọc quyết định trục thời gian của đồ
                                    // thị — không truyền thì ảnh xuất hàng loạt
                                    // lệch trục so với ảnh từ màn chi tiết.
                                    readingIntervalSec:
                                        widget.settings.readingIntervalSec,
                                  ),
                                ),
                              ),
                              Icon(Icons.chevron_right, color: cs.onSurfaceVariant),
                            ],
                          ),
                        ),
                      );
                    },
                  ),
                ),
        ),
      ],
    );
  }
}

class _CloudHint extends StatelessWidget {
  final IconData icon;
  final String text;
  final VoidCallback? onRetry;
  const _CloudHint({required this.icon, required this.text, this.onRetry});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Icon đặt trong đĩa "lõm" (surfaceSunken) — mẫu empty-state template.
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: AppSemantic.of(context).surfaceSunken,
                shape: BoxShape.circle,
              ),
              child: Icon(icon, size: 48, color: cs.onSurfaceVariant),
            ),
            const SizedBox(height: 18),
            Text(text,
                textAlign: TextAlign.center,
                style: Theme.of(context)
                    .textTheme
                    .bodyMedium
                    ?.copyWith(color: cs.onSurfaceVariant)),
            if (onRetry != null) ...[
              const SizedBox(height: 16),
              OutlinedButton.icon(
                onPressed: onRetry,
                icon: const Icon(Icons.refresh),
                label: const Text('Thử lại'),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
