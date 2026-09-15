/// Mục **Chạy trạm** (tab Sản xuất) — màn kiểu kiosk cho thao tác viên xưởng:
/// quét số máy → BẮT ĐẦU → xem từng bước chạy tới đâu → PASS/FAIL to rõ.
///
/// Kịch bản nằm ở `services/ate_runner.dart` (thuần Dart, có test); màn này chỉ
/// lo giao diện + cắm bản thật của [AteStation] (esptool + cổng COM) + lưu hồ sơ
/// (hàng đợi cục bộ trước, đẩy server sau).
///
/// **Dùng chung desktop + web**: mọi thứ chạm phần cứng đi qua facade
/// `services/ate_station.dart` (desktop = esptool + libserialport · web =
/// esptool-js + Web Serial), hàng đợi qua `services/ate_queue.dart`. KHÔNG có
/// bản `_web` riêng của màn này — hai bản song song là hai chỗ phải sửa mỗi lần
/// (bài học tab Kỹ Thuật).
///
/// Khác biệt theo nền tảng mà màn phải nói ra: web lấy firmware từ **kho OTA của
/// server** (`ateUsesServerFirmware`) và phải **xin quyền cổng** một lần
/// (`ateRequestPort`).
library;

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/ate_record.dart';
import '../services/app_settings.dart';
import '../services/ate_api.dart';
import '../services/ate_prefs.dart';
import '../services/ate_queue.dart';
import '../services/ate_runner.dart';
import '../services/ate_station.dart';
import '../services/fbt_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../util/platform_files.dart' as pf;

class AteRunScreen extends StatefulWidget {
  final AppSettings settings;

  // --- Cửa tiêm phụ thuộc, CHỈ dùng cho test + chụp ảnh tài liệu ---
  // Bỏ trống = bản thật (esptool + libserialport + Engineer Server). Có ba cửa
  // vì màn này chạm ba thứ ngoài tầm test: phần cứng, cổng COM, và mạng.
  // Rẻ hơn nhiều so với dựng cả một bo mạch giả để chụp một tấm ảnh hướng dẫn.

  /// Thay lớp chạm phần cứng (esptool + cổng COM).
  final AteStation Function(String port, int baud)? stationFactory;

  /// Thay client hồ sơ/ngưỡng.
  final AteApi? api;

  /// Thay bộ liệt kê cổng COM.
  final List<String> Function()? portLister;

  /// Ép chế độ "firmware lấy từ kho OTA của server" (mặc định: theo nền tảng).
  /// Có cửa này vì [ateUsesServerFirmware] là hằng CHỌN LÚC BIÊN DỊCH — chạy
  /// `flutter test` trên VM thì luôn là false, nghĩa là nhánh giao diện của bản
  /// web không có cách nào test được. Đã dính một lỗi thật ở đúng nhánh đó.
  final bool? useServerFirmware;

  /// Thay bộ liệt kê .bin trong kho OTA.
  final Future<List<String>> Function()? binLister;

  const AteRunScreen({
    super.key,
    required this.settings,
    this.stationFactory,
    this.api,
    this.portLister,
    this.useServerFirmware,
    this.binLister,
  });

  @override
  State<AteRunScreen> createState() => _AteRunScreenState();
}

class _AteRunScreenState extends State<AteRunScreen> {
  late final AteApi _api = widget.api ?? AteApi.of(widget.settings);

  /// Kho firmware (`/ota`) — cùng URL + token với Engineer Server. Bản web nạp
  /// firmware TỪ ĐÂY (trình duyệt không giữ được đường dẫn file qua F5).
  late final FbtApi _ota = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  AtePrefs? _cfg;
  AteLimits _limits = AteLimits.fallback;
  String _limitsNote = '';
  List<String> _ports = const [];

  final TextEditingController _sn = TextEditingController();
  final TextEditingController _note = TextEditingController();
  final FocusNode _snFocus = FocusNode();
  final ScrollController _logScroll = ScrollController();

  AteRunner? _runner;
  AteStation? _station;

  /// Firmware lấy từ kho OTA của server thay vì file trên máy (bản web).
  bool get _serverFw => widget.useServerFirmware ?? ateUsesServerFirmware;

  /// Các bản .bin trong kho OTA (chỉ dùng khi [_serverFw]).
  List<String> _serverBins = const [];
  bool _loadingBins = false;
  final Map<String, AteStepResult> _steps = {};
  String _current = ''; // mã bước đang chạy
  final StringBuffer _log = StringBuffer();
  AteRecord? _record; // hồ sơ vừa chốt
  String _sendState = ''; // câu trạng thái sau khi lưu/gửi hồ sơ
  bool _sendError = false;
  int _queued = 0;
  bool _busy = false;
  bool _showSetup = false;

