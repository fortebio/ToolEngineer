/// Cấu hình **của một trạm ATE** (không phải của người dùng): mã trạm, thao tác
/// viên, cổng COM, bộ 3 file .bin đang chốt cho lô, tham số nạp.
///
/// Tách khỏi `AppSettings` có chủ đích: đây là thứ gắn với CÁI MÁY TÍNH đặt ở
/// xưởng, không phải sở thích người dùng — hai trạm cạnh nhau có mã trạm và bộ
/// .bin khác nhau dù cùng một tài khoản đăng nhập. Vẫn dùng `shared_preferences`
/// với tiền tố khoá `ate_` như mọi trạng thái toàn cục khác của app.
///
/// Persist để thao tác viên KHÔNG phải khai lại cho từng máy: một trạm chạy vài
/// chục máy một ca, mỗi lần gõ lại đường dẫn .bin là một lần nạp nhầm bản.
library;

import 'package:shared_preferences/shared_preferences.dart';

class AtePrefs {
  String station; // mã trạm/PC (ghi vào hồ sơ)
  String batch; // mã lô sản xuất → quyết định bộ tiêu chuẩn áp dụng
  String operator; // thao tác viên (ghi vào hồ sơ)
  String port; // cổng COM đã chọn lần trước
  String chip; // auto | esp32 | esp32s3…
  int flashBaud;
  String flashMode; // keep | dio | qio…
  String flashSize; // keep | detect | 8MB…
  String bootloaderPath;
  String bootloaderOffset;
  String partitionPath;
  String partitionOffset;
  String appPath;
  String appOffset;
  int paraVersion; // 'para version' gửi kèm mọi lệnh cấu hình Serial
  String pcbVersion;
  String expectFwVersion; // version kỳ vọng trong log boot ('' = bỏ qua)
  bool eraseFirst;

  AtePrefs({
    this.station = '',
    this.batch = '',
    this.operator = '',
    this.port = '',
    this.chip = 'auto',
    this.flashBaud = 921600,
    this.flashMode = 'dio',
    this.flashSize = 'detect',
    this.bootloaderPath = '',
    this.bootloaderOffset = '0x1000',
    this.partitionPath = '',
    this.partitionOffset = '0x8000',
    this.appPath = '',
    this.appOffset = '0x10000',
    this.paraVersion = 1,
    this.pcbVersion = '',
    this.expectFwVersion = '',
    this.eraseFirst = false,
  });

  static const _kStation = 'ate_station';
  static const _kBatch = 'ate_batch';
  static const _kOperator = 'ate_operator';
  static const _kPort = 'ate_port';
  static const _kChip = 'ate_chip';
  static const _kBaud = 'ate_flash_baud';
  static const _kMode = 'ate_flash_mode';
  static const _kSize = 'ate_flash_size';
  static const _kBoot = 'ate_bin_bootloader';
  static const _kBootOff = 'ate_off_bootloader';
  static const _kPart = 'ate_bin_partition';
  static const _kPartOff = 'ate_off_partition';
  static const _kApp = 'ate_bin_app';
  static const _kAppOff = 'ate_off_app';
  static const _kPara = 'ate_para_version';
  static const _kPcb = 'ate_pcb_version';
  static const _kExpectFw = 'ate_expect_fw';
  static const _kErase = 'ate_erase_first';

  static Future<AtePrefs> load() async {
    final p = await SharedPreferences.getInstance();
    final d = AtePrefs();
    return AtePrefs(
      station: p.getString(_kStation) ?? '',
      batch: p.getString(_kBatch) ?? '',
      operator: p.getString(_kOperator) ?? '',
      port: p.getString(_kPort) ?? '',
      chip: p.getString(_kChip) ?? d.chip,
      flashBaud: p.getInt(_kBaud) ?? d.flashBaud,
      flashMode: p.getString(_kMode) ?? d.flashMode,
      flashSize: p.getString(_kSize) ?? d.flashSize,
      bootloaderPath: p.getString(_kBoot) ?? '',
      bootloaderOffset: p.getString(_kBootOff) ?? d.bootloaderOffset,
      partitionPath: p.getString(_kPart) ?? '',
      partitionOffset: p.getString(_kPartOff) ?? d.partitionOffset,
      appPath: p.getString(_kApp) ?? '',
      appOffset: p.getString(_kAppOff) ?? d.appOffset,
      paraVersion: p.getInt(_kPara) ?? d.paraVersion,
      pcbVersion: p.getString(_kPcb) ?? '',
      expectFwVersion: p.getString(_kExpectFw) ?? '',
      eraseFirst: p.getBool(_kErase) ?? false,
    );
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kStation, station);
    await p.setString(_kBatch, batch);
    await p.setString(_kOperator, operator);
    await p.setString(_kPort, port);
    await p.setString(_kChip, chip);
    await p.setInt(_kBaud, flashBaud);
    await p.setString(_kMode, flashMode);
    await p.setString(_kSize, flashSize);
    await p.setString(_kBoot, bootloaderPath);
    await p.setString(_kBootOff, bootloaderOffset);
    await p.setString(_kPart, partitionPath);
    await p.setString(_kPartOff, partitionOffset);
    await p.setString(_kApp, appPath);
    await p.setString(_kAppOff, appOffset);
    await p.setInt(_kPara, paraVersion);
    await p.setString(_kPcb, pcbVersion);
    await p.setString(_kExpectFw, expectFwVersion);
    await p.setBool(_kErase, eraseFirst);
  }

  /// Đã đủ để bấm BẮT ĐẦU chưa (thiếu file .bin thì nạp cái gì?).
  bool get ready => port.trim().isNotEmpty && appPath.trim().isNotEmpty;
}
