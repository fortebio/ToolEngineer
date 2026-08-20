import 'package:flutter/material.dart';

import '../services/auth_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';

/// Màn **Quản lý User** (chỉ admin): tạo user, cấp/đổi mã máy được cấp,
/// tắt/bật tài khoản, xóa. Mọi thao tác gọi Engineer Server (POST /auth) với
/// adminUser + adminPassword (hỏi 1 lần khi vào màn, giữ trong bộ nhớ).
class UserManagementScreen extends StatefulWidget {
  const UserManagementScreen({super.key});

  @override
  State<UserManagementScreen> createState() => _UserManagementScreenState();
}

class _UserManagementScreenState extends State<UserManagementScreen> {
  // Tài khoản trên Engineer Server (POST /auth) — nạp URL/token từ Cài đặt 1 lần.
  AuthApi? _apiCache;
  Future<AuthApi> get _api async => _apiCache ??= await AuthApi.engineer();
  final _passCtrl = TextEditingController();

  String get _adminUser => SessionStore.current?.username ?? '';
  String? _adminPass; // mật khẩu admin đã xác thực (chỉ trong bộ nhớ)
  List<AccountInfo> _users = [];
  bool _busy = false;
  String? _error;

  @override
  void dispose() {
    _passCtrl.dispose();
    super.dispose();
  }

