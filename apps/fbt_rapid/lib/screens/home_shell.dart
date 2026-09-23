import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../services/app_settings.dart';
import '../services/session_store.dart';
import '../services/storage_paths.dart';
import '../theme/app_theme.dart';
import '../util/app_version.dart';
import '../util/i18n.dart';
import '../util/serial_support.dart';
// Tab Sản xuất (ATE): bản desktop chạy trạm (esptool + cổng COM qua dart:io);
// bản web chỉ tra hồ sơ + thống kê (HTTP) — cùng tên class, khác nội dung.
import 'ate_screen.dart' if (dart.library.html) 'ate_screen_web.dart';
import 'calib_screen.dart';
import 'folder_screen.dart';
import 'history_combined_screen.dart';
import 'login_screen.dart';
import 'manager_machine_screen.dart';
import 'monitor_screen.dart';
import 'support_screen.dart';
// Tab Kỹ Thuật: desktop dùng COM/esptool (dart:ffi/dart:io); web import bản
// Web Serial + esptool-js (tech_screen_web.dart) — không kéo native vào web.
import 'tech_screen.dart' if (dart.library.html) 'tech_screen_web.dart';
import 'user_management_screen.dart';
import 'user_settings_screen.dart';

/// Cờ TẠM ĐÓNG "rê chuột vào rail thì bung" (2026-09-23, yêu cầu chủ dự án).
///
/// Đóng rồi thì bề rộng rail CHỈ đổi bằng nút thu/mở ở chân rail (`_RailFoot`) —
/// đưa chuột ngang qua không còn làm bố cục nhảy. Không mất thông tin: rail thu
/// đã có tooltip tên mục (`_RailItem`). Bật lại = đổi thành `true`, toàn bộ code
/// hover vẫn nằm nguyên chỗ.
const bool kRailHoverExpand = false;

