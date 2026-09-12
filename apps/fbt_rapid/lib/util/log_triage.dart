/// Quét log UART của máy (ESP32 / firmware Forte Rapid+) và rút ra **dấu hiệu**
/// mà nhân viên chăm sóc khách hàng đọc hiểu được — không cần biết ESP32.
///
/// Thuần Dart (không Flutter, không i18n): trả về **khoá i18n** (`titleKey` /
/// `hintKey`) để màn hình dịch bằng `tr()`. Nhờ vậy test được bằng `dart test`
/// và dùng chung cho desktop lẫn web.
///
/// Các mẫu chuỗi lấy từ thông điệp CHUẨN của ROM/ESP-IDF (`Brownout detector`,
/// `Guru Meditation`, `invalid header`, `rst:0x..`) — đúng những dòng CLAUDE.md
/// mục "Debug nạp xong chip tự reset" liệt kê — cộng vài mẫu riêng của máy
/// (`-127` = cảm biến nhiệt Dallas mất kết nối, mã máy `RPL#####`).
library;

enum TriageLevel { error, warning, info }

/// Một luật quét: regex + mức độ + khoá i18n cho tiêu đề/gợi ý.
class TriageRule {
  final String key; // định danh ổn định (gửi kèm log lên server)
  final RegExp pattern;
  final TriageLevel level;
  const TriageRule(this.key, this.pattern, this.level);

  String get titleKey => 'triage.$key';
  String get hintKey => 'triage.${key}Hint';
}

/// Kết quả một luật khớp: số dòng khớp + dòng đầu tiên (đã cắt) để dẫn chứng.
class TriageFinding {
  final TriageRule rule;
  final int count;
  final String sample;
  const TriageFinding(this.rule, this.count, this.sample);

  String get key => rule.key;
  TriageLevel get level => rule.level;
  String get titleKey => rule.titleKey;
  String get hintKey => rule.hintKey;
}

/// Toàn bộ kết quả quét một bộ log.
class TriageReport {
  final List<TriageFinding> findings; // xếp lỗi → cảnh báo → thông tin
  final String? version; // version firmware in trong log (nếu thấy)
  final String? deviceId; // mã máy in trong log (nếu thấy)
  final int lineCount;

  const TriageReport({
    this.findings = const [],
    this.version,
    this.deviceId,
    this.lineCount = 0,
  });

  static const empty = TriageReport();

  bool get hasError => findings.any((f) => f.level == TriageLevel.error);
}

/// Bộ luật. Thứ tự = thứ tự ưu tiên hiển thị trong cùng một mức.
///
/// Ghi chú vì sao từng luật:
/// - `brownout`: máy tự reset khi điện áp tụt — nguyên nhân số 1 ngoài thị trường
///   là adapter/cáp nguồn, nhân viên CSKH xử lý được mà không cần kỹ thuật.
/// - `crash`: `Guru Meditation` + các biến thể (`abort()`, `assert failed`,
///   `Stack canary`, `LoadProhibited`…) — firmware lỗi, chỉ kỹ thuật sửa được.
/// - `noFirmware`: `invalid header: 0xffffffff` = flash trống/sai offset — máy
///   sau khi nạp code hỏng.
/// - `flashRead`: sai flash mode (DIO/QIO) hoặc flash hỏng.
/// - `watchdog`: một tác vụ treo → reset; kèm theo lý do reset `RTCWDT…`.
/// - `tempSensor`: DS18B20 trả `-127` khi đứt dây/không đọc được; `nan` cùng ý.
/// - `wifi`, `upload`, `ota`: máy KHÔNG lên mạng / KHÔNG gửi được kết quả là
///   ca CSKH gặp nhiều nhất ("máy chạy xong mà không thấy trên app").
/// - `idfError`: dòng `E (ms) tag:` chuẩn ESP-IDF — gom đếm cho kỹ thuật.
/// - `resetPower`/`resetSoft`: THÔNG TIN, để đọc "máy vừa bật nguồn hay tự
///   khởi động lại".
final List<TriageRule> kTriageRules = List.unmodifiable([
  TriageRule(
      'brownout',
      RegExp(r'Brownout detector was triggered|RTCWDT_BROWN_OUT_RESET',
          caseSensitive: false),
      TriageLevel.error),
  TriageRule(
      'crash',
      RegExp(
          r'Guru Meditation Error|abort\(\) was called|assert failed|'
          r'Stack canary watchpoint|StoreProhibited|LoadProhibited|'
          r'IllegalInstruction|InstrFetchProhibited|Unhandled debug exception',
          caseSensitive: false),
      TriageLevel.error),
  TriageRule(
      'noFirmware',
      RegExp(r'invalid header: 0x[0-9a-f]+', caseSensitive: false),
      TriageLevel.error),
  TriageRule(
      'flashRead',
      RegExp(r'flash read err|checksum failed|csum err|ota_data|'
          r'image .* invalid|Image length .* doesn.t fit',
          caseSensitive: false),
      TriageLevel.error),
  TriageRule(
      'watchdog',
      RegExp(
          r'Task watchdog got triggered|Interrupt wdt timeout|'
          r'RTCWDT_RTC_RESET|RTCWDT_CPU_RESET|TG0WDT_SYS_RESET|TG1WDT_SYS_RESET|'
          r'TG0WDT_CPU_RESET|TG1WDT_CPU_RESET',
          caseSensitive: false),
      TriageLevel.error),
  TriageRule(
      'tempSensor',
      // -127 đứng riêng (không phải một phần của số khác) hoặc `nan` là token.
      RegExp(r'(?<![0-9.-])-127(?:\.0+)?(?![0-9])|\bnan\b', caseSensitive: false),
      TriageLevel.warning),
  TriageRule(
      'wifi',
      RegExp(
          r'wi-?fi[^\n]{0,60}?(fail|disconnect|lost|timeout|not found|no ap|'
          r'auth|wrong password)|WIFI_REASON|STA_DISCONNECTED|'
          r'Connection failed',
          caseSensitive: false),
      TriageLevel.warning),
  TriageRule(
      'upload',
      RegExp(
          r'(http|post|upload|send)[^\n]{0,60}?(fail|error|-1\b|-11\b|'
          r'\b4\d\d\b|\b5\d\d\b|timeout)',
          caseSensitive: false),
      TriageLevel.warning),
  TriageRule(
      'ota',
      RegExp(r'\bota\b[^\n]{0,60}?(fail|error|abort|invalid)',
          caseSensitive: false),
      TriageLevel.warning),
  TriageRule('idfError', RegExp(r'^E \(\d+\) ', multiLine: true),
      TriageLevel.warning),
  TriageRule(
      'resetPower',
      RegExp(r'rst:0x1\b|POWERON_RESET', caseSensitive: false),
      TriageLevel.info),
  TriageRule(
      'resetSoft',
      RegExp(r'rst:0xc\b|rst:0x3\b|SW_CPU_RESET|SW_RESET|DEEPSLEEP_RESET',
          caseSensitive: false),
      TriageLevel.info),
]);

