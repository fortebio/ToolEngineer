import 'package:flutter/material.dart';

import '../models/test_result.dart';
import '../services/app_settings.dart';
import '../services/device_api.dart';
import '../services/history_store.dart';
import '../util/format.dart';
import 'result_detail_screen.dart';

class HistoryScreen extends StatefulWidget {
  final AppSettings settings;
  const HistoryScreen({super.key, required this.settings});

  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  final _store = HistoryStore();
  List<TestResult> _items = [];
  bool _loading = true;
  bool _fetching = false;

  @override
  void initState() {
    super.initState();
    _reload();
  }

  Future<void> _reload() async {
    setState(() => _loading = true);
    final items = await _store.load();
    if (!mounted) return;
    setState(() {
      _items = items;
      _loading = false;
    });
  }

  Future<void> _fetchFromDevice() async {
    setState(() => _fetching = true);
    try {
      final api = DeviceApi(widget.settings.deviceIp);
      final result = await api.fetchLatest();
      await _store.add(result);
      await _reload();
      _snack('Đã lấy kết quả từ máy ${result.deviceId}.');
    } catch (e) {
      _snack('$e', error: true);
    } finally {
      if (mounted) setState(() => _fetching = false);
    }
  }

  Future<void> _delete(TestResult r) async {
    await _store.delete(r.id);
    await _reload();
  }

  Future<void> _clearAll() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Xóa toàn bộ lịch sử?'),
        content: const Text('Hành động này không thể hoàn tác.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: const Text('Hủy')),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Xóa hết')),
        ],
      ),
    );
    if (ok == true) {
      await _store.clear();
      await _reload();
    }
  }

  void _snack(String msg, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(msg),
      backgroundColor: error ? Colors.red.shade700 : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Lịch sử xét nghiệm'),
        actions: [
          IconButton(
            tooltip: 'Tải lại',
            onPressed: _reload,
            icon: const Icon(Icons.refresh),
          ),
          if (_items.isNotEmpty)
            IconButton(
              tooltip: 'Xóa tất cả',
              onPressed: _clearAll,
              icon: const Icon(Icons.delete_sweep_outlined),
            ),
        ],
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _fetching ? null : _fetchFromDevice,
        icon: _fetching
            ? const SizedBox(
                width: 18,
                height: 18,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.download),
        label: Text(_fetching ? 'Đang lấy...' : 'Lấy kết quả từ máy'),
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _items.isEmpty
              ? _EmptyState(deviceIp: widget.settings.deviceIp)
              : ListView.separated(
                  padding: const EdgeInsets.fromLTRB(12, 12, 12, 88),
                  itemCount: _items.length,
                  separatorBuilder: (_, __) => const SizedBox(height: 8),
                  itemBuilder: (context, i) => _HistoryTile(
                    result: _items[i],
                    onTap: () => Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => ResultDetailScreen(
                          result: _items[i],
                          readingIntervalSec:
                              widget.settings.readingIntervalSec,
                        ),
                      ),
                    ),
                    onDelete: () => _delete(_items[i]),
                  ),
                ),
    );
  }
}

class _HistoryTile extends StatelessWidget {
  final TestResult result;
  final VoidCallback onTap;
  final VoidCallback onDelete;

  const _HistoryTile({
    required this.result,
    required this.onTap,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final pos = result.countOf(Classification.positive);
    final slight = result.countOf(Classification.slightPositive);
    final neg = result.countOf(Classification.negative);
    final err = result.countOf(Classification.error);

    return Card(
      child: ListTile(
        onTap: onTap,
        leading: const CircleAvatar(child: Icon(Icons.science_outlined)),
        title: Text(formatDateTime(result.timestamp)),
        subtitle: Text(
          'Máy: ${result.deviceId.isEmpty ? "(không rõ)" : result.deviceId}\n'
          'Dương $pos · Dương nhẹ $slight · Âm $neg · Lỗi $err',
        ),
        isThreeLine: true,
        trailing: IconButton(
          tooltip: 'Xóa',
          icon: const Icon(Icons.delete_outline),
          onPressed: onDelete,
        ),
      ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  final String deviceIp;
  const _EmptyState({required this.deviceIp});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.history, size: 64, color: Colors.grey),
            const SizedBox(height: 16),
            const Text(
              'Chưa có lịch sử.',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Text(
              deviceIp.isEmpty
                  ? 'Vào Cài đặt để nhập địa chỉ IP của máy, sau đó bấm "Lấy kết quả từ máy".'
                  : 'Bấm "Lấy kết quả từ máy" để tải kết quả mới nhất từ $deviceIp.',
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