/// Cờ TẠM ẩn tab Thư Mục (JSON data). Bật lại = đổi thành `true`.
/// ponytail: 1 cờ const thay vì xoá code / thêm mục Cài đặt — chưa ai cần bật-tắt lúc chạy.
const bool kShowFolderTab = false;

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;
  bool _wide = false; // rail đang bung rộng (phủ LÊN nội dung, không đẩy)
  bool _menuOpen = false; // menu tài khoản mở → giữ rail bung
  /// Rail đang THU (64px) — mặc định, và là thứ nút thu/mở ở CHÂN rail bật tắt.
  bool _collapsed = true;

  AppSettings? _settings;

  /// Khoá lưu trạng thái thu/mở rail (`shared_preferences`).
  ///
  /// Lưu CỤC BỘ trong màn này chứ không nhét vào `AppPrefs`: `AppPrefs` gọi
  /// `notifyListeners()` để dựng lại `MaterialApp`, mà thu/mở rail chỉ là
  /// chuyện bố cục của `HomeShell` — dựng lại cả app cho nó là phí và làm mọi
  /// màn đang mở mất trạng thái cuộn.
  static const String _kCollapsed = 'rail_collapsed_v1';

  /// Khoá CŨ của hai bản nháp cùng ngày (nút ghim, rồi enum 3 nấc). Đọc bù để
  /// máy nào đang để rail mở sẵn thì giữ nguyên sau khi cập nhật app.
  static const String _kRailModeLegacy = 'rail_mode_v1';
  static const String _kPinnedLegacy = 'rail_pinned_v1';

  /// Rail có đang bung hay không.
  ///
  /// ĐANG MỞ thì luôn bung. ĐANG THU thì xưa nay vẫn bung TẠM lúc rê chuột vào
  /// (hành vi từ 2026-08-19) — nay nhánh đó nằm sau [kRailHoverExpand], đang
  /// TẠM ĐÓNG nên thu là thu hẳn.
  bool get _isWide {
    if (!_collapsed) return true;
    if (kRailHoverExpand) return _wide || _menuOpen;
    return false;
  }

  /// Bề rộng rail khi thu. Nội dung inset đúng bằng con số này, **một lần**.
  ///
  /// **Thu = CHỈ ICON** (2026-08-19, theo yêu cầu chủ dự án). Không mất thông
  /// tin: mỗi icon có **tooltip** tên mục (`_RailItem`) — từ khi [kRailHoverExpand]
  /// tạm đóng thì tooltip là đường DUY NHẤT đọc tên mục mà không bung rail, nên
  /// đừng bỏ nó.
  ///
  /// Nhờ bỏ nhãn, rail 96 → **64**: trả 32px cho vùng nội dung ở MỌI màn. 64 =
  /// lề ListView 10×2 + ô bấm 44 (icon 22 nằm giữa). Ràng buộc "đo theo từ dài
  /// nhất" của bản cũ cũng hết hiệu lực luôn — không còn chữ nào để vỡ dòng.
  static const double _slim = 64;

  /// Bề rộng khi bung. Nội dung **co theo** (xem `_buildDesktop`).
  static const double _open = 248;

  /// Bề ngang tối thiểu vùng nội dung khi rail bung thì mới ĐẨY nội dung.
  ///
  /// Dưới ngưỡng này rail quay về kiểu cũ — phủ LÊN nội dung — vì ép tab Hiệu
  /// chuẩn/Sản xuất xuống ~500px là bảng số đo vỡ cột, tệ hơn hẳn việc bị che
  /// trong lúc rê chuột. 560 = bảng hẹp nhất còn đọc được (đo ở tab Hiệu chuẩn).
  static const double _minContentWidth = 560;

  /// Nhịp bung/thu rail. Nội dung dùng CHUNG hằng này để hai bên chạy khớp —
  /// lệch nhịp là thấy sọc nền lọt giữa rail và nội dung suốt lúc animate.
  static const Duration _railAnim = Duration(milliseconds: 220);
  static const Curve _railCurve = Curves.easeOutCubic;

  @override
  void initState() {
    super.initState();
    AppSettings.load().then((s) {
      StoragePaths.setParent(s.saveDir); // áp vị trí lưu đã cấu hình
      if (!mounted) return;
      setState(() => _settings = s);
    });
    SharedPreferences.getInstance().then((p) {
      if (!mounted) return;
      final collapsed = p.getBool(_kCollapsed) ??
          // Chưa có khoá mới → suy từ hai khoá nháp cũ: ai đang để rail mở sẵn
          // (`open` / đã ghim) thì vẫn mở, còn lại thu.
          !(p.getString(_kRailModeLegacy) == 'open' ||
              p.getBool(_kPinnedLegacy) == true);
      setState(() => _collapsed = collapsed);
    });
  }

  /// Thu / mở rail rồi ghi nhớ.
  Future<void> _toggleCollapsed() async {
    final next = !_collapsed;
    setState(() {
      _collapsed = next;
      // Bấm THU lúc con trỏ vẫn đang nằm trên rail: `onExit` sẽ không bắn nữa
      // (chuột có rời đâu) nên `_wide` kẹt `true` và rail vẫn bung — trông như
      // nút hỏng. Hạ luôn ở đây; rê ra rồi vào lại là `onEnter` bật.
      if (next) _wide = false;
    });
    final p = await SharedPreferences.getInstance();
    await p.setBool(_kCollapsed, next);
  }

  /// Mở màn Thiết lập (ẩn trong icon tài khoản). DÙNG CHUNG 1 màn cho cả admin
  /// lẫn user. Đẩy như 1 route.
  void _openSettings() {
    final settings = _settings;
    if (settings == null) return;
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => UserSettingsScreen(settings: settings),
    ));
  }

  Future<void> _logout() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('common.logoutTitle')),
        content: Text(tr('common.logoutConfirm')),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('common.logout'))),
        ],
      ),
    );
    if (ok != true) return;
    await SessionStore.clear();
    if (!mounted) return;
    Navigator.of(context).pushReplacement(
      MaterialPageRoute(builder: (_) => const LoginScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final settings = _settings;
    if (settings == null) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }

    final session = SessionStore.current;
    // Mặc định fail-closed: không có phiên → KHÔNG quyền nào cả (ẩn hết tab
    // công cụ), tránh lộ chúng trong khoảnh khắc đăng xuất/race.
    //
    // Gác theo QUYỀN CỦA VIỆC, không theo chức danh: từ 2026-09-07 có thêm hai
    // vai trò xưởng (`manager`, `operator`) chỉ được vào tab Sản xuất — nếu vẫn
    // gác bằng `isStaff` thì cho họ thấy tab Sản xuất đồng nghĩa với trao luôn
    // OTA + Kỹ Thuật. Bảng quyền: docs/plan/tai-khoan-nha-may.md §3.1.
    final canSeeClinical = session?.canSeeClinical ?? false;
    final canSupport = session?.canSupport ?? false;
    final canSeeOta = session?.canSeeOta ?? false;
    final canSeeProduction = session?.canSeeProduction ?? false;
    final canUseTech = session?.canUseTech ?? false;
    final canManageUsers = session?.canManageUsers ?? false;
    final canSeeCalib = session?.canSeeCalib ?? false;
    final isRoot = session?.isRoot ?? false; // tab Giám sát: hạ tầng, chỉ root

    // CHỈ các mục NỘI DUNG vào thanh tab dọc. Thiết lập nằm trong icon tài khoản.
    //
    // Dựng bằng `add` (mỗi tab một `if`): tab nào hiện với ai đọc theo quyền
    // ngay tại chỗ. "Tab đang được xem" đi qua `TickerMode` ở IndexedStack bên
    // dưới — màn con (Chăm sóc KH nhả cổng COM, Giám sát dừng poll) tự đọc,
    // không tab nào cần biết chỉ số của mình.
    final tabs = <_Tab>[
      // Lịch sử = dữ liệu LÂM SÀNG của khách hàng. Người của xưởng không có
      // việc gì ở đây (chủ dự án chốt 2026-09-07) nên tab này biến mất với họ,
      // và tab đầu tiên của họ là Sản xuất.
      if (canSeeClinical)
        _Tab(
          icon: Icons.history_outlined,
          selectedIcon: Icons.history,
          label: tr('nav.history'),
          page: HistoryCombinedScreen(
            key:
                ValueKey('histcomb_${settings.deviceIp}_${settings.cloudApiUrl}'),
            settings: settings,
          ),
        ),
      if (kShowFolderTab && canSeeClinical)
        _Tab(
          icon: Icons.folder_outlined,
          selectedIcon: Icons.folder,
          label: tr('nav.folder'),
          page: FolderScreen(
            key: ValueKey('folder_${settings.engineerUrl}'),
            settings: settings,
          ),
        ),
    ];
    if (canSupport) {
      // Nhân sự: tab Chăm sóc KH (Thông tin máy | Xử lý sự cố) — công cụ cho
      // nhân viên CSKH: tra cứu máy + cắm USB đọc log rồi gửi về kỹ thuật.
      // Chạy cả web (Web Serial) — màn tự báo khi trình duyệt không hỗ trợ,
      // nên KHÔNG gác `serialToolsAvailable`: mục Thông tin máy vẫn hữu ích
      // trên điện thoại.
      tabs.add(_Tab(
        icon: Icons.support_agent_outlined,
        selectedIcon: Icons.support_agent,
        label: tr('nav.support'),
        page: SupportScreen(
          key: ValueKey('support_${settings.engineerUrl}'),
          settings: settings,
        ),
      ));
    }
    if (canSeeOta) {
      // Nhân sự: tab Quản lý máy (Cập nhật OTA | Trạng thái máy). Chỉ HTTP tới
      // Engineer Server nên chạy cả web, không cần bản _web riêng.
      // Quản lý sản xuất CÓ tab này nhưng chỉ-xem: mọi nút ghi trong màn gác
      // riêng bằng `canWriteOta`.
      tabs.add(_Tab(
        icon: Icons.precision_manufacturing_outlined,
        selectedIcon: Icons.precision_manufacturing,
        label: tr('nav.manager'),
        page: ManagerMachineScreen(
          key: ValueKey('mm_${settings.engineerUrl}'),
          settings: settings,
        ),
      ));
    }
    if (canSeeProduction) {
      // Tab Sản xuất (ATE) — trạm nạp + khai sinh + hồ sơ nghiệm thu cho xưởng.
      // Nhân sự + quản lý sản xuất + thao tác viên. KHÔNG gác
      // `serialToolsAvailable`: bản web của tab này bỏ mục "Chạy trạm" nhưng vẫn
      // tra được hồ sơ máy + FPY, và đó là thứ quản lý cần xem trên điện thoại.
      tabs.add(_Tab(
        icon: Icons.factory_outlined,
        selectedIcon: Icons.factory,
        label: tr('nav.ate'),
        page: AteScreen(
          key: ValueKey('ate_${settings.engineerUrl}'),
          settings: settings,
        ),
      ));
    }
    if (canSeeCalib) {
      // Tab Hiệu chuẩn (2026-09-21): ống chuẩn quang — pha dung dịch Fluorescein,
      // đo thô, xếp hạng tổ hợp, đóng bộ ống, cấp cho máy. Chỉ HTTP tới Engineer
      // Server (/calib/*) nên chạy cả web. Kỹ sư + người của xưởng; ngưỡng PASS
      // chỉ nhân sự kỹ thuật sửa (gác trong màn bằng `canEditLimits`).
      tabs.add(_Tab(
        icon: Icons.science_outlined,
        selectedIcon: Icons.science,
        label: tr('nav.calib'),
        page: CalibScreen(
          key: ValueKey('calib_${settings.engineerUrl}_${settings.engineerToken}'),
          settings: settings,
        ),
      ));
    }
    // Nhân sự: tab Kỹ Thuật (Log nhiệt | Đọc serial | Nạp code). Trên web =
    // bản Web Serial + esptool-js — conditional import ở đầu file chọn bản đúng.
    // `serialToolsAvailable`: trình duyệt ĐIỆN THOẠI không có Web Serial nên
    // cả ba công cụ đều vô dụng ở đó. Bày một tab mà bấm vào chỉ ra màn báo
    // lỗi thì tệ hơn không bày. Gác theo KHẢ NĂNG nền tảng, không theo bề rộng
    // — cửa sổ desktop kéo hẹp vẫn phải giữ tab này.
    if (canUseTech && serialToolsAvailable) {
      tabs.add(_Tab(
        icon: Icons.build_outlined,
        selectedIcon: Icons.build,
        label: tr('nav.tech'),
        page: const TechScreen(),
      ));
    }
    // Root: tình trạng Engineer Server. Chỉ root vì đây là hạ tầng, không
    // phải dữ liệu vận hành hằng ngày — cùng mức với Quản lý User. Gác bằng
    // `isRoot` chứ KHÔNG `canManageUsers`: từ 2026-09-07 quản lý sản xuất cũng
    // quản lý được tài khoản (operator) nhưng không có việc gì với hạ tầng.
    // Lưu ý: gác này chỉ ẩn TAB; endpoint /monitor bên server vẫn nhận mọi
    // token hợp lệ (xem doc đầu monitor_screen.dart).
    if (isRoot) {
      tabs.add(_Tab(
        icon: Icons.monitor_heart_outlined,
        selectedIcon: Icons.monitor_heart,
        label: tr('nav.monitor'),
        page: MonitorScreen(
          // Key gồm cả TOKEN: đổi token trong Cài đặt mà key không đổi thì
          // `late final _api` giữ header cũ và tab 401 tới khi khởi động lại
          // app. (`mm_`/`folder_`/`histcomb_` còn nguyên lỗ này — sửa riêng.)
          key: ValueKey('mon_${settings.engineerUrl}_${settings.engineerToken}'),
          settings: settings,
        ),
      ));
    }
    if (canManageUsers) {
      tabs.add(_Tab(
        icon: Icons.manage_accounts_outlined,
        selectedIcon: Icons.manage_accounts,
        label: tr('um.title'),
        page: const UserManagementScreen(),
      ));
    }

    if (_index >= tabs.length) _index = 0;
    final wide = _isWide;

    return LayoutBuilder(builder: (context, c) {
      // Điện thoại: rail dọc 64px ăn mất 1/6 bề ngang một màn 390px, và không có
      // chuột thì cơ chế "rê vào để bung" không tồn tại. Đổi sang thanh điều
      // hướng DƯỚI — ngón cái với tới được, đúng chỗ mọi app điện thoại đặt nó.
      if (c.maxWidth < kMobileMaxWidth) return _buildMobile(tabs);
      // Bung rail có ĐẨY nội dung hay không phụ thuộc còn đủ chỗ hay không:
      // cửa sổ hẹp mà vẫn đẩy thì nội dung bị ép xuống dưới ngưỡng dùng được,
      // thà để rail phủ lên như cũ (người dùng nhả chuột là thấy lại ngay).
      final pushes = c.maxWidth - _open >= _minContentWidth;
      return _buildDesktop(tabs, wide, pushes);
    });
  }

  /// Bố cục ĐIỆN THOẠI: **thanh trên** (nút mở ngăn kéo + nút Cài đặt) và một
  /// **ngăn kéo** chứa các mục điều hướng.
  ///
  /// Vì sao không dùng rail như desktop: rail 64px ăn 1/6 bề ngang một màn 390px,
  /// và không có chuột thì cơ chế "rê vào để bung" không tồn tại. Ngăn kéo chỉ
  /// chiếm chỗ khi đang mở.
  ///
  /// Nút mở ngăn kéo KHÔNG cần tự dựng: `Scaffold` tự thêm khi có `drawer`, và
  /// bản của nó đã kèm nhãn khả truy cập + xử lý phím sẵn.
  Widget _buildMobile(List<_Tab> tabs) {
    return Scaffold(
      appBar: AppBar(
        // Tiêu đề = tên mục ĐANG xem, và `AppTabScaffold` giấu tiêu đề của nó ở
        // khổ này — nếu không thì cùng một chữ hiện hai lần chồng nhau.
        title: Text(tabs[_index].label),
        actions: [
          IconButton(
            tooltip: tr('nav.settings'),
            icon: const Icon(Icons.settings_outlined),
            onPressed: _openSettings,
          ),
        ],
      ),
      drawer: _MobileDrawer(
        tabs: tabs,
        index: _index,
        onSelect: (i) => setState(() => _index = i),
        onLogout: _logout,
      ),
      body: IndexedStack(
        index: _index,
        // `TickerMode` = cờ "tab này đang được xem", dùng CHUNG cho mọi tab.
        // `IndexedStack` dựng HẾT các con (đã đo: con bị ẩn vẫn `tickerMode=true`)
        // nên nếu không tắt, animation và mọi thứ nghe theo ticker của 4 tab ẩn
        // vẫn chạy suốt phiên. Màn nào cần biết mình có đang hiển thị không thì
        // đọc `TickerMode.of(context)` — không phải thêm cờ `active` riêng.
        children: [
          for (var i = 0; i < tabs.length; i++)
            TickerMode(enabled: i == _index, child: tabs[i].page),
        ],
      ),
    );
  }

  /// Bố cục DESKTOP: rail dọc luôn hiện, bung khi rê chuột.
  ///
  /// `pushes` = còn đủ bề ngang để **đẩy** nội dung sang phải khi rail bung.
  /// Bản trước luôn inset nội dung đúng `_slim` rồi cho rail phủ lên (để
  /// `fl_chart` khỏi relayout) — nhưng bung 248px là nuốt mất 184px mép trái:
  /// thanh mục con của tab Hiệu chuẩn (`Lô pha | Bộ ống | Ngưỡng`) và đầu mã lô
  /// biến mất, đúng lúc người dùng đang rê chuột để đọc tên mục. Nay nội dung co
  /// theo rail bằng CÙNG nhịp animate (`_railAnim`/`_railCurve`) nên không còn
  /// gì bị che; cái giá là tab có đồ thị relayout trong 220 ms đó.
  Widget _buildDesktop(List<_Tab> tabs, bool wide, bool pushes) {
    // Rail có ĐẨY nội dung hay không. Ghim thì LUÔN đẩy, kể cả khi dưới
    // `_minContentWidth`: ngưỡng đó để bảo vệ người đang rê chuột ngang qua
    // (bung/thu ngoài ý muốn), còn ghim là người dùng CHỦ ĐỘNG chọn giữ rail —
    // che mất nội dung vĩnh viễn mới là hỏng. Hẹp quá thì họ bỏ ghim.
    final pushing = wide && (!_collapsed || pushes);
    final contentLeft = pushing ? _open : _slim;
    return Scaffold(
      body: Stack(
        children: [
          AnimatedPositioned(
            duration: _railAnim,
            curve: _railCurve,
            left: contentLeft,
            top: 0,
            right: 0,
            bottom: 0,
            child: IndexedStack(
              index: _index,
              children: [
                for (var i = 0; i < tabs.length; i++)
                  TickerMode(enabled: i == _index, child: tabs[i].page),
              ],
            ),
          ),
          // Rail LUÔN HIỆN, nằm trên cùng để bóng đổ lên mép nội dung.
          AnimatedPositioned(
            duration: _railAnim,
            curve: _railCurve,
            left: 0,
            top: 0,
            bottom: 0,
            width: wide ? _open : _slim,
            child: MouseRegion(
              // Đang đóng hover thì KHÔNG setState — chuột đi ngang qua rail
              // mà dựng lại cả cây widget là phí, nhất là tab có `fl_chart`.
              onEnter: (_) {
                if (kRailHoverExpand) setState(() => _wide = true);
              },
              onExit: (_) {
                if (kRailHoverExpand && !_menuOpen) {
                  setState(() => _wide = false);
                }
              },
              child: _Rail(
                tabs: tabs,
                index: _index,
                wide: wide,
                // Chỉ đổ bóng khi rail THẬT SỰ nằm đè lên nội dung. Ghim (hoặc
                // hover còn đủ chỗ đẩy) thì nó là panel đặt CẠNH nội dung —
                // bóng dày ở đó đọc ra như một lớp nổi vô cớ.
                overlay: wide && !pushing,
                collapsed: _collapsed,
                onToggleCollapsed: _toggleCollapsed,
                onSelect: (i) => setState(() {
                  _index = i;
                  _wide = false;
                }),
                onSettings: _openSettings,
                onLogout: _logout,
                onMenuOpenChanged: (open) => setState(() {
                  _menuOpen = open;
                  if (!open) _wide = false;
                }),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Tab {
  final IconData icon;
  final IconData selectedIcon;
  final String label;
  final Widget page;
  const _Tab({
    required this.icon,
    required this.selectedIcon,
    required this.label,
    required this.page,
  });
}

/// Thanh điều hướng dọc, **luôn nhìn thấy**.
///
/// Tự dựng thay vì dùng `NavigationRail`: widget đó assert `labelType == none`
/// khi `extended: true`, nên kiểu "thu thì có nhãn nhỏ, bung thì nhãn nằm cạnh"
/// phải dựng lại widget mỗi lần đổi trạng thái — đúng thứ gây giật. Ở đây một
/// widget lo cả hai trạng thái, chỉ đổi bố cục bên trong.
class _Rail extends StatelessWidget {
  final List<_Tab> tabs;
  final int index;
  final bool wide;

  /// Rail đang nằm ĐÈ lên nội dung (bung mà không đẩy được) → cần đổ bóng.
  final bool overlay;

  /// Rail đang thu (nút ở chân rail bật tắt).
  final bool collapsed;
  final VoidCallback onToggleCollapsed;
  final ValueChanged<int> onSelect;
  final VoidCallback onSettings;
  final VoidCallback onLogout;
  final ValueChanged<bool> onMenuOpenChanged;

  const _Rail({
    required this.tabs,
    required this.index,
    required this.wide,
    required this.overlay,
    required this.collapsed,
    required this.onToggleCollapsed,
    required this.onSelect,
    required this.onSettings,
    required this.onLogout,
    required this.onMenuOpenChanged,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Material(
      color: cs.surface,
      // Chỉ đổ bóng khi đang phủ LÊN nội dung, không thì chữ chồng chữ.
      elevation: overlay ? 10 : 0,
      shadowColor: kBrandDeep.withValues(alpha: 0.35),
      // Bo hai góc PHẢI thôi. Ba mép kia áp sát cạnh cửa sổ — bo ở đó chỉ để hở
      // một mẩu nền ở góc màn hình chứ không ai đọc ra là "bo góc". Mép phải mới
      // là mép người ta nhìn, và bo nó làm rail đọc ra như một tấm panel đặt cạnh
      // nội dung, cùng ngôn ngữ với thẻ bo 20 ở vùng nội dung.
      //
      // `shape` chứ không phải `BoxDecoration`: `BoxDecoration` có `borderRadius`
      // thì **assert** nếu border không đồng đều (bản trước chỉ kẻ viền phải), và
      // `Material.shape` lo luôn cả đổ bóng theo đúng hình bo lẫn cắt nội dung.
      shape: RoundedRectangleBorder(
        side: BorderSide(color: cs.outlineVariant),
        borderRadius: const BorderRadius.only(
          topRight: Radius.circular(AppRadius.card),
          bottomRight: Radius.circular(AppRadius.card),
        ),
      ),
      clipBehavior: Clip.antiAlias,
      child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // KHÔNG còn khối thương hiệu ở đầu rail (bỏ 2026-08-19 theo yêu cầu
            // chủ dự án). Logo giờ nằm ở icon ứng dụng — thanh tiêu đề, thanh
            // tác vụ, bộ cài — và ở màn đăng nhập; in lại lần thứ ba ngay cạnh
            // chúng là thừa. Đổi lại rail có thêm ~96px cho mục điều hướng.
            const SizedBox(height: 12),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(horizontal: 10),
                children: [
                  for (var i = 0; i < tabs.length; i++)
                    _RailItem(
                      tab: tabs[i],
                      selected: i == index,
                      wide: wide,
                      onTap: () => onSelect(i),
                    ),
                ],
              ),
            ),
            Divider(height: 1, color: cs.outlineVariant),
            _AccountMenu(
              wide: wide,
              onSettings: onSettings,
              onLogout: onLogout,
              onMenuOpenChanged: onMenuOpenChanged,
            ),
            // Chân rail: số phiên bản (CHỈ khi bung — thu chỉ rộng 64px, nhét
            // chữ vào đó là vỡ) + nút thu/mở LUÔN hiện. Bản dev tô màu cảnh báo
            // để ai ngồi trước máy cũng thấy mình đang chạy bản chưa phát hành.
            // Chi tiết đầy đủ: Thiết lập › Phiên bản.
            _RailFoot(
                wide: wide,
                collapsed: collapsed,
                onToggle: onToggleCollapsed),
          ],
      ),
    );
  }
}

/// Dấu thương hiệu ở đầu rail. Chấm cyan là chỗ DUY NHẤT màu logo xuất hiện
/// nguyên bản — nó là mark, không phải chữ (2.09:1, xem `app_theme.dart`).
class _RailItem extends StatelessWidget {
  final _Tab tab;
  final bool selected;
  final bool wide;
  final VoidCallback onTap;

  const _RailItem({
    required this.tab,
    required this.selected,
    required this.wide,
    required this.onTap,
  });

  /// Chiều cao CỐ ĐỊNH, giống hệt nhau ở trạng thái thu và bung.
  ///
  /// Không ghim thì hàng thu cao 60–72px (nhãn 2 dòng như "Manage machines" cao
  /// hơn nhãn 1 dòng) còn hàng bung chỉ 48px → **rê chuột vào rail làm mọi mục
  /// nhảy chỗ ngay trong lúc người ta đang nhắm**, và cú bấm rơi vào mục khác.
  /// Gặp thật khi chụp màn hình kiểm tra 2026-08-19: nhắm "Engineering", trúng
  /// "Manage machines".
  ///
  /// 62 = icon 22 + khe 5 + hai dòng nhãn 10px (≈23) + 6 trên/dưới.
  static const double height = 62;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final fg = selected ? cs.onPrimaryContainer : cs.onSurfaceVariant;
    final icon = Icon(selected ? tab.selectedIcon : tab.icon, size: 22, color: fg);
    final style = TextStyle(
      fontSize: 14,
      height: 1.15,
      fontWeight: selected ? FontWeight.w600 : FontWeight.w500,
      color: selected ? cs.onSurface : cs.onSurfaceVariant,
    );

    final item = Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Material(
        color: selected ? cs.primaryContainer : Colors.transparent,
        borderRadius: BorderRadius.circular(AppRadius.base),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(AppRadius.base),
          child: SizedBox(
            height: height,
            child: Padding(
              padding: EdgeInsets.symmetric(horizontal: wide ? 14 : 6),
              child: wide
                  ? Row(children: [
                      icon,
                      const SizedBox(width: 14),
                      Expanded(
                          child: Text(tab.label,
                              overflow: TextOverflow.ellipsis, style: style)),
                    ])
                  // Thu: CHỈ icon, căn giữa. Không nhãn.
                  : Center(child: icon),
            ),
          ),
        ),
      ),
    );

    // Tooltip CHỈ khi thu. Lúc bung thì nhãn đã nằm ngay cạnh icon — thêm
    // tooltip nữa là nói cùng một điều hai lần, và nó còn che mất mục bên dưới.
    //
    // Từ khi `kRailHoverExpand` tạm đóng, đây là đường DUY NHẤT đọc tên mục lúc
    // rail thu — bỏ tooltip là người dùng chỉ còn 8 cái icon để đoán.
    return wide
        ? item
        : Tooltip(message: tab.label, waitDuration: const Duration(milliseconds: 400), child: item);
  }
}

/// Tài khoản ở chân rail: **Thiết lập** + **Đăng xuất**. Icon theo vai trò.
class _AccountMenu extends StatelessWidget {
  final bool wide;
  final VoidCallback onSettings;
  final VoidCallback onLogout;
  final ValueChanged<bool> onMenuOpenChanged;

  const _AccountMenu({
    required this.wide,
    required this.onSettings,
    required this.onLogout,
    required this.onMenuOpenChanged,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final s = SessionStore.current;
    final who = s == null
        ? ''
        : (s.name.isNotEmpty ? s.name : s.username);
    final role = s == null ? '' : roleLabel(s.roleCode);
    final avatar = CircleAvatar(
      radius: 17,
      backgroundColor: cs.primaryContainer,
      child: Icon(
        // Ba nhóm, ba biểu tượng: nhân sự kỹ thuật (khiên) · người của xưởng
        // (nhà máy) · khách hàng (người). Nhìn icon là biết đang đăng nhập bằng
        // tài khoản nào — ở xưởng dùng chung máy trạm thì đây là dấu hiệu duy
        // nhất thấy ngay khi ai đó quên đăng xuất.
        (s?.isStaff ?? false)
            ? Icons.shield_outlined
            : (s?.isFactory ?? false)
                ? Icons.factory_outlined
                : Icons.person_outline,
        size: 20,
        color: cs.onPrimaryContainer,
      ),
    );

    return Padding(
      // Đáy 6 khi bung: ngay dưới còn dòng phiên bản (`_RailVersion`) tự mang
      // đệm của nó. Lúc thu không có dòng đó nên giữ 16 như cũ.
      padding: EdgeInsets.fromLTRB(wide ? 14 : 10, 12, wide ? 14 : 10, wide ? 6 : 16),
      child: PopupMenuButton<String>(
        tooltip: wide ? '' : '$who · $role',
        position: PopupMenuPosition.over,
        padding: EdgeInsets.zero,
        onOpened: () => onMenuOpenChanged(true),
        onCanceled: () => onMenuOpenChanged(false),
        onSelected: (v) {
          onMenuOpenChanged(false);
          if (v == 'settings') onSettings();
          if (v == 'logout') onLogout();
        },
        itemBuilder: (_) => [
          PopupMenuItem(
            enabled: false,
            height: 38,
            child: Text('$who · $role',
                style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
          ),
          const PopupMenuDivider(),
          PopupMenuItem(
            value: 'settings',
            child: ListTile(
              dense: true,
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.settings_outlined),
              title: Text(tr('us.title')),
            ),
          ),
          PopupMenuItem(
            value: 'logout',
            child: ListTile(
              dense: true,
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.logout),
              title: Text(tr('common.logout')),
            ),
          ),
        ],
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 4),
          child: wide
              ? Row(children: [
                  avatar,
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(who,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                                fontSize: 13.5, fontWeight: FontWeight.w600)),
                        Text(role,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                                fontSize: 11.5, color: cs.onSurfaceVariant)),
                      ],
                    ),
                  ),
                  Icon(Icons.more_horiz, size: 18, color: cs.onSurfaceVariant),
                ])
              : Center(child: avatar),
        ),
      ),
    );
  }
}

