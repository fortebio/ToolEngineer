import 'package:flutter/material.dart';

import '../services/temperature_store.dart';
import '../util/format.dart';
import 'raw_uart_screen.dart';
import 'saved_log_detail_screen.dart';

/// Danh sách các phiên log nhiệt đã lưu → bấm để xem lại đồ thị.
class SavedLogsScreen extends StatefulWidget {
  const SavedLogsScreen({super.key});

  @override
  State<SavedLogsScreen> createState() => _SavedLogsScreenState();
}

class _SavedLogsScreenState extends State<SavedLogsScreen> {
  List<SavedLog> _items = [];

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  void _refresh() => setState(() => _items = TemperatureStore.listSessions());

  Future<void> _delete(SavedLog s) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Xoá log này?'),
        content: Text(formatDateTime(s.savedAt)),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: const Text('Hủy')),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Xoá')),
        ],
      ),
    );
    if (ok == true) {
      TemperatureStore.deleteSession(s.file);
      _refresh();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Log đã lưu'),
        actions: [
          IconButton(
            tooltip: 'Mở thư mục',
            onPressed: () =>
                TemperatureStore.revealInExplorer(TemperatureStore.rootDir),
            icon: const Icon(Icons.folder_open),
          ),
          IconButton(
            tooltip: 'Tải lại',
            onPressed: _refresh,
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: _items.isEmpty
          ? const Center(
              child: Text('Chưa có log nào được lưu.',
                  style: TextStyle(color: Colors.grey)))
          : ListView.separated(
              padding: const EdgeInsets.all(12),
              itemCount: _items.length,
              separatorBuilder: (_, __) => const SizedBox(height: 8),
              itemBuilder: (context, i) {
                final s = _items[i];
                return Card(
                  child: ListTile(
                    leading: const CircleAvatar(child: Icon(Icons.thermostat)),
                    title: Text(formatDateTime(s.savedAt)),
                    subtitle: Text('Máy ${s.port} · ${s.count} mẫu'
                        '${s.hasChart ? ' · có đồ thị' : ''}'
                        '${s.hasRaw ? ' · có UART' : ''}'),
                    trailing: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        if (s.hasRaw)
                          IconButton(
                            tooltip: 'Xem UART',
                            icon: const Icon(Icons.terminal),
                            onPressed: () => Navigator.push(
                              context,
                              MaterialPageRoute(
                                builder: (_) => RawUartScreen(
                                  title: 'UART — ${s.port}',
                                  text: TemperatureStore.loadRawText(s.file) ??
                                      '',
                                ),
                              ),
                            ),
                          ),
                        IconButton(
                          tooltip: 'Xoá',
                          icon: const Icon(Icons.delete_outline),
                          onPressed: () => _delete(s),
                        ),
                      ],
                    ),
                    onTap: () => Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => SavedLogDetailScreen(file: s.file),
                      ),
                    ),
                  ),
                );
              },
            ),
    );
  }
}