  @override
  void initState() {
    super.initState();
    _refreshPorts();
    if (_serverFw) _loadServerBins();
    // Ngưỡng phải nạp SAU khi biết lô: `AtePrefs.load()` là async, gọi
    // `_loadLimits()` song song thì nó hỏi server bằng lô RỖNG và trạm chạy cả
    // ca bằng bộ CHUNG dù lô đã khai. Gặp thật khi dựng ảnh tài liệu — chip cảnh
    // báo "lô chưa có tiêu chuẩn riêng" hiện lên trong khi lô có bộ riêng.
    AtePrefs.load().then((c) {
      if (!mounted) return;
      if (c.port.isEmpty && _ports.isNotEmpty) c.port = _ports.first;
      setState(() {
        _cfg = c;
        _showSetup = !c.ready; // chưa cấu hình xong thì mở sẵn phần cấu hình
      });
      _loadLimits();
    });
    _countQueue();
  }

  @override
  void dispose() {
    _runner?.cancel();
    _killTool();
    _sn.dispose();
    _note.dispose();
    _snFocus.dispose();
    _logScroll.dispose();
    super.dispose();
  }

  Future<void> _loadLimits() async {
    try {
      // Ngưỡng theo LÔ: trạm chấm máy theo đúng bộ tiêu chuẩn của lô đang chạy.
      final l = await _api.limits(batch: _cfg?.batch.trim() ?? '');
      if (!mounted) return;
      setState(() {
        _limits = l;
        _limitsNote = '';
      });
    } catch (e) {
      // Không chặn trạm: chạy tiếp bằng bộ dự phòng, nhưng NÓI RÕ trên màn —
      // hồ sơ sẽ ghi `limits_ver = local-fallback` và người đọc sau phải biết.
      if (!mounted) return;
      setState(() => _limitsNote = '$e');
    }
  }

  Future<void> _countQueue() async {
    final n = await AteQueue.pendingCount();
    if (mounted) setState(() => _queued = n);
  }

  /// Kho firmware trên server — nguồn .bin của bản web.
  Future<void> _loadServerBins() async {
    setState(() => _loadingBins = true);
    try {
      final lister = widget.binLister;
      final names = lister != null
          ? await lister()
          : [for (final f in (await _ota.listOta()).files) f.name];
      if (!mounted) return;
      setState(() => _serverBins = names);
    } catch (e) {
      // Không chặn trạm: người dùng vẫn thấy lỗi ở chip cạnh bộ ngưỡng.
      if (!mounted) return;
      setState(() => _limitsNote = '$e');
    } finally {
      if (mounted) setState(() => _loadingBins = false);
    }
  }

  /// Dừng việc đang chạy: desktop giết tiến trình esptool, web ngắt cổng.
  void _killTool() => _station?.cancel();

  Future<void> _refreshPorts() async {
    final lister = widget.portLister;
    final ports = lister != null ? lister() : await ateListPorts();
    if (!mounted) return;
    setState(() {
      _ports = ports;
      final c = _cfg;
      if (c != null && !ports.contains(c.port) && ports.isNotEmpty) {
        c.port = ports.first;
      }
    });
  }

  /// Web: xin quyền cổng qua hộp thoại trình duyệt — chỉ phải làm MỘT LẦN cho
  /// cả ca, lần sau `ateListPorts()` tự thấy cổng đã cấp.
  Future<void> _grantPort() async {
    final label = await ateRequestPort();
    if (label == null || !mounted) return;
    await _refreshPorts();
    final c = _cfg;
    if (c == null) return;
    setState(() => c.port = label);
    await c.save();
  }