/// Ngăn kéo điều hướng cho khổ điện thoại — cùng danh sách mục với rail desktop.
class _MobileDrawer extends StatelessWidget {
  final List<_Tab> tabs;
  final int index;
  final ValueChanged<int> onSelect;
  final VoidCallback onLogout;

  const _MobileDrawer({
    required this.tabs,
    required this.index,
    required this.onSelect,
    required this.onLogout,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final s = SessionStore.current;
    return Drawer(
      child: SafeArea(
        child: Column(
          children: [
            // Đầu ngăn kéo: ai đang đăng nhập. Trên desktop thông tin này nằm ở
            // chân rail; điện thoại không có rail nên nó về đây.
            ListTile(
              leading: CircleAvatar(
                backgroundColor: cs.primaryContainer,
                child: Icon(
                  (s?.isStaff ?? false)
                      ? Icons.shield_outlined
                      : (s?.isFactory ?? false)
                          ? Icons.factory_outlined
                          : Icons.person_outline,
                  color: cs.onPrimaryContainer,
                ),
              ),
              title: Text(
                s == null ? '' : (s.name.isNotEmpty ? s.name : s.username),
                style: const TextStyle(fontWeight: FontWeight.w600),
              ),
              subtitle: Text(s == null ? '' : roleLabel(s.roleCode)),
            ),
            Divider(height: 1, color: cs.outlineVariant),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(vertical: 8),
                children: [
                  for (var i = 0; i < tabs.length; i++)
                    ListTile(
                      selected: i == index,
                      selectedTileColor: cs.primaryContainer,
                      shape: RoundedRectangleBorder(
                          borderRadius:
                              BorderRadius.circular(AppRadius.base)),
                      leading: Icon(
                          i == index ? tabs[i].selectedIcon : tabs[i].icon),
                      title: Text(tabs[i].label),
                      onTap: () {
                        Navigator.pop(context); // đóng ngăn kéo trước
                        onSelect(i);
                      },
                    ),
                ],
              ),
            ),
            Divider(height: 1, color: cs.outlineVariant),
            ListTile(
              leading: const Icon(Icons.logout),
              title: Text(tr('common.logout')),
              onTap: () {
                Navigator.pop(context);
                onLogout();
              },
            ),
            const _RailVersion(),
          ],
        ),
      ),
    );
  }
}

