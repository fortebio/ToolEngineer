/// **Bộ chạy kịch bản trạm ATE** (pha P0: nạp — khai sinh — hồ sơ).
///
/// Thuần Dart: KHÔNG Flutter, KHÔNG `dart:io`, KHÔNG `flutter_libserialport`.
/// Mọi thứ chạm phần cứng đi qua [AteStation] — màn hình cắm bản thật
/// (esptool + cổng COM), test cắm bản giả. Kế hoạch nói thẳng vì sao:
/// "phần này sai thì hỏng cả lô, không nên chỉ kiểm bằng cách cắm máy thật"
/// (`docs/plan/ate-san-xuat.md` §7.3).
///
/// Kịch bản P0 — 5 bước, dừng ĐÚNG bước hỏng:
///
/// | Mã | Việc | Đường |
/// |---|---|---|
/// | FW-02 | Nhận chip / flash / MAC | USB (esptool `flash_id`) |
/// | FW-01 | Nạp 3 file .bin + verify + **sha256 của .bin** | USB (esptool) |
/// | BOOT-01 | Nghe UART sau khi nạp, quét log bằng `log_triage` | USB |
/// | ID-01 | Ghi số máy rồi **đọc lại đối chiếu** (`ParaRead`) | UART |
/// | ID-02 | Ghi tham số mặc định của lô + đọc lại | UART |
///
/// Pha P1 (tự kiểm bán tự động) thêm 6 bước:
///
/// | Mã | Việc | Đường |
/// |---|---|---|
/// | OPT-01 | 10 cảm biến quang có mặt, không mã lỗi | HTTP `/errors` (có IP) hoặc UART `R` |
/// | OPT-03 | Tín hiệu sáng từng slot (`testShot`) + độ lệch giữa 10 kênh | UART `0`–`9` |
/// | TMP-01 | 6 kênh nhiệt: không `-127`/NaN, lệch kênh, lệch nhiệt phòng | UART `TemperatureOutput` |
/// | FAN-01 | Quạt chạy | UART `Fan On` + người xác nhận |
/// | BUZ-01 | Còi kêu | UART `Buzzer On` + người xác nhận |
/// | HMI-01 | Màn TFT + 3 nút vật lý | người xác nhận (checklist) |
///
/// **Ngưỡng chưa chốt ⇒ bước trả `info`, KHÔNG phải `pass`.** Đây là chế độ thu
/// số liệu golden unit: chạy 10–20 máy tốt đã biết, đọc số trong hồ sơ, rồi mới
/// `PUT /ate/limits` bộ ngưỡng thật. Trả `pass` khi chưa có ngưỡng là nói dối
/// một cách có hệ thống — hồ sơ sẽ đầy chữ ĐẠT mà không ai đo gì cả.
///
/// FW-02 chạy TRƯỚC FW-01 (kế hoạch xếp ngược lại): bo mạch chết/cáp không có
/// dây data thì biết sau 2 giây thay vì sau một lượt nạp hỏng nửa chừng.
///
/// EOL-01 (hồ sơ + nhãn) KHÔNG phải một bước ở đây: hồ sơ CHÍNH LÀ sản phẩm của
/// runner. Thêm bước "đã đẩy lên server" vào `steps` sẽ đổi nội dung hồ sơ sau
/// khi đã gửi → đổi luôn khoá idempotent của server và sinh bản ghi trùng.
library;

import 'dart:async';
import 'dart:convert';

import '../models/ate_record.dart';
import 'temp_types.dart';
import '../util/log_triage.dart';
import '../util/sha256.dart';

/// Kết quả một thao tác của trạm (chạy esptool, nạp, đọc chip…).
class AteToolResult {
  final int exitCode;
  final String output; // log đã trộn theo thứ tự thời gian
  const AteToolResult(this.exitCode, this.output);
  bool get ok => exitCode == 0;
}

/// Thông tin bo mạch đọc được trước khi nạp (FW-02).
class AteChipInfo {
  /// Nói chuyện được với bo mạch không. `false` = cáp/nguồn/cổng, chưa cần
  /// nhìn tới các trường dưới.
  final bool ok;
  final String chip; // vd 'ESP32-D0WD-V3'
  final String mac; // 'AA:BB:CC:DD:EE:FF' (rỗng = không đọc được)
  final String flashSize; // vd '8MB'
  final String raw; // log thô để ghi vào hồ sơ
  const AteChipInfo({
    required this.ok,
    this.chip = '',
    this.mac = '',
    this.flashSize = '',
    this.raw = '',
  });
}

/// Một lượt nạp: nạp gì, ở offset nào, có xoá/verify không.
class AteFlashRequest {
  final List<AteBinPart> parts;

  /// `auto` = để công cụ tự nhận chip.
  final String chip;
  final int baud;
  final String flashMode; // keep | dio | qio…
  final String flashSize; // keep | detect | 8MB…
  final bool erase;
  final bool verify;

  /// Phần **app** — dùng để verify sau khi nạp.
  final String appOffset;
  final String appBin;

  const AteFlashRequest({
    required this.parts,
    this.chip = 'auto',
    this.baud = 921600,
    this.flashMode = 'dio',
    this.flashSize = 'detect',
    this.erase = false,
    this.verify = true,
    this.appOffset = '0x10000',
    this.appBin = '',
  });
}

/// Kết quả một lượt nạp. [stage] cho biết HỎNG Ở KHÂU NÀO (`erase` · `write` ·
/// `verify`) — khác nhau về cách xử lý: verify lệch là nghi flash hỏng, còn
/// write lỗi thường là cáp/nguồn.
class AteFlashResult {
  final bool ok;
  final String output;
  final String stage;

  /// Đã ĐỐI CHIẾU lại nội dung flash chưa. Desktop: `esptool verify_flash`.
  /// Bản web (esptool-js) hiện CHƯA làm được → trả `false`, và bước FW-01 hạ
  /// xuống `info` thay vì `pass`: máy vẫn nạp được, nhưng hồ sơ không được nói
  /// dối rằng đã verify.
  final bool verified;

  const AteFlashResult({
    required this.ok,
    this.output = '',
    this.stage = '',
    this.verified = false,
  });
}