  void _appendLog(String s) {
    if (!mounted) return;
    setState(() => _log.write(s.endsWith('\n') ? s : '$s\n'));
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_logScroll.hasClients) {
        _logScroll.jumpTo(_logScroll.position.maxScrollExtent);
      }
    });
  }

  AteJob _buildJob(AtePrefs c) => AteJob(
        sn: _sn.text.trim(),
        batch: c.batch.trim(),
        station: c.station.trim(),
        operator: c.operator.trim().isNotEmpty
            ? c.operator.trim()
            : (SessionStore.current?.username ?? ''),
        pcbVersion: c.pcbVersion.trim(),
        chip: c.chip,
        flashBaud: c.flashBaud,
        flashMode: c.flashMode,
        flashSize: c.flashSize,
        appBinPath: c.appPath.trim(),
        appOffset: c.appOffset.trim(),
        paraVersion: c.paraVersion,
        expectFwVersion: c.expectFwVersion.trim(),
        eraseFirst: c.eraseFirst,
        parts: [
          if (c.bootloaderPath.trim().isNotEmpty)
            AteBinPart('Bootloader', c.bootloaderOffset.trim(), c.bootloaderPath.trim()),
          if (c.partitionPath.trim().isNotEmpty)
            AteBinPart('Partition', c.partitionOffset.trim(), c.partitionPath.trim()),
          if (c.appPath.trim().isNotEmpty)
            AteBinPart('App', c.appOffset.trim(), c.appPath.trim()),
        ],
      );

  Future<void> _start() async {
    final c = _cfg;
    if (c == null || _busy) return;
    if (!SessionStore.canRunStation) return;
    if (_sn.text.trim().isEmpty) {
      _snFocus.requestFocus();
      return;
    }
    if (!c.ready) {
      setState(() => _showSetup = true);
      return;
    }
    await c.save();
    final station = widget.stationFactory != null
        ? widget.stationFactory!(c.port, _limits.serialBaud)
        : AteStationImpl(
            port: c.port,
            baud: _limits.serialBaud,
            // Web: tải .bin từ kho OTA theo TÊN file; desktop bỏ qua tham số này.
            fetchBin: (name) => _ota.downloadOta(name),
          );
    final runner = AteRunner(
      station: station,
      job: _buildJob(c),
      limits: _limits,
      onLog: _appendLog,
      onStep: (s) => setState(() => _steps[s.code] = s),
      nameOf: (code) => tr('ate.step.$code'),
      confirm: _askOperator,
    );
    setState(() {
      _busy = true;
      _record = null;
      _sendState = '';
      _sendError = false;
      _steps.clear();
      _log.clear();
      _station = station;
      _runner = runner;
      _current = AteRunner.stepCodes.first;
    });
    // Cập nhật "bước đang chạy" theo tiến độ: onStep bắn khi bước XONG, nên mã
    // bước kế tiếp mới là bước đang chạy.
    for (final code in AteRunner.stepCodes) {
      if (runner.cancelled) break;
      setState(() => _current = code);
      final r = await runner.runStep(code);
      if (r.failed) break;
    }
    if (!mounted) return;
    setState(() {
      _current = '';
      _busy = false;
    });
    await _finalize();
  }

  /// Hỏi người vận hành cho các bước bán tự động (quạt · còi · màn hình + nút).
  ///
  /// Trả `false` khi màn đã bị huỷ: không có ai trả lời thì KHÔNG được coi là đạt.
  Future<bool> _askOperator(String prompt) async {
    if (!mounted) return false;
    final cs = Theme.of(context).colorScheme;
    final ok = await showDialog<bool>(
      context: context,
      barrierDismissible: false, // buộc trả lời, không bấm ra ngoài cho xong
      builder: (c) => AlertDialog(
        title: Text(tr('ate.confirmTitle')),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          Text(prompt, style: const TextStyle(fontSize: 16)),
          const SizedBox(height: 10),
          Text(tr('ate.confirmHint'),
              style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
        ]),
        actions: [
          TextButton(
            style: TextButton.styleFrom(foregroundColor: cs.error),
            onPressed: () => Navigator.pop(c, false),
            child: Text(tr('ate.confirmNo')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(c, true),
            child: Text(tr('ate.confirmYes')),
          ),
        ],
      ),
    );
    return ok ?? false;
  }

  Future<void> _rerunStep(String code) async {
    final r = _runner;
    if (r == null || _busy) return;
    setState(() {
      _busy = true;
      _current = code;
    });
    await r.runStep(code);
    if (!mounted) return;
    setState(() {
      _busy = false;
      _current = '';
    });
    // Chạy lại = một hồ sơ MỚI (không sửa hồ sơ cũ). Kết luận được tính lại từ
    // toàn bộ kết quả hiện có — kế hoạch §9.10: không có nút sửa verdict.
    await _finalize();
  }

  /// Chốt hồ sơ: **ghi file trước** (hàng đợi cục bộ), rồi mới đẩy server.
  Future<void> _finalize() async {
    final r = _runner;
    if (r == null) return;
    final rec = r.buildRecord(note: _note.text.trim());
    if (rec.steps.isEmpty) {
      // Bấm DỪNG trước khi bước nào chạy xong → không có gì để nghiệm thu. Lưu
      // hồ sơ rỗng thì server từ chối (thiếu `steps`) và nó nằm lại hàng đợi.
      setState(() {
        _record = null;
        _sendState = tr('ate.nothingRan');
        _sendError = false;
      });
      return;
    }
    setState(() => _record = rec);
    String state;
    var err = false;
    try {
      await AteQueue.save(rec);
    } catch (e) {
      state = tr('ate.saveFailed').replaceFirst('{err}', '$e');
      err = true;
      setState(() {
        _sendState = state;
        _sendError = err;
      });
      return;
    }
    try {
      final id = await _api.putRecord(rec);
      // Đẩy được thì cuốn luôn hàng đợi cũ: mạng vừa sống lại.
      final flushed = await AteQueue.flush(_api);
      state = tr('ate.sentOk').replaceFirst('{id}', id) +
          (flushed.sent > 0
              ? ' ${tr('ate.flushed').replaceFirst('{n}', '${flushed.sent}')}'
              : '');
    } catch (e) {
      state = tr('ate.savedOffline').replaceFirst('{err}', '$e');
      err = true;
    }
    await _countQueue();
    if (!mounted) return;
    setState(() {
      _sendState = state;
      _sendError = err;
    });
    if (!err) {
      _sn.clear();
      _snFocus.requestFocus(); // sẵn sàng quét máy kế tiếp
    }
  }

  Future<void> _flushQueue() async {
    final res = await AteQueue.flush(_api);
    await _countQueue();
    if (!mounted) return;
    final rejected = res.rejected > 0
        ? ' ${tr('ate.rejected').replaceFirst('{n}', '${res.rejected}')}'
        : '';
    setState(() {
      _sendError = res.error != null;
      _sendState = res.error == null
          ? tr('ate.flushed').replaceFirst('{n}', '${res.sent}') + rejected
          : '${res.error}$rejected';
    });
  }

  void _stop() {
    _runner?.cancel();
    _killTool();
    setState(() => _busy = false);
  }

  Future<void> _pickBin(void Function(String) set) async {
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'Firmware', extensions: ['bin'])
    ]);
    if (f == null) return;
    setState(() => set(f.path));
    await _cfg?.save();
  }

  // ------------------------------------------------------------------ build

  @override
  Widget build(BuildContext context) {
    final c = _cfg;
    if (c == null) {
      return const Center(child: CircularProgressIndicator());
    }
    final cs = Theme.of(context).colorScheme;
    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        _portRow(c, cs),
        if (_showSetup) ...[const SizedBox(height: 12), _setupCard(c)],
        const SizedBox(height: 12),
        _machineCard(c, cs),
        if (_busy) ...[const SizedBox(height: 12), _progressCard(cs)],
        if (_record != null) ...[const SizedBox(height: 12), _verdictCard(cs)],
        const SizedBox(height: 12),
        _stepsCard(cs),
        const SizedBox(height: 12),
        _logCard(cs),
      ],
    );
  }

  Widget _portRow(AtePrefs c, ColorScheme cs) {
    return Wrap(
      spacing: 12,
      runSpacing: 8,
      crossAxisAlignment: WrapCrossAlignment.center,
      children: [
        SizedBox(
          width: 190,
          child: DropdownButtonFormField<String>(
            initialValue: _ports.contains(c.port) ? c.port : null,
            decoration: InputDecoration(
              labelText: tr('ate.port'),
              border: const OutlineInputBorder(),
              isDense: true,
            ),
            items: [
              for (final p in _ports) DropdownMenuItem(value: p, child: Text(p))
            ],
            onChanged: _busy
                ? null
                : (v) => setState(() {
                      c.port = v ?? '';
                      c.save();
                    }),
          ),
        ),
        IconButton(
          tooltip: tr('sp.refreshPorts'),
          onPressed: _busy ? null : _refreshPorts,
          icon: const Icon(Icons.refresh),
        ),
        // Web: trình duyệt chỉ giao cổng khi người dùng tự chọn — một lần/ca.
        if (_serverFw)
          OutlinedButton.icon(
            onPressed: _busy ? null : _grantPort,
            icon: const Icon(Icons.usb, size: 18),
            label: Text(tr('ate.grantPort')),
          ),
        TextButton.icon(
          onPressed: () => setState(() => _showSetup = !_showSetup),
          icon: Icon(_showSetup ? Icons.expand_less : Icons.tune),
          label: Text(tr('ate.setup')),
        ),
        Chip(
          avatar: Icon(Icons.rule, size: 16, color: cs.onSurfaceVariant),
          label: Text(
            '${tr('ate.limits')}: ${_limits.version}'
            '${c.batch.trim().isEmpty ? '' : ' · ${c.batch.trim()}'}',
          ),
        ),
        // Lô có khai mà bộ ngưỡng lại đến từ nơi khác → nói ngay. "Tưởng đang
        // chấm theo tiêu chuẩn của lô" là kiểu nhầm không nhìn đâu ra được.
        if (c.batch.trim().isNotEmpty &&
            _limits.source.isNotEmpty &&
            _limits.source != 'batch')
          Chip(
            avatar: Icon(Icons.warning_amber_outlined,
                size: 16, color: AppSemantic.of(context).warning),
            label: Text(tr('ate.limitsBatchWarn')
                .replaceFirst('{b}', c.batch.trim())
                .replaceFirst('{s}', _limits.source)),
          ),
        if (_limitsNote.isNotEmpty)
          Tooltip(
            message: _limitsNote,
            child: Chip(
              avatar: Icon(Icons.cloud_off, size: 16, color: cs.error),
              label: Text(tr('ate.limitsOffline')),
            ),
          ),
        if (_queued > 0)
          ActionChip(
            avatar: const Icon(Icons.upload_file, size: 16),
            label: Text(tr('ate.queued').replaceFirst('{n}', '$_queued')),
            onPressed: _flushQueue,
          ),
      ],
    );
  }

  Widget _setupCard(AtePrefs c) {
    Widget bin(String label, String path, String offset,
        void Function(String) setPath, void Function(String) setOffset) {
      // Bản web: chọn TÊN bản trong kho OTA của server thay vì đường dẫn file.
      if (_serverFw) {
        final cs = Theme.of(context).colorScheme;
        // Đã chốt một bản, kho đã tải về, mà tên đó không còn trong kho nữa.
        final gone = path.isNotEmpty &&
            _serverBins.isNotEmpty &&
            !_serverBins.contains(path);
        return Padding(
          padding: const EdgeInsets.only(bottom: 10),
          child: Row(children: [
            SizedBox(
              width: 96,
              child: TextFormField(
                initialValue: offset,
                decoration: InputDecoration(
                  labelText: tr('ate.offset'),
                  border: const OutlineInputBorder(),
                  isDense: true,
                ),
                style:
                    const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 13),
                onChanged: (v) {
                  setOffset(v);
                  c.save();
                },
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: DropdownButtonFormField<String>(
                initialValue: _serverBins.contains(path) ? path : null,
                isExpanded: true,
                decoration: InputDecoration(
                  labelText: label,
                  // Kho trên server đổi được bất cứ lúc nào (ai đó tải bản mới
                  // lên, xoá bản cũ) trong khi cấu hình trạm vẫn giữ TÊN bản
                  // ngày trước. Không nói ra thì ô chỉ hiện trống như chưa ai
                  // chọn, còn thợ thì tưởng trạm vẫn nạp bản hôm qua.
                  helperText: gone
                      ? tr('ate.binGone').replaceFirst('{f}', path)
                      : tr('ate.fromServer'),
                  helperStyle: gone ? TextStyle(color: cs.error) : null,
                  helperMaxLines: 2,
                  border: const OutlineInputBorder(),
                  isDense: true,
                ),
                items: [
                  for (final n in _serverBins)
                    DropdownMenuItem(
                        value: n,
                        child: Text(n, overflow: TextOverflow.ellipsis)),
                ],
                onChanged: _busy
                    ? null
                    : (v) => setState(() {
                          setPath(v ?? '');
                          c.save();
                        }),
              ),
            ),
            IconButton(
              tooltip: tr('history.jsonRefresh'),
              onPressed: _loadingBins ? null : _loadServerBins,
              icon: const Icon(Icons.refresh),
            ),
          ]),
        );
      }
      return Padding(
        padding: const EdgeInsets.only(bottom: 10),
        child: Row(children: [
          SizedBox(
            width: 96,
            child: TextFormField(
              initialValue: offset,
              decoration: InputDecoration(
                labelText: tr('ate.offset'),
                border: const OutlineInputBorder(),
                isDense: true,
              ),
              style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 13),
              onChanged: (v) {
                setOffset(v);
                c.save();
              },
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              path.isEmpty ? tr('ate.noBin') : path,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 12.5),
            ),
          ),
          const SizedBox(width: 8),
          OutlinedButton(
            onPressed: _busy ? null : () => _pickBin(setPath),
            child: Text(label),
          ),
        ]),
      );
    }

    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(tr('ate.setup'),
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
        const SizedBox(height: 4),
        Text(tr('ate.setupHint'),
            style: TextStyle(
                fontSize: 12.5,
                color: Theme.of(context).colorScheme.onSurfaceVariant)),
        const SizedBox(height: 14),
        Wrap(spacing: 12, runSpacing: 12, children: [
          SizedBox(
            width: 200,
            child: TextFormField(
              initialValue: c.batch,
              decoration: InputDecoration(
                labelText: tr('ate.batch'),
                helperText: tr('ate.batchHint'),
                helperMaxLines: 3,
                border: const OutlineInputBorder(),
                isDense: true,
              ),
              onChanged: (v) {
                c.batch = v;
                c.save();
              },
              // Đổi lô = đổi bộ tiêu chuẩn → nạp lại ngưỡng ngay, đừng để chạy
              // tiếp bằng ngưỡng của lô trước.
              onFieldSubmitted: (_) => _loadLimits(),
            ),
          ),
          _text(tr('ate.station'), c.station, (v) => c.station = v, width: 180),
          _text(tr('ate.operator'), c.operator, (v) => c.operator = v, width: 180),
          _text(tr('ate.pcbVersion'), c.pcbVersion, (v) => c.pcbVersion = v,
              width: 150),
          _text(tr('ate.expectFw'), c.expectFwVersion,
              (v) => c.expectFwVersion = v,
              width: 150),
          _text(tr('ate.paraVersion'), '${c.paraVersion}',
              (v) => c.paraVersion = int.tryParse(v) ?? c.paraVersion,
              width: 130),
        ]),
        const SizedBox(height: 14),
        bin(tr('ate.pickBootloader'), c.bootloaderPath, c.bootloaderOffset,
            (v) => c.bootloaderPath = v, (v) => c.bootloaderOffset = v),
        bin(tr('ate.pickPartition'), c.partitionPath, c.partitionOffset,
            (v) => c.partitionPath = v, (v) => c.partitionOffset = v),
        bin(tr('ate.pickApp'), c.appPath, c.appOffset, (v) => c.appPath = v,
            (v) => c.appOffset = v),
        Wrap(spacing: 12, runSpacing: 12, crossAxisAlignment: WrapCrossAlignment.center, children: [
          _dropdown(tr('ate.chip'), c.chip,
              const ['auto', 'esp32', 'esp32s3', 'esp32c3', 'esp8266'],
              (v) => c.chip = v),
          _dropdown(tr('ate.flashMode'), c.flashMode,
              const ['keep', 'dio', 'dout', 'qio', 'qout'], (v) => c.flashMode = v),
          _dropdown(tr('ate.flashSize'), c.flashSize,
              const ['keep', 'detect', '4MB', '8MB', '16MB'], (v) => c.flashSize = v),
          _dropdown(tr('ate.flashBaud'), '${c.flashBaud}',
              const ['115200', '460800', '921600'],
              (v) => c.flashBaud = int.tryParse(v) ?? c.flashBaud),
          FilterChip(
            selected: c.eraseFirst,
            label: Text(tr('ate.eraseFirst')),
            onSelected: _busy
                ? null
                : (v) => setState(() {
                      c.eraseFirst = v;
                      c.save();
                    }),
          ),
        ]),
      ]),
    );
  }

  Widget _text(String label, String value, void Function(String) set,
      {double width = 200}) {
    return SizedBox(
      width: width,
      child: TextFormField(
        initialValue: value,
        decoration: InputDecoration(
            labelText: label, border: const OutlineInputBorder(), isDense: true),
        onChanged: (v) {
          set(v);
          _cfg?.save();
        },
      ),
    );
  }

  Widget _dropdown(String label, String value, List<String> options,
      void Function(String) set) {
    return SizedBox(
      width: 160,
      child: DropdownButtonFormField<String>(
        // Ô rộng cố định mà chữ thì dài ngắn tuỳ lựa chọn ("921600", "detect")
        // → thiếu `isExpanded` là RenderFlex tràn phải: bản debug kẻ sọc vàng,
        // bản release cắt cụt âm thầm. Cho nó co chữ bằng dấu … thay vì tràn.
        isExpanded: true,
        initialValue: options.contains(value) ? value : options.first,
        decoration: InputDecoration(
            labelText: label, border: const OutlineInputBorder(), isDense: true),
        items: [
          for (final o in options) DropdownMenuItem(value: o, child: Text(o))
        ],
        onChanged: _busy
            ? null
            : (v) => setState(() {
                  set(v ?? options.first);
                  _cfg?.save();
                }),
      ),
    );
  }

  /// Đang chạy tới bước thứ mấy trên tổng số — thứ người đứng máy nhìn nhiều
  /// nhất. Không có nó thì phải dò trong danh sách 11 dòng xem cái nào đang quay.
  Widget _progressCard(ColorScheme cs) {
    final total = AteRunner.stepCodes.length;
    final idx = AteRunner.stepCodes.indexOf(_current);
    final done = _steps.length;
    return AppCard(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const SizedBox(
              width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2.5)),
          const SizedBox(width: 12),
          Expanded(
            child: Text(
              '${tr('ate.stepOf').replaceFirst('{i}', '${idx < 0 ? done : idx + 1}').replaceFirst('{n}', '$total')}'
              ' · ${_current.isEmpty ? '' : tr('ate.step.$_current')}',
              style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w700),
            ),
          ),
          Text(_sn.text.trim(),
              style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontSize: 15,
                  fontWeight: FontWeight.w600)),
        ]),
        const SizedBox(height: 10),
        ClipRRect(
          borderRadius: BorderRadius.circular(AppRadius.sm),
          child: LinearProgressIndicator(
            value: idx < 0 ? null : (idx + 1) / total,
            minHeight: 8,
            backgroundColor: cs.surfaceContainerHighest,
          ),
        ),
      ]),
    );
  }

  Widget _machineCard(AtePrefs c, ColorScheme cs) {
    final canRun = SessionStore.canRunStation;
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(tr('ate.scanHint'),
            style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
        const SizedBox(height: 10),
        Row(children: [
          Expanded(
            child: TextField(
              controller: _sn,
              focusNode: _snFocus,
              autofocus: true,
              enabled: !_busy && canRun,
              textCapitalization: TextCapitalization.characters,
              style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontSize: 24,
                  fontWeight: FontWeight.w700),
              decoration: InputDecoration(
                labelText: tr('ate.sn'),
                border: const OutlineInputBorder(),
                helperText: tr('ate.snHelp')
                    .replaceFirst('{n}', '${_limits.snMaxLen}'),
              ),
              // Máy quét mã vạch USB hoạt động như bàn phím và kết thúc bằng
              // Enter → quét xong là chạy luôn, thao tác viên không phải bấm.
              onSubmitted: (_) => _start(),
            ),
          ),
          const SizedBox(width: 12),
          SizedBox(
            height: 56,
            child: _busy
                ? FilledButton.icon(
                    style: FilledButton.styleFrom(backgroundColor: cs.error),
                    onPressed: _stop,
                    icon: const Icon(Icons.stop),
                    label: Text(tr('ate.stop')),
                  )
                : FilledButton.icon(
                    onPressed: canRun ? _start : null,
                    icon: const Icon(Icons.play_arrow),
                    label: Text(tr('ate.start')),
                  ),
          ),
        ]),
        const SizedBox(height: 10),
        TextField(
          controller: _note,
          enabled: !_busy,
          decoration: InputDecoration(
              labelText: tr('ate.note'),
              border: const OutlineInputBorder(),
              isDense: true),
        ),
      ]),
    );
  }

  Widget _verdictCard(ColorScheme cs) {
    final rec = _record!;
    final sem = AppSemantic.of(context);
    final ok = rec.verdict == AteVerdict.pass;
    final color = ok
        ? sem.success
        : rec.verdict == AteVerdict.fail
            ? cs.error
            : sem.warning;
    return AppCard(
      padding: const EdgeInsets.all(18),
      child: Row(children: [
        // Kết luận là thứ người đứng máy liếc từ xa — để nhỏ là bắt họ cúi vào
        // đọc từng máy một.
        Icon(ok ? Icons.check_circle : Icons.cancel, size: 64, color: color),
        const SizedBox(width: 18),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(
              ok
                  ? tr('ate.pass')
                  : rec.verdict == AteVerdict.fail
                      ? tr('ate.fail')
                      : tr('ate.aborted'),
              style: TextStyle(
                  fontSize: 36, fontWeight: FontWeight.w800, color: color),
            ),
            const SizedBox(height: 2),
            Text(
              '${rec.sn}'
              '${rec.failCode.isEmpty ? '' : ' · ${tr('ate.failedAt')} ${rec.failCode}'}'
              '${rec.fwVersion.isEmpty ? '' : ' · v${rec.fwVersion}'}',
              style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontSize: 14,
                  fontFeatures: [FontFeature.tabularFigures()]),
            ),
            if (_sendState.isNotEmpty) ...[
              const SizedBox(height: 6),
              Text(_sendState,
                  style: TextStyle(
                      fontSize: 12.5,
                      color: _sendError ? cs.error : cs.onSurfaceVariant)),
            ],
            if (ok && !_sendError) ...[
              const SizedBox(height: 4),
              Text(tr('ate.nextMachine'),
                  style: TextStyle(fontSize: 13.5, color: sem.success)),
            ],
          ]),
        ),
      ]),
    );
  }

  Widget _stepsCard(ColorScheme cs) {
    final sem = AppSemantic.of(context);
    return AppCard(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
      child: Column(children: [
        for (final code in AteRunner.stepCodes) _stepRow(code, cs, sem),
      ]),
    );
  }

  Widget _stepRow(String code, ColorScheme cs, AppSemantic sem) {
    final s = _steps[code];
    final running = _current == code && _busy;
    final icon = running
        ? const SizedBox(
            width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
        : Icon(
            s == null
                ? Icons.radio_button_unchecked
                : s.verdict == AteVerdict.pass
                    ? Icons.check_circle
                    : s.verdict == AteVerdict.fail
                        ? Icons.cancel
                        : Icons.remove_circle_outline,
            color: s == null
                ? cs.outline
                : s.verdict == AteVerdict.pass
                    ? sem.success
                    : s.verdict == AteVerdict.fail
                        ? cs.error
                        : cs.onSurfaceVariant,
          );
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        SizedBox(width: 26, child: Center(child: icon)),
        const SizedBox(width: 8),
        SizedBox(
          width: 68,
          child: Text(code,
              style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontSize: 12.5,
                  fontWeight: FontWeight.w600)),
        ),
        Expanded(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(tr('ate.step.$code'),
                style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
            if (s != null && s.detail.isNotEmpty)
              Text(s.detail,
                  style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
          ]),
        ),
        if (s != null && !_busy)
          TextButton(
            onPressed: () => _rerunStep(code),
            child: Text(tr('ate.rerun')),
          ),
      ]),
    );
  }

  /// Nhật ký trạm — **thu gọn mặc định**: công nhân đứng máy không đọc log
  /// esptool, còn kỹ thuật thì bung ra khi cần. Để nó mở sẵn chỉ đẩy phần kết
  /// luận và danh sách bước lên trên, xa tầm mắt.
  Widget _logCard(ColorScheme cs) {
    final sem = AppSemantic.of(context);
    final text = _log.toString();
    final has = text.isNotEmpty;
    final lines = has ? '\n'.allMatches(text.trimRight()).length + 1 : 0;
    return AppCard(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      child: ExpansionTile(
        tilePadding: EdgeInsets.zero,
        // Con của ExpansionTile mặc định căn GIỮA → khung log co lại bằng dòng
        // dài nhất rồi nằm lọt thỏm giữa thẻ, hai bên trắng trơn. Phải stretch.
        expandedCrossAxisAlignment: CrossAxisAlignment.stretch,
        childrenPadding: const EdgeInsets.only(bottom: 10),
        shape: const Border(),
        collapsedShape: const Border(),
        // Lúc thu gọn ScrollController chưa có client nên các dòng chạy vào
        // không kéo xuống được → bung ra là thấy đầu log của máy đầu ca. Kéo
        // xuống cuối ngay khi mở: cái người ta cần xem là dòng VỪA chạy.
        onExpansionChanged: (open) {
          if (!open) return;
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (_logScroll.hasClients) {
              _logScroll.jumpTo(_logScroll.position.maxScrollExtent);
            }
          });
        },
        title: Row(children: [
          Text(tr('ate.log'),
              style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13.5)),
          const SizedBox(width: 8),
          // Đếm dòng: lúc thu gọn vẫn biết có log hay không mà không phải bung.
          if (has)
            Text(tr('ate.logLines').replaceFirst('{n}', '$lines'),
                style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
          const Spacer(),
          IconButton(
            tooltip: tr('common.copy'),
            iconSize: 18,
            onPressed:
                has ? () => Clipboard.setData(ClipboardData(text: text)) : null,
            icon: const Icon(Icons.copy_all_outlined),
          ),
          IconButton(
            tooltip: tr('ate.logSave'),
            iconSize: 18,
            onPressed: has ? _saveLog : null,
            icon: const Icon(Icons.download_outlined),
          ),
        ]),
        children: [
          Container(
            height: 280,
            width: double.infinity,
            decoration: BoxDecoration(
              color: sem.consoleBg,
              borderRadius: BorderRadius.circular(AppRadius.base),
            ),
            padding: const EdgeInsets.all(12),
            child: SingleChildScrollView(
              controller: _logScroll,
              child: SelectableText(
                has ? text : tr('ate.logEmpty'),
                style: TextStyle(
                    fontFamily: 'JetBrains Mono',
                    fontSize: 12,
                    height: 1.4,
                    color: has ? sem.consoleFg : sem.consoleDim),
              ),
            ),
          ),
        ],
      ),
    );
  }

  /// Tải nhật ký ra file `.txt` (desktop: hộp thoại lưu · web: Downloads).
  ///
  /// Kèm phần đầu ghi máy/lô/trạm/bộ ngưỡng: log gửi cho kỹ thuật mà không nói
  /// của máy nào, chấm theo ngưỡng nào thì đọc xong cũng không kết luận được.
  Future<void> _saveLog() async {
    final text = _log.toString();
    if (text.isEmpty) return;
    final c = _cfg;
    final sn = _sn.text.trim();
    final t = DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    final stamp = '${t.year}${two(t.month)}${two(t.day)}_'
        '${two(t.hour)}${two(t.minute)}${two(t.second)}';
    final name = 'nhatky_${sn.isEmpty ? 'tram' : sn}_$stamp.txt'
        .replaceAll(RegExp(r'[^A-Za-z0-9_.\-]'), '_');
    final head = [
      '# ${tr('ate.log')} — FBT_RAPID',
      '# ${tr('ate.sn')}: ${sn.isEmpty ? '-' : sn}'
          ' · ${tr('ate.batch')}: ${c?.batch.isNotEmpty == true ? c!.batch : '-'}'
          ' · ${tr('ate.station')}: ${c?.station.isNotEmpty == true ? c!.station : '-'}'
          ' · ${tr('ate.operator')}: ${c?.operator.isNotEmpty == true ? c!.operator : '-'}',
      '# ${tr('ate.limits')}: ${_limits.version}'
          ' · ${t.toIso8601String()}',
      '',
    ].join('\n');
    try {
      final saved = await pf.saveTextFileDialog(name, '$head$text',
          label: 'Text', extensions: const ['txt']);
      if (!mounted || saved == null) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(tr('ate.logSaved')
            .replaceFirst('{p}', saved.isEmpty ? name : saved)),
      ));
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        backgroundColor: Theme.of(context).colorScheme.error,
        content: Text('$e'),
      ));
    }
  }
}
