import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Tuỳ chọn ứng dụng **toàn cục**: theme (sáng/tối) + ngôn ngữ. Lưu cục bộ
/// (`shared_preferences`) và `notifyListeners()` để rebuild `MaterialApp` khi
/// đổi. Dùng qua singleton [AppPrefs.instance].
class AppPrefs extends ChangeNotifier {
  AppPrefs._();
  static final AppPrefs instance = AppPrefs._();

  static const _kTheme = 'app_theme_mode';
  static const _kLocale = 'app_locale';

  ThemeMode _themeMode = ThemeMode.light;
  String _localeCode = 'vi';

  ThemeMode get themeMode => _themeMode;
  String get localeCode => _localeCode;
  bool get isDark => _themeMode == ThemeMode.dark;

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    switch (p.getString(_kTheme)) {
      case 'dark':
        _themeMode = ThemeMode.dark;
        break;
      case 'system':
        _themeMode = ThemeMode.system;
        break;
      default:
        _themeMode = ThemeMode.light;
    }
    _localeCode = p.getString(_kLocale) ?? 'vi';
    notifyListeners();
  }

  Future<void> setThemeMode(ThemeMode m) async {
    if (_themeMode == m) return;
    _themeMode = m;
    notifyListeners();
    final p = await SharedPreferences.getInstance();
    await p.setString(
        _kTheme,
        m == ThemeMode.dark
            ? 'dark'
            : m == ThemeMode.system
                ? 'system'
                : 'light');
  }

  Future<void> setDark(bool dark) =>
      setThemeMode(dark ? ThemeMode.dark : ThemeMode.light);

  Future<void> setLocale(String code) async {
    if (_localeCode == code) return;
    _localeCode = code;
    notifyListeners();
    final p = await SharedPreferences.getInstance();
    await p.setString(_kLocale, code);
  }
}

/// Các ngôn ngữ hỗ trợ (mã → tên hiển thị). Việt/Anh dịch trước; Trung/Thái
/// đã có khung, bổ sung bản dịch dần.
const Map<String, String> kSupportedLanguages = {
  'vi': 'Tiếng Việt',
  'en': 'English',
  'zh': '中文',
  'th': 'ไทย',
};