  void _snack(String m, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(m),
      backgroundColor: error ? Theme.of(context).colorScheme.error : null,
      duration: Duration(seconds: error ? 4 : 2),
    ));
  }

  /// Xác thực mật khẩu admin bằng cách gọi listUsers.
  Future<void> _verify() async {
    final pass = _passCtrl.text;
    if (pass.isEmpty) return;
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final users = await (await _api).listUsers(_adminUser, pass);
      setState(() {
        _adminPass = pass;
        _users = users;
      });
    } catch (e) {
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _reload() async {
    final pass = _adminPass;
    if (pass == null) return;
    setState(() => _busy = true);
    try {
      final users = await (await _api).listUsers(_adminUser, pass);
      setState(() => _users = users);
    } catch (e) {
      _snack('$e', error: true);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _toggleActive(AccountInfo u) async {
    final pass = _adminPass;
    if (pass == null) return;
    setState(() => _busy = true);
    try {
      await (await _api).saveUser(
        _adminUser,
        pass,
        username: u.username,
        role: u.role,
        ids: u.ids,
        name: u.name,
        email: u.email,
        active: !u.active, // tắt ↔ bật
      );
      await _reload();
    } catch (e) {
      _snack('$e', error: true);
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _openEditor({AccountInfo? existing}) async {
    final result = await showDialog<_UserForm>(
      context: context,
      builder: (_) => _UserEditDialog(existing: existing),
    );
    if (result == null) return;
    final pass = _adminPass;
    if (pass == null) return;
    setState(() => _busy = true);
    try {
      await (await _api).saveUser(
        _adminUser,
        pass,
        username: result.username,
        role: result.role,
        ids: result.ids,
        name: result.name,
        email: result.email,
        password: result.password, // rỗng/null = giữ nguyên
        active: result.active,
      );
      _snack(tr('common.saved'));
      await _reload();
    } catch (e) {
      _snack('$e', error: true);
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _delete(AccountInfo u) async {
    final pass = _adminPass;
    if (pass == null) return;
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text('${tr('common.delete')} "${u.username}"'),
        content: Text(tr('um.deleteConfirm')),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              style: FilledButton.styleFrom(
                  backgroundColor: Theme.of(context).colorScheme.error),
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('common.delete'))),
        ],
      ),
    );
    if (ok != true) return;
    setState(() => _busy = true);
    try {
      await (await _api).deleteUser(_adminUser, pass, u.username);
      await _reload();
    } catch (e) {
      _snack('$e', error: true);
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    // Cùng khuôn với mọi tab khác: tiêu đề + phụ đề do `AppTabScaffold` lo, một
    // mục duy nhất nên dải chọn tự ẩn. "Tạo user" chuyển từ FAB lên hàng tiêu đề
    // — FAB nổi ở góc phải dưới che mất dòng cuối danh sách, và app này không
    // có cái FAB nào khác để nó thuộc về.
    return AppTabScaffold(
      title: tr('um.title'),
      subtitle: tr('um.hint'),
      index: 0,
      onChanged: (_) {},
      actions: [
        if (_adminPass != null) ...[
          IconButton(
            tooltip: tr('common.refresh'),
            onPressed: _busy ? null : _reload,
            icon: const Icon(Icons.refresh),
          ),
          FilledButton.icon(
            onPressed: _busy ? null : () => _openEditor(),
            icon: const Icon(Icons.person_add),
            label: Text(tr('um.create')),
          ),
        ],
      ],
      tabs: [
        AppTab(
          icon: Icons.manage_accounts_outlined,
          label: tr('um.title'),
          page: _adminPass == null ? _passwordGate() : _list(),
        ),
      ],
    );
  }

  Widget _passwordGate() {
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 360),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.admin_panel_settings_outlined, size: 48),
              const SizedBox(height: 12),
              Text(tr('um.adminPass'), textAlign: TextAlign.center),
              const SizedBox(height: 16),
              TextField(
                controller: _passCtrl,
                obscureText: true,
                autofocus: true,
                enabled: !_busy,
                onSubmitted: (_) => _verify(),
                decoration: InputDecoration(
                  labelText: tr('login.password'),
                  prefixIcon: const Icon(Icons.key_outlined),
                ),
              ),
              if (_error != null) ...[
                const SizedBox(height: 10),
                Text(_error!,
                    style: TextStyle(color: Theme.of(context).colorScheme.error)),
              ],
              const SizedBox(height: 16),
              FilledButton.icon(
                onPressed: _busy ? null : _verify,
                icon: _busy
                    ? const SizedBox(
                        width: 16,
                        height: 16,
                        child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.lock_open),
                label: Text(tr('um.continue')),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _list() {
    if (_users.isEmpty) {
      return Center(child: Text(tr('um.empty')));
    }
    return Stack(
      children: [
        ListView.separated(
          // 88px dưới trước đây chừa cho FAB "Tạo user" — FAB đã chuyển
          // lên hàng tiêu đề, chỗ trống đó giờ chỉ ăn mất một dòng tài khoản.
          padding: const EdgeInsets.all(12),
          itemCount: _users.length,
          separatorBuilder: (_, __) => const SizedBox(height: 6),
          itemBuilder: (context, i) {
            final u = _users[i];
            // Root luôn full (super-admin). Còn lại (kể cả admin) hiện đúng mã
            // được cấp; "*" trong ids = được xem full.
            final idsText =
                u.isRoot ? '*' : (u.ids.isEmpty ? '—' : u.ids.join(', '));
            return AppFadeIn(
              index: i,
              child: Card(
              margin: EdgeInsets.zero,
              child: ListTile(
                leading: CircleAvatar(
                  backgroundColor: u.active
                      ? Theme.of(context).colorScheme.primaryContainer
                      : Theme.of(context).colorScheme.outlineVariant,
                  child: Icon(u.isRoot
                      ? Icons.security
                      : u.isStaff
                          ? Icons.shield_outlined
                          : Icons.person_outline),
                ),
                title: Row(children: [
                  Flexible(
                    child: Text(
                      u.name.isNotEmpty ? u.name : u.username,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                  const SizedBox(width: 6),
                  _chip(roleLabel(u.role), _roleColor(context, u.role)),
                ]),
                subtitle: Text(
                  '@${u.username}'
                  '${u.email.isNotEmpty ? ' · ${u.email}' : ''}\n'
                  '${tr('us.devices')}: $idsText',
                ),
                isThreeLine: true,
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Tắt/Bật account
                    Switch(
                      value: u.active,
                      onChanged: _busy ? null : (_) => _toggleActive(u),
                    ),
                    PopupMenuButton<String>(
                      onSelected: (v) {
                        if (v == 'edit') _openEditor(existing: u);
                        if (v == 'delete') _delete(u);
                      },
                      itemBuilder: (_) => [
                        PopupMenuItem(value: 'edit', child: Text(tr('um.edit'))),
                        PopupMenuItem(
                            value: 'delete', child: Text(tr('common.delete'))),
                      ],
                    ),
                  ],
                ),
                onTap: _busy ? null : () => _openEditor(existing: u),
              ),
              ),
            );
          },
        ),
        if (_busy) const LinearProgressIndicator(minHeight: 2),
      ],
    );
  }

  /// Màu huy hiệu vai trò — lấy từ token, KHÔNG hardcode Colors.*.
  /// Tránh amber cho CHỮ (tương phản kém trên nền sáng): root = primary,
  /// admin = info, khách = onSurfaceVariant — đọc được ở cả light lẫn dark.
  Color _roleColor(BuildContext c, String role) {
    final cs = Theme.of(c).colorScheme;
    switch (role.toLowerCase()) {
      case 'root':
        return cs.primary;
      case 'admin':
        return AppSemantic.of(c).info;
      default:
        return cs.onSurfaceVariant;
    }
  }

  Widget _chip(String text, Color color) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.15),
          borderRadius: BorderRadius.circular(AppRadius.sm),
        ),
        child: Text(text,
            style: TextStyle(
                color: color, fontSize: 11, fontWeight: FontWeight.w600)),
      );
}

