import 'dart:convert';
import 'dart:io';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../services/app_prefs.dart';
import '../services/app_settings.dart';
import '../services/auth_api.dart';
import '../services/backup_service.dart';
import '../services/session_store.dart';
import '../services/storage_paths.dart';
import '../util/i18n.dart';

/// Màn **Thiết lập** dùng CHUNG cho cả admin lẫn user (đồng bộ giống nhau):
/// tài khoản (email, mã máy được cấp, đổi mật khẩu/email), nhà cung cấp, giao
/// diện sáng/tối, ngôn ngữ, khoảng đọc, nơi lưu file, sao lưu & khôi phục.
class UserSettingsScreen extends StatefulWidget {
  final AppSettings settings;
  const UserSettingsScreen({super.key, required this.settings});

  @override
  State<UserSettingsScreen> createState() => _UserSettingsScreenState();
}

class _UserSettingsScreenState extends State<UserSettingsScreen> {
  late final TextEditingController _interval = TextEditingController(
      text: widget.settings.readingIntervalSec.toString());
  late final TextEditingController _name =
      TextEditingController(text: widget.settings.userName);
  late final TextEditingController _org =
      TextEditingController(text: widget.settings.userOrg);
  late final TextEditingController _rapidUrl =
      TextEditingController(text: widget.settings.rapidErpUrl);
  late final TextEditingController _rapidKey =
      TextEditingController(text: widget.settings.rapidErpKey);
  late final TextEditingController _rapidIds =
      TextEditingController(text: widget.settings.rapidErpDeviceIds);

  AuthApi get _auth => AuthApi(kDefaultAuthApiUrl);