/// Một phần flash: nhãn + offset (chuỗi hex như esptool nhận) + đường dẫn .bin.
class AteBinPart {
  final String label;
  final String offset;
  final String path;
  const AteBinPart(this.label, this.offset, this.path);
}

/// Lớp chạm phần cứng. Bản thật ở `ate_station_io.dart`; test dùng bản giả.
abstract class AteStation {
  /// Đọc chip · dung lượng flash · MAC (FW-02). KHÔNG được ném: bo mạch không
  /// trả lời là `ok: false` kèm log giải thích — đó là một kết quả đo, không
  /// phải sự cố của app.
  ///
  /// Vì sao interface nói "đọc chip" chứ không phải "chạy `esptool flash_id`":
  /// bản web không có tiến trình nào để chạy, nó gọi esptool-js trong trình
  /// duyệt. Ranh giới đặt ở Ý ĐỊNH thì một kịch bản chạy được cả hai nền tảng;
  /// đặt ở cú pháp dòng lệnh thì bản web phải đi phân tích chuỗi tham số.
  Future<AteChipInfo> chipInfo({void Function(String)? onLog});

  /// Nạp firmware (kèm xoá/verify nếu [AteFlashRequest] yêu cầu).
  Future<AteFlashResult> flash(AteFlashRequest req, {void Function(String)? onLog});

  /// Mở cổng, (tuỳ chọn) gửi [send], gom log tới khi [until] đúng hoặc hết
  /// [window]. Trả toàn bộ text đã nhận. Cổng phải đóng lại trước khi trả về —
  /// esptool cần cổng ở bước sau (CLAUDE.md: cổng COM là tài nguyên độc quyền).
  Future<String> serialCapture({
    String? send,
    required Duration window,
    bool Function(String buffer)? until,
    void Function(String)? onLog,
  });

  /// Dừng việc đang chạy (nút DỪNG): desktop giết tiến trình esptool, web ngắt
  /// cổng. Không ném nếu chẳng có gì để dừng.
  void cancel();

  /// Bytes của một phần firmware — desktop đọc file theo đường dẫn, web tải từ
  /// kho OTA của server theo TÊN FILE (và nhớ lại để không tải hai lần).
  /// Dùng để tính sha256 ghi vào hồ sơ.
  Future<List<int>> readFile(String path);

  /// GET tới **web server của chính máy đang test** (`http://<ip><path>`).
  /// Trả body, hoặc `null` nếu không gọi được (chưa có mạng, máy bận, timeout).
  ///
  /// KHÔNG ném: ở P1 máy thường CHƯA vào WiFi xưởng (việc đó là NET-01 của P2),
  /// nên "không gọi được" là trạng thái bình thường và bước phải tự lùi về đường
  /// UART chứ không được FAIL oan.
  Future<String?> dutGet(String ip, String path,
      {Duration timeout = const Duration(seconds: 5)});
}

/// Khoá JSON của lệnh cấu hình Serial `{...}@`.
///
/// ⚠️ Lấy theo `GET /config` của firmware v2.4.4 (kế hoạch §3.1). **Đối chiếu
/// lại với firmware trước khi chạy trạm thật**: sai tên khoá thì
/// `JsonDataConfig()` bỏ qua mà **vẫn trả true** — máy không được ghi gì nhưng
/// trạm tưởng xong (§9.12). Đó là lý do ID-01 BẮT BUỘC đọc lại `ParaRead` và
/// đối chiếu, chứ không tin ACK.
const String kAteKeyDeviceId = 'device ID';
const String kAteKeyParaVersion = 'para version';
const String kAteKeyPcbVersion = 'PCB version';

/// Lệnh đọc lại toàn bộ tham số EEPROM (an toàn, không ghi gì).
const String kAteCmdParaRead = 'ParaRead';

/// Bật xuất 6 kênh nhiệt ra UART (dòng `TimeRT`/`TimeRB`, cùng thứ mà tab Log
/// nhiệt đọc). Lệnh AN TOÀN, gửi lại nhiều lần không sao.
const String kAteCmdTempOutput = 'TemperatureOutput';

/// Cấu hình lại toàn bộ cảm biến quang — dùng để dò cảm biến mất/chết khi máy
/// chưa vào được mạng (đường HTTP `/errors` mới là đường chính).
const String kAteCmdOptoReconfig = 'R';

/// Quạt / còi. ⚠️ Tên lệnh lấy theo bảng trong kế hoạch (§3) — **đối chiếu lại
/// với firmware trước lô đầu**; sai tên thì máy im lặng và bước sẽ FAIL vì
/// người vận hành trả lời "không đạt", chứ không âm thầm cho qua.
const String kAteCmdFanOn = 'Fan On';
const String kAteCmdBuzzer = 'Buzzer On';

/// Lệnh khởi động lại máy — dùng khi cổng mở ra mà máy không in gì (đã boot
/// xong từ trước). KHÔNG dùng `P` (đặt PWM LED): nó chặn `while
/// (Serial.available()==0)` và treo `SettingTask` (§9.2).
const String kAteCmdReset = 'Res';

/// Số trung bình quang một slot in ra sau `testShot` — firmware in dạng
/// `{Green: 12345}` (cũng nhận `Green: 12345` trần). Trả null nếu không thấy.
///
/// ⚠️ `testShot` **KHÔNG echo số slot** (kế hoạch §9.3) → hàm này chỉ đọc CON SỐ;
/// việc "số này của slot nào" do runner giữ bằng cách gửi ĐÚNG MỘT slot rồi chờ
/// đúng một dòng. Đừng gửi cả 10 slot rồi parse hàng loạt: một dòng log xen vào
/// là lệch kết quả sang slot khác mà vẫn "PASS" — kiểu sai nguy hiểm nhất.
double? parseGreenMean(String out) {
  final m = RegExp(r'green\s*[:=]\s*(-?\d+(?:\.\d+)?)', caseSensitive: false)
      .firstMatch(out);
  return m == null ? null : double.tryParse(m.group(1)!);
}

