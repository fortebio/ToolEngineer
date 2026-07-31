import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/session_store.dart';
import '../services/storage_paths.dart';
import '../util/i18n.dart';
import 'folder_screen.dart';
import 'history_combined_screen.dart';
import 'login_screen.dart';
// Tab Kỹ Thuật: desktop dùng COM/esptool (dart:ffi/dart:io); web import bản
// Web Serial + esptool-js (tech_screen_web.dart) — không kéo native vào web.
import 'tech_screen.dart' if (dart.library.html) 'tech_screen_web.dart';
import 'user_management_screen.dart';
import 'user_settings_screen.dart';

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;
  bool _railVisible = false; // thanh tab ẩn hẳn; hover mép trái → hiện (pop-up)
  bool _menuOpen = false; // menu tài khoản đang mở → KHÔNG ẩn rail (giữ nguyên)
  AppSettings? _settings;

  static const double _railWidth = 240; // bề rộng thanh nav khi hiện

  void _hideRail() {
    if (!_menuOpen && _railVisible) setState(() => _railVisible = false);
  }

  @override
  void initState() {
    super.initState();
    AppSettings.load().then((s) {
      StoragePaths.setParent(s.saveDir); // áp vị trí lưu đã cấu hình
      if (!mounted) return;
      setState(() => _settings = s);
    });
  }

  /// Mở màn Thiết lập (ẩn trong icon tài khoản). DÙNG CHUNG 1 màn cho cả admin
  /// lẫn user (đồng bộ giống nhau). Đẩy như 1 route.
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
    // Mặc định fail-closed: không có phiên → coi như KHÔNG phải nhân sự (ẩn tab
    // Kỹ Thuật / Quản lý User), tránh lộ công cụ kỹ thuật lúc đăng xuất/race.
    final isStaff = session?.isStaff ?? false; // nhân sự (root + nhân viên)
    final isRoot = session?.isRoot ?? false; // chỉ root quản lý tài khoản

    // CHỈ các mục NỘI DUNG vào thanh tab dọc. Thiết lập nằm trong icon tài khoản.
    final tabs = <_Tab>[
      _Tab(
        icon: Icons.history_outlined,
        selectedIcon: Icons.history,
        label: tr('nav.history'),
        page: HistoryCombinedScreen(
          key: ValueKey('histcomb_${settings.deviceIp}_${settings.cloudApiUrl}'),
          settings: settings,
        ),
      ),
      // Mọi vai trò: tab Thư Mục (JSON data — file trên Engineer Server, xem
      // thô). HTTP thuần nên có cả trên web.
      _Tab(
        icon: Icons.folder_outlined,
        selectedIcon: Icons.folder,
        label: tr('nav.folder'),
        page: FolderScreen(
          key: ValueKey('folder_${settings.engineerUrl}'),
          settings: settings,
        ),
      ),
      // Nhân sự (root + nhân viên): tab Kỹ Thuật (Log nhiệt | Đọc serial | Nạp
      // code). Trên web = bản Web Serial + esptool-js (tech_screen_web.dart,
      // chỉ Chrome/Edge desktop) — conditional import ở đầu file chọn bản đúng.
      if (isStaff)
        _Tab(
          icon: Icons.build_outlined,
          selectedIcon: Icons.build,
          label: tr('nav.tech'),
          page: const TechScreen(),
        ),
      // CHỈ root: Quản lý User (thêm/xóa nhân viên + khách hàng).
      if (isRoot)
        _Tab(
          icon: Icons.manage_accounts_outlined,
          selectedIcon: Icons.manage_accounts,
          label: tr('um.title'),
          page: const UserManagementScreen(),
        ),
    ];

    if (_index >= tabs.length) _index = 0;

    return Scaffold(
      body: Stack(
        children: [
          // Nội dung chiếm TRỌN bề rộng — thanh tab dọc ẩn hẳn.
          Positioned.fill(
            child: IndexedStack(
              index: _index,
              children: [for (final t in tabs) t.page],
            ),
          ),
          // Mép trái: đưa chuột tới đây → bung thanh tab. "Tay nắm" mờ gợi ý.
          // (Chỉ hiện khi rail đang ẩn.)
          if (!_railVisible)
            Positioned(
              left: 0,
              top: 0,
              bottom: 0,
              width: 14,
              child: MouseRegion(
                onEnter: (_) => setState(() => _railVisible = true),
                child: Center(
                  child: Container(
                    width: 4,
                    height: 56,
                    decoration: BoxDecoration(
                      color: Theme.of(context).colorScheme.primary,
                      borderRadius: BorderRadius.circular(4),
                    ),
                  ),
                ),
              ),
            ),
          // Thanh tab DỌC nổi (overlay) — TRƯỢT vào/ra mượt. Ẩn = trượt khuất
          // trái. Không ẩn khi menu tài khoản đang mở (giữ nguyên để bấm được).
          AnimatedPositioned(
            duration: const Duration(milliseconds: 220),
            curve: Curves.easeOutCubic,
            left: _railVisible ? 0 : -_railWidth,
            top: 0,
            bottom: 0,
            width: _railWidth,
            child: MouseRegion(
              onExit: (_) => _hideRail(),
              child: Material(
                elevation: 12,
                child: NavigationRail(
                  extended: true,
                  minExtendedWidth: _railWidth,
                  labelType: NavigationRailLabelType.none,
                  selectedIndex: _index,
                  onDestinationSelected: (i) => setState(() {
                    _index = i;
                    _railVisible = false; // chọn xong → ẩn lại
                  }),
                  destinations: [
                    for (final t in tabs)
                      NavigationRailDestination(
                        icon: Icon(t.icon),
                        selectedIcon: Icon(t.selectedIcon),
                        label: Text(t.label),
                      ),
                  ],
                  // Icon tài khoản: bấm → menu Thiết lập / Đăng xuất.
                  trailing: Expanded(
                    child: Align(
                      alignment: Alignment.bottomCenter,
                      child: _AccountMenu(
                        onSettings: _openSettings,
                        onLogout: _logout,
                        onMenuOpenChanged: (open) {
                          // Giữ rail khi menu mở; menu đóng → ẩn rail.
                          _menuOpen = open;
                          if (!open) {
                            setState(() => _railVisible = false);
                          }
                        },
                      ),
                    ),
                  ),
                ),
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

/// Icon tài khoản ở chân thanh điều hướng. Bấm mở menu: **Thiết lập** (mục thiết
/// lập ẩn trong đây) + **Đăng xuất**. Icon khác nhau theo vai trò (shield/person).
class _AccountMenu extends StatelessWidget {
  final VoidCallback onSettings;
  final VoidCallback onLogout;
  final ValueChanged<bool> onMenuOpenChanged;
  const _AccountMenu({
    required this.onSettings,
    required this.onLogout,
    required this.onMenuOpenChanged,
  });

  @override
  Widget build(BuildContext context) {
    final s = SessionStore.current;
    final label = s == null
        ? ''
        : '${s.name.isNotEmpty ? s.name : s.username}'
            ' (${roleLabel(s.roleCode)})';
    return Padding(
      padding: const EdgeInsets.only(bottom: 16, top: 8),
      child: PopupMenuButton<String>(
        tooltip: label,
        position: PopupMenuPosition.over,
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
            child: Text(label, style: const TextStyle(fontSize: 12)),
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
        child: CircleAvatar(
          radius: 18,
          backgroundColor:
              Theme.of(context).colorScheme.primaryContainer,
          child: Icon(
            (s?.isStaff ?? false)
                ? Icons.shield_outlined
                : Icons.person_outline,
            size: 22,
          ),
        ),
      ),
    );
  }
}
