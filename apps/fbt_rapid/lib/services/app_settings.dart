import 'package:shared_preferences/shared_preferences.dart';

/// URL Apps Script (/exec) **gắn sẵn** để khỏi phải nhập. Nếu deploy URL mới
/// thì đổi đúng 1 dòng này (rồi build lại). Vẫn có thể ghi đè trong Cài đặt.
const String kDefaultCloudApiUrl =
    'https://script.google.com/macros/s/AKfycbw2VXXLX6fUMgmyRrSgNgEi3b4gSyE2bdctQe_DNOnlZ58EfPclQrXrlMenH0y7SH5X/exec';

/// Cấu hình app lưu cục bộ: địa chỉ máy, khoảng đọc, thông tin người dùng.
class AppSettings {
  String deviceIp;
  int readingIntervalSec; // dùng quy đổi trục thời gian đồ thị
  String userName;
  String userOrg; // đơn vị / phòng khám
  String cloudApiUrl; // URL Apps Script /exec để đọc lịch sử cloud (doGet)
  String saveDir; // thư mục gốc lưu file (trống = Documents)

  AppSettings({
    this.deviceIp = '',
    this.readingIntervalSec = 20,
    this.userName = '',
    this.userOrg = '',
    this.cloudApiUrl = kDefaultCloudApiUrl,
    this.saveDir = '',
  });

  static const _kIp = 'device_ip';
  static const _kInterval = 'reading_interval_sec';
  static const _kUserName = 'user_name';
  static const _kUserOrg = 'user_org';
  static const _kCloudUrl = 'cloud_api_url';
  static const _kSaveDir = 'save_dir';

  static Future<AppSettings> load() async {
    final p = await SharedPreferences.getInstance();
    return AppSettings(
      deviceIp: p.getString(_kIp) ?? '',
      readingIntervalSec: p.getInt(_kInterval) ?? 20,
      userName: p.getString(_kUserName) ?? '',
      userOrg: p.getString(_kUserOrg) ?? '',
      // Trống/chưa có → dùng URL gắn sẵn (khỏi nhập).
      cloudApiUrl: _orDefault(p.getString(_kCloudUrl)),
      saveDir: p.getString(_kSaveDir) ?? '',
    );
  }

  static String _orDefault(String? stored) {
    return (stored == null || stored.trim().isEmpty)
        ? kDefaultCloudApiUrl
        : stored;
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kIp, deviceIp);
    await p.setInt(_kInterval, readingIntervalSec);
    await p.setString(_kUserName, userName);
    await p.setString(_kUserOrg, userOrg);
    await p.setString(_kCloudUrl, cloudApiUrl);
    await p.setString(_kSaveDir, saveDir);
  }
}
