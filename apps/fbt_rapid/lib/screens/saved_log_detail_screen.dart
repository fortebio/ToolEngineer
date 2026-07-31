import 'dart:io';

import 'package:flutter/material.dart';

import '../services/temperature_serial.dart';
import '../services/temperature_store.dart';
import '../util/chart_capture.dart';
import '../util/format.dart';
import '../widgets/temp_chart.dart';

/// Xem lại 1 phiên log đã lưu: vẽ lại đồ thị + thống kê, lưu được ảnh đồ thị.
class SavedLogDetailScreen extends StatefulWidget {
  final File file;
  const SavedLogDetailScreen({super.key, required this.file});

  @override
  State<SavedLogDetailScreen> createState() => _SavedLogDetailScreenState();
}

class _SavedLogDetailScreenState extends State<SavedLogDetailScreen> {
  final _repaintKey = GlobalKey();
  LoadedLog? _log;
  String? _error;
  // Kênh nhiệt (0..5) đang hiển thị trên đồ thị; bấm chip để ẩn/hiện.
  final Set<int> _visibleCh = {0, 1, 2, 3, 4, 5};

  void _toggleCh(int i) => setState(() {
        if (!_visibleCh.add(i)) _visibleCh.remove(i); // có rồi → bỏ (ẩn)
      });

  @override
  void initState() {
    super.initState();
    try {
      _log = TemperatureStore.loadSession(widget.file);
    } catch (e) {
      _error = '$e';
    }
  }

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? Theme.of(context).colorScheme.error : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  Future<void> _saveChart() async {
    if (_log == null) return;
    try {
      final png = await captureBoundaryPng(_repaintKey);
      if (png == null) {
        _snack('Chưa chụp được đồ thị.', error: true);
        return;
      }
      // ghi/cập nhật chart.png ngay trong folder của bản ghi này
      await TemperatureStore.chartFileFor(widget.file).writeAsBytes(png);
      _snack('Đã cập nhật ảnh đồ thị trong folder.');
    } catch (e) {
      _snack('Lỗi lưu đồ thị: $e', error: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(
        appBar: AppBar(title: const Text('Log đã lưu')),
        body: Center(child: Text('Lỗi đọc file:\n$_error')),
      );
    }
    final log = _log;
    if (log == null) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }

    final samples = log.samples;
    final durationMin = samples.length >= 2
        ? (samples.last.t - samples.first.t) / 60.0
        : 0.0;

    return Scaffold(
      appBar: AppBar(
        title: Text('Log ${log.port} · ${formatDateTime(log.savedAt)}'),
        actions: [
          IconButton(
            tooltip: 'Cập nhật ảnh đồ thị (chart.png)',
            onPressed: _saveChart,
            icon: const Icon(Icons.image_outlined),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              '${samples.length} mẫu · ${durationMin.toStringAsFixed(1)} phút · máy ${log.port}',
              style: Theme.of(context).textTheme.bodyMedium,
            ),
            const SizedBox(height: 12),
            // Chip bật/tắt từng kênh (ngoài vùng chụp ảnh).
            TempChannelBar(visible: _visibleCh, onToggle: _toggleCh),
            const SizedBox(height: 8),
            // Đồ thị chiếm hết không gian còn lại → co giãn theo cửa sổ.
            Expanded(
              child: RepaintBoundary(
                key: _repaintKey,
                child: Container(
                  color: Colors.white,
                  padding: const EdgeInsets.all(12),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      TempLegend(visible: _visibleCh),
                      const SizedBox(height: 8),
                      Expanded(
                          child: TempChart(
                              samples: samples, visibleChannels: _visibleCh)),
                    ],
                  ),
                ),
              ),
            ),
            const SizedBox(height: 12),
            ConstrainedBox(
              constraints: const BoxConstraints(maxHeight: 220),
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Thống kê',
                        style: Theme.of(context).textTheme.titleMedium),
                    const SizedBox(height: 8),
                    _StatsTable(samples: samples),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _StatsTable extends StatelessWidget {
  final List<TempSample> samples;
  const _StatsTable({required this.samples});

  @override
  Widget build(BuildContext context) {
    final rows = <DataRow>[];
    for (var ch = 0; ch < 6; ch++) {
      double? mn, mx, last;
      for (final s in samples) {
        final v = ch < s.v.length ? s.v[ch] : null;
        if (v == null) continue;
        mn = (mn == null || v < mn) ? v : mn;
        mx = (mx == null || v > mx) ? v : mx;
        last = v;
      }
      String f(double? v) => v == null ? '--' : v.toStringAsFixed(1);
      rows.add(DataRow(cells: [
        DataCell(Row(mainAxisSize: MainAxisSize.min, children: [
          CircleAvatar(backgroundColor: kTempColors[ch], radius: 6),
          const SizedBox(width: 6),
          Text(kTempChannels[ch]),
        ])),
        DataCell(Text(f(mn))),
        DataCell(Text(f(mx))),
        DataCell(Text(f(last))),
      ]));
    }
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: DataTable(
        columns: const [
          DataColumn(label: Text('Kênh')),
          DataColumn(label: Text('Min')),
          DataColumn(label: Text('Max')),
          DataColumn(label: Text('Cuối')),
        ],
        rows: rows,
      ),
    );
  }
}
