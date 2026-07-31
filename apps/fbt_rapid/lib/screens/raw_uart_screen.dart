import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Xem dữ liệu UART thô (dạng văn bản): cuộn được, chọn/sao chép.
class RawUartScreen extends StatelessWidget {
  final String title;
  final String text;
  const RawUartScreen({super.key, required this.title, required this.text});

  @override
  Widget build(BuildContext context) {
    final lines = text.isEmpty ? const <String>[] : text.split('\n');
    return Scaffold(
      appBar: AppBar(
        title: Text(title),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Center(
              child: Text('${lines.length} dòng',
                  style: const TextStyle(fontSize: 12, color: Colors.white70)),
            ),
          ),
          IconButton(
            tooltip: 'Sao chép tất cả',
            icon: const Icon(Icons.copy_all),
            onPressed: () {
              Clipboard.setData(ClipboardData(text: text));
              ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
                  content: Text('Đã sao chép UART vào clipboard.')));
            },
          ),
        ],
      ),
      body: Container(
        color: const Color(0xFF1E1E1E),
        child: lines.isEmpty
            ? Center(
                child: Text('Không có dữ liệu UART.',
                    style: TextStyle(
                        color: Theme.of(context).colorScheme.onSurfaceVariant)))
            : Scrollbar(
                thumbVisibility: true,
                child: ListView.builder(
                  padding: const EdgeInsets.all(12),
                  itemCount: lines.length,
                  itemBuilder: (c, i) => SelectableText(
                    lines[i],
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 12,
                      color: Color(0xFFD4D4D4),
                      height: 1.35,
                    ),
                  ),
                ),
              ),
      ),
    );
  }
}
