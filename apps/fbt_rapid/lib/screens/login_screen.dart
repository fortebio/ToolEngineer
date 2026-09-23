import 'package:flutter/material.dart';

import '../services/auth_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/app_version.dart';
import 'home_shell.dart';

/// Màn đăng nhập (username + mật khẩu) — gọi `POST /auth` của Engineer Server để
/// xác thực và lấy vai trò + danh sách mã máy được cấp. Xong thì lưu phiên rồi
/// vào màn chính (phân quyền theo vai trò).
class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _userCtrl = TextEditingController();
  final _passCtrl = TextEditingController();
  final _passFocus = FocusNode();
  bool _obscure = true;
  bool _busy = false;
  String? _error;

  @override
  void dispose() {
    _userCtrl.dispose();
    _passCtrl.dispose();
    _passFocus.dispose();
    super.dispose();
  }

  Future<void> _login() async {
    final user = _userCtrl.text.trim();
    final pass = _passCtrl.text;
    if (user.isEmpty || pass.isEmpty) {
      setState(() => _error = 'Nhập đầy đủ tài khoản và mật khẩu.');
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final api = await AuthApi.engineer();
      final session = await api.login(user, pass);
      await SessionStore.save(session);
      if (!mounted) return;
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(builder: (_) => const HomeShell()),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;

    return Scaffold(
      body: Stack(
        children: [
          // Nền loang thương hiệu, rất nhạt. Đây là chỗ DUY NHẤT trong app có
          // nền không phẳng — màn đăng nhập là màn duy nhất không phải làm việc,
          // nên là chỗ duy nhất được phép trang trí.
          const Positioned.fill(child: _BrandWash()),
          Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(28),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 420),
                child: AppFadeIn(
                  child: AppCard(
                    padding: const EdgeInsets.fromLTRB(32, 36, 32, 32),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        const Center(child: _Mark()),
                        const SizedBox(height: 22),
                        Text(
                          'FBT RAPID',
                          textAlign: TextAlign.center,
                          style: theme.textTheme.headlineSmall?.copyWith(
                            // DM Sans, KHÔNG Source Serif 4: chữ có chân đá nhau
                            // với tông soft. Nhấn bằng độ đậm + giãn chữ.
                            fontFamily: 'DM Sans',
                            fontWeight: FontWeight.w700,
                            letterSpacing: 1.6,
                            color: cs.onSurface,
                          ),
                        ),
                        const SizedBox(height: 6),
                        Text(
                          'Đăng nhập để tiếp tục',
                          textAlign: TextAlign.center,
                          style: theme.textTheme.bodyMedium
                              ?.copyWith(color: cs.onSurfaceVariant),
                        ),
                        const SizedBox(height: 30),
                        TextField(
                          controller: _userCtrl,
                          autofocus: true,
                          enabled: !_busy,
                          textInputAction: TextInputAction.next,
                          onSubmitted: (_) => _passFocus.requestFocus(),
                          decoration: const InputDecoration(
                            labelText: 'Tài khoản',
                            prefixIcon: Icon(Icons.person_outline),
                          ),
                        ),
                        const SizedBox(height: 16),
                        TextField(
                          controller: _passCtrl,
                          focusNode: _passFocus,
                          enabled: !_busy,
                          obscureText: _obscure,
                          textInputAction: TextInputAction.done,
                          onSubmitted: (_) => _busy ? null : _login(),
                          decoration: InputDecoration(
                            labelText: 'Mật khẩu',
                            prefixIcon: const Icon(Icons.key_outlined),
                            suffixIcon: IconButton(
                              tooltip: _obscure ? 'Hiện' : 'Ẩn',
                              icon: Icon(_obscure
                                  ? Icons.visibility_outlined
                                  : Icons.visibility_off_outlined),
                              onPressed: () =>
                                  setState(() => _obscure = !_obscure),
                            ),
                          ),
                        ),
                        if (_error != null) ...[
                          const SizedBox(height: 16),
                          _ErrorNote(text: _error!),
                        ],
                        const SizedBox(height: 26),
                        FilledButton.icon(
                          onPressed: _busy ? null : _login,
                          icon: _busy
                              ? SizedBox(
                                  width: 18,
                                  height: 18,
                                  child: CircularProgressIndicator(
                                    strokeWidth: 2,
                                    // KHÔNG `Colors.white`: nền tối dùng nút cyan
                                    // sáng, chữ/spinner trên đó là màu mực đậm.
                                    color: cs.onPrimary,
                                  ),
                                )
                              : const Icon(Icons.arrow_forward_rounded),
                          label: Text(_busy ? 'Đang đăng nhập…' : 'Đăng nhập'),
                          style: FilledButton.styleFrom(
                              padding:
                                  const EdgeInsets.symmetric(vertical: 17)),
                        ),
                        // KHÔNG in "Forte Biotech" ở chân thẻ nữa: logo phía
                        // trên đã mang đúng dòng chữ đó, in lại là tên công ty
                        // xuất hiện HAI lần trong một thẻ. Dashboard web của
                        // firmware đã gặp và sửa đúng lỗi này khi đổi sang logo
                        // đầy đủ (FBT-DXD243/CLAUDE.md, mục Brand).
                        //
                        // Phiên bản thì CÓ in: màn đăng nhập là chỗ duy nhất
                        // ai cũng đi qua, và khi hỗ trợ từ xa câu hỏi đầu tiên
                        // luôn là "bản nào". Chỉ nhãn ngắn — chi tiết ở
                        // Thiết lập › Phiên bản sau khi vào.
                        const SizedBox(height: 18),
                        Center(
                          child: Text(
                            kIsDevBuild
                                ? '$appVersionLabel · $kAppBuildDate'
                                : appVersionLabel,
                            style: theme.textTheme.bodySmall?.copyWith(
                              fontFamily: 'JetBrains Mono',
                              fontSize: 11.5,
                              color: kIsDevBuild
                                  ? kWarning
                                  : cs.onSurfaceVariant,
                            ),
                          ),
                        ),
                      ],
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

/// Logo công ty — cùng file với dấu ở đầu thanh điều hướng, to hơn.
///
/// Ở đây logo được đọc như logo (có chữ "FORTE BIOTECH"), khác với trên rail nơi
/// nó chỉ còn là một dấu 40px. Vì vậy dòng chữ "FBT RAPID" bên dưới vẫn cần —
/// nó là tên SẢN PHẨM, logo mang tên CÔNG TY.
class _Mark extends StatelessWidget {
  const _Mark();

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Image.asset(
      'assets/images/forte-logo.png',
      height: 78,
      filterQuality: FilterQuality.medium,
      // Asset hỏng thì vẫn phải đăng nhập được — lùi về dấu chấm thương hiệu cũ.
      errorBuilder: (_, __, ___) => Container(
        width: 66,
        height: 66,
        decoration: BoxDecoration(
          color: cs.primaryContainer,
          borderRadius: BorderRadius.circular(AppRadius.card),
        ),
        alignment: Alignment.center,
        child: Container(
          width: 26,
          height: 26,
          decoration: BoxDecoration(
            // Cyan logo nguyên bản — là MARK, không phải chữ. Xem `app_theme.dart`.
            color: AppSemantic.of(context).mark,
            shape: BoxShape.circle,
          ),
        ),
      ),
    );
  }
}

/// Nền loang. Hai vệt tròn rất nhạt màu thương hiệu — đủ để nền không phẳng,
/// không đủ để tranh chấp với thẻ đăng nhập.
class _BrandWash extends StatelessWidget {
  const _BrandWash();

  @override
  Widget build(BuildContext context) {
    final a = kBrand.withValues(alpha: 0.16);
    final b = kBrandMint.withValues(alpha: 0.12);
    return DecoratedBox(
      decoration: BoxDecoration(
        gradient: RadialGradient(
          center: const Alignment(-0.8, -0.9),
          radius: 1.1,
          colors: [a, a.withValues(alpha: 0)],
        ),
      ),
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: RadialGradient(
            center: const Alignment(1.0, 1.0),
            radius: 1.0,
            colors: [b, b.withValues(alpha: 0)],
          ),
        ),
        child: const SizedBox.expand(),
      ),
    );
  }
}

/// Khối lỗi: nền lỗi rất nhạt + viền, thay cho một dòng chữ đỏ trần. Lỗi đăng
/// nhập là thứ người ta đọc trong lúc bực — nó cần một khối để mắt bắt được.
class _ErrorNote extends StatelessWidget {
  final String text;
  const _ErrorNote({required this.text});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: cs.error.withValues(alpha: 0.09),
        border: Border.all(color: cs.error.withValues(alpha: 0.35)),
        borderRadius: BorderRadius.circular(AppRadius.base),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(Icons.error_outline, size: 18, color: cs.error),
          const SizedBox(width: 10),
          Expanded(
            child: Text(text,
                style: TextStyle(fontSize: 13.5, height: 1.4, color: cs.error)),
          ),
        ],
      ),
    );
  }
}
