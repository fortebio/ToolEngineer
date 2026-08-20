import 'dart:io';

import 'package:flutter/material.dart';

import '../services/temperature_store.dart';

/// Danh sách ảnh đồ thị đã lưu (PNG) → bấm để xem to.
/// Tên phiên đo của một ảnh đồ thị đã lưu.
///
/// `TemperatureStore.listCharts()` quét từng thư mục con của `FBT_RAPID_templog`
/// và chỉ lấy đúng file tên `chart.png` trong đó — nên **mọi ảnh đều trùng tên**.
/// Thứ phân biệt các phiên là TÊN THƯ MỤC CHA (`<COM>_<yyyyMMdd_HHmmssSSS>`, đặt
/// ở `saveBundle`). Dùng `pathSegments.last` như trước là in ra "chart.png" cho
/// mọi dòng: danh sách 5 phiên thành 5 dòng chữ y hệt nhau.
///
/// Đi qua `uri` chứ không tách chuỗi theo dấu phân cách: dự án không có package
/// `path`, và `uri.pathSegments` đúng trên cả Windows lẫn nơi khác. Thư mục cho
/// phần tử cuối rỗng nên phải bỏ phần tử rỗng.
String _tenPhien(FileSystemEntity f) {
  final seg = f.parent.uri.pathSegments.where((e) => e.isNotEmpty);
  return seg.isEmpty ? f.uri.pathSegments.last : seg.last;
}

class SavedChartsScreen extends StatefulWidget {
  const SavedChartsScreen({super.key});

  @override
  State<SavedChartsScreen> createState() => _SavedChartsScreenState();
}

class _SavedChartsScreenState extends State<SavedChartsScreen> {
  List<File> _items = [];

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  void _refresh() => setState(() => _items = TemperatureStore.listCharts());

  Future<void> _delete(File f) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Xoá ảnh này?'),
        content: Text(_tenPhien(f)),
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
      TemperatureStore.deleteFile(f);
      _refresh();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Đồ thị đã lưu'),
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
          ? Center(
              child: Text('Chưa có ảnh đồ thị nào.',
                  style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant)))
          : ListView.separated(
              padding: const EdgeInsets.all(12),
              itemCount: _items.length,
              separatorBuilder: (_, __) => const SizedBox(height: 12),
              itemBuilder: (context, i) {
                final f = _items[i];
                return Card(
                  clipBehavior: Clip.antiAlias,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      InkWell(
                        onTap: () => Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (_) => _ChartViewer(file: f),
                          ),
                        ),
                        child: Image.file(
                          f,
                          height: 220,
                          fit: BoxFit.contain,
                          errorBuilder: (_, __, ___) => const SizedBox(
                            height: 220,
                            child: Center(child: Icon(Icons.broken_image)),
                          ),
                        ),
                      ),
                      ListTile(
                        dense: true,
                        title: Text(_tenPhien(f),
                            style: const TextStyle(fontSize: 12)),
                        trailing: IconButton(
                          tooltip: 'Xoá',
                          icon: const Icon(Icons.delete_outline),
                          onPressed: () => _delete(f),
                        ),
                      ),
                    ],
                  ),
                );
              },
            ),
    );
  }
}

/// Xem ảnh to, zoom/pan được.
class _ChartViewer extends StatelessWidget {
  final File file;
  const _ChartViewer({required this.file});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
        title: Text(_tenPhien(file)),
      ),
      body: Center(
        child: InteractiveViewer(
          minScale: 0.5,
          maxScale: 5,
          child: Image.file(file),
        ),
      ),
    );
  }
}