/// Các mẫu nhiệt đọc từ log UART: mỗi mẫu 6 kênh theo thứ tự [kTempChannels]
/// (Lysis · Amp1 · Amp2 · Hotlid1 · Hotlid2 · Ambient).
///
/// Định dạng dòng SAO Y `temperature_serial.dart::_parseLine`: tab-separated,
/// `TimeRB<TAB>t<TAB>..<TAB>..<TAB>a<TAB>b<TAB>c` cho 3 kênh đáy và `TimeRT` cho
/// 3 kênh trên; một mẫu = cặp RT+RB. Viết lại ở đây (thay vì dùng lại lớp kia)
/// vì lớp kia kéo `flutter_libserialport`, mà runner phải thuần Dart.
List<List<double?>> parseTempSamples(String log) {
  final out = <List<double?>>[];
  List<double?>? pendingTop;
  for (final raw in log.split('\n')) {
    final parts = raw.trim().split('\t');
    if (parts.length < 7) continue;
    if (double.tryParse(parts[1]) == null) continue;
    final v = [
      double.tryParse(parts[4]),
      double.tryParse(parts[5]),
      double.tryParse(parts[6]),
    ];
    if (parts[0] == 'TimeRT') {
      pendingTop = v;
    } else if (parts[0] == 'TimeRB') {
      out.add([...v, ...(pendingTop ?? const [null, null, null])]);
      pendingTop = null;
    }
  }
  return out;
}

/// Mã lỗi máy tự báo qua `GET /errors` → danh sách `(slot, code, text)`, BỎ mã
/// rỗng và mã toàn số 0 (slot sạch). Body không phải JSON → danh sách rỗng.
List<({int slot, String code, String text})> parseDutErrors(String body) {
  final out = <({int slot, String code, String text})>[];
  Object? j;
  try {
    j = jsonDecode(body);
  } catch (_) {
    return out;
  }
  final list = (j is Map) ? (j['errors'] ?? j['slots']) : j;
  if (list is! List) return out;
  for (var i = 0; i < list.length; i++) {
    final e = list[i];
    final code = (e is Map ? (e['code'] ?? e['error_code'] ?? '') : e).toString().trim();
    if (code.isEmpty || int.tryParse(code) == 0) continue;
    out.add((
      slot: (e is Map && e['slot'] is num) ? (e['slot'] as num).toInt() : i,
      code: code,
      text: (e is Map ? (e['text'] ?? e['message'] ?? '') : '').toString(),
    ));
  }
  return out;
}

/// Địa chỉ IP máy tự in trong log boot (`WiFi connected, IP 192.168.1.23`).
/// Null nếu máy chưa vào mạng — bình thường ở P1.
String? parseDutIp(String log) {
  final m = RegExp(r'\b(?:ip|IP)\b\D{0,12}(\d{1,3}(?:\.\d{1,3}){3})').firstMatch(log);
  final ip = m?.group(1);
  return (ip == null || ip == '0.0.0.0') ? null : ip;
}

/// Cấu hình một lượt chạy trạm cho MỘT máy.
class AteJob {
  final String sn;

  /// Mã lô sản xuất. Quyết định **bộ tiêu chuẩn** máy này bị chấm theo (admin
  /// đặt ngưỡng cho từng lô) và đi thẳng vào hồ sơ.
  final String batch;

  final String station;
  final String operator;
  final String pcbVersion;

  /// `auto` = để esptool tự nhận (bỏ hẳn cờ `--chip`, đúng như tab Nạp code).
  final String chip;
  final int flashBaud;
  final String flashMode; // keep | dio | qio…
  final String flashSize; // keep | detect | 8MB…
  final List<AteBinPart> parts;

  /// File .bin của **app** — dùng để tính sha256 ghi vào hồ sơ và để
  /// `verify_flash`. Rỗng thì lấy phần cuối trong [parts].
  final String appBinPath;
  final String appOffset;

  /// `para version` bắt buộc phải có trong JSON gửi qua Serial (§9.12).
  final int paraVersion;

  /// Tham số mặc định của lô (ID-02). Rỗng = bỏ qua bước đó.
  final Map<String, dynamic> extraParams;

  /// Version firmware kỳ vọng đọc được trong log boot. Rỗng = không đối chiếu.
  final String expectFwVersion;

  final bool eraseFirst;

  const AteJob({
    required this.sn,
    required this.parts,
    this.batch = '',
    this.station = '',
    this.operator = '',
    this.pcbVersion = '',
    this.chip = 'auto',
    this.flashBaud = 921600,
    this.flashMode = 'dio',
    this.flashSize = 'detect',
    this.appBinPath = '',
    this.appOffset = '0x10000',
    this.paraVersion = 1,
    this.extraParams = const {},
    this.expectFwVersion = '',
    this.eraseFirst = false,
  });

  String get effectiveAppBin =>
      appBinPath.isNotEmpty ? appBinPath : (parts.isEmpty ? '' : parts.last.path);
}

/// Bộ chạy kịch bản. Một đối tượng = một máy qua trạm.
class AteRunner {
  final AteStation station;
  final AteJob job;
  final AteLimits limits;
  final DateTime Function() now;
  final void Function(String line)? onLog;
  final void Function(AteStepResult step)? onStep;

  /// Tên hiển thị của bước (màn hình truyền `tr('ate.step.<mã>')` vào).
  final String Function(String code)? nameOf;

  /// Hỏi người vận hành (các bước BÁN TỰ ĐỘNG: quạt, còi, màn hình + nút).
  /// `true` = đạt, `false` = không đạt. **Không cắm hook này thì các bước đó trả
  /// `skip`** — không ai xác nhận thì không được tự cho là đạt.
  final Future<bool> Function(String prompt)? confirm;

  AteRunner({
    required this.station,
    required this.job,
    required this.limits,
    DateTime Function()? now,
    this.onLog,
    this.onStep,
    this.nameOf,
    this.confirm,
  }) : now = now ?? DateTime.now;

  /// Thứ tự chuẩn của kịch bản P0.
  static const List<String> stepCodes = [
    // P0 — nạp + khai sinh
    'FW-02',
    'FW-01',
    'BOOT-01',
    'ID-01',
    'ID-02',
    // P1 — tự kiểm phần cứng (thứ tự: đo trước, làm ồn sau)
    'OPT-01',
    'OPT-03',
    'TMP-01',
    'FAN-01',
    'BUZ-01',
    'HMI-01',
  ];

