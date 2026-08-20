import '../models/test_result.dart';

/// Một **quãng** máy chạy cùng một bản firmware.
class FirmwareStint {
  /// Version nguyên văn máy báo về (giữ hậu tố: `v2.4.3AT`).
  final String version;

  /// Lần đo ĐẦU TIÊN mang version này — mốc gần nhất ta biết về thời điểm nạp.
  final DateTime firstSeen;

  /// Lần đo CUỐI mang version này.
  final DateTime lastSeen;

  /// Số lần đo trong quãng.
  final int runs;

  /// Version máy chạy NGAY TRƯỚC quãng này. Rỗng nếu đây là quãng cũ nhất.
  ///
  /// Đây mới là thứ quyết định lần nạp đó đi bằng đường nào — xem
  /// [updatedViaOta].
  final String fromVersion;

  /// Quãng CŨ NHẤT trong dữ liệu — **không phải** một lần cập nhật.
  ///
  /// Ta chỉ thấy máy từ lần đo đầu tiên nó gửi về; bản nó đang chạy lúc đó đến
  /// từ đâu thì không có dữ liệu nào nói. Gọi đó là "cập nhật" là bịa.
  final bool isFirstKnown;

  /// Mốc **THẬT** máy tự khai đã lên bản này — server ghi vào `fw_log.json`
  /// ngay lúc máy khởi động lại sau khi nạp xong (`GET /devices/{id}/fw-log`).
  ///
  /// `null` = không có mốc nào: dữ liệu có trước khi bật cơ chế này, hoặc máy
  /// chạy firmware < v2.4.5 (chưa biết tự khai) ⇒ ngày phải SUY từ [firstSeen].
  final DateTime? installedAt;

  const FirmwareStint({
    required this.version,
    required this.firstSeen,
    required this.lastSeen,
    required this.runs,
    required this.isFirstKnown,
    this.fromVersion = '',
    this.installedAt,
    this.confirmed = false,
  });

  /// Ngày đem ra hiển thị: mốc thật nếu có, không thì lần đo đầu báo bản này.
  DateTime get updatedAt => installedAt ?? firstSeen;

  /// `false` = ngày này là SUY ĐOÁN (lần nạp xảy ra TRƯỚC nó, có khi hàng ngày).
  bool get dateIsExact => installedAt != null;

  /// Máy tự khai vừa nạp xong bản này — xem [FwLogEntry.confirmed].
  ///
  /// Dùng để KHÔNG gộp hai dòng cùng bản liền nhau: gộp thì lần nạp lại cùng một bản biến
  /// mất, trái đúng yêu cầu "cập nhật bao nhiêu lần thì lưu bấy nhiêu".
  final bool confirmed;

  /// Lần nạp lên quãng này đi bằng OTA hay tay. `null` = không kết luận được
  /// (quãng cũ nhất — không biết trước đó máy chạy gì).
  ///
  /// ⚠️ **SUY LUẬN THEO QUY TẮC, không phải sổ ghi.** Server không lưu lần tải
  /// OTA nào: `/ota/check` không ghi người gọi, `/ota/{file}` không mang mã máy.
  ///
  /// Xét bản **ĐANG CHẠY TRƯỚC ĐÓ**, không phải bản vừa lên — khả năng tự cập
  /// nhật nằm ở firmware đang chạy:
  ///  * trước đó **≥ v2.4.0** → firmware đó biết tự cập nhật (v2.4.0–v2.4.3
  ///    poll GitHub, v2.4.4+ poll Engineer Server) ⇒ **OTA**.
  ///  * trước đó **< v2.4.0** → chưa có đường OTA nào ⇒ chỉ có thể **nạp tay**.
  ///
  /// Ví dụ thật (RPL02001): v2.3.6 → v2.4.0. Xét theo bản MỚI thì ra "OTA", mà
  /// máy đang chạy v2.3.6 **không có** đường OTA nào — lần đó bắt buộc nạp tay.
  bool? get updatedViaOta =>
      isFirstKnown ? null : versionAtLeast(fromVersion, [2, 4, 0]);
}

