import 'package:flutter/material.dart';

import '../services/com_names.dart';
import '../services/temperature_serial.dart';
import '../services/temperature_store.dart';
import '../util/chart_capture.dart';
import '../widgets/temp_chart.dart';
import 'all_temp_charts_screen.dart';
import 'saved_charts_screen.dart';
import 'saved_logs_screen.dart';

/// Tab "Nhiệt độ": đọc nhiệt từ UART (COM) của máy RPL, đa COM (đọc ngầm),
/// bấm 1 cổng để xem đồ thị realtime của cổng đó (co giãn theo cửa sổ),
/// lưu log + lưu đồ thị, xem lại log/đồ thị đã lưu.
class TemperatureLogScreen extends StatefulWidget {
  const TemperatureLogScreen({super.key});

  @override
  State<TemperatureLogScreen> createState() => _TemperatureLogScreenState();
}

class _TemperatureLogScreenState extends State<TemperatureLogScreen> {
  final _ctrl = TemperatureLogController();
  final _names = ComNames();
  final Map<String, GlobalKey> _repaintKeys = {};
  String? _active; // cổng đang xem đồ thị

  @override
  void initState() {
    super.initState();
    _ctrl.refreshPorts();
    _names.load().then((_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _ctrl.dispose(); // dừng + đóng mọi cổng
    super.dispose();
  }

  GlobalKey _keyFor(String port) =>
      _repaintKeys.putIfAbsent(port, () => GlobalKey());

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? Colors.red.shade700 : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  /// Bấm 1 cổng: nếu chưa đọc → bắt đầu; rồi chọn làm cổng đang xem.
  void _select(String port) {
    if (!_ctrl.isReading(port)) _ctrl.start(port);
    setState(() => _active = port);
  }

  void _stop(String port) {
    _ctrl.stop(port);
    setState(() {
      if (_active == port) {
        _active =
            _ctrl.readers.keys.isNotEmpty ? _ctrl.readers.keys.first : null;
      }
    });
  }

  void _stopAll() {
    _ctrl.stopAll();
    setState(() => _active = null);
  }

  Future<void> _rename(String port) async {
    final ctrl = TextEditingController(text: _names.nameOf(port));
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text('Đặt tên cho $port'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          decoration: const InputDecoration(
            labelText: 'Tên gợi nhớ (để trống = bỏ tên)',
            hintText: 'vd: Máy Lysis, Buồng A…',
            border: OutlineInputBorder(),
          ),
          onSubmitted: (_) => Navigator.pop(c, true),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: const Text('Hủy')),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Lưu')),
        ],
      ),
    );
    if (ok == true) {
      await _names.setName(port, ctrl.text);
      if (mounted) setState(() {});
    }
  }

  /// Lưu **gộp**: log.json + csv + uart.txt + chart.png vào **1 folder/lần lưu**.
  Future<void> _save(TempPortReader r, GlobalKey key) async {
    if (r.samples.isEmpty) {
      _snack('Chưa có dữ liệu để lưu.', error: true);
      return;
    }
    try {
      final png = await captureBoundaryPng(key); // ảnh đồ thị (có thể null)
      final dir = await TemperatureStore.saveBundle(
        _names.label(r.portName),
        List.of(r.samples),
        rawLines: List.of(r.rawLines),
        chartPng: png,
      );
      final name = dir.path.split('\\').last;
      _snack('Đã lưu log + đồ thị vào folder "$name".');
    } catch (e) {
      _snack('Lỗi lưu: $e', error: true);
    }
  }

  void _open(Widget screen) =>
      Navigator.push(context, MaterialPageRoute(builder: (_) => screen));

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Nhiệt độ (UART)'),
        actions: [
          IconButton(
            tooltip: 'Làm mới cổng',
            onPressed: _ctrl.refreshPorts,
            icon: const Icon(Icons.refresh),
          ),
          IconButton(
            tooltip: 'Dừng tất cả',
            onPressed: _stopAll,
            icon: const Icon(Icons.stop_circle_outlined),
          ),
          IconButton(
            tooltip: 'Tất cả đồ thị (COM hoạt động)',
            onPressed: () => _open(
                AllTempChartsScreen(controller: _ctrl, names: _names)),
            icon: const Icon(Icons.grid_view),
          ),
          PopupMenuButton<int>(
            tooltip: 'Đã lưu',
            icon: const Icon(Icons.history),
            onSelected: (i) => _open(
                i == 0 ? const SavedLogsScreen() : const SavedChartsScreen()),
            itemBuilder: (_) => const [
              PopupMenuItem(value: 0, child: Text('Log đã lưu')),
              PopupMenuItem(value: 1, child: Text('Đồ thị đã lưu')),
            ],
          ),
        ],
      ),
      body: ListenableBuilder(
        listenable: _ctrl,
        builder: (context, _) {
          final active =
              _active != null ? _ctrl.readers[_active] : null;
          return Column(
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text('Cổng COM (bấm để xem đồ thị)',
                        style: TextStyle(
                            fontSize: 15, fontWeight: FontWeight.bold)),
                    const SizedBox(height: 8),
                    if (_ctrl.available.isEmpty)
                      const Text(
                          'Không thấy cổng COM nào. Cắm máy rồi bấm Làm mới.',
                          style: TextStyle(color: Colors.grey))
                    else
                      Wrap(
                        spacing: 8,
                        runSpacing: 8,
                        children: _ctrl.available.map((port) {
                          final reading = _ctrl.isReading(port);
                          final isActive = _active == port;
                          return GestureDetector(
                            onLongPress: () => _rename(port),
                            child: FilterChip(
                              selected: reading,
                              showCheckmark: false,
                              onSelected: (_) => _select(port),
                              tooltip: 'Bấm: xem · Giữ: đổi tên',
                              avatar: Icon(
                                isActive
                                    ? Icons.play_arrow
                                    : (reading ? Icons.usb : Icons.usb_off),
                                size: 18,
                              ),
                              side: isActive
                                  ? BorderSide(
                                      color: Theme.of(context)
                                          .colorScheme
                                          .primary,
                                      width: 2)
                                  : null,
                              label: Text(_names.label(port)),
                            ),
                          );
                        }).toList(),
                      ),
                  ],
                ),
              ),
              const Divider(height: 1),
              Expanded(
                child: active == null
                    ? const Center(
                        child: Padding(
                          padding: EdgeInsets.all(24),
                          child: Text(
                            'Bấm một cổng COM ở trên để xem đồ thị nhiệt realtime.\n'
                            'Có thể bật nhiều cổng cùng lúc (đọc ngầm) và bấm để chuyển xem.',
                            textAlign: TextAlign.center,
                            style: TextStyle(color: Colors.grey),
                          ),
                        ),
                      )
                    : _ActiveChart(
                        key: ValueKey(active.portName),
                        reader: active,
                        label: _names.label(active.portName),
                        repaintKey: _keyFor(active.portName),
                        onSave: () => _save(active, _keyFor(active.portName)),
                        onStop: () => _stop(active.portName),
                        onRename: () => _rename(active.portName),
                      ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// Khu vực đồ thị của cổng đang chọn — chiếm hết không gian còn lại (co giãn).
/// Có thể bật bảng **UART thô** (chia đôi không gian với đồ thị).
class _ActiveChart extends StatefulWidget {
  final TempPortReader reader;
  final String label;
  final GlobalKey repaintKey;
  final VoidCallback onSave;
  final VoidCallback onStop;
  final VoidCallback onRename;

  const _ActiveChart({
    super.key,
    required this.reader,
    required this.label,
    required this.repaintKey,
    required this.onSave,
    required this.onStop,
    required this.onRename,
  });

  @override
  State<_ActiveChart> createState() => _ActiveChartState();
}

class _ActiveChartState extends State<_ActiveChart> {
  bool _showRaw = false;
  // Kênh nhiệt (0..5) đang hiển thị trên đồ thị; bấm chip để ẩn/hiện.
  final Set<int> _visibleCh = {0, 1, 2, 3, 4, 5};

  void _toggleCh(int i) => setState(() {
        if (!_visibleCh.add(i)) _visibleCh.remove(i); // có rồi → bỏ (ẩn)
      });

  @override
  Widget build(BuildContext context) {
    final reader = widget.reader;
    final label = widget.label;
    return ListenableBuilder(
      listenable: reader,
      builder: (context, _) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.thermostat,
                      color: reader.isOpen
                          ? Colors.green
                          : (reader.isReconnecting
                              ? Colors.orange
                              : Colors.grey)),
                  const SizedBox(width: 8),
                  Flexible(
                    child: Text(label,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                            fontWeight: FontWeight.bold, fontSize: 16)),
                  ),
                  IconButton(
                    tooltip: 'Đổi tên cổng',
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.edit, size: 18),
                    onPressed: widget.onRename,
                  ),
                  const SizedBox(width: 8),
                  Text('${reader.samples.length} mẫu',
                      style: Theme.of(context).textTheme.bodySmall),
                  const Spacer(),
                  if (reader.isReconnecting)
                    const Padding(
                      padding: EdgeInsets.only(right: 6),
                      child: SizedBox(
                          width: 14,
                          height: 14,
                          child: CircularProgressIndicator(strokeWidth: 2)),
                    ),
                  if (reader.error != null)
                    Flexible(
                      child: Text(reader.error!,
                          textAlign: TextAlign.end,
                          style: TextStyle(
                              color: reader.isReconnecting
                                  ? Colors.orange.shade800
                                  : Colors.red,
                              fontSize: 12)),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              // Chip mỗi kênh kèm giá trị live; bấm để ẩn/hiện đường trên đồ thị.
              TempChannelBar(
                visible: _visibleCh,
                latest: reader.latest,
                onToggle: _toggleCh,
              ),
              const SizedBox(height: 8),
              // Đồ thị (+ bảng UART thô nếu bật) co giãn theo cửa sổ.
              Expanded(
                child: Column(
                  children: [
                    Expanded(
                      flex: 3,
                      child: RepaintBoundary(
                        key: widget.repaintKey,
                        child: Container(
                          color: Colors.white,
                          padding: const EdgeInsets.all(12),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text('Nhiệt độ — $label',
                                  style: const TextStyle(
                                      fontWeight: FontWeight.bold)),
                              const SizedBox(height: 6),
                              TempLegend(visible: _visibleCh),
                              const SizedBox(height: 8),
                              Expanded(
                                  child: TempChart(
                                      samples: reader.samples,
                                      visibleChannels: _visibleCh)),
                            ],
                          ),
                        ),
                      ),
                    ),
                    if (_showRaw) ...[
                      const SizedBox(height: 8),
                      Expanded(flex: 2, child: _RawUartPanel(reader: reader)),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                children: [
                  OutlinedButton.icon(
                    onPressed: reader.sendToggle,
                    icon: const Icon(Icons.send),
                    label: const Text('Gửi lệnh'),
                  ),
                  OutlinedButton.icon(
                    onPressed: () => setState(() => _showRaw = !_showRaw),
                    icon: Icon(_showRaw ? Icons.expand_more : Icons.terminal),
                    label: Text(_showRaw ? 'Ẩn UART' : 'UART thô'),
                  ),
                  OutlinedButton.icon(
                    onPressed: widget.onSave,
                    icon: const Icon(Icons.save_alt),
                    label: const Text('Lưu'),
                  ),
                  OutlinedButton.icon(
                    onPressed: reader.clearData,
                    icon: const Icon(Icons.clear),
                    label: const Text('Xóa dữ liệu'),
                  ),
                  OutlinedButton.icon(
                    onPressed: widget.onStop,
                    icon: const Icon(Icons.stop),
                    label: const Text('Dừng cổng'),
                  ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Bảng hiển thị **UART thô** đang đọc (live), tự cuộn xuống dòng mới nhất.
class _RawUartPanel extends StatefulWidget {
  final TempPortReader reader;
  const _RawUartPanel({required this.reader});

  @override
  State<_RawUartPanel> createState() => _RawUartPanelState();
}

class _RawUartPanelState extends State<_RawUartPanel> {
  final _scroll = ScrollController();
  bool _tail = true; // tự cuộn theo dòng mới nhất

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF1E1E1E),
        borderRadius: BorderRadius.circular(6),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(10, 4, 4, 0),
            child: Row(
              children: [
                const Icon(Icons.terminal, size: 16, color: Color(0xFF9CDCFE)),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    'UART thô — ${widget.reader.rawLines.length} dòng',
                    style: const TextStyle(
                        color: Color(0xFFD4D4D4),
                        fontSize: 12,
                        fontWeight: FontWeight.bold),
                  ),
                ),
                IconButton(
                  visualDensity: VisualDensity.compact,
                  tooltip: _tail ? 'Đang tự cuộn' : 'Bật tự cuộn xuống cuối',
                  icon: Icon(
                      _tail
                          ? Icons.vertical_align_bottom
                          : Icons.pause_circle_outline,
                      size: 18,
                      color: const Color(0xFF9CDCFE)),
                  onPressed: () => setState(() {
                    _tail = !_tail;
                    if (_tail && _scroll.hasClients) {
                      _scroll.jumpTo(_scroll.position.maxScrollExtent);
                    }
                  }),
                ),
              ],
            ),
          ),
          Expanded(
            child: ListenableBuilder(
              listenable: widget.reader,
              builder: (context, _) {
                final lines = widget.reader.rawLines;
                if (_tail) {
                  WidgetsBinding.instance.addPostFrameCallback((_) {
                    if (_scroll.hasClients) {
                      _scroll.jumpTo(_scroll.position.maxScrollExtent);
                    }
                  });
                }
                if (lines.isEmpty) {
                  return const Center(
                    child: Text('Chưa có dữ liệu UART.',
                        style: TextStyle(color: Colors.grey, fontSize: 12)),
                  );
                }
                return Scrollbar(
                  controller: _scroll,
                  thumbVisibility: true,
                  child: ListView.builder(
                    controller: _scroll,
                    padding: const EdgeInsets.fromLTRB(10, 0, 10, 8),
                    itemCount: lines.length,
                    itemBuilder: (c, i) => Text(
                      lines[i],
                      style: const TextStyle(
                        fontFamily: 'monospace',
                        fontSize: 11,
                        color: Color(0xFFD4D4D4),
                        height: 1.3,
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}
