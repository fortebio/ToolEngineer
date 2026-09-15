import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Tuỳ chọn ứng dụng **toàn cục**: ngôn ngữ. Lưu cục bộ (`shared_preferences`)
/// và `notifyListeners()` để rebuild `MaterialApp` khi đổi. Dùng qua singleton
/// [AppPrefs.instance].
///
/// **Dark mode đã bỏ 2026-08-19** — app chỉ còn theme sáng. Khoá cũ
/// `app_theme_mode` cố ý KHÔNG đọc nữa: máy nào đang lưu `'dark'` mà vẫn đọc thì
/// sẽ mang theo một lựa chọn không còn đường nào đổi lại.
class AppPrefs extends ChangeNotifier {
  AppPrefs._();
  static final AppPrefs instance = AppPrefs._();

  static const _kLocale = 'app_locale';

  String _localeCode = 'vi';

  String get localeCode => _localeCode;

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    _localeCode = p.getString(_kLocale) ?? 'vi';
    notifyListeners();
  }

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