/// Chân rail: số phiên bản (khi bung) + **nút THU/MỞ** (luôn hiện).
///
/// Đặt ở chân theo mẫu sidebar hubOTA (`04.FBT-OTA`): không tranh chỗ với mục
/// điều hướng, và là nơi người ta đã quen tìm nút thu ngăn kéo.
///
/// Đây là nút DUY NHẤT điều khiển bề rộng rail (nút ghim ở đầu rail đã bỏ
/// 2026-09-23 theo yêu cầu chủ dự án — hai nút cho một việc là rối). Mở = rail
/// đứng nguyên 248px; thu = 64px nhưng rê chuột vào vẫn bung tạm để đọc tên mục.
class _RailFoot extends StatelessWidget {
  final bool wide;
  final bool collapsed;
  final VoidCallback onToggle;

  const _RailFoot({
    required this.wide,
    required this.collapsed,
    required this.onToggle,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final button = IconButton(
      tooltip: collapsed ? tr('nav.railExpand') : tr('nav.railCollapse'),
      icon: Icon(
        // Mũi tên CHỈ HƯỚNG SẼ ĐI, không phải trạng thái đang có: đang thu thì
        // vẽ ">>" (bấm là mở ra). Vẽ ngược lại là ai cũng bấm nhầm một lần.
        collapsed
            ? Icons.keyboard_double_arrow_right
            : Icons.keyboard_double_arrow_left,
        size: 20,
        color: cs.onSurfaceVariant,
      ),
      onPressed: onToggle,
      visualDensity: VisualDensity.compact,
    );

    // Thu: chỉ nút, căn giữa như mọi icon khác. Bung: version bên trái, nút bên
    // phải — `Expanded` để nhãn version dài không đẩy nút ra khỏi rail.
    return Padding(
      padding: EdgeInsets.fromLTRB(wide ? 12 : 6, 0, wide ? 8 : 6, 6),
      child: wide
          ? Row(children: [
              const Expanded(child: _RailVersion(dense: true)),
              button,
            ])
          : Align(alignment: Alignment.center, child: button),
    );
  }
}

/// Dòng phiên bản ở chân thanh điều hướng (rail bung + ngăn kéo điện thoại).
///
/// Chỉ NHÃN NGẮN (`v1.1.0-dev`); ngày dựng/commit/danh sách việc đang thêm nằm
/// ở Thiết lập › Phiên bản. Bản dev in màu cảnh báo + tooltip nói rõ; bản phát
/// hành in chữ phụ, im lặng.
class _RailVersion extends StatelessWidget {
  /// Bản gọn: bỏ đệm ngoài + căn trái, để nằm chung hàng với nút thu/mở
  /// (`_RailFoot`). Bản thường (ngăn kéo điện thoại) tự mang đệm và căn giữa.
  final bool dense;

  const _RailVersion({this.dense = false});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final dev = kIsDevBuild;
    return Tooltip(
      message: dev ? '${tr('ver.devNote')} ($kAppBuildDate)' : appVersionFull,
      child: Padding(
        padding: dense
            ? EdgeInsets.zero
            : const EdgeInsets.fromLTRB(12, 0, 12, 12),
        child: Row(
            mainAxisAlignment:
                dense ? MainAxisAlignment.start : MainAxisAlignment.center,
            children: [
          if (dev) ...[
            const Icon(Icons.construction_outlined, size: 13, color: kWarning),
            const SizedBox(width: 5),
          ],
          Flexible(
            child: Text(
              appVersionLabel,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 11.5,
                fontFamily: 'JetBrains Mono',
                color: dev ? kWarning : cs.onSurfaceVariant,
              ),
            ),
          ),
        ]),
      ),
    );
  }
}
