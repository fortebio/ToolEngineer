import 'package:shared_preferences/shared_preferences.dart';

/// Nguồn dữ liệu cloud trong app:
/// - [google]   : Google Apps Script (mặc định, hợp đồng `action=ids|runs|run`).
/// - [rapidErp] : API RAPID ERP ngoài (REST `/external/...`, header `X-API-Key`).
enum CloudSource { google, rapidErp }

/// URL Apps Script (/exec) **gắn sẵn** để khỏi phải nhập. Nếu deploy URL mới
/// thì đổi đúng 1 dòng này (rồi build lại). Vẫn có thể ghi đè trong Cài đặt.
const String kDefaultCloudApiUrl =
    'https://script.google.com/macros/s/AKfycbw2VXXLX6fUMgmyRrSgNgEi3b4gSyE2bdctQe_DNOnlZ58EfPclQrXrlMenH0y7SH5X/exec';

/// URL Apps Script **accounts/auth** (web app RIÊNG — sheet/userAuth.js) dùng
/// để đăng nhập + phân quyền. Đổi deploy mới thì sửa đúng 1 dòng này.
const String kDefaultAuthApiUrl =
    // 'https://script.google.com/macros/s/AKfycbyW1BHL3-zBAoFjMJRMFLCRkwbDFWoGnneMwA_8XDzKWysucfmGX0dNAVSOraagyCEY/exec';
    'https://script.google.com/macros/s/AKfycbwuO8JSHFqZzhetYDv8Fd01iwlkb9SWEzXTTHqhGDNh5-jaes-xFc2xaXI3UBuzqetQ/exec';

/// URL gốc REST của API **RAPID ERP** (server NGOÀI — không nằm trong repo).
/// Hợp đồng: `GET /external/device/{id}/results`, `/external/results/{id}/detail`.
/// Đổi deploy mới thì sửa đúng 1 dòng này (vẫn ghi đè được trong Cài đặt).
const String kDefaultRapidErpUrl = 'https://api.fortebio.tech/api/v1/results';

/// Cấu hình app lưu cục bộ: địa chỉ máy, khoảng đọc, thông tin người dùng.
class AppSettings {
  String deviceIp;
  int readingIntervalSec; // dùng quy đổi trục thời gian đồ thị
  String userName;
  String userOrg; // đơn vị / phòng khám
  String cloudApiUrl; // URL Apps Script /exec để đọc lịch sử cloud (doGet)
  String saveDir; // thư mục gốc lưu file (trống = Documents)
  String rapidErpUrl; // URL gốc REST API RAPID ERP (server ngoài)
  String rapidErpKey; // X-API-Key (đọc nguồn RAPID ERP); dùng chung key /ingest
  String rapidErpDeviceIds; // danh sách mã máy admin nhập tay (API ko liệt kê)

  AppSettings({
    this.deviceIp = '',
    this.readingIntervalSec = 20,
    this.userName = '',
    this.userOrg = '',
    this.cloudApiUrl = kDefaultCloudApiUrl,
    this.saveDir = '',
    this.rapidErpUrl = kDefaultRapidErpUrl,
    this.rapidErpKey = '',
    this.rapidErpDeviceIds = '',
  });

  static const _kIp = 'device_ip';
  static const _kInterval = 'reading_interval_sec';
  static const _kUserName = 'user_name';
  static const _kUserOrg = 'user_org';
  static const _kCloudUrl = 'cloud_api_url';
  static const _kSaveDir = 'save_dir';
  static const _kRapidErpUrl = 'rapid_erp_url';
  static const _kRapidErpKey = 'rapid_erp_key';
  static const _kRapidErpDeviceIds = 'rapid_erp_device_ids';

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
      // Trống/chưa có → dùng URL gắn sẵn (khỏi nhập).
      rapidErpUrl: _orDefaultUrl(p.getString(_kRapidErpUrl), kDefaultRapidErpUrl),
      rapidErpKey: p.getString(_kRapidErpKey) ?? '',
      rapidErpDeviceIds: p.getString(_kRapidErpDeviceIds) ?? '',
    );
  }

  static String _orDefault(String? stored) =>
      _orDefaultUrl(stored, kDefaultCloudApiUrl);

  static String _orDefaultUrl(String? stored, String fallback) {
    return (stored == null || stored.trim().isEmpty) ? fallback : stored;
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kIp, deviceIp);
    await p.setInt(_kInterval, readingIntervalSec);
    await p.setString(_kUserName, userName);
    await p.setString(_kUserOrg, userOrg);
    await p.setString(_kCloudUrl, cloudApiUrl);
    await p.setString(_kSaveDir, saveDir);
    await p.setString(_kRapidErpUrl, rapidErpUrl);
    await p.setString(_kRapidErpKey, rapidErpKey);
    await p.setString(_kRapidErpDeviceIds, rapidErpDeviceIds);
  }

  /// URL cloud theo nguồn đang chọn.
  String cloudUrlFor(CloudSource s) {
    switch (s) {
      case CloudSource.rapidErp:
        return rapidErpUrl;
      case CloudSource.google:
        return cloudApiUrl;
    }
  }

  /// Header theo nguồn: RAPID ERP kèm `X-API-Key`; Google rỗng. (Header rỗng nếu
  /// chưa nhập key.)
  Map<String, String> cloudHeadersFor(CloudSource s) {
    switch (s) {
      case CloudSource.rapidErp:
        return rapidErpKey.trim().isNotEmpty
            ? {'X-API-Key': rapidErpKey.trim()}
            : const {};
      case CloudSource.google:
        return const {};
    }
  }

  /// Danh sách mã máy admin nhập tay cho nguồn RAPID ERP (vì API KHÔNG có
  /// endpoint liệt kê máy). Tách theo phẩy/xuống dòng/khoảng trắng, bỏ rỗng và
  /// `'*'`, dedupe không phân biệt hoa thường, giữ thứ tự nhập.
  List<String> get rapidErpDeviceIdList {
    final seen = <String>{};
    final out = <String>[];
    for (final raw in rapidErpDeviceIds.split(RegExp(r'[\s,;]+'))) {
      final id = raw.trim();
      if (id.isEmpty || id == '*') continue;
      if (seen.add(id.toLowerCase())) out.add(id);
    }
    return out;
  }
}
