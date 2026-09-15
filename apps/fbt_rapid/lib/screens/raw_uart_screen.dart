import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
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
              // AppBar nền `surface` (sáng) → chữ trắng cũ gần như vô hình.
              child: Text('${lines.length} dòng',
                  style: TextStyle(
                      fontSize: 12,
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                      fontFeatures: const [FontFeature.tabularFigures()])),
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
        // Nền LUÔN tối như terminal — cùng token với log esptool.
        color: AppSemantic.of(context).consoleBg,
        child: lines.isEmpty
            ? Center(
                // `onSurfaceVariant` là màu chữ mờ của theme SÁNG (#6B7280);
                // đặt lên nền console tối chỉ còn ~3.4:1 — dưới chuẩn AA và
                // gần như không đọc được. Dùng màu mờ RIÊNG của console (7.45:1).
                child: Text('Không có dữ liệu UART.',
                    style: TextStyle(color: AppSemantic.of(context).consoleDim)))
            : ScrollbarTheme(
                // Thumb mặc định lấy `onSurfaceVariant` pha alpha 0.3 — màu tính
                // cho nền `surface` SÁNG. Ở đây nền luôn là `consoleBg` tối nên
                // nó chỉ 1.4–1.8:1: thanh cuộn chìm hẳn vào nền đen, đúng ở màn
                // duy nhất cần dò log dài. Lấy màu từ chính bộ console.
                data: ScrollbarThemeData(
                  thumbColor: WidgetStatePropertyAll(
                      AppSemantic.of(context).consoleDim),
                ),
                child: Scrollbar(
                thumbVisibility: true,
                child: ListView.builder(
                  padding: const EdgeInsets.all(12),
                  itemCount: lines.length,
                  itemBuilder: (c, i) => SelectableText(
                    lines[i],
                    // Nền console LUÔN tối (như terminal) → màu chữ cố định,
                    // không theo theme; font mono chuẩn của design system.
                    style: TextStyle(
                      fontFamily: 'JetBrains Mono',
                      fontSize: 12,
                      color: AppSemantic.of(context).consoleFg,
                      height: 1.35,
                    ),
                  ),
                ),
              ),
              ),
      ),
    );
  }
}