  static const Map<String, String> _viNames = {
    'FW-02': 'Nhận chip · flash · MAC',
    'FW-01': 'Nạp firmware',
    'BOOT-01': 'Khởi động sạch',
    'ID-01': 'Ghi số máy',
    'ID-02': 'Tham số mặc định của lô',
    'OPT-01': '10 cảm biến quang',
    'OPT-03': 'Tín hiệu sáng từng slot',
    'TMP-01': '6 kênh nhiệt',
    'FAN-01': 'Quạt',
    'BUZ-01': 'Còi',
    'HMI-01': 'Màn hình + 3 nút',
  };

  final Map<String, AteStepResult> _results = {};
  DateTime? _startedAt;
  bool _cancelled = false;
  bool _running = false;

  String _mac = '';
  String _fwSha256 = '';
  String _fwVersion = '';
  String _dutIp = ''; // IP máy đọc được trong log boot (rỗng = chưa vào mạng)
  DateTime? _bootAt; // mốc BOOT-01 xong — quang cần ~900s tính TỪ LÚC BOOT (§9.4)

  Map<String, AteStepResult> get results => Map.unmodifiable(_results);
  bool get running => _running;
  bool get cancelled => _cancelled;
  String get mac => _mac;
  String get dutIp => _dutIp;
  String get fwSha256 => _fwSha256;
  String get fwVersion => _fwVersion;
  DateTime? get startedAt => _startedAt;

  String stepName(String code) =>
      nameOf?.call(code) ?? _viNames[code] ?? code;

  void cancel() => _cancelled = true;

  void _log(String s) => onLog?.call(s);

  /// Chạy hết kịch bản, **dừng ở bước hỏng đầu tiên** (kế hoạch §5). Trả về
  /// hồ sơ đã dựng — người gọi lo việc đẩy lên server / lưu hàng đợi.
  Future<AteRecord> runAll({String note = ''}) async {
    _cancelled = false;
    _running = true;
    _results.clear();
    _startedAt = now();
    try {
      for (final code in stepCodes) {
        if (_cancelled) break;
        final r = await runStep(code);
        if (r.failed) break;
      }
    } finally {
      _running = false;
    }
    return buildRecord(note: note);
  }

  /// Chạy lại ĐÚNG một bước (nút "chạy lại bước này"). Kết quả cũ của bước đó
  /// bị thay — nhưng cả hai lần đều nằm trong hồ sơ của lần chạy tương ứng, và
  /// mỗi lần bấm Bắt đầu là một hồ sơ mới: retry cũng là dữ liệu (§5).
  Future<AteStepResult> runStep(String code) async {
    _startedAt ??= now();
    final t0 = now();
    _log('▶ $code — ${stepName(code)}');
    AteStepResult r;
    try {
      switch (code) {
        case 'FW-02':
          r = await _chipInfo(t0);
          break;
        case 'FW-01':
          r = await _flash(t0);
          break;
        case 'BOOT-01':
          r = await _boot(t0);
          break;
        case 'ID-01':
          r = await _writeId(t0);
          break;
        case 'ID-02':
          r = await _writeParams(t0);
          break;
        case 'OPT-01':
          r = await _optoPresent(t0);
          break;
        case 'OPT-03':
          r = await _optoBright(t0);
          break;
        case 'TMP-01':
          r = await _temps(t0);
          break;
        case 'FAN-01':
          r = await _fan(t0);
          break;
        case 'BUZ-01':
          r = await _buzzer(t0);
          break;
        case 'HMI-01':
          r = await _hmi(t0);
          break;
        default:
          r = _step(code, AteVerdict.skip, t0, detail: 'Bước không có trong kịch bản.');
      }
    } catch (e) {
      // Ném từ tầng phần cứng (cáp rút giữa chừng, cổng bị chiếm…) = FAIL của
      // bước, KHÔNG phải sập cả trạm: thao tác viên cần thấy hỏng ở đâu.
      r = _step(code, AteVerdict.fail, t0, detail: 'Lỗi khi chạy bước: $e');
    }
    _results[code] = r;
    onStep?.call(r);
    _log('   ${r.verdict.name.toUpperCase()} — ${r.detail}');
    return r;
  }

  AteStepResult _step(
    String code,
    AteVerdict verdict,
    DateTime t0, {
    String detail = '',
    String raw = '',
    num? value,
    String unit = '',
    num? min,
    num? max,
  }) =>
      AteStepResult(
        code: code,
        name: stepName(code),
        verdict: verdict,
        startedAt: t0,
        took: now().difference(t0),
        detail: detail,
        raw: raw,
        value: value,
        unit: unit,
        min: min,
        max: max,
      );

  // ------------------------------------------------------------- FW-02

  Future<AteStepResult> _chipInfo(DateTime t0) async {
    final info = await station.chipInfo(onLog: onLog);
    if (!info.ok) {
      return _step('FW-02', AteVerdict.fail, t0,
          detail: 'Không nói chuyện được với bo mạch. Kiểm cáp USB có dây data, '
              'cổng đúng chưa, và bo có đang bị màn khác giữ cổng không.',
          raw: info.raw);
    }
    _mac = info.mac.toUpperCase();
    if (_mac.isEmpty) {
      return _step('FW-02', AteVerdict.fail, t0,
          detail: 'Kết nối được nhưng không đọc được MAC — bo mạch trả lời không '
              'đầy đủ, thử cắm lại cáp.',
          raw: info.raw);
    }
    final want = limits.flashSizeExpect.trim();
    final size = info.flashSize;
    if (want.isNotEmpty &&
        size.toLowerCase().replaceAll(' ', '') !=
            want.toLowerCase().replaceAll(' ', '')) {
      return _step('FW-02', AteVerdict.fail, t0,
          detail: 'Dung lượng flash đọc được là "$size", bộ ngưỡng yêu cầu '
              '"$want" — sai loại flash hoặc sai bo.',
          raw: info.raw);
    }
    return _step('FW-02', AteVerdict.pass, t0,
        detail: '${info.chip.isEmpty ? 'chip ?' : info.chip} · flash '
            '${size.isEmpty ? '?' : size} · MAC $_mac',
        raw: info.raw);
  }

