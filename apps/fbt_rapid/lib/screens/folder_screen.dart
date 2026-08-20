import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';
import 'json_files_screen.dart';

/// Tab **Thư Mục** (mọi vai trò) — gom các mục duyệt dữ liệu dạng file.
/// Hiện có 1 mục con: **JSON data**. Thêm mục mới = thêm một [AppTab];
/// dải chọn tự hiện khi có từ 2 mục trở lên.
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
    return AppTabScaffold(
      title: tr('nav.folder'),
      subtitle: tr('folder.hint'),
      index: _seg,
      onChanged: (i) => setState(() => _seg = i),
      tabs: [
        AppTab(
          icon: Icons.data_object_outlined,
          label: tr('folder.jsonData'),
          page: JsonFilesScreen(settings: widget.settings),
        ),
      ],
    );
  }
}
