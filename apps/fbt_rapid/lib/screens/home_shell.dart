import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/storage_paths.dart';
import 'cloud_devices_screen.dart';
import 'history_screen.dart';
import 'settings_screen.dart';
import 'temperature_log_screen.dart';

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;
  AppSettings? _settings;

  @override
  void initState() {
    super.initState();
    AppSettings.load().then((s) {
      StoragePaths.setParent(s.saveDir); // áp vị trí lưu đã cấu hình
      if (!mounted) return;
      setState(() => _settings = s);
    });
  }

  @override
  Widget build(BuildContext context) {
    final settings = _settings;
    if (settings == null) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }

    final pages = [
      // Key theo IP để HistoryScreen tải lại khi đổi máy.
      HistoryScreen(key: ValueKey('hist_${settings.deviceIp}'), settings: settings),
      // Key theo URL cloud để tải lại khi đổi cấu hình.
      CloudDevicesScreen(
        key: ValueKey('cloud_${settings.cloudApiUrl}'),
        settings: settings,
      ),
      const TemperatureLogScreen(),
      SettingsScreen(
        settings: settings,
        onChanged: () => setState(() {}),
      ),
    ];

    return Scaffold(
      body: Row(
        children: [
          NavigationRail(
            selectedIndex: _index,
            onDestinationSelected: (i) => setState(() => _index = i),
            labelType: NavigationRailLabelType.all,
            destinations: const [
              NavigationRailDestination(
                icon: Icon(Icons.history_outlined),
                selectedIcon: Icon(Icons.history),
                label: Text('Lịch sử'),
              ),
              NavigationRailDestination(
                icon: Icon(Icons.cloud_outlined),
                selectedIcon: Icon(Icons.cloud),
                label: Text('Cloud'),
              ),
              NavigationRailDestination(
                icon: Icon(Icons.thermostat_outlined),
                selectedIcon: Icon(Icons.thermostat),
                label: Text('Log nhiệt'),
              ),
              NavigationRailDestination(
                icon: Icon(Icons.settings_outlined),
                selectedIcon: Icon(Icons.settings),
                label: Text('Cài đặt'),
              ),
            ],
          ),
          const VerticalDivider(width: 1),
          Expanded(
            child: IndexedStack(index: _index, children: pages),
          ),
        ],
      ),
    );
  }
}