  // ------------------------------------------------------------- FW-01

  Future<AteStepResult> _flash(DateTime t0) async {
    if (job.parts.isEmpty) {
      return _step('FW-01', AteVerdict.fail, t0,
          detail: 'Chưa chọn file .bin nào để nạp.');
    }
    final log = StringBuffer();

    // sha256 TRƯỚC khi nạp: nạp xong mới đọc file thì file có thể đã bị thay.
    final bin = job.effectiveAppBin;
    if (bin.isNotEmpty) {
      try {
        _fwSha256 = sha256Hex(await station.readFile(bin));
        log.writeln('sha256($bin) = $_fwSha256');
      } catch (e) {
        return _step('FW-01', AteVerdict.fail, t0,
            detail: 'Không đọc được file firmware để tính sha256: $e',
            raw: log.toString());
      }
    }

    final verify = limits.requireFlashVerify && bin.isNotEmpty;
    final r = await station.flash(
      AteFlashRequest(
        parts: job.parts,
        chip: job.chip,
        baud: job.flashBaud,
        flashMode: job.flashMode,
        flashSize: job.flashSize,
        erase: job.eraseFirst,
        verify: verify,
        appOffset: job.appOffset,
        appBin: bin,
      ),
      onLog: onLog,
    );
    log.writeln(r.output);
    if (!r.ok) {
      final detail = switch (r.stage) {
        'erase' => 'Xoá flash thất bại — xem log thô.',
        'verify' => 'Nạp xong nhưng verify KHÔNG khớp: nội dung flash khác file '
            '.bin. Nạp lại; còn lệch thì nghi flash lỗi.',
        _ => 'Nạp thất bại — xem log thô.',
      };
      return _step('FW-01', AteVerdict.fail, t0,
          detail: detail, raw: log.toString());
    }
    // Ngưỡng ĐÒI verify mà nền tảng không làm được → `info`, không phải `pass`.
    final missedVerify = verify && !r.verified;
    return _step('FW-01', missedVerify ? AteVerdict.info : AteVerdict.pass, t0,
        detail: 'Nạp ${job.parts.length} phần, '
                '${missedVerify ? 'CHƯA đối chiếu lại được nội dung flash trên nền tảng này' : (verify ? 'verify khớp' : 'không verify')}'
            '${_fwSha256.isEmpty ? '' : ', sha256 ${_fwSha256.substring(0, 12)}…'}',
        raw: log.toString());
  }

  // ----------------------------------------------------------- BOOT-01

  /// Chỉ ngắt nghe SỚM khi log đã lộ lỗi (crash/brownout/boot-loop) — còn lại
  /// nghe hết cửa sổ, vì sau dòng version còn dòng IP WiFi mà OPT-01 cần.
  ///
  /// Trước đây ngắt ngay khi thấy banner ROM (`ets `/`rst:0x`). Trên bo thật
  /// UART về từng mẩu vài chục byte nên bộ nghe cắt đúng ở
  /// `ets Jul 29 2019 12:21:46 / rst:` rồi kết luận "không thấy version" —
  /// 2026-09-09, cả 14 hồ sơ đầu tiên của trạm web FAIL vì thế. Máy giả trả cả
  /// khối một lần nên test không lộ; giờ máy giả trả theo mẩu.
  static bool _bootFatal(String b) => triageLog(b).hasError;

  Future<AteStepResult> _boot(DateTime t0) async {
    final total = Duration(seconds: limits.bootWatchSec);
    final half = Duration(milliseconds: total.inMilliseconds ~/ 2);
    // Lượt 1: chỉ NGHE. esptool `--after hard_reset` đã cho máy khởi động lại,
    // nên bình thường banner boot tự tới.
    var log = await station.serialCapture(
        window: half, until: _bootFatal, onLog: onLog);
    if (log.trim().isEmpty) {
      // Không nghe thấy gì: có thể máy boot xong trước khi cổng kịp mở. Bảo máy
      // khởi động lại bằng lệnh AN TOÀN `Res` rồi nghe nốt phần thời gian.
      log = await station.serialCapture(
          send: kAteCmdReset, window: half, until: _bootFatal, onLog: onLog);
    }
    if (log.trim().isEmpty) {
      return _step('BOOT-01', AteVerdict.fail, t0,
          detail: 'Máy không in gì ra UART trong ${total.inSeconds}s sau khi nạp '
              '— nghi không boot, sai baud, hoặc nhầm cổng.');
    }
    final report = triageLog(log);
    _fwVersion = report.version ?? '';
    _dutIp = parseDutIp(log) ?? '';
    _bootAt = now();
    if (report.hasError) {
      final keys = report.findings
          .where((f) => f.level == TriageLevel.error)
          .map((f) => f.key)
          .join(', ');
      return _step('BOOT-01', AteVerdict.fail, t0,
          detail: 'Log boot có dấu hiệu lỗi: $keys. Xem log thô để biết chi tiết.',
          raw: log);
    }
    final want = job.expectFwVersion.trim().replaceAll(RegExp('^v'), '');
    if (want.isNotEmpty && _fwVersion != want) {
      return _step('BOOT-01', AteVerdict.fail, t0,
          detail: _fwVersion.isEmpty
              ? 'Không thấy version firmware trong log boot (kỳ vọng v$want).'
              : 'Máy đang chạy v$_fwVersion nhưng bản vừa nạp là v$want — nghi nạp '
                  'nhầm file hoặc nạp không ăn.',
          raw: log);
    }
    return _step('BOOT-01', AteVerdict.pass, t0,
        detail: 'Khởi động sạch'
            '${_fwVersion.isEmpty ? '' : ', firmware v$_fwVersion'}'
            '${report.deviceId == null ? '' : ', máy tự khai ${report.deviceId}'}',
        raw: log);
  }

  // ------------------------------------------------------------- ID-01

