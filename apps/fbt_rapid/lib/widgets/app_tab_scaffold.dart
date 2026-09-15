import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Một mục con trong khuôn [AppTabScaffold].
class AppTab {
  final IconData icon;
  final String label;
  final Widget page;
  const AppTab({required this.icon, required this.label, required this.page});
}

/// Khuôn dùng chung cho các tab có **mục con**: tiêu đề màn + dải chọn mục +
/// nội dung. Hiện dùng ở *Kỹ Thuật* (desktop + web) và *Thư Mục*; *Lịch sử* và
/// *Quản lý máy* sẽ dùng ở đợt sau.
///
/// Vì sao tách ra: năm màn đang tự dựng cùng một `Padding(12,8,12,4)` +
/// `SegmentedButton` + `IndexedStack`. Năm bản sao của một bố cục là năm chỗ
/// phải sửa mỗi lần chỉnh nhịp, và trên thực tế chúng đã lệch nhau.
///
/// Khuôn này cũng bù lại một thứ **thanh điều hướng mới lấy đi**: app không có
/// AppBar, nên nếu không có tiêu đề ở đây thì màn hình mở ra bằng một dải nút
/// trôi lơ lửng, không nói mình là màn gì.
class AppTabScaffold extends StatelessWidget {
  final String title;

  /// Câu một dòng dưới tiêu đề. Để trống nếu màn tự nói được nó là gì.
  final String? subtitle;
  final List<AppTab> tabs;
  final int index;
  final ValueChanged<int> onChanged;

  /// Nút/điều khiển nằm bên phải hàng tiêu đề (làm mới, tải lên…).
  final List<Widget> actions;

  /// Chỉ dựng mục ĐANG chọn, các mục khác là chỗ trống.
  ///
  /// Mặc định `false` — `IndexedStack` dựng hết và GIỮ state, đúng cho mục con
  /// nắm phần cứng (3 màn Kỹ Thuật dùng chung cổng COM, chuyển mục mà mất state
  /// là mất luôn buffer log).
  ///
  /// Bật `true` khi mỗi mục **tự mở kết nối riêng**: *Lịch sử* có 3 nguồn cloud,
  /// dựng cả 3 là bắn 3 request cùng lúc tới 3 server khác nhau ngay khi mở tab
  /// — hai trong số đó người dùng không hề xem, và nguồn chưa cấu hình còn dựng
  /// sẵn một màn báo lỗi.
  final bool lazy;

  const AppTabScaffold({
    super.key,
    required this.title,
    required this.tabs,
    required this.index,
    required this.onChanged,
    this.subtitle,
    this.actions = const [],
    this.lazy = false,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    // Lề 28px viết cho màn rộng; trên điện thoại 390px nó ăn 14% bề ngang.
    final mobile = isMobileWidth(context);
    final padX = mobile ? 16.0 : 28.0;

    return Scaffold(
      backgroundColor: Colors.transparent, // nền do scaffold gốc của app lo
      body: SafeArea(
        bottom: false,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: EdgeInsets.fromLTRB(padX, mobile ? 14 : 22, padX, mobile ? 10 : 16),
              // `Wrap` chứ KHÔNG phải `Row`. Bản trước là `Row` với cụm hành
              // động bọc trong `Wrap` kèm comment "nó xuống dòng thay vì tràn"
              // — comment đó SAI: `RenderFlex` cấp cho con không-flex ràng buộc
              // `maxWidth: infinity`, nên `Wrap` bên trong luôn thấy bề rộng vô
              // hạn và không bao giờ ngắt dòng; phần vượt ra thì `Expanded` của
              // tiêu đề bị ép về 0 và `Row` tràn (sọc vàng-đen). Con của `Wrap`
              // thì nhận `maxWidth` thật, nên ở đây nó xuống dòng đúng như mong.
              child: Wrap(
                spacing: 16,
                runSpacing: 12,
                alignment: WrapAlignment.spaceBetween,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  ConstrainedBox(
                    constraints: const BoxConstraints(minWidth: 200),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        // Khổ điện thoại: tiêu đề đã nằm trên thanh trên của
                        // `HomeShell` — in lại ở đây là cùng một chữ hai lần,
                        // ngay dưới nhau. Phụ đề thì GIỮ, nó nói thêm điều khác.
                        if (!mobile)
                          Text(
                            title,
                            style: TextStyle(
                              fontSize: 23,
                              fontWeight: FontWeight.w700,
                              letterSpacing: -0.35,
                              color: cs.onSurface,
                            ),
                          ),
                        if (subtitle != null) ...[
                          if (!mobile) const SizedBox(height: 4),
                          Text(
                            subtitle!,
                            style: TextStyle(
                                fontSize: 13.5, color: cs.onSurfaceVariant),
                          ),
                        ],
                      ],
                    ),
                  ),
                  if (actions.isNotEmpty)
                    Wrap(
                      spacing: 8,
                      runSpacing: 8,
                      crossAxisAlignment: WrapCrossAlignment.center,
                      children: actions,
                    ),
                ],
              ),
            ),
            // Dải chọn mục chỉ hiện khi CÓ mục để chọn. Một nút đơn độc trông
            // như điều khiển hỏng — Thư Mục hiện đúng ở trạng thái đó.
            if (tabs.length > 1)
              Padding(
                padding: EdgeInsets.fromLTRB(padX, 0, padX, mobile ? 12 : 18),
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: SingleChildScrollView(
                    scrollDirection: Axis.horizontal,
                    child: SegmentedButton<int>(
                      showSelectedIcon: false,
                      segments: [
                        for (var i = 0; i < tabs.length; i++)
                          ButtonSegment(
                            value: i,
                            icon: Icon(tabs[i].icon, size: 18),
                            label: Text(tabs[i].label),
                          ),
                      ],
                      selected: {index},
                      onSelectionChanged: (s) => onChanged(s.first),
                    ),
                  ),
                ),
              )
            else
              const SizedBox(height: 2),
            Expanded(
              child: Padding(
                // Chừa 4px trên: nhãn nổi của ô nhập/ô chọn nhô LÊN TRÊN viền
                // ô, chạm mép `ClipRRect` là bị cắt mất nửa trên (gặp thật ở
                // "Cổng COM"/"Baud" trong tab Kỹ Thuật).
                padding: EdgeInsets.fromLTRB(
                    mobile ? 10 : 20, 4, mobile ? 10 : 20, mobile ? 10 : 20),
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(AppRadius.card),
                  child: IndexedStack(
                    index: index,
                    // `TickerMode` = cờ "mục này đang được xem", CÙNG cơ chế
                    // HomeShell dùng cho tab cấp trên: `IndexedStack` dựng hết
                    // con và con bị ẩn vẫn tick (spinner, AppFadeIn, timer vẽ)
                    // nếu không tắt. Màn con cần dừng poll/nhả phần cứng khi ẩn
                    // đọc `TickerMode.valuesOf(context).enabled` trong
                    // `didChangeDependencies` (mẫu monitor_screen.dart) — không
                    // cần luồn cờ `active` riêng qua từng tầng nữa.
                    children: [
                      for (var i = 0; i < tabs.length; i++)
                        if (!lazy || i == index)
                          TickerMode(enabled: i == index, child: tabs[i].page)
                        else
                          const SizedBox.shrink(),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
