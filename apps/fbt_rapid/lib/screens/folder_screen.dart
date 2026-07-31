import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../util/i18n.dart';
import 'json_files_screen.dart';

/// Tab **Thư Mục** (mọi vai trò) — gom các mục duyệt dữ liệu dạng file.
/// Hiện có 1 mục con: **JSON data** (file JSON thiết bị đẩy lên Engineer
/// Server, xem thô). Mẫu segmented giống tab Lịch sử/Kỹ Thuật — thêm mục con
/// mới chỉ việc thêm ButtonSegment + màn vào IndexedStack.
class FolderScreen extends StatefulWidget {
  final AppSettings settings;
  const FolderScreen({super.key, required this.settings});

  @override
  State<FolderScreen> createState() => _FolderScreenState();
}

class _FolderScreenState extends State<FolderScreen> {
  int _seg = 0; // 0 = JSON data

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
              child: SegmentedButton<int>(
                showSelectedIcon: false,
                segments: [
                  ButtonSegment(
                    value: 0,
                    icon: const Icon(Icons.data_object_outlined),
                    label: Text(tr('folder.jsonData')),
                  ),
                ],
                selected: {_seg},
                onSelectionChanged: (sel) => setState(() => _seg = sel.first),
              ),
            ),
            Expanded(
              child: IndexedStack(
                index: _seg,
                children: [JsonFilesScreen(settings: widget.settings)],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