  Future<AteStepResult> _writeId(DateTime t0) async {
    final sn = job.sn.trim();
    if (sn.isEmpty) {
      return _step('ID-01', AteVerdict.fail, t0, detail: 'Chưa quét số máy.');
    }
    if (sn.length > limits.snMaxLen) {
      // Chặn TRƯỚC khi ghi: firmware giữ `char device_id[10]`, ID dài hơn bị từ
      // chối và máy nằm lại ở "UNSET" (§9.1) — ghi rồi mới biết là mất công.
      return _step('ID-01', AteVerdict.fail, t0,
          value: sn.length,
          unit: 'ký tự',
          max: limits.snMaxLen,
          detail: 'Số máy "$sn" dài ${sn.length} ký tự, firmware chỉ nhận tối đa '
              '${limits.snMaxLen}. Máy sẽ không nhận mã này.');
    }
    final payload = _configJson({
      kAteKeyDeviceId: sn,
      if (job.pcbVersion.trim().isNotEmpty) kAteKeyPcbVersion: job.pcbVersion.trim(),
    });
    final ack = await station.serialCapture(
        send: payload,
        window: Duration(seconds: limits.ackTimeoutSec),
        onLog: onLog);
    // KHÔNG chấm theo ACK: firmware trả true cả khi bỏ qua cấu hình (§9.12).
    final back = await station.serialCapture(
      send: kAteCmdParaRead,
      window: Duration(seconds: limits.ackTimeoutSec),
      until: (b) => b.toUpperCase().contains(sn.toUpperCase()),
      onLog: onLog,
    );
    final raw = 'GỬI: $payload\n$ack\nGỬI: $kAteCmdParaRead\n$back';
    if (!back.toUpperCase().contains(sn.toUpperCase())) {
      return _step('ID-01', AteVerdict.fail, t0,
          detail: back.trim().isEmpty
              ? 'Máy không trả lời lệnh $kAteCmdParaRead — không xác nhận được số máy.'
              : 'Đọc lại KHÔNG thấy số máy "$sn" trong tham số máy — ghi không ăn '
                  '(kiểm tên khoá JSON so với firmware).',
          raw: raw);
    }
    return _step('ID-01', AteVerdict.pass, t0,
        detail: 'Đã ghi và đọc lại khớp số máy $sn', raw: raw);
  }

  // ------------------------------------------------------------- ID-02

  Future<AteStepResult> _writeParams(DateTime t0) async {
    if (job.extraParams.isEmpty) {
      return _step('ID-02', AteVerdict.skip, t0,
          detail: 'Lô này không khai tham số mặc định nào.');
    }
    final payload = _configJson(job.extraParams);
    final ack = await station.serialCapture(
        send: payload,
        window: Duration(seconds: limits.ackTimeoutSec),
        onLog: onLog);
    final back = await station.serialCapture(
        send: kAteCmdParaRead,
        window: Duration(seconds: limits.ackTimeoutSec),
        onLog: onLog);
    final raw = 'GỬI: $payload\n$ack\nGỬI: $kAteCmdParaRead\n$back';
    // Đối chiếu từng giá trị vô hướng có mặt trong bản đọc lại. Không tinh vi,
    // nhưng bắt đúng ca hay gặp: sai tên khoá → firmware im lặng không ghi gì.
    final missing = <String>[];
    job.extraParams.forEach((k, v) {
      if (v is Map || v is List) return; // không đối chiếu được bằng chuỗi
      if (!back.toLowerCase().contains(v.toString().toLowerCase())) missing.add(k);
    });
    if (missing.isNotEmpty) {
      return _step('ID-02', AteVerdict.fail, t0,
          detail: 'Đọc lại không thấy giá trị của: ${missing.join(', ')} — tham số '
              'chưa vào máy.',
          raw: raw);
    }
    return _step('ID-02', AteVerdict.pass, t0,
        detail: 'Đã ghi ${job.extraParams.length} tham số và đọc lại khớp',
        raw: raw);
  }

  // ------------------------------------------------------------ OPT-01

  /// Dấu hiệu lỗi trong log lệnh `R` (đường lùi khi máy chưa có mạng).
  static final RegExp _optoErrRe = RegExp(
      r'\b(error|err|fail|failed|not\s*found|no\s*device|timeout|nack)\b',
      caseSensitive: false);

  Future<AteStepResult> _optoPresent(DateTime t0) async {
    // Đường CHÍNH: hỏi thẳng máy những mã lỗi nó tự phát hiện. Đây là dữ liệu
    // máy đọc được, không phải log tự do — chấm được dứt khoát.
    if (_dutIp.isNotEmpty) {
      final body = await station.dutGet(_dutIp, '/errors');
      if (body != null) {
        final errs = parseDutErrors(body);
        if (errs.isEmpty) {
          return _step('OPT-01', AteVerdict.pass, t0,
              value: 0,
              unit: 'mã lỗi',
              detail: 'Máy không báo mã lỗi cảm biến nào (GET /errors qua $_dutIp).',
              raw: body);
        }
        final lines = [
          for (final e in errs)
            'slot ${e.slot}: ${e.code}${e.text.isEmpty ? '' : ' — ${e.text}'}'
        ].join('; ');
        return _step('OPT-01', AteVerdict.fail, t0,
            value: errs.length,
            unit: 'mã lỗi',
            max: 0,
            detail: 'Máy báo ${errs.length} mã lỗi cảm biến: $lines',
            raw: body);
      }
    }
    // Đường LÙI: máy chưa vào mạng (bình thường ở P1 — cấu hình WiFi là NET-01
    // của P2). Bảo máy cấu hình lại cảm biến quang rồi đọc log.
    final out = await station.serialCapture(
        send: kAteCmdOptoReconfig,
        window: Duration(seconds: limits.ackTimeoutSec),
        onLog: onLog);
    if (out.trim().isEmpty) {
      return _step('OPT-01', AteVerdict.fail, t0,
          detail: 'Máy không trả lời lệnh $kAteCmdOptoReconfig — không kiểm được '
              'cảm biến quang.');
    }
    if (_optoErrRe.hasMatch(out)) {
      return _step('OPT-01', AteVerdict.fail, t0,
          detail: 'Log cấu hình cảm biến quang có dấu hiệu lỗi — xem log thô.',
          raw: out);
    }
    // KHÔNG trả pass: log lệnh `R` không cho biết có đủ 10 kênh hay không, chỉ
    // cho biết "không thấy lỗi". Nói đúng mức tin cậy mình có.
    return _step('OPT-01', AteVerdict.info, t0,
        detail: 'Máy chưa vào mạng nên chỉ đọc được log lệnh $kAteCmdOptoReconfig: '
            'không thấy dấu hiệu lỗi, nhưng KHÔNG đếm được đủ 10 kênh. Muốn chấm '
            'dứt khoát thì cho máy vào mạng để dùng GET /errors.',
        raw: out);
  }

