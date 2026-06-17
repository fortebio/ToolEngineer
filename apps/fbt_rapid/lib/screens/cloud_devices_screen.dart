import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/cloud_cache.dart';
import '../services/cloud_history_api.dart';
import '../util/format.dart';
import 'cloud_runs_screen.dart';

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
  const CloudDevicesScreen({super.key, required this.settings});

  @override
  State<CloudDevicesScreen> createState() => _CloudDevicesScreenState();
}

class _CloudDevicesScreenState extends State<CloudDevicesScreen> {
  static const _ttl = Duration(minutes: 5);

  late final CloudHistoryApi _api =
      CloudHistoryApi(widget.settings.cloudApiUrl);
  final _cache = CloudCache();
  List<CloudDevice> _devices = [];
  bool _loading = false; // tải lần đầu (chưa có gì để hiện)
  bool _refreshing = false; // đang làm mới ngầm (đã có dữ liệu cũ)
  DateTime? _cachedAt; // thời điểm dữ liệu đang hiện được lấy
  String? _error;
  String _query = '';
  _DeviceSort _sort = _DeviceSort.latestDesc;

  String get _url => widget.settings.cloudApiUrl;

  @override
  void initState() {
    super.initState();
    if (_url.isNotEmpty) _init();
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
          backgroundColor: Colors.orange.shade800,
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

  /// Lọc theo ô tìm kiếm rồi sắp xếp theo [_sort].
  List<CloudDevice> get _visibleDevices {
    final q = _query.trim().toLowerCase();
    final list = q.isEmpty
        ? List<CloudDevice>.from(_devices)
        : _devices.where((d) => d.id.toLowerCase().contains(q)).toList();
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
    return Scaffold(
      appBar: AppBar(
        title: const Text('Lịch sử theo máy (Cloud)'),
        actions: [
          PopupMenuButton<_DeviceSort>(
            tooltip: 'Sắp xếp',
            icon: const Icon(Icons.sort),
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
            tooltip: 'Làm mới (lấy dữ liệu mới nhất)',
            onPressed: _refreshing ? null : () => _refresh(fresh: true),
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: _buildBody(),
    );
  }

  Widget _buildBody() {
    if (widget.settings.cloudApiUrl.isEmpty) {
      return const _CloudHint(
        icon: Icons.cloud_off_outlined,
        text: 'Chưa cấu hình URL cloud.\n\n'
            'Vào Cài đặt → "Lịch sử đám mây" để dán URL Apps Script /exec.',
      );
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

    final list = _visibleDevices;
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 12, 12, 8),
          child: TextField(
            decoration: const InputDecoration(
              hintText: 'Tìm mã máy...',
              prefixIcon: Icon(Icons.search),
              border: OutlineInputBorder(),
              isDense: true,
            ),
            onChanged: (v) => setState(() => _query = v),
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  '${list.length} máy · sắp xếp: ${_sort.label}'
                  '${_cachedAt != null ? ' · cập nhật ${formatTime(_cachedAt!)}' : ''}',
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
              if (_refreshing)
                Text('đang làm mới…',
                    style: Theme.of(context).textTheme.bodySmall),
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
                    padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
                    itemCount: list.length,
                    separatorBuilder: (_, __) => const SizedBox(height: 8),
                    itemBuilder: (context, i) {
                      final d = list[i];
                      return Card(
                        child: ListTile(
                          leading:
                              const CircleAvatar(child: Icon(Icons.memory)),
                          title: Text(d.id,
                              style: const TextStyle(
                                  fontWeight: FontWeight.bold)),
                          subtitle: Text(
                            '${d.runCount} lần chạy'
                            '${d.version.isNotEmpty ? ' · FW ${d.version}' : ''}'
                            '${d.latest != null ? ' · mới nhất ${formatDateTime(d.latest!)}' : ''}',
                          ),
                          trailing: const Icon(Icons.chevron_right),
                          onTap: () => Navigator.push(
                            context,
                            MaterialPageRoute(
                              builder: (_) => CloudRunsScreen(
                                settings: widget.settings,
                                device: d,
                              ),
                            ),
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
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 64, color: Colors.grey),
            const SizedBox(height: 16),
            Text(text, textAlign: TextAlign.center),
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
