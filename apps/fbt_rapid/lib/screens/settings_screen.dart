import 'dart:convert';
import 'dart:io';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../services/app_prefs.dart';
import '../services/app_settings.dart';
import '../services/backup_service.dart';
import '../services/device_api.dart';
import '../services/storage_paths.dart';
import '../util/i18n.dart';

class SettingsScreen extends StatefulWidget {
  final AppSettings settings;
  final VoidCallback onChanged;

  const SettingsScreen({
    super.key,
    required this.settings,
    required this.onChanged,
  });

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _ip =
      TextEditingController(text: widget.settings.deviceIp);
  late final TextEditingController _interval = TextEditingController(
      text: widget.settings.readingIntervalSec.toString());
  late final TextEditingController _name =
      TextEditingController(text: widget.settings.userName);
  late final TextEditingController _org =
      TextEditingController(text: widget.settings.userOrg);

  final _ssid = TextEditingController();
  final _pass = TextEditingController();

  bool _testing = false;

  @override
  void dispose() {
    _ip.dispose();
    _interval.dispose();
    _name.dispose();
    _org.dispose();
    _ssid.dispose();
    _pass.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    widget.settings
      ..deviceIp = _ip.text.trim()
      ..readingIntervalSec =
          int.tryParse(_interval.text.trim()) ?? widget.settings.readingIntervalSec
      ..userName = _name.text.trim()
      ..userOrg = _org.text.trim();
    StoragePaths.setParent(widget.settings.saveDir);
    await widget.settings.save();
    widget.onChanged();
    _snack('Đã lưu cài đặt.');
  }

  Future<void> _pickSaveDir() async {
    final dir = await getDirectoryPath(
        confirmButtonText: 'Chọn thư mục lưu file');
    if (dir == null) return; // huỷ
    setState(() => widget.settings.saveDir = dir);
    StoragePaths.setParent(dir);
    await widget.settings.save();
    widget.onChanged();
    _snack('Vị trí lưu file: $dir');
  }

  Future<void> _resetSaveDir() async {
    setState(() => widget.settings.saveDir = '');
    StoragePaths.setParent('');
    await widget.settings.save();
    widget.onChanged();
    _snack('Đã đặt lại vị trí lưu về mặc định (Documents).');
  }

  void _openSaveDir() {
    try {
      final d = Directory(StoragePaths.parent);
      if (!d.existsSync()) d.createSync(recursive: true);
      Process.run('explorer.exe', [d.path]);
    } catch (_) {}
  }

  Future<void> _testConnection() async {
    setState(() => _testing = true);
    try {
      final api = DeviceApi(_ip.text.trim());
      final id = await api.ping();
      _snack('Kết nối OK. Mã máy: $id');
    } catch (e) {
      _snack('$e', error: true);
    } finally {
      if (mounted) setState(() => _testing = false);
    }
  }

  Future<void> _sendWifi() async {
    if (_ssid.text.trim().isEmpty) {
      _snack('Nhập tên WiFi (SSID).', error: true);
      return;
    }
    try {
      final api = DeviceApi(_ip.text.trim());
      await api.setWifi(_ssid.text.trim(), _pass.text);
      _snack('Đã gửi cấu hình WiFi tới máy.');
    } catch (e) {
      // Firmware hiện chưa có /setwifi -> hướng dẫn captive portal
      _showCaptivePortalHelp('$e');
    }
  }