final RegExp _versionRe = RegExp(
    r'(?:firmware\s*ver(?:sion)?|version|ver|fw)\s*[:=]?\s*v?(\d+\.\d+\.\d+[A-Za-z0-9_]*)',
    caseSensitive: false);
final RegExp _versionBareRe = RegExp(r'\bv(\d+\.\d+\.\d+[A-Za-z0-9_]*)\b');

/// Mã máy Forte Rapid+ dạng `RPL` + 4–6 chữ số (vd `RPL02013`). Không phân
/// biệt hoa thường khi dò, trả về in HOA để khớp cách server lưu.
final RegExp _deviceIdRe = RegExp(r'\b(RPL\d{4,6})\b', caseSensitive: false);

/// Dòng có "mùi lỗi" không — dùng cho bộ lọc "Chỉ dòng nghi lỗi" ở màn hình.
/// Nhẹ hơn [triageLog]: chỉ trả lời có/không cho MỘT dòng.
bool isSuspiciousLine(String line) {
  if (line.trim().isEmpty) return false;
  for (final r in kTriageRules) {
    if (r.level == TriageLevel.info) continue;
    if (r.pattern.hasMatch(line)) return true;
  }
  return false;
}

/// Quét toàn bộ [text]. Rẻ (vài regex trên vài trăm KB) nên gọi lại sau mỗi
/// đợt dữ liệu — màn hình debounce là đủ.
TriageReport triageLog(String text) {
  if (text.isEmpty) return TriageReport.empty;
  final lines = text.split('\n');
  final findings = <TriageFinding>[];
  for (final rule in kTriageRules) {
    var count = 0;
    String sample = '';
    for (final raw in lines) {
      if (rule.pattern.hasMatch(raw)) {
        count++;
        if (sample.isEmpty) sample = _trimSample(raw);
      }
    }
    if (count > 0) findings.add(TriageFinding(rule, count, sample));
  }
  // Lỗi lên đầu, rồi cảnh báo, rồi thông tin; trong cùng mức giữ thứ tự luật.
  findings.sort((a, b) => a.level.index.compareTo(b.level.index));

  String? version;
  final vm = _versionRe.firstMatch(text) ?? _versionBareRe.firstMatch(text);
  if (vm != null) version = vm.group(1);

  String? deviceId;
  final dm = _deviceIdRe.firstMatch(text);
  if (dm != null) deviceId = dm.group(1)!.toUpperCase();

  return TriageReport(
    findings: findings,
    version: version,
    deviceId: deviceId,
    lineCount: lines.length,
  );
}

String _trimSample(String s) {
  final t = s.trim();
  return t.length <= 120 ? t : '${t.substring(0, 117)}…';
}
