import 'package:flutter/material.dart';

import '../util/i18n.dart';

/// Ô tìm kiếm dùng chung cho các màn có danh sách dài.
///
/// Lọc **trên danh sách đã tải sẵn trong RAM**, không gọi lại server: kho `.bin`,
/// bảng máy, danh sách thiết bị cloud đều đã nằm trong `State` rồi, và ~100 phần
/// tử thì lọc là tức thì — thêm debounce hay endpoint tìm kiếm ở đây chỉ là bộ
/// phận chuyển động không mua được gì.
///
/// Trước 2026-08-19 widget này là `_SearchBox` riêng trong
/// `manager_machine_screen.dart`. Chỗ thứ ba cần nó (Lịch sử cloud) là lúc tách ra.
class AppSearchBox extends StatefulWidget {
  final String hint;
  final ValueChanged<String> onChanged;

  /// `<số khớp>/<tổng>`, chỉ hiện khi đang lọc — nếu không người dùng không biết
  /// cái mình đang nhìn là toàn bộ hay một phần.
  final String? count;

  /// Nút đặt bên phải, CÙNG hàng với ô tìm (làm mới, sắp xếp…).
  ///
  /// Cùng hàng chứ không phải hàng riêng: một nút biểu tượng đứng một mình
  /// chiếm trọn chiều ngang màn hình là khoảng trống chết, và trên bảng dài thì
  /// mỗi hàng bỏ đi là một dòng dữ liệu đọc được thêm.
  final List<Widget> trailing;

  const AppSearchBox({
    super.key,
    required this.hint,
    required this.onChanged,
    this.count,
    this.trailing = const [],
  });

  @override
  State<AppSearchBox> createState() => _AppSearchBoxState();
}

class _AppSearchBoxState extends State<AppSearchBox> {
  final _c = TextEditingController();

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  void _set(String v) {
    widget.onChanged(v);
    setState(() {}); // chỉ để hiện/ẩn nút xoá
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Row(
      children: [
        Expanded(
          child: TextField(
            controller: _c,
            onChanged: _set,
            textInputAction: TextInputAction.search,
            decoration: InputDecoration(
              isDense: true,
              hintText: widget.hint,
              prefixIcon: const Icon(Icons.search, size: 20),
              suffixIcon: _c.text.isEmpty
                  ? null
                  : IconButton(
                      tooltip: tr('common.cancel'),
                      icon: const Icon(Icons.close, size: 18),
                      onPressed: () {
                        _c.clear();
                        _set('');
                      },
                    ),
              border: const OutlineInputBorder(),
            ),
          ),
        ),
        if (widget.count != null) ...[
          const SizedBox(width: 12),
          Text(widget.count!,
              style: TextStyle(
                fontSize: 12,
                color: cs.onSurfaceVariant,
                fontFeatures: const [FontFeature.tabularFigures()],
              )),
        ],
        ...widget.trailing,
      ],
    );
  }
}