  void _showCaptivePortalHelp(String reason) {
    showDialog(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Cấu hình WiFi cho máy'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Chưa gửi trực tiếp được ($reason).\n',
                  style: const TextStyle(color: Colors.redAccent)),
              const Text(
                'Cách cấu hình hiện tại (WiFiManager captive portal):\n'
                '1. Trên máy, vào chế độ cấu hình WiFi (theo hướng dẫn thiết bị).\n'
                '2. Máy tạo điểm phát WiFi (AP) cấu hình.\n'
                '3. Trên PC, kết nối vào AP đó.\n'
                '4. Trang cấu hình tự mở — chọn WiFi và nhập mật khẩu.\n\n'
                'Sau khi firmware bổ sung endpoint /setwifi, app sẽ gửi trực tiếp '
                'mà không cần đổi mạng PC (xem docs/APP_SPEC.md, mục roadmap).',
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c), child: const Text('Đã hiểu')),
        ],
      ),
    );
  }

  void _snack(String msg, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(msg),
      backgroundColor: error ? Colors.red.shade700 : null,
      duration: Duration(seconds: error ? 4 : 1),
    ));
  }

  Future<void> _backup() async {
    try {
      final json = await BackupService.buildJson();
      String p2(int x) => x.toString().padLeft(2, '0');
      final n = DateTime.now();
      final name = 'FBT_RAPID_backup_${n.year}${p2(n.month)}${p2(n.day)}_'
          '${p2(n.hour)}${p2(n.minute)}.json';
      final loc = await getSaveLocation(
        suggestedName: name,
        acceptedTypeGroups: const [
          XTypeGroup(label: 'JSON', extensions: ['json'])
        ],
      );
      if (loc == null) return; // người dùng huỷ
      await File(loc.path).writeAsString(json);
      _snack('Đã sao lưu: ${loc.path}');
    } catch (e) {
      _snack('Lỗi sao lưu: $e', error: true);
    }
  }

  Future<void> _restore() async {
    try {
      final file = await openFile(acceptedTypeGroups: const [
        XTypeGroup(label: 'JSON', extensions: ['json'])
      ]);
      if (file == null) return;
      final backup =
          jsonDecode(await file.readAsString()) as Map<String, dynamic>;
      if (!BackupService.isValid(backup)) {
        _snack('File không phải bản sao lưu FBT_RAPID.', error: true);
        return;
      }
      if (!mounted) return;
      final mode = await showDialog<String>(
        context: context,
        builder: (c) => AlertDialog(
          title: const Text('Khôi phục từ bản sao lưu'),
          content: const Text(
              'Cài đặt sẽ được cập nhật theo file. Với lịch sử xét nghiệm, chọn:'),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(c, 'cancel'),
                child: const Text('Hủy')),
            TextButton(
                onPressed: () => Navigator.pop(c, 'replace'),
                child: const Text('Thay thế')),
            FilledButton(
                onPressed: () => Navigator.pop(c, 'merge'),
                child: const Text('Gộp')),
          ],
        ),
      );
      if (mode == null || mode == 'cancel') return;
      final count =
          await BackupService.restore(backup, mergeHistory: mode == 'merge');
      if (!mounted) return;
      // nạp lại cài đặt vào màn hình
      final fresh = await AppSettings.load();
      if (!mounted) return;
      widget.settings
        ..deviceIp = fresh.deviceIp
        ..readingIntervalSec = fresh.readingIntervalSec
        ..userName = fresh.userName
        ..userOrg = fresh.userOrg
        ..cloudApiUrl = fresh.cloudApiUrl
        ..saveDir = fresh.saveDir;
      StoragePaths.setParent(fresh.saveDir);
      _ip.text = fresh.deviceIp;
      _interval.text = fresh.readingIntervalSec.toString();
      _name.text = fresh.userName;
      _org.text = fresh.userOrg;
      widget.onChanged();
      setState(() {});
      _snack('Đã khôi phục (${mode == 'merge' ? 'gộp' : 'thay thế'}) — '
          '$count lần chạy. Mở tab Lịch sử để xem.');
    } catch (e) {
      _snack('Lỗi khôi phục: $e', error: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Cài đặt'),
        actions: [
          TextButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.save),
            label: const Text('Lưu'),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _section('${tr('us.appearance')} & ${tr('us.language')}'),
          Card(
            margin: EdgeInsets.zero,
            child: Column(
              children: [
                SwitchListTile(
                  secondary: Icon(AppPrefs.instance.isDark
                      ? Icons.dark_mode
                      : Icons.light_mode),
                  title: Text(tr('us.darkMode')),
                  value: AppPrefs.instance.isDark,
                  onChanged: (v) => AppPrefs.instance.setDark(v),
                ),
                const Divider(height: 1),
                ListTile(
                  leading: const Icon(Icons.language),
                  title: Text(tr('us.language')),
                  trailing: DropdownButton<String>(
                    value: AppPrefs.instance.localeCode,
                    underline: const SizedBox.shrink(),
                    items: [
                      for (final e in kSupportedLanguages.entries)
                        DropdownMenuItem(value: e.key, child: Text(e.value)),
                    ],
                    onChanged: (v) {
                      if (v != null) AppPrefs.instance.setLocale(v);
                    },
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 24),
          // _section('Kết nối máy'),
          // TextField(
          //   controller: _ip,
          //   decoration: const InputDecoration(
          //     labelText: 'Địa chỉ IP của máy',
          //     hintText: 'vd: 192.168.1.50',
          //     prefixIcon: Icon(Icons.lan_outlined),
          //     border: OutlineInputBorder(),
          //   ),
          // ),
          // const SizedBox(height: 8),
          // Align(
          //   alignment: Alignment.centerLeft,
          //   child: OutlinedButton.icon(
          //     onPressed: _testing ? null : _testConnection,
          //     icon: _testing
          //         ? const SizedBox(
          //             width: 16,
          //             height: 16,
          //             child: CircularProgressIndicator(strokeWidth: 2))
          //         : const Icon(Icons.wifi_find),
          //     label: const Text('Kiểm tra kết nối'),
          //   ),
          // ),
          const SizedBox(height: 12),
          TextField(
            controller: _interval,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(
              labelText: 'Khoảng đọc (giây) — để quy đổi trục thời gian đồ thị',
              hintText: 'mặc định 20',
              prefixIcon: Icon(Icons.timer_outlined),
              border: OutlineInputBorder(),
            ),
          ),

          // const SizedBox(height: 24),
          // _section('Cài WiFi cho máy'),
          // TextField(
          //   controller: _ssid,
          //   decoration: const InputDecoration(
          //     labelText: 'Tên WiFi (SSID)',
          //     prefixIcon: Icon(Icons.wifi),
          //     border: OutlineInputBorder(),
          //   ),
          // ),
          // const SizedBox(height: 8),
          // TextField(
          //   controller: _pass,
          //   obscureText: true,
          //   decoration: const InputDecoration(
          //     labelText: 'Mật khẩu WiFi',
          //     prefixIcon: Icon(Icons.password),
          //     border: OutlineInputBorder(),
          //   ),
          // ),
          // const SizedBox(height: 8),
          // Align(
          //   alignment: Alignment.centerLeft,
          //   child: FilledButton.icon(
          //     onPressed: _sendWifi,
          //     icon: const Icon(Icons.send),
          //     label: const Text('Gửi tới máy'),
          //   ),
          // ),
          // const SizedBox(height: 4),
          // const Text(
          //   'Lưu ý: bản firmware hiện tại cấu hình WiFi qua captive portal. '
          //   'Nút "Gửi tới máy" sẽ hoạt động khi firmware bổ sung endpoint /setwifi.',
          //   style: TextStyle(fontSize: 12, color: Colors.grey),
          // ),

          // const SizedBox(height: 24),
          // _section('Thông tin người dùng'),
          // TextField(
          //   controller: _name,
          //   decoration: const InputDecoration(
          //     labelText: 'Tên người dùng',
          //     prefixIcon: Icon(Icons.person_outline),
          //     border: OutlineInputBorder(),
          //   ),
          // ),
          // const SizedBox(height: 8),
          // TextField(
          //   controller: _org,
          //   decoration: const InputDecoration(
          //     labelText: 'Đơn vị / phòng khám',
          //     prefixIcon: Icon(Icons.business_outlined),
          //     border: OutlineInputBorder(),
          //   ),
          // ),

          const SizedBox(height: 24),
          _section('Vị trí lưu file'),
          Card(
            margin: EdgeInsets.zero,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Icon(Icons.folder_outlined, size: 18),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          StoragePaths.parent,
                          style: const TextStyle(fontWeight: FontWeight.w600),
                        ),
                      ),
                      if (widget.settings.saveDir.isEmpty)
                        const Text('(mặc định)',
                            style:
                                TextStyle(fontSize: 12, color: Colors.grey)),
                    ],
                  ),
                  const SizedBox(height: 6),
                  const Text('├ Kết quả CT  →  FBT_RAPID_ketqua\\',
                      style: TextStyle(fontSize: 12, color: Colors.grey)),
                  const Text('└ Log nhiệt   →  FBT_RAPID_templog\\',
                      style: TextStyle(fontSize: 12, color: Colors.grey)),
                ],
              ),
            ),
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              OutlinedButton.icon(
                onPressed: _pickSaveDir,
                icon: const Icon(Icons.drive_folder_upload_outlined),
                label: const Text('Chọn thư mục…'),
              ),
              // OutlinedButton.icon(
              //   onPressed:
              //       widget.settings.saveDir.isEmpty ? null : _resetSaveDir,
              //   icon: const Icon(Icons.restart_alt),
              //   label: const Text('Về mặc định'),
              // ),
              OutlinedButton.icon(
                onPressed: _openSaveDir,
                icon: const Icon(Icons.folder_open),
                label: const Text('Mở thư mục'),
              ),
            ],
          ),
          const SizedBox(height: 4),
          const Text(
            'Nơi lưu kết quả CT (4 đồ thị + data.json) và log nhiệt (log/đồ thị/UART). '
            'Mặc định là thư mục Documents.',
            style: TextStyle(fontSize: 12, color: Colors.grey),
          ),

          // const SizedBox(height: 24),
          // _section('Sao lưu & Khôi phục'),
          // Wrap(
          //   spacing: 8,
          //   runSpacing: 8,
          //   children: [
          //     OutlinedButton.icon(
          //       onPressed: _backup,
          //       icon: const Icon(Icons.backup_outlined),
          //       label: const Text('Sao lưu lịch sử'),
          //     ),
          //     OutlinedButton.icon(
          //       onPressed: _restore,
          //       icon: const Icon(Icons.restore),
          //       label: const Text('Khôi phục từ file'),
          //     ),
          //   ],
          // ),
          // const SizedBox(height: 4),
          // const Text(
          //   'Sao lưu xuất lịch sử + cài đặt ra 1 file JSON (chọn nơi lưu — vd USB, Drive). '
          //   'Khôi phục đọc lại file đó: Gộp (giữ dữ liệu hiện có) hoặc Thay thế.',
          //   style: TextStyle(fontSize: 12, color: Colors.grey),
          // ),

          // const SizedBox(height: 24),
          // Center(
          //   child: FilledButton.icon(
          //     onPressed: _save,
          //     icon: const Icon(Icons.save),
          //     label: const Text('Lưu cài đặt'),
          //   ),
          // ),
        ],
      ),
    );
  }

  Widget _section(String title) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(title,
            style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
      );
}
