import 'package:flutter/material.dart';

import '../services/com_names.dart';
import '../services/temperature_serial.dart';
import '../widgets/temp_chart.dart';

/// Xem ĐỒNG THỜI đồ thị realtime của tất cả các cổng COM đang hoạt động.
class AllTempChartsScreen extends StatelessWidget {
  final TemperatureLogController controller;
  final ComNames names;

  const AllTempChartsScreen({
    super.key,
    required this.controller,
    required this.names,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Tất cả đồ thị (COM hoạt động)')),
      body: ListenableBuilder(
        listenable: controller,
        builder: (context, _) {
          final readers = controller.readers.values.toList();
          if (readers.isEmpty) {
            return const Center(
              child: Padding(
                padding: EdgeInsets.all(24),
                child: Text(
                  'Chưa có cổng COM nào đang hoạt động.\n'
                  'Bật cổng ở tab Nhiệt độ rồi quay lại đây.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Colors.grey),
                ),
              ),
            );
          }
          return LayoutBuilder(builder: (context, c) {
            final twoCol = c.maxWidth >= 900;
            final w = twoCol ? (c.maxWidth - 16 * 3) / 2 : c.maxWidth - 32;
            return SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Wrap(
                spacing: 16,
                runSpacing: 16,
                children: [
                  for (final r in readers)
                    SizedBox(
                      width: w,
                      child: _ChartCard(reader: r, label: names.label(r.portName)),
                    ),
                ],
              ),
            );
          });
        },
      ),
    );
  }
}

class _ChartCard extends StatelessWidget {
  final TempPortReader reader;
  final String label;
  const _ChartCard({required this.reader, required this.label});

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: reader,
      builder: (context, _) {
        return Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.thermostat,
                        size: 20,
                        color: reader.isOpen
                            ? Colors.green
                            : (reader.isReconnecting
                                ? Colors.orange
                                : Colors.grey)),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(label,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(fontWeight: FontWeight.bold)),
                    ),
                    if (reader.isReconnecting)
                      const SizedBox(
                          width: 14,
                          height: 14,
                          child: CircularProgressIndicator(strokeWidth: 2)),
                  ],
                ),
                const SizedBox(height: 6),
                Wrap(
                  spacing: 8,
                  runSpacing: 2,
                  children: List.generate(6, (i) {
                    final v = reader.latest[i];
                    return Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        CircleAvatar(
                            backgroundColor: kTempColors[i], radius: 5),
                        const SizedBox(width: 3),
                        Text(
                          '${kTempChannels[i]} '
                          '${v == null ? '--' : v.toStringAsFixed(1)}',
                          style: const TextStyle(fontSize: 11),
                        ),
                      ],
                    );
                  }),
                ),
                const SizedBox(height: 8),
                SizedBox(height: 240, child: TempChart(samples: reader.samples)),
              ],
            ),
          ),
        );
      },
    );
  }
}