/// Dựng lịch sử firmware của một máy từ danh sách lần đo.
///
/// **Nguồn dữ liệu là `sessions.version`** — version firmware máy tự kèm theo mỗi
/// lần gửi kết quả. Không có bảng "nhật ký cập nhật" nào trên server, nên lịch sử
/// ở đây là **suy ra**: version đổi giữa hai lần đo liên tiếp ⇒ đã có một lần nạp
/// xen vào giữa.
///
/// Hệ quả phải nói thẳng với người dùng:
///  * `firstSeen` là **lần đo đầu tiên báo bản mới**, không phải giờ nạp. Lần nạp
///    xảy ra đâu đó giữa lần đo cuối của bản cũ và lần đo này.
///  * Máy nạp xong mà **chưa chạy lần nào** thì không xuất hiện ở đây — server
///    chỉ biết máy qua kết quả đo nó gửi lên.
///  * Quãng cũ nhất KHÔNG phải một lần cập nhật (xem [FirmwareStint.isFirstKnown]).
///
/// Trả về **mới nhất trước**. Bỏ qua lần đo không có version (máy đời cũ không
/// gửi trường này) — coi như không biết, chứ không gộp vào quãng liền kề.
List<FirmwareStint> buildFirmwareHistory(List<TestResult> runs) {
  final valid = runs.where((r) => r.version.trim().isNotEmpty).toList()
    ..sort((a, b) => a.timestamp.compareTo(b.timestamp)); // cũ → mới

  final out = <FirmwareStint>[];
  for (final r in valid) {
    final v = r.version.trim();
    if (out.isNotEmpty && out.last.version == v) {
      final p = out.removeLast();
      out.add(FirmwareStint(
        version: p.version,
        firstSeen: p.firstSeen,
        lastSeen: r.timestamp.isAfter(p.lastSeen) ? r.timestamp : p.lastSeen,
        runs: p.runs + 1,
        isFirstKnown: p.isFirstKnown,
        fromVersion: p.fromVersion,
      ));
    } else {
      out.add(FirmwareStint(
        version: v,
        firstSeen: r.timestamp,
        lastSeen: r.timestamp,
        runs: 1,
        isFirstKnown: out.isEmpty,
        fromVersion: out.isEmpty ? '' : out.last.version,
      ));
    }
  }
  return out.reversed.toList(); // mới nhất trước
}

/// So tiền tố số của version với mốc. Bỏ hậu tố (`2.4.3at` → 2.4.3).
///
/// Tách ra từ `supportsPerDevicePin` khi cần mốc thứ hai (2.4.0) — hai bản sao
/// của cùng một phép so là hai chỗ phải sửa mỗi lần đổi luật.
bool versionAtLeast(String version, List<int> want) {
  final m = RegExp(r'^(\d+(?:\.\d+)*)').firstMatch(version.trim().toLowerCase()
      .replaceFirst(RegExp(r'^v'), ''));
  if (m == null) return false;
  final got = m.group(1)!.split('.').map(int.parse).toList();
  for (var i = 0; i < want.length; i++) {
    final g = i < got.length ? got[i] : 0;
    if (g != want[i]) return g > want[i];
  }
  return true;
}

/// Một mốc máy **TỰ KHAI** version, do server ghi lại (`GET /devices/{id}/fw-log`).
class FwLogEntry {
  final String version;
  final DateTime at;

  /// Máy TỰ KHAI nó vừa nạp xong bản này (`how: "update"` — firmware gửi `?updated=1`).
  ///
  /// Khác với mốc suy từ "version đổi giữa hai lượt poll": mốc đó cũng là ngày thật nhưng
  /// chỉ chặn được tới lượt poll gần nhất, và **mù hẳn với lần nạp lại CÙNG một bản**.
  final bool confirmed;

  const FwLogEntry({
    required this.version,
    required this.at,
    this.confirmed = false,
  });
}