  // ------------------------------------------------------------ OPT-03

  Future<AteStepResult> _optoBright(DateTime t0) async {
    final reads = <double?>[];
    final raw = StringBuffer();
    for (var slot = 0; slot < 10; slot++) {
      if (_cancelled) break;
      // MỖI LẦN MỘT SLOT rồi chờ đúng một con số: `testShot` không echo số slot
      // (§9.3), gửi cả loạt là lệch kết quả sang slot khác mà vẫn "đạt".
      final out = await station.serialCapture(
        send: '$slot',
        window: Duration(seconds: limits.ackTimeoutSec),
        until: (b) => parseGreenMean(b) != null,
        onLog: onLog,
      );
      final v = parseGreenMean(out);
      reads.add(v);
      raw.writeln('slot $slot → ${v ?? 'KHÔNG ĐỌC ĐƯỢC'}   | ${out.trim()}');
    }
    final missing = [
      for (var i = 0; i < reads.length; i++)
        if (reads[i] == null) i
    ];
    if (reads.length < 10 || missing.isNotEmpty) {
      return _step('OPT-03', AteVerdict.fail, t0,
          value: 10 - missing.length,
          unit: 'slot đọc được',
          min: 10,
          detail: 'Không đọc được tín hiệu quang của slot: '
              '${missing.isEmpty ? 'bị dừng giữa chừng' : missing.join(', ')}.',
          raw: raw.toString());
    }

    final vals = [for (final v in reads) v!];
    final lo = vals.reduce((a, b) => a < b ? a : b);
    final hi = vals.reduce((a, b) => a > b ? a : b);
    final spread = hi <= 0 ? 0.0 : (hi - lo) / hi * 100;
    // Quang cần ~900 s làm nóng TÍNH TỪ LÚC BOOT (§9.4). Ở trạm thì không đợi
    // được, nên ghi lại mốc để so sánh giữa các máy đo cùng điều kiện.
    final sinceBoot =
        _bootAt == null ? null : now().difference(_bootAt!).inSeconds;
    final ctx = 'min $lo · max $hi · lệch ${spread.toStringAsFixed(1)}%'
        '${sinceBoot == null ? '' : ' · đo ở giây ${sinceBoot}s sau boot'}';

    final bad = <String>[];
    if (limits.brightMin != null && lo < limits.brightMin!) {
      bad.add('slot yếu nhất $lo < ngưỡng ${limits.brightMin}');
    }
    if (limits.brightMax != null && hi > limits.brightMax!) {
      bad.add('slot mạnh nhất $hi > ngưỡng ${limits.brightMax}');
    }
    if (limits.brightSpreadPct != null && spread > limits.brightSpreadPct!) {
      bad.add('lệch giữa 10 kênh ${spread.toStringAsFixed(1)}% > '
          '${limits.brightSpreadPct}%');
    }
    if (bad.isNotEmpty) {
      return _step('OPT-03', AteVerdict.fail, t0,
          value: double.parse(spread.toStringAsFixed(1)),
          unit: '% lệch',
          min: limits.brightMin,
          max: limits.brightSpreadPct,
          detail: '${bad.join('; ')}. ($ctx)',
          raw: raw.toString());
    }
    final hasLimits = limits.brightMin != null ||
        limits.brightMax != null ||
        limits.brightSpreadPct != null;
    return _step('OPT-03', hasLimits ? AteVerdict.pass : AteVerdict.info, t0,
        value: double.parse(spread.toStringAsFixed(1)),
        unit: '% lệch',
        min: limits.brightMin,
        max: limits.brightSpreadPct,
        detail: hasLimits
            ? 'Đủ 10 slot trong dải cho phép ($ctx).'
            : 'Đủ 10 slot: $ctx. CHƯA có ngưỡng quang trong bộ '
                '"${limits.version}" nên chỉ GHI SỐ, không chấm — chạy 10–20 máy '
                'tốt rồi chốt ngưỡng từ chính các số này.',
        raw: raw.toString());
  }

  // ------------------------------------------------------------ TMP-01