/// Dữ liệu trả về từ dialog tạo/sửa user.
class _UserForm {
  final String username;
  final String role;
  final List<String> ids;
  final String name;
  final String email;
  final String? password; // null/rỗng = giữ nguyên (khi sửa)
  final bool active;
  _UserForm({
    required this.username,
    required this.role,
    required this.ids,
    required this.name,
    required this.email,
    required this.password,
    required this.active,
  });
}

class _UserEditDialog extends StatefulWidget {
  final AccountInfo? existing;
  const _UserEditDialog({this.existing});

  @override
  State<_UserEditDialog> createState() => _UserEditDialogState();
}

class _UserEditDialogState extends State<_UserEditDialog> {
  late final TextEditingController _user =
      TextEditingController(text: widget.existing?.username ?? '');
  late final TextEditingController _name =
      TextEditingController(text: widget.existing?.name ?? '');
  late final TextEditingController _email =
      TextEditingController(text: widget.existing?.email ?? '');
  late final TextEditingController _ids = TextEditingController(
      text: (widget.existing?.ids ?? const []).join(', '));
  final _pass = TextEditingController();
  late String _role = widget.existing?.role == 'admin' ? 'admin' : 'user';
  late bool _active = widget.existing?.active ?? true;
  String? _error;

  bool get _isEdit => widget.existing != null;

  @override
  void dispose() {
    _user.dispose();
    _name.dispose();
    _email.dispose();
    _ids.dispose();
    _pass.dispose();
    super.dispose();
  }

  void _submit() {
    final username = _user.text.trim();
    if (username.isEmpty) {
      setState(() => _error = tr('um.needUsername'));
      return;
    }
    if (!_isEdit && _pass.text.isEmpty) {
      setState(() => _error = tr('um.needPassword')); // tạo mới bắt buộc mật khẩu
      return;
    }
    final ids = _ids.text
        .split(RegExp(r'[\s,;]+'))
        .map((e) => e.trim())
        .where((e) => e.isNotEmpty)
        .toList();
    Navigator.pop(
      context,
      _UserForm(
        username: username,
        role: _role,
        ids: ids,
        name: _name.text.trim(),
        email: _email.text.trim(),
        password: _pass.text.isEmpty ? null : _pass.text,
        active: _active,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(_isEdit ? tr('um.edit') : tr('um.create')),
      content: SizedBox(
        width: 360,
        child: SingleChildScrollView(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            TextField(
              controller: _user,
              enabled: !_isEdit, // sửa: không đổi username (khoá khớp dòng)
              decoration: InputDecoration(labelText: tr('us.username')),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _pass,
              obscureText: true,
              decoration: InputDecoration(
                  labelText: _isEdit ? tr('um.pwKeep') : tr('um.pwNew')),
            ),
            const SizedBox(height: 10),
            DropdownButtonFormField<String>(
              value: _role,
              decoration: InputDecoration(labelText: tr('us.role')),
              items: [
                DropdownMenuItem(value: 'user', child: Text(roleLabel('user'))),
                DropdownMenuItem(
                    value: 'admin', child: Text(roleLabel('admin'))),
                DropdownMenuItem(value: 'root', child: Text(roleLabel('root'))),
              ],
              onChanged: (v) => setState(() => _role = v ?? 'user'),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _ids,
              decoration: InputDecoration(labelText: tr('um.ids')),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _name,
              decoration: InputDecoration(labelText: tr('us.name')),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: _email,
              keyboardType: TextInputType.emailAddress,
              decoration: InputDecoration(labelText: tr('us.email')),
            ),
            const SizedBox(height: 4),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: Text(tr('um.active')),
              value: _active,
              onChanged: (v) => setState(() => _active = v),
            ),
            if (_error != null) ...[
              const SizedBox(height: 6),
              Align(
                alignment: Alignment.centerLeft,
                child: Text(_error!,
                    style:
                        TextStyle(color: Theme.of(context).colorScheme.error)),
              ),
            ],
          ]),
        ),
      ),
      actions: [
        TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(tr('common.cancel'))),
        FilledButton(onPressed: _submit, child: Text(tr('common.save'))),
      ],
    );
  }
}