/// Ghép **mốc thật** (máy tự khai) lên trên **phần suy đoán** (từ các lần đo).
///
/// Vì sao phải ghép chứ không chọn một:
///  * Nhật ký chỉ có dữ liệu **từ lúc firmware ≥ v2.4.5 và server bật ghi** —
///    phần trước đó không tồn tại ở đâu cả.
///  * Suy từ lần đo phủ được quá khứ, nhưng ngày của nó là *"lần đo đầu tiên
///    báo bản đó"*: muộn hơn giờ nạp thật, và **không có dòng nào** nếu khách
///    nạp xong chưa chạy mẫu lần nào.
///
/// Luật: nhật ký là sự thật cho khoảng nó phủ, phần suy đoán chỉ dùng cho quãng
/// **CŨ HƠN** mốc sớm nhất trong nhật ký. Không trộn hai nguồn trong cùng một
/// khoảng — làm thế thì một lần cập nhật hiện thành HAI dòng với hai ngày khác
/// nhau, đúng thứ khiến người vận hành hết tin cả bảng.
///
/// Số lần đo thì vẫn lấy từ phiên: nhật ký không đếm lần đo, bỏ qua là mọi dòng
/// mới hiện "0 lần đo" trong khi máy chạy cả trăm mẫu.
///
/// Trả về **mới nhất trước**. `fromVersion` tính lại trên danh sách ĐÃ ghép, nên
/// phép suy OTA/tay đúng cả ở chỗ giáp ranh hai nguồn.
List<FirmwareStint> mergeFirmwareLog(
  List<FwLogEntry> log,
  List<FirmwareStint> stints,
) {
  if (log.isEmpty) return stints;

  final logAsc = [...log]..sort((a, b) => a.at.compareTo(b.at));
  final bien = logAsc.first.at;
  final asc = stints.reversed.toList(); // cũ → mới, cho dễ đọc

  final rows = [for (final s in asc) if (s.firstSeen.isBefore(bien)) s];
  // Quãng suy đoán NẰM TRONG khoảng nhật ký: không dựng thành dòng riêng (trùng),
  // nhưng số lần đo của nó thì có thật — gả vào dòng chính xác cùng version.
  final trong = [for (final s in asc) if (!s.firstSeen.isBefore(bien)) s];

  for (final e in logAsc) {
    // Máy khai lại đúng bản của dòng liền trước = khởi động lại, không phải cập nhật.
    // TRỪ khi máy nói thẳng nó vừa nạp xong: đó là lần NẠP LẠI cùng bản, một sự kiện thật.
    if (!e.confirmed && rows.isNotEmpty && rows.last.version == e.version) continue;
    final i = trong.indexWhere(
        (s) => s.version == e.version && !s.firstSeen.isBefore(e.at));
    final m = i < 0 ? null : trong.removeAt(i); // gả rồi thì bỏ ra, tránh dùng lại
    rows.add(FirmwareStint(
      version: e.version,
      firstSeen: m?.firstSeen ?? e.at,
      lastSeen: m?.lastSeen ?? e.at,
      installedAt: e.at,
      runs: m?.runs ?? 0,
      isFirstKnown: false,
      confirmed: e.confirmed,
    ));
  }

  // Quãng suy đoán mà nhật ký KHÔNG giải thích được — CHỈ có thể là hạ bản: máy bị nạp về
  // firmware < v2.4.5 thì nó ngừng gửi `?ver=`, nhật ký câm từ đó, và chỉ các lần đo còn nói
  // máy đang chạy gì. Bỏ chúng đi là xoá đúng BẢN MÁY ĐANG CHẠY khỏi lịch sử — hộp thoại nói
  // ngược hẳn thực tế. Gặp thật với RPL02013 (2026-08-19).
  rows.addAll(trong);
  rows.sort((a, b) => a.updatedAt.compareTo(b.updatedAt));

  // Gộp mốc trùng LIỀN NHAU: sau khi trộn, cùng một bản có thể tới từ cả hai nguồn.
  final gon = <FirmwareStint>[];
  for (final r in rows) {
    if (!r.confirmed && gon.isNotEmpty && gon.last.version == r.version) continue;
    gon.add(r);
  }

  return [
    for (var i = 0; i < gon.length; i++)
      FirmwareStint(
        version: gon[i].version,
        firstSeen: gon[i].firstSeen,
        lastSeen: gon[i].lastSeen,
        installedAt: gon[i].installedAt,
        confirmed: gon[i].confirmed,
        runs: gon[i].runs,
        isFirstKnown: i == 0,
        fromVersion: i == 0 ? '' : gon[i - 1].version,
      ),
  ].reversed.toList();
}