  Future<AteStepResult> _temps(DateTime t0) async {
    final window = Duration(seconds: limits.tempWindowSec);
    final log = await station.serialCapture(
      send: kAteCmdTempOutput,
      window: window,
      until: (b) => parseTempSamples(b).length >= 2,
      onLog: onLog,
    );
    final samples = parseTempSamples(log);
    if (samples.isEmpty) {
      return _step('TMP-01', AteVerdict.fail, t0,
          detail: 'Máy không xuất dòng nhiệt nào trong ${window.inSeconds}s sau '
              'lệnh $kAteCmdTempOutput.',
          raw: log);
    }
    final last = samples.last;
    // -127 = cảm biến Dallas mất kết nối (cùng luật với bộ quét log CSKH).
    final bad = <String>[];
    for (var i = 0; i < kTempChannels.length && i < last.length; i++) {
      final v = last[i];
      if (v == null || v.isNaN || v <= -100 || v > 200) {
        bad.add('${kTempChannels[i]}=${v ?? '—'}');
      }
    }
    if (bad.isNotEmpty) {
      return _step('TMP-01', AteVerdict.fail, t0,
          detail: 'Kênh nhiệt không đọc được (mất cảm biến / đứt dây): '
              '${bad.join(', ')}.',
          raw: log);
    }

    final vals = [for (final v in last) v!];
    final lo = vals.reduce((a, b) => a < b ? a : b);
    final hi = vals.reduce((a, b) => a > b ? a : b);
    final spread = hi - lo;
    final table = [
      for (var i = 0; i < kTempChannels.length; i++)
        '${kTempChannels[i]} ${vals[i].toStringAsFixed(1)}'
    ].join(' · ');

    final fails = <String>[];
    if (limits.tempSpreadC != null && spread > limits.tempSpreadC!) {
      // Máy ở trạm còn NGUỘI → 6 kênh phải xấp xỉ nhau. Lệch nhiều = một cảm
      // biến đọc sai chứ không phải máy đang gia nhiệt.
      fails.add('lệch giữa các kênh ${spread.toStringAsFixed(1)}°C > '
          '${limits.tempSpreadC}°C');
    }
    if (limits.ambientC != null && limits.tempTolC != null) {
      final off = [
        for (var i = 0; i < vals.length; i++)
          if ((vals[i] - limits.ambientC!).abs() > limits.tempTolC!)
            '${kTempChannels[i]} ${vals[i].toStringAsFixed(1)}°C'
      ];
      if (off.isNotEmpty) {
        fails.add('lệch nhiệt phòng (${limits.ambientC}°C) quá '
            '${limits.tempTolC}°C: ${off.join(', ')}');
      }
    }
    if (fails.isNotEmpty) {
      return _step('TMP-01', AteVerdict.fail, t0,
          value: double.parse(spread.toStringAsFixed(1)),
          unit: '°C lệch',
          max: limits.tempSpreadC,
          detail: '${fails.join('; ')}. ($table)',
          raw: log);
    }
    final judged = limits.tempSpreadC != null ||
        (limits.ambientC != null && limits.tempTolC != null);
    return _step('TMP-01', judged ? AteVerdict.pass : AteVerdict.info, t0,
        value: double.parse(spread.toStringAsFixed(1)),
        unit: '°C lệch',
        max: limits.tempSpreadC,
        detail: judged
            ? 'Đủ 6 kênh, lệch ${spread.toStringAsFixed(1)}°C ($table).'
            : 'Đủ 6 kênh, lệch ${spread.toStringAsFixed(1)}°C ($table). Bộ '
                '"${limits.version}" chưa khai nhiệt phòng/ngưỡng lệch nên chỉ '
                'GHI SỐ, không chấm.',
        raw: log);
  }

  // ------------------------------------------- FAN-01 · BUZ-01 · HMI-01

  /// Bước BÁN TỰ ĐỘNG: gửi lệnh (nếu có) rồi hỏi người vận hành.
  ///
  /// Không cắm [confirm] → `skip`. Cố tình KHÔNG mặc định là `pass`: một trạm
  /// chạy không người mà vẫn đóng dấu "quạt đạt" thì cả cột dữ liệu đó vô giá trị.
  Future<AteStepResult> _confirmStep(
    String code,
    DateTime t0, {
    String? command,
    required String prompt,
    Duration settle = const Duration(seconds: 2),
  }) async {
    final raw = StringBuffer();
    if (command != null) {
      final out = await station.serialCapture(
          send: command, window: settle, onLog: onLog);
      raw.writeln('GỬI: $command');
      raw.writeln(out);
    }
    final ask = confirm;
    if (ask == null) {
      return _step(code, AteVerdict.skip, t0,
          detail: 'Không có người xác nhận (trạm chạy tự động) — bỏ qua.',
          raw: raw.toString());
    }
    final ok = await ask(prompt);
    return _step(code, ok ? AteVerdict.pass : AteVerdict.fail, t0,
        detail: ok
            ? 'Người vận hành xác nhận ĐẠT.'
            : 'Người vận hành xác nhận KHÔNG ĐẠT.',
        raw: raw.toString());
  }

  Future<AteStepResult> _fan(DateTime t0) => _confirmStep('FAN-01', t0,
      command: kAteCmdFanOn,
      settle: Duration(seconds: limits.fanWaitSec),
      prompt: 'Quạt có chạy không? (nghe tiếng gió / thấy cánh quay)');

  Future<AteStepResult> _buzzer(DateTime t0) => _confirmStep('BUZ-01', t0,
      command: kAteCmdBuzzer, prompt: 'Còi có kêu không?');

  Future<AteStepResult> _hmi(DateTime t0) => _confirmStep('HMI-01', t0,
      prompt: 'Màn hình TFT sáng, hiện đúng nội dung, và CẢ BA nút vật lý '
          '(đỏ · xanh lá · trắng) đều ăn?');

  /// JSON cấu hình gửi qua Serial: `{...}@`. LUÔN kèm `para version` — thiếu
  /// khoá này thì `JsonDataConfig()` **không áp gì mà vẫn trả true** (§9.12);
  /// đường `POST /config` được firmware tự chèn, đường Serial thì không.
  String _configJson(Map<String, dynamic> fields) =>
      '${jsonEncode({kAteKeyParaVersion: job.paraVersion, ...fields})}@';

  // ------------------------------------------------------------ hồ sơ

  /// Dựng hồ sơ từ những bước ĐÃ chạy.
  ///
  /// Kết luận: có bước FAIL → `fail`. Chạy đủ kịch bản và không bước nào FAIL →
  /// `pass`. Còn lại (dừng giữa chừng, người bấm huỷ) → `aborted` — **không**
  /// được thành `pass` chỉ vì chưa bước nào hỏng.
  AteRecord buildRecord({String note = '', bool? aborted}) {
    final steps = [
      for (final c in stepCodes)
        if (_results.containsKey(c)) _results[c]!
    ];
    final failed = steps.where((s) => s.failed).toList();
    final ran = steps.length == stepCodes.length;
    final verdict = failed.isNotEmpty
        ? AteVerdict.fail
        : (aborted == true || _cancelled || !ran)
            ? AteVerdict.aborted
            : AteVerdict.pass;
    final t0 = _startedAt ?? now();
    return AteRecord(
      sn: job.sn.trim(),
      batch: job.batch.trim(),
      mac: _mac,
      station: job.station,
      operator: job.operator,
      fwVersion: _fwVersion,
      fwSha256: _fwSha256,
      pcbVersion: job.pcbVersion,
      limitsVer: limits.version,
      startedAt: t0,
      finishedAt: now(),
      verdict: verdict,
      failCode: failed.isEmpty ? '' : failed.first.code,
      note: note,
      steps: steps,
    );
  }
}