  @override
  void dispose() {
    _interval.dispose();
    _name.dispose();
    _org.dispose();
    _rapidUrl.dispose();
    _rapidKey.dispose();
    _rapidIds.dispose();
    super.dispose();
  }

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? Colors.red.shade700 : null,
      duration: Duration(seconds: error ? 4 : 2),
    ));
  }

  Future<void> _save() async {
    widget.settings
      ..readingIntervalSec = int.tryParse(_interval.text.trim()) ??
          widget.settings.readingIntervalSec
      ..userName = _name.text.trim()
      ..userOrg = _org.text.trim()
      ..rapidErpUrl = _rapidUrl.text.trim()
      ..rapidErpKey = _rapidKey.text.trim()
      ..rapidErpDeviceIds = _rapidIds.text.trim();
    await widget.settings.save();
    _snack(tr('common.saved'));
  }

  // ---- Nơi lưu file ----
  Future<void> _pickSaveDir() async {
    final dir = await getDirectoryPath();
    if (dir == null) return;
    setState(() => widget.settings.saveDir = dir);
    StoragePaths.setParent(dir);
    await widget.settings.save();
    _snack('${tr('us.saveLocation')}: $dir');
  }

  Future<void> _resetSaveDir() async {
    setState(() => widget.settings.saveDir = '');
    StoragePaths.setParent('');
    await widget.settings.save();
    _snack(tr('common.saved'));
  }

  void _openSaveDir() {
    try {
      final d = Directory(StoragePaths.parent);
      if (!d.existsSync()) d.createSync(recursive: true);
      Process.run('explorer.exe', [d.path]);
    } catch (_) {}
  }

  // ---- Sao lưu & Khôi phục ----
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
      if (loc == null) return;
      await File(loc.path).writeAsString(json);
      _snack('${tr('common.saved')} ${loc.path}');
    } catch (e) {
      _snack('$e', error: true);
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
          title: Text(tr('us.restoreBtn')),
          content: const Text(
              'Cài đặt sẽ cập nhật theo file. Với lịch sử: Gộp hoặc Thay thế.'),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(c, 'cancel'),
                child: Text(tr('common.cancel'))),
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
      _interval.text = fresh.readingIntervalSec.toString();
      _name.text = fresh.userName;
      _org.text = fresh.userOrg;
      setState(() {});
      _snack('${tr('common.saved')} ($count)');
    } catch (e) {
      _snack('$e', error: true);
    }
  }

  // ---- Đổi mật khẩu ----
  Future<void> _changePassword() async {
    final cur = TextEditingController();
    final nw = TextEditingController();
    final cf = TextEditingController();
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('cp.title')),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          _pwField(cur, tr('cp.current')),
          const SizedBox(height: 10),
          _pwField(nw, tr('cp.new')),
          const SizedBox(height: 10),
          _pwField(cf, tr('cp.confirm')),
        ]),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('common.save'))),
        ],
      ),
    );
    if (ok != true) return;
    if (nw.text.isEmpty || nw.text != cf.text) {
      _snack(tr('cp.mismatch'), error: true);
      return;
    }
    final s = SessionStore.current;
    if (s == null) return;
    try {
      await _auth.changePassword(s.username, cur.text, nw.text);
      _snack(tr('cp.ok'));
    } catch (e) {
      _snack('$e', error: true);
    }
  }

  // ---- Đổi email ----
  Future<void> _changeEmail() async {
    final s = SessionStore.current;
    if (s == null) return;
    final email = TextEditingController(text: s.email);
    final pass = TextEditingController();
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('ce.title')),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(
              controller: email,
              keyboardType: TextInputType.emailAddress,
              decoration: InputDecoration(
                  labelText: tr('ce.new'),
                  border: const OutlineInputBorder())),
          const SizedBox(height: 10),
          _pwField(pass, tr('login.password')),
        ]),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('common.save'))),
        ],
      ),
    );
    if (ok != true) return;
    try {
      await _auth.changeEmail(s.username, pass.text, email.text.trim());
      await SessionStore.save(s.copyWith(email: email.text.trim()));
      if (mounted) setState(() {});
      _snack(tr('ce.ok'));
    } catch (e) {
      _snack('$e', error: true);
    }
  }

  Widget _pwField(TextEditingController c, String label) => TextField(
        controller: c,
        obscureText: true,
        decoration:
            InputDecoration(labelText: label, border: const OutlineInputBorder()),
      );

  @override
  Widget build(BuildContext context) {
    final s = SessionStore.current;
    return Scaffold(
      appBar: AppBar(
        title: Text(tr('us.title')),
        actions: [
          TextButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.save),
            label: Text(tr('common.save')),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // --- Tài khoản (theo phiên đăng nhập) ---
          _section(tr('us.accountInfo')),
          Card(
            margin: EdgeInsets.zero,
            child: Column(
              children: [
                _row(Icons.badge_outlined, tr('us.username'),
                    s?.username ?? '—'),
                _row(Icons.email_outlined, tr('us.email'),
                    (s?.email.isNotEmpty ?? false) ? s!.email : '—'),
                _row(Icons.verified_user_outlined, tr('us.role'),
                    s != null ? roleLabel(s.roleCode) : '—'),
                _row(
                  Icons.memory_outlined,
                  tr('us.devices'),
                  (s?.allowAll ?? false)
                      ? '*'
                      : (s?.ids.isNotEmpty ?? false)
                          ? s!.ids.join(', ')
                          : '—',
                ),
              ],
            ),
          ),
          const SizedBox(height: 8),
          Wrap(spacing: 8, runSpacing: 8, children: [
            OutlinedButton.icon(
              onPressed: _changePassword,
              icon: const Icon(Icons.password),
              label: Text(tr('us.changePassword')),
            ),
            OutlinedButton.icon(
              onPressed: _changeEmail,
              icon: const Icon(Icons.alternate_email),
              label: Text(tr('us.changeEmail')),
            ),
          ]),

          // // --- Thông tin hiển thị (Tên / Đơn vị) ---
          // const SizedBox(height: 24),
          // _section(tr('us.display')),
          // TextField(
          //   controller: _name,
          //   decoration: InputDecoration(
          //     labelText: tr('us.userName'),
          //     prefixIcon: const Icon(Icons.person_outline),
          //     border: const OutlineInputBorder(),
          //   ),
          // ),
          // const SizedBox(height: 8),
          // TextField(
          //   controller: _org,
          //   decoration: InputDecoration(
          //     labelText: tr('us.userOrg'),
          //     prefixIcon: const Icon(Icons.business_outlined),
          //     border: const OutlineInputBorder(),
          //   ),
          // ),


          // --- Giao diện + Ngôn ngữ ---
          const SizedBox(height: 24),
          _section('${tr('us.appearance')} & ${tr('us.language')}'),
          Card(
            margin: EdgeInsets.zero,
            child: Column(children: [
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
            ]),
          ),

          // // --- Khoảng đọc ---
          // const SizedBox(height: 24),
          // _section(tr('us.interval')),
          // TextField(
          //   controller: _interval,
          //   keyboardType: TextInputType.number,
          //   decoration: InputDecoration(
          //     labelText: tr('us.interval'),
          //     hintText: tr('us.intervalHint'),
          //     prefixIcon: const Icon(Icons.timer_outlined),
          //     border: const OutlineInputBorder(),
          //   ),
          // ),

          // --- Nơi lưu file ---
          const SizedBox(height: 24),
          _section(tr('us.saveLocation')),
          Card(
            margin: EdgeInsets.zero,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Row(children: [
                const Icon(Icons.folder_outlined, size: 18),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(StoragePaths.parent,
                      style: const TextStyle(fontWeight: FontWeight.w600)),
                ),
              ]),
            ),
          ),
          const SizedBox(height: 8),
          Wrap(spacing: 8, runSpacing: 8, children: [
            OutlinedButton.icon(
              onPressed: _pickSaveDir,
              icon: const Icon(Icons.drive_folder_upload_outlined),
              label: Text(tr('us.chooseFolder')),
            ),
            // OutlinedButton.icon(
            //   onPressed: widget.settings.saveDir.isEmpty ? null : _resetSaveDir,
            //   icon: const Icon(Icons.restart_alt),
            //   label: Text(tr('us.defaultFolder')),
            // ),
            OutlinedButton.icon(
              onPressed: _openSaveDir,
              icon: const Icon(Icons.folder_open),
              label: Text(tr('us.openFolder')),
            ),
          ]),
          // --- Nhà cung cấp ---
          const SizedBox(height: 24),
          _section(tr('us.provider')),
          const Card(
            margin: EdgeInsets.zero,
            child: ListTile(
              leading: CircleAvatar(child: Icon(Icons.business)),
              title: Text('Fortebiotech'),
              subtitle:
                  Text('Forte Rapid+ / FBT_RAPID\nhttps://fortebiotech.com'),
              isThreeLine: true,
            ),
          ),

          // --- RAPID ERP (API ngoài) — CHỈ admin nhập ---
          if (SessionStore.canWrite) ...[
            const SizedBox(height: 24),
            _section(tr('us.rapidErp')),
            TextField(
              controller: _rapidUrl,
              decoration: InputDecoration(
                labelText: tr('us.rapidErpUrl'),
                hintText: kDefaultRapidErpUrl,
                prefixIcon: const Icon(Icons.api_outlined),
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _rapidKey,
              obscureText: true,
              decoration: InputDecoration(
                labelText: tr('us.rapidErpKey'),
                prefixIcon: const Icon(Icons.vpn_key_outlined),
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 6),
            Text(tr('us.rapidErpHint'),
                style: Theme.of(context).textTheme.bodySmall),
            const SizedBox(height: 12),
            TextField(
              controller: _rapidIds,
              minLines: 2,
              maxLines: 4,
              decoration: InputDecoration(
                labelText: tr('us.rapidErpIds'),
                hintText: 'RPL03010, RPL02013, …',
                prefixIcon: const Icon(Icons.list_alt),
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 6),
            Text(tr('us.rapidErpIdsHint'),
                style: Theme.of(context).textTheme.bodySmall),
          ],

          // // --- Sao lưu & Khôi phục ---
          // const SizedBox(height: 24),
          // _section(tr('us.backup')),
          // Wrap(spacing: 8, runSpacing: 8, children: [
          //   OutlinedButton.icon(
          //     onPressed: _backup,
          //     icon: const Icon(Icons.backup_outlined),
          //     label: Text(tr('us.backupBtn')),
          //   ),
          //   OutlinedButton.icon(
          //     onPressed: _restore,
          //     icon: const Icon(Icons.restore),
          //     label: Text(tr('us.restoreBtn')),
          //   ),
          // ]),
          // const SizedBox(height: 24),
        ],
      ),
    );
  }

  Widget _section(String title) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(title,
            style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
      );

  Widget _row(IconData icon, String label, String value) => ListTile(
        dense: true,
        leading: Icon(icon, size: 20),
        title: Text(label, style: const TextStyle(fontSize: 13)),
        subtitle: Text(value, style: const TextStyle(fontSize: 15)),
      );
}
