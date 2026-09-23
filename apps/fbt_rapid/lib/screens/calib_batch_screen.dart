/// Màn **một lô pha** ống chuẩn (mở từ mục Lô pha của tab Hiệu chuẩn).
///
/// Đi đúng thứ tự công việc của kỹ sư, mỗi phần là một thẻ:
/// 1. **Nguyên liệu + máy tham chiếu** — lô Fluorescein (NIST-traceable), EDTA, máy/khe đọc.
/// 2. **Bước pha** — checklist theo bàn giao + WI: mỗi bước có thể tích DỰ KIẾN (C1·V1 = C2·V2)
///    và ô ghi THỰC TẾ; tick = ghi giờ + người làm ngay lên server (không có nút Lưu riêng —
///    quên lưu là mất truy vết).
/// 3. **Số đo thô** — bảng ống × nồng độ. Hai cách điền: **Đọc từ máy** (nối cổng COM/Web
///    Serial máy tham chiếu, đặt ống, bấm ĐỌC hoặc Enter → app gửi lệnh khe, nhận `{Green: N}`,
///    điền ô đang chọn, LƯU NGAY từng ô rồi tự nhảy sang ống kế — `services/calib_reader.dart`)
///    hoặc gõ tay rồi bấm Lưu số đo (server gộp từng ô, ô trống = xoá).
/// 4. **Xếp hạng** — server thử mọi tổ hợp, trả top + gợi ý bộ KHÔNG trùng ống + QC blank
///    (SD, SNR 3,3, LOD). Chọn gợi ý → **Đóng gói** → bộ ống `<lô>-S<nn>` in nhãn túi zip.
/// 5. **Bộ ống của lô** — trạng thái, cấp máy/thu hồi tại chỗ.
library;

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/app_settings.dart';
import '../services/calib_api.dart';
import '../services/calib_reader.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/serial_link.dart';
import 'calib_screen.dart';

class CalibBatchScreen extends StatefulWidget {
  final AppSettings settings;
  final String batchId;
  const CalibBatchScreen({super.key, required this.settings, required this.batchId});

  @override
  State<CalibBatchScreen> createState() => _CalibBatchScreenState();
}

class _CalibBatchScreenState extends State<CalibBatchScreen> {
  late final CalibApi _api = CalibApi.of(widget.settings);
  Map<String, dynamic>? _doc;
  bool _loading = false;
  bool _saving = false;
  String? _error;

  // Thẻ 1
  final _stockLot = TextEditingController();
  final _stockExpiry = TextEditingController();
  final _stockConc = TextEditingController();
  final _bufferLot = TextEditingController();
  final _readerDevice = TextEditingController();
  final _readerSlot = TextEditingController();
  final _readerFw = TextEditingController();
  final _note = TextEditingController();

  // Thẻ 2: thể tích thực tế theo mã bước
  final Map<String, TextEditingController> _actualDye = {};
  final Map<String, TextEditingController> _actualBuf = {};

  // Thẻ 3: ô số đo [nồng độ][số ống]
  final Map<String, Map<int, TextEditingController>> _cells = {};

  // Thẻ 3: đọc từ máy tham chiếu
  CalibReader? _reader;
  List<String> _ports = const [];
  String? _port;
  bool _connecting = false;
  bool _reading = false;
  ({String conc, int tube})? _target;
  String _lastRead = '';
  // Firmware v2.4.6 (chế độ đọc ống chuẩn): máy tự in số khi bấm ĐỎ, đổi khe khi bấm XANH.
  StreamSubscription<double>? _readingsSub;
  StreamSubscription<int>? _slotSub;
  StreamSubscription<String>? _errSub;
  StreamSubscription<bool>? _modeSub;

  // Thẻ 4
  CalibRank? _rank;
  bool _ranking = false;
  final Set<String> _picked = {};
  final _expiresDays = TextEditingController();

  // Thanh tiến độ đầu màn: mỗi thẻ một key để nút "Đi tới" cuộn thẳng tới việc cần làm.
  final _cardKeys = <int, GlobalKey>{for (var i = 1; i <= 5; i++) i: GlobalKey()};

  bool get _canWrite => SessionStore.canWriteCalib;
  bool get _closed => (_doc?['status'] ?? '') == 'closed';
  bool get _editable => _canWrite && !_closed && !_saving;

  List<String> get _concs =>
      [for (final c in (_doc?['concentrations'] as List?) ?? const []) _concKey(c)];
  int get _tubesPerConc => ((_doc?['tubes_per_conc'] as num?) ?? 0).toInt();

  static String _concKey(dynamic c) {
    final d = (c as num?)?.toDouble() ?? 0;
    return d == d.roundToDouble() ? d.toInt().toString() : d.toString();
  }

  @override
  void initState() {
    super.initState();
    // Phím Enter/Space = ĐỌC, bắt ở tầng HardwareKeyboard chứ KHÔNG qua Shortcuts/Focus:
    // kỹ sư một tay cầm ống, tay kia gõ Enter — không được đòi "bấm vào panel trước cho
    // có focus" (đã thử CallbackShortcuts: sau khi bấm nút, Enter rơi vào đâu không rõ).
    HardwareKeyboard.instance.addHandler(_onKey);
    _refreshPorts();
    _load();
  }

  bool _onKey(KeyEvent e) {
    if (e is! KeyDownEvent) return false;
    final k = e.logicalKey;
    if (k != LogicalKeyboardKey.enter && k != LogicalKeyboardKey.numpadEnter && k != LogicalKeyboardKey.space) {
      return false;
    }
    if (_reader == null || _reading || !mounted) return false;
    // Đang gõ trong ô nhập (số đo tay, lot…) hoặc đang mở dialog → để yên cho họ.
    if (FocusManager.instance.primaryFocus?.context?.widget is EditableText) return false;
    if (ModalRoute.of(context)?.isCurrent != true) return false;
    _readOne();
    return true;
  }

  @override
  void dispose() {
    HardwareKeyboard.instance.removeHandler(_onKey);
    _readingsSub?.cancel();
    _slotSub?.cancel();
    _errSub?.cancel();
    _modeSub?.cancel();
    final r = _reader;
    if (r != null) {
      // Rời màn = trả máy về màn chính (best-effort, không chờ).
      unawaited(r.endMode().catchError((_) {}).whenComplete(r.close));
    }
    for (final c in [
      _stockLot, _stockExpiry, _stockConc, _bufferLot, _readerDevice, _readerSlot, _readerFw,
      _note, _expiresDays,
    ]) {
      c.dispose();
    }
    for (final c in _actualDye.values) {
      c.dispose();
    }
    for (final c in _actualBuf.values) {
      c.dispose();
    }
    for (final m in _cells.values) {
      for (final c in m.values) {
        c.dispose();
      }
    }
    super.dispose();
  }

  // --- nạp / đồng bộ controller -----------------------------------------------------

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final doc = await _api.batch(widget.batchId);
      if (!mounted) return;
      setState(() {
        _doc = doc;
        _fill(doc);
      });
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  void _fill(Map<String, dynamic> doc) {
    final stock = (doc['stock'] as Map?) ?? const {};
    final buffer = (doc['buffer'] as Map?) ?? const {};
    final reader = (doc['reader'] as Map?) ?? const {};
    _stockLot.text = (stock['lot'] ?? '').toString();
    _stockExpiry.text = (stock['expiry'] ?? '').toString();
    _stockConc.text = _num(stock['conc_nM']);
    _bufferLot.text = (buffer['lot'] ?? '').toString();
    _readerDevice.text = (reader['device'] ?? '').toString();
    _readerSlot.text = (reader['slot'] ?? '').toString();
    _readerFw.text = (reader['fw'] ?? '').toString();
    _note.text = (doc['note'] ?? '').toString();
    for (final s in (doc['steps'] as List?) ?? const []) {
      if (s is! Map) continue;
      final code = (s['code'] ?? '').toString();
      _actualDye.putIfAbsent(code, TextEditingController.new).text = _num(s['actual_dye_ul']);
      _actualBuf.putIfAbsent(code, TextEditingController.new).text = _num(s['actual_buffer_ul']);
    }
    final readings = (doc['readings'] as Map?) ?? const {};
    for (final ck in _concs) {
      final row = _cells.putIfAbsent(ck, () => {});
      final have = (readings[ck] as Map?) ?? const {};
      for (var no = 1; no <= _tubesPerConc; no++) {
        row.putIfAbsent(no, TextEditingController.new).text = _num(have['$no']);
      }
    }
  }

  static String _num(dynamic v) {
    if (v == null || v == '') return '';
    if (v is num) return v == v.roundToDouble() ? v.toInt().toString() : v.toString();
    return v.toString();
  }

  Future<void> _patch(Map<String, dynamic> patch, {String? okMsg}) async {
    setState(() {
      _saving = true;
      _error = null;
    });
    try {
      final doc = await _api.updateBatch(widget.batchId, patch, by: SessionStore.username);
      if (!mounted) return;
      setState(() {
        _doc = doc;
        _fill(doc);
      });
      if (okMsg != null) calibSnack(context, okMsg);
    } catch (e) {
      if (mounted) {
        setState(() => _error = e.toString());
        calibSnack(context, e.toString(), error: true);
      }
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  // --- đọc từ máy tham chiếu ------------------------------------------------------------

  void _refreshPorts() {
    if (!serialLinkCanListPorts) return;
    setState(() {
      _ports = listSerialLinkPorts();
      if (_port == null || !_ports.contains(_port)) _port = _ports.isEmpty ? null : _ports.first;
    });
  }

  int get _slot => (int.tryParse(_readerSlot.text.trim()) ?? 1).clamp(1, 10);

  /// Đổi khe đọc từ panel: cập nhật ô "Khe đọc" ở thẻ 1 VÀ lưu ngay `reader.slot` lên server —
  /// khe nào đọc lô này là dữ liệu truy vết (số thô phụ thuộc khe), không để trong RAM.
  Future<void> _setSlot(int v, {bool fromDevice = false}) async {
    if (v < 1 || v > 10) return;
    if (int.tryParse(_readerSlot.text.trim()) == v) return;
    _readerSlot.text = '$v';
    setState(() {});
    // Đổi từ app → báo máy (LCD đổi khe). Đổi từ máy (nút XANH) thì máy đã biết, và máy
    // trả lời CalibSlot bằng chính {CalibSlot: n} → gửi lại là vòng lặp.
    if (!fromDevice) {
      try {
        await _reader?.setSlot(v);
      } catch (_) {}
    }
    await _patch({
      'reader': {'slot': v}
    });
  }

  /// Nhãn máy hiện trên LCD = ô app đang chờ đọc. Best-effort: firmware cũ không hiểu.
  Future<void> _sendLabel() async {
    final r = _reader;
    if (r == null) return;
    try {
      await r.setLabel(calibLabelFor(_target));
    } catch (_) {}
  }

  /// Ô trống đầu tiên theo thứ tự đọc (hết ống của nồng độ đầu rồi sang nồng độ kế).
  ({String conc, int tube})? _firstEmptyCell() {
    for (final ck in _concs) {
      for (var no = 1; no <= _tubesPerConc; no++) {
        if ((_cells[ck]?[no]?.text.trim() ?? '').isEmpty) return (conc: ck, tube: no);
      }
    }
    return null;
  }

  Future<void> _connect() async {
    if (_reader != null) return;
    setState(() => _connecting = true);
    try {
      final link = await openSerialLink(name: _port, baud: 115200);
      if (link == null) return; // web: người dùng Huỷ hộp thoại chọn cổng
      if (!mounted) {
        await link.close();
        return;
      }
      final reader = CalibReader(link);
      setState(() {
        _reader = reader;
        _target ??= _firstEmptyCell() ?? (conc: _concs.first, tube: 1);
        _lastRead = '';
      });
      _readingsSub = reader.readings.listen(_onDeviceReading);
      _slotSub = reader.slotChanges.listen((n) => _setSlot(n, fromDevice: true));
      _errSub = reader.calibErrors.listen((e) {
        if (mounted) calibSnack(context, 'Máy báo: $e', error: true);
      });
      _modeSub = reader.modeChanges.listen((on) {
        if (!mounted || on) return;
        // Nút TRẮNG trên máy: không phải lỗi — lần ĐỌC kế app tự đưa máy vào lại chế độ.
        calibSnack(context, 'Máy đã thoát chế độ đọc ống (nút TRẮNG). Bấm ĐỌC/Enter là máy vào lại.');
      });
      // Vào chế độ đọc ống chuẩn trên máy (LCD sang màn đọc, ngừng quay LED) + nhãn ô đầu.
      // Firmware chưa có chế độ này trả "Command is not supported!" — vẫn đọc được bằng 1 byte.
      try {
        await reader.startMode(_slot);
        await _sendLabel();
      } catch (e) {
        if (mounted) calibSnack(context, e.toString(), error: true);
      }
    } catch (e) {
      if (mounted) calibSnack(context, e.toString(), error: true);
    } finally {
      if (mounted) setState(() => _connecting = false);
    }
  }

  Future<void> _disconnect() async {
    final r = _reader;
    await _readingsSub?.cancel();
    await _slotSub?.cancel();
    await _errSub?.cancel();
    await _modeSub?.cancel();
    _readingsSub = _slotSub = _errSub = _modeSub = null;
    setState(() => _reader = null);
    if (r == null) return;
    try {
      await r.endMode(); // máy về màn chính
    } catch (_) {}
    await r.close();
  }

  /// Máy tự đọc (kỹ sư bấm nút ĐỎ trên máy) → điền như một lần ĐỌC từ app.
  Future<void> _onDeviceReading(double v) async {
    if (_reading || !_editable || !mounted) return;
    if (_target == null) {
      calibSnack(context, 'Máy vừa đọc $v nhưng lô đã đủ số — bấm vào ô muốn đọc lại.');
      return;
    }
    setState(() => _reading = true);
    try {
      await _applyReading(v);
    } finally {
      if (mounted) setState(() => _reading = false);
    }
  }

  /// Điền ô đang chọn → LƯU NGAY ô đó → nhảy ống kế → báo nhãn mới cho máy.
  Future<void> _applyReading(double v) async {
    final tg = _target;
    if (tg == null) return;
    final txt = v == v.roundToDouble() ? v.toInt().toString() : v.toString();
    _cells[tg.conc]?[tg.tube]?.text = txt;
    setState(() => _lastRead = '${tg.conc} nM · ống ${tg.tube} = $txt');
    await _patch({
      'readings': {
        tg.conc: {'${tg.tube}': v}
      }
    });
    if (!mounted) return;
    setState(() {
      _target = nextCalibCell(_concs, _tubesPerConc, tg.conc, tg.tube);
      _rank = null;
      _picked.clear();
    });
    await _sendLabel();
  }

  /// Một lần bấm ĐỌC: gửi lệnh khe → nhận raw → điền ô đang chọn → LƯU NGAY ô đó → nhảy ống kế.
  /// Lưu từng ô chứ không gom: đang đo 40 ống mà rớt mạng/đóng nhầm màn thì mất hết.
  Future<void> _readOne() async {
    final r = _reader;
    final tg = _target;
    if (r == null || _reading || !_editable) return;
    if (tg == null) {
      calibSnack(context, 'Đã đọc hết ống của lô — chọn ô muốn đọc lại bằng cách bấm vào ô đó.');
      return;
    }
    setState(() => _reading = true);
    try {
      final v = await r.readSlot(_slot);
      if (!mounted) return;
      await _applyReading(v);
    } on CalibReadException catch (e) {
      if (mounted) calibSnack(context, e.message, error: true);
    } catch (e) {
      if (mounted) calibSnack(context, e.toString(), error: true);
    } finally {
      if (mounted) setState(() => _reading = false);
    }
  }

  // --- thao tác từng thẻ --------------------------------------------------------------

  Future<void> _saveInfo() => _patch({
        'stock': {
          'lot': _stockLot.text.trim(),
          'expiry': _stockExpiry.text.trim(),
          if (double.tryParse(_stockConc.text.trim()) != null)
            'conc_nM': double.parse(_stockConc.text.trim()),
        },
        'buffer': {'lot': _bufferLot.text.trim()},
        'reader': {
          'device': _readerDevice.text.trim(),
          'slot': int.tryParse(_readerSlot.text.trim()),
          'fw': _readerFw.text.trim(),
        },
        'note': _note.text.trim(),
      }, okMsg: 'Đã lưu thông tin lô');

  Future<void> _toggleStep(String code, bool done) => _patch({
        'steps': [
          {
            'code': code,
            'done_at': done ? DateTime.now().toUtc().toIso8601String() : '',
            'by': done ? SessionStore.username : '',
            'actual_dye_ul': _actualDye[code]?.text.trim() ?? '',
            'actual_buffer_ul': _actualBuf[code]?.text.trim() ?? '',
          }
        ],
      });

  Future<void> _saveReadings() async {
    final readings = <String, Map<String, dynamic>>{};
    var bad = '';
    for (final ck in _concs) {
      final row = <String, dynamic>{};
      for (final e in (_cells[ck] ?? const <int, TextEditingController>{}).entries) {
        final t = e.value.text.trim().replaceAll(',', '.');
        if (t.isEmpty) {
          row['${e.key}'] = null;
        } else if (double.tryParse(t) == null) {
          bad = '$ck nM / ống ${e.key}: "$t" không phải số';
        } else {
          row['${e.key}'] = double.parse(t);
        }
      }
      readings[ck] = row;
    }
    if (bad.isNotEmpty) {
      calibSnack(context, bad, error: true);
      return;
    }
    await _patch({'readings': readings}, okMsg: 'Đã lưu số đo');
    setState(() {
      _rank = null;
      _picked.clear();
    });
  }

  Future<void> _doRank() async {
    setState(() {
      _ranking = true;
      _error = null;
      _picked.clear();
    });
    try {
      final r = await _api.rank(widget.batchId, top: 30);
      if (!mounted) return;
      setState(() {
        _rank = r;
        // Mặc định chọn sẵn mọi gợi ý — kỹ sư bỏ tick cái nào không muốn đóng.
        _picked.addAll(r.suggested.map((c) => c.key));
      });
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _ranking = false);
    }
  }

  Future<void> _pack() async {
    final r = _rank;
    if (r == null) return;
    final combos = [for (final c in r.suggested) if (_picked.contains(c.key)) c];
    if (combos.isEmpty) return;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Đóng gói ${combos.length} bộ ống?'),
        content: Text(
          'Mỗi bộ = 4 ống bỏ chung túi zip, in mã bộ lên nhãn. Ống đã đóng bộ không được '
          'dùng cho bộ khác.\n\n${combos.map((c) => '#${c.rank}  ${calibTubesLabel(c.tubes)}').join('\n')}',
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Huỷ')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Đóng gói')),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    setState(() => _saving = true);
    try {
      final sets = await _api.createSets(widget.batchId, combos,
          by: SessionStore.username, expiresDays: int.tryParse(_expiresDays.text.trim()));
      if (!mounted) return;
      calibSnack(context, 'Đã tạo ${sets.length} bộ: ${sets.map((s) => s.id).join(', ')}');
      setState(() {
        _rank = null;
        _picked.clear();
      });
      await _load();
    } catch (e) {
      if (mounted) calibSnack(context, e.toString(), error: true);
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  Future<void> _setStatus(String status) => _patch({'status': status},
      okMsg: status == 'closed' ? 'Đã đóng lô' : 'Đã mở lại lô');

  Future<void> _delete() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Xoá lô ${widget.batchId}?'),
        content: const Text('Chỉ xoá được lô chưa có bộ ống (tạo nhầm). Không hoàn tác được.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Không')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Xoá')),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    try {
      await _api.deleteBatch(widget.batchId, by: SessionStore.username);
      if (!mounted) return;
      calibSnack(context, 'Đã xoá lô ${widget.batchId}');
      Navigator.of(context).pop();
    } catch (e) {
      if (mounted) calibSnack(context, e.toString(), error: true);
    }
  }

  // --- giao diện ---------------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final doc = _doc;
    final mobile = isMobileWidth(context);
    final padX = mobile ? 12.0 : 24.0;
    return Scaffold(
      appBar: AppBar(
        title: Row(children: [
          Text(widget.batchId),
          if (doc != null) ...[const SizedBox(width: 10), CalibStatusChip((doc['status'] ?? '').toString())],
        ]),
        actions: [
          IconButton(tooltip: 'Tải lại', onPressed: _loading ? null : _load, icon: const Icon(Icons.refresh)),
          if (doc != null && _canWrite)
            PopupMenuButton<String>(
              onSelected: (v) {
                if (v == 'close') _setStatus('closed');
                if (v == 'reopen') _setStatus('ranked');
                if (v == 'delete') _delete();
              },
              itemBuilder: (_) => [
                if (!_closed) const PopupMenuItem(value: 'close', child: Text('Đóng lô (khoá sửa)')),
                if (_closed) const PopupMenuItem(value: 'reopen', child: Text('Mở lại lô')),
                if (((doc['n_sets'] as num?) ?? 0) == 0)
                  const PopupMenuItem(value: 'delete', child: Text('Xoá lô tạo nhầm')),
              ],
            ),
        ],
      ),
      body: doc == null
          ? Center(
              child: _loading
                  ? const CircularProgressIndicator()
                  : Text(_error ?? 'Không có dữ liệu', style: TextStyle(color: cs.error)))
          : ListView(
              padding: EdgeInsets.fromLTRB(padX, 12, padX, 32),
              children: [
                if (_error != null)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: Text(_error!, style: TextStyle(color: cs.error)),
                  ),
                if (_closed)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: Text('Lô đã đóng — chỉ xem. Mở lại từ menu ⋮ nếu cần sửa.',
                        style: TextStyle(color: cs.outline)),
                  ),
                _progressHeader(doc),
                const SizedBox(height: 12),
                KeyedSubtree(key: _cardKeys[1], child: _infoCard(doc)),
                const SizedBox(height: 12),
                KeyedSubtree(key: _cardKeys[2], child: _stepsCard(doc)),
                const SizedBox(height: 12),
                KeyedSubtree(key: _cardKeys[3], child: _readingsCard(doc)),
                const SizedBox(height: 12),
                KeyedSubtree(key: _cardKeys[4], child: _rankCard(doc)),
                const SizedBox(height: 12),
                KeyedSubtree(key: _cardKeys[5], child: _setsCard(doc)),
              ],
            ),
    );
  }

  /// Thẻ chuẩn của màn. [hint] = MỘT câu bằng lời thường cho người vận hành; [tech] = phần
  /// giải thích kỹ thuật (công thức, ngưỡng, giao thức) gập lại sau nút "Chi tiết kỹ thuật" —
  /// nhân viên không cần đọc, kỹ sư mở khi cần. [done] vẽ dấu ✓ xanh cạnh tiêu đề.
  Widget _card(String title,
      {String? hint,
      String? tech,
      bool? done,
      required List<Widget> children,
      List<Widget> actions = const []}) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              if (done != null)
                Padding(
                  padding: const EdgeInsets.only(right: 8),
                  child: Icon(done ? Icons.check_circle : Icons.radio_button_unchecked,
                      size: 20, color: done ? sem.success : cs.outline),
                ),
              Expanded(child: Text(title, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 16))),
              ...actions,
            ]),
            if (hint != null)
              Padding(
                padding: const EdgeInsets.only(top: 4, bottom: 6),
                child: Text(hint, style: TextStyle(color: cs.onSurfaceVariant, fontSize: 13, height: 1.4)),
              ),
            if (tech != null) _techNote(tech),
            const SizedBox(height: 6),
            ...children,
          ],
        ),
      ),
    );
  }

  /// Khối "Chi tiết kỹ thuật" gập mặc định — giữ mọi công thức/ngưỡng ở đó thay vì ở hint.
  Widget _techNote(String text) {
    final cs = Theme.of(context).colorScheme;
    return Theme(
      data: Theme.of(context).copyWith(dividerColor: Colors.transparent),
      child: ExpansionTile(
        tilePadding: EdgeInsets.zero,
        childrenPadding: const EdgeInsets.only(bottom: 6),
        dense: true,
        visualDensity: VisualDensity.compact,
        title: Text('Chi tiết kỹ thuật', style: TextStyle(color: cs.outline, fontSize: 12)),
        children: [
          Align(
            alignment: Alignment.centerLeft,
            child: Text(text, style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12, height: 1.4)),
          ),
        ],
      ),
    );
  }

  // ---- tiến độ lô: 4 việc, việc nào xong, việc gì làm tiếp ----------------------------------

  int _filledCount() {
    var filled = 0;
    for (final ck in _concs) {
      filled += (_cells[ck]?.values ?? const <TextEditingController>[]).where((c) => c.text.trim().isNotEmpty).length;
    }
    return filled;
  }

  /// Việc đang chờ người vận hành, bằng lời thường: (số thẻ, câu hướng dẫn).
  (int, String) _nextAction(Map<String, dynamic> doc) {
    final stock = (doc['stock'] as Map?) ?? const {};
    final slotSet = int.tryParse(_readerSlot.text.trim()) != null;
    if ((stock['lot'] ?? '').toString().trim().isEmpty || !slotSet) {
      return (1, 'Ghi lô thuốc thử đang dùng và chọn khe sẽ đặt ống vào máy, rồi bấm Lưu.');
    }
    final steps = [for (final s in (doc['steps'] as List?) ?? const []) if (s is Map) s];
    final undone = steps.where((s) => (s['done_at'] ?? '').toString().isEmpty).toList();
    if (undone.isNotEmpty) {
      return (2, 'Pha: ${undone.first['name']} — làm xong thì tick vào ô.');
    }
    final total = _tubesPerConc * _concs.length;
    final filled = _filledCount();
    if (filled < total) {
      return (3, 'Đo ống: còn ${total - filled}/$total ống. Nối máy, đặt ống vào khe rồi bấm ĐỌC.');
    }
    final nSets = ((doc['n_sets'] as num?) ?? 0).toInt();
    if (nSets == 0) {
      return (4, 'Đo xong rồi. Bấm "Tìm bộ đạt" để máy chọn bộ ống đạt chuẩn, tick bộ muốn giữ rồi Đóng gói.');
    }
    return (5, 'Xong: $nSets bộ đã đóng. In mã bộ lên nhãn túi; menu ⋮ → Đóng lô để khoá.');
  }

  Widget _progressHeader(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final (nextCard, nextText) = _nextAction(doc);
    const labels = ['Nguyên liệu', 'Pha', 'Đo ống', 'Đóng bộ'];
    // Thẻ 5 (bộ đã đóng) là kết quả của việc 4 → thanh có 4 việc.
    final current = nextCard >= 5 ? 5 : nextCard;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          for (var i = 1; i <= 4; i++) ...[
            Expanded(
              child: Column(children: [
                Container(
                  height: 6,
                  decoration: BoxDecoration(
                    color: i < current
                        ? sem.success
                        : i == current
                            ? cs.primary
                            : cs.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(3),
                  ),
                ),
                const SizedBox(height: 4),
                Text(labels[i - 1],
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: i == current ? FontWeight.w700 : FontWeight.w500,
                      color: i <= current ? cs.onSurface : cs.outline,
                    )),
              ]),
            ),
            if (i < 4) const SizedBox(width: 6),
          ],
        ]),
        const SizedBox(height: 10),
        Material(
          color: current == 5 ? sem.success.withValues(alpha: 0.10) : cs.primaryContainer.withValues(alpha: 0.55),
          borderRadius: BorderRadius.circular(AppRadius.base),
          child: InkWell(
            borderRadius: BorderRadius.circular(AppRadius.base),
            onTap: () => _scrollToCard(nextCard),
            child: Padding(
              padding: const EdgeInsets.fromLTRB(14, 10, 10, 10),
              child: Row(children: [
                Icon(current == 5 ? Icons.check_circle : Icons.arrow_circle_right_outlined,
                    color: current == 5 ? sem.success : cs.primary),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Text(current == 5 ? 'Lô này đã xong' : 'Việc tiếp theo',
                        style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
                    Text(nextText, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600, height: 1.3)),
                  ]),
                ),
                if (current != 5) Icon(Icons.chevron_right, color: cs.outline),
              ]),
            ),
          ),
        ),
      ],
    );
  }

  void _scrollToCard(int n) {
    final ctx = _cardKeys[n]?.currentContext;
    if (ctx == null) return;
    Scrollable.ensureVisible(ctx,
        duration: const Duration(milliseconds: 350), curve: Curves.easeOutCubic, alignment: 0.05);
  }

  Widget _tf(TextEditingController c, String label, {String? hint, bool number = false, double? width}) {
    final f = TextField(
      controller: c,
      enabled: _editable,
      keyboardType: number ? const TextInputType.numberWithOptions(decimal: true) : null,
      decoration: InputDecoration(labelText: label, hintText: hint, isDense: true),
    );
    return width == null ? f : SizedBox(width: width, child: f);
  }

  // Thẻ 1
  Widget _infoCard(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final stock = (doc['stock'] as Map?) ?? const {};
    final slotSet = int.tryParse(_readerSlot.text.trim()) != null;
    return _card(
      '1. Nguyên liệu & máy',
      done: (stock['lot'] ?? '').toString().trim().isNotEmpty && slotSet,
      hint: 'Chép số lô trên nhãn ống thuốc thử và chọn khe của máy sẽ đặt ống vào (dùng MỘT khe cho cả lô).',
      tech: '${stock['name'] ?? ''} ${stock['supplier'] ?? ''} ${stock['catalog'] ?? ''} — WI yêu cầu thuốc thử '
          'NIST-traceable; ${doc['tubes_per_conc']} ống × ${_num(doc['aliquot_ul'])} µL mỗi nồng độ '
          '(${_concs.join('/')} nM). Tạo ${calibFmtTs((doc['created_at'] ?? '').toString())}'
          '${(doc['created_by'] ?? '').toString().isEmpty ? '' : ' bởi ${doc['created_by']}'}.',
      actions: [
        if (_editable)
          FilledButton.tonal(onPressed: _saveInfo, child: const Text('Lưu')),
      ],
      children: [
        Wrap(spacing: 10, runSpacing: 10, children: [
          _tf(_stockLot, 'Lô Fluorescein', width: 180),
          _tf(_stockExpiry, 'HSD stock', hint: 'yyyy-mm-dd', width: 140),
          _tf(_stockConc, 'Stock (nM)', number: true, width: 130),
          _tf(_bufferLot, 'Lô EDTA pH 8', width: 160),
        ]),
        const SizedBox(height: 10),
        Wrap(spacing: 10, runSpacing: 10, children: [
          _tf(_readerDevice, 'Số máy (SN)', width: 180),
          _tf(_readerSlot, 'Đặt ống vào khe số', hint: '1–10', number: true, width: 150),
          _tf(_readerFw, 'Firmware máy', width: 130),
        ]),
        const SizedBox(height: 10),
        _tf(_note, 'Ghi chú lô'),
        if (stock['conc_nM'] != null && ((stock['conc_nM'] as num?) ?? 0) > 1e6)
          Padding(
            padding: const EdgeInsets.only(top: 6),
            child: Text('⚠ Stock ${_num(stock['conc_nM'])} nM = ${((stock['conc_nM'] as num) / 1e6).toStringAsFixed(2)} mM '
                '— bảng pha 52× của bàn giao chỉ đúng với 52 µM; kiểm lại nhãn ống.',
                style: TextStyle(color: cs.error, fontSize: 12)),
          ),
      ],
    );
  }

  // Thẻ 2
  Widget _stepsCard(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final steps = [for (final s in (doc['steps'] as List?) ?? const []) if (s is Map) s];
    final done = steps.where((s) => (s['done_at'] ?? '').toString().isNotEmpty).length;
    var nextFound = false;
    return _card(
      '2. Pha dung dịch  ($done/${steps.length})',
      done: steps.isNotEmpty && done == steps.length,
      hint: 'Làm lần lượt từ trên xuống, xong bước nào tick bước đó. Ly tâm sau MỖI lần pha, tránh nắng.',
      tech: 'Giờ và người làm ghi ngay lên server khi tick. Bước pha có thể tích dự kiến (C1·V1 = C2·V2); '
          'ghi thể tích thực tế nếu lệch.',
      children: [
        for (final s in steps)
          Builder(builder: (_) {
            final isDone = (s['done_at'] ?? '').toString().isNotEmpty;
            final isNext = !isDone && !nextFound;
            if (isNext) nextFound = true;
            return _stepRow(s, cs, isNext: isNext);
          }),
      ],
    );
  }

  Widget _stepRow(Map s, ColorScheme cs, {bool isNext = false}) {
    final code = (s['code'] ?? '').toString();
    final isDone = (s['done_at'] ?? '').toString().isNotEmpty;
    final dil = s['target_nM'] != null;
    final plan = dil
        ? '${_num(s['vol_dye_ul'])} µL dịch ${_num(s['from_nM'])} nM + ${_num(s['vol_buffer_ul'])} µL đệm '
            '= ${_num(s['total_ul'])} µL  (×${_num(s['factor'])})'
        : '';
    // Bước kế tiếp nổi lên (nền + viền trái màu chính) để người pha biết đang ở đâu mà không đọc hết.
    return Container(
      margin: const EdgeInsets.symmetric(vertical: 2),
      padding: isNext ? const EdgeInsets.only(left: 6) : EdgeInsets.zero,
      decoration: isNext
          ? BoxDecoration(
              color: cs.primaryContainer.withValues(alpha: 0.35),
              borderRadius: BorderRadius.circular(AppRadius.sm),
              border: Border(left: BorderSide(color: cs.primary, width: 4)),
            )
          : null,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          CheckboxListTile(
            value: isDone,
            onChanged: _editable ? (v) => _toggleStep(code, v ?? false) : null,
            controlAffinity: ListTileControlAffinity.leading,
            contentPadding: EdgeInsets.zero,
            dense: !isNext,
            title: Text((s['name'] ?? code).toString(),
                style: TextStyle(
                  decoration: isDone ? TextDecoration.lineThrough : null,
                  color: isDone ? cs.outline : null,
                  fontWeight: isNext ? FontWeight.w700 : null,
                )),
            subtitle: isDone
                ? Text('${calibFmtTs((s['done_at'] ?? '').toString())}'
                    '${(s['by'] ?? '').toString().isEmpty ? '' : ' · ${s['by']}'}'
                    '${plan.isEmpty ? '' : '\n$plan'}')
                : (plan.isEmpty ? null : Text(plan)),
            isThreeLine: isDone && plan.isNotEmpty,
          ),
          if (dil)
            Padding(
              padding: const EdgeInsets.only(left: 48, bottom: 6),
              child: Wrap(spacing: 8, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
                _tf(_actualDye.putIfAbsent(code, TextEditingController.new), 'Thực tế dịch (µL)',
                    number: true, width: 150),
                _tf(_actualBuf.putIfAbsent(code, TextEditingController.new), 'Thực tế đệm (µL)',
                    number: true, width: 150),
                if (_editable)
                  TextButton(
                    onPressed: () => _toggleStep(code, isDone),
                    child: const Text('Lưu thể tích'),
                  ),
              ]),
            ),
        ],
      ),
    );
  }

  // Thẻ 3
  Widget _readingsCard(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final n = _tubesPerConc;
    final concs = _concs;
    final filled = _filledCount();
    return _card(
      '3. Đo ống  ($filled/${n * concs.length})',
      done: n * concs.length > 0 && filled >= n * concs.length,
      hint: 'Nối máy → đặt ống app đang chỉ vào khe → bấm ĐỌC. App tự điền số, lưu ngay và chỉ sang ống kế. '
          'Ô trống = chưa đo; bấm vào ô để đo lại ống đó.',
      tech: 'Đọc MỘT khe cho mọi ống (WI). Enter/Space hoặc nút ĐỎ trên máy cũng là ĐỌC; XANH = khe +1; '
          'TRẮNG = thoát chế độ (bấm ĐỌC là vào lại). LCD máy hiện số vừa đọc và ống đang chờ. '
          'Gõ số tay thì bấm Lưu số đo.',
      actions: [
        if (_editable)
          TextButton(onPressed: _saveReadings, child: const Text('Lưu số gõ tay')),
      ],
      children: [
        if (_editable) _readerPanel(cs),
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: Table(
            defaultColumnWidth: const FixedColumnWidth(96),
            columnWidths: const {0: FixedColumnWidth(56)},
            defaultVerticalAlignment: TableCellVerticalAlignment.middle,
            children: [
              TableRow(children: [
                const Padding(padding: EdgeInsets.all(6), child: Text('Ống', style: TextStyle(fontWeight: FontWeight.w600))),
                for (final ck in concs)
                  Padding(
                    padding: const EdgeInsets.all(6),
                    child: Text('$ck nM', textAlign: TextAlign.center, style: const TextStyle(fontWeight: FontWeight.w600)),
                  ),
              ]),
              for (var no = 1; no <= n; no++)
                TableRow(
                  decoration: no.isEven ? BoxDecoration(color: cs.surfaceContainerHighest.withValues(alpha: 0.35)) : null,
                  children: [
                    Padding(padding: const EdgeInsets.all(6), child: Text('$no', textAlign: TextAlign.center)),
                    for (final ck in concs)
                      Padding(
                        padding: const EdgeInsets.all(3),
                        child: TextField(
                          controller: _cells[ck]?[no],
                          enabled: _editable,
                          textAlign: TextAlign.center,
                          keyboardType: const TextInputType.numberWithOptions(decimal: true),
                          textInputAction: TextInputAction.next,
                          style: const TextStyle(fontFamily: 'monospace', fontSize: 13),
                          onTap: () {
                            setState(() => _target = (conc: ck, tube: no));
                            _sendLabel();
                          },
                          decoration: InputDecoration(
                            isDense: true,
                            // Ô cao ≥ 44 px để chạm được trên cảm ứng (đổi ống đang đọc bằng ngón tay).
                            contentPadding: const EdgeInsets.symmetric(horizontal: 6, vertical: 13),
                            border: const OutlineInputBorder(),
                            // Ô ĐANG CHỌN để đọc từ máy: viền đậm màu thương hiệu.
                            enabledBorder: _target?.conc == ck && _target?.tube == no
                                ? OutlineInputBorder(
                                    borderSide: BorderSide(color: AppSemantic.of(context).mark, width: 2))
                                : null,
                            filled: _target?.conc == ck && _target?.tube == no,
                            fillColor: AppSemantic.of(context).mark.withValues(alpha: 0.08),
                          ),
                        ),
                      ),
                  ],
                ),
            ],
          ),
        ),
      ],
    );
  }

  /// Panel "Đọc từ máy": chọn cổng (desktop) / hộp thoại (web) → Kết nối → ĐỌC. Enter/Space
  /// ở bất kỳ đâu trên màn (trừ lúc gõ ô nhập) = ĐỌC — xem [_onKey].
  Widget _readerPanel(ColorScheme cs) {
    final sem = AppSemantic.of(context);
    if (!serialLinkAvailable) {
      return Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text('Trình duyệt này không nối được máy (điện thoại) — gõ số tay, hoặc mở bằng Chrome/Edge trên máy tính.',
            style: TextStyle(color: cs.outline, fontSize: 12)),
      );
    }
    final on = _reader != null;
    final tg = _target;
    final slotSet = int.tryParse(_readerSlot.text.trim()) != null;
    final total = _tubesPerConc * _concs.length;
    final filled = _filledCount();
    final mobile = isMobileWidth(context);
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 14),
      decoration: BoxDecoration(
        color: sem.surfaceSunken,
        borderRadius: BorderRadius.circular(AppRadius.base),
        border: on ? Border.all(color: sem.success.withValues(alpha: 0.5)) : null,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Hàng 1: máy nối chưa + khe + nút nối/ngắt. Một câu trạng thái, không kèm giao thức.
          Wrap(
            spacing: 10,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              Chip(
                avatar: Icon(Icons.usb, size: 18, color: on ? sem.success : cs.outline),
                label: Text(on ? 'Máy đã nối' : 'Chưa nối máy'),
                side: BorderSide(color: on ? sem.success : cs.outline),
                visualDensity: VisualDensity.compact,
              ),
              SizedBox(
                width: 150,
                child: DropdownButtonFormField<int>(
                  key: ValueKey('cslot_$_slot'),
                  initialValue: _slot,
                  isDense: true,
                  decoration: InputDecoration(
                    labelText: 'Ống đặt vào khe',
                    isDense: true,
                    // Chưa ghi khe → viền đỏ nhắc: đang mặc định khe 1, có thể không phải khe đang cắm ống.
                    enabledBorder: slotSet
                        ? null
                        : OutlineInputBorder(borderSide: BorderSide(color: cs.error, width: 1.5)),
                  ),
                  items: [for (var s = 1; s <= 10; s++) DropdownMenuItem(value: s, child: Text('Khe $s'))],
                  onChanged: _saving || _reading ? null : (v) {
                    if (v != null) _setSlot(v);
                  },
                ),
              ),
              if (!on && serialLinkCanListPorts)
                SizedBox(
                  width: 150,
                  child: DropdownButtonFormField<String>(
                    key: ValueKey('cports_${_ports.join('|')}'),
                    initialValue: _port,
                    isDense: true,
                    decoration: const InputDecoration(labelText: 'Cổng USB', isDense: true),
                    items: [for (final p in _ports) DropdownMenuItem(value: p, child: Text(p))],
                    onChanged: (v) => setState(() => _port = v),
                  ),
                ),
              if (!on && serialLinkCanListPorts)
                IconButton(tooltip: 'Quét lại cổng', onPressed: _refreshPorts, icon: const Icon(Icons.refresh)),
              if (!on)
                FilledButton.icon(
                  onPressed: _connecting || (serialLinkCanListPorts && _port == null) ? null : _connect,
                  icon: _connecting
                      ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                      : const Icon(Icons.usb),
                  label: const Text('Nối máy'),
                ),
              if (on) ...[
                Text(_reader!.link.label, style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12)),
                TextButton(onPressed: _disconnect, child: const Text('Ngắt')),
              ],
            ],
          ),
          if (!slotSet)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text('Chưa chọn khe — đang dùng khe 1. Chọn đúng khe anh/chị đặt ống vào.',
                  style: TextStyle(color: cs.error, fontSize: 13)),
            ),
          if (!on)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                serialLinkCanListPorts
                    ? 'Cắm cáp USB vào máy, chọn cổng rồi bấm Nối máy.'
                    : 'Cắm cáp USB vào máy, bấm Nối máy rồi chọn cổng trong hộp thoại của trình duyệt.',
                style: TextStyle(color: cs.onSurfaceVariant, fontSize: 13),
              ),
            ),
          if (on) ...[
            const SizedBox(height: 12),
            // Việc đang chờ: câu hướng dẫn to, nồng độ + số ống in đậm — đọc được từ xa.
            if (tg != null)
              RichText(
                text: TextSpan(
                  style: TextStyle(fontSize: 17, color: cs.onSurface, height: 1.35),
                  children: [
                    const TextSpan(text: 'Đặt ống '),
                    TextSpan(
                        text: '${tg.conc} nM · số ${tg.tube}',
                        style: TextStyle(fontWeight: FontWeight.w800, color: cs.primary)),
                    const TextSpan(text: ' vào '),
                    TextSpan(text: 'khe $_slot', style: const TextStyle(fontWeight: FontWeight.w800)),
                    const TextSpan(text: ', rồi bấm ĐỌC.'),
                  ],
                ),
              )
            else
              Row(children: [
                Icon(Icons.check_circle, color: sem.success),
                const SizedBox(width: 8),
                Expanded(
                  child: Text('Đã đo đủ $filled/$total ống. Sang việc 4: Tìm bộ đạt.',
                      style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
                ),
              ]),
            const SizedBox(height: 10),
            // Nút ĐỌC: mục tiêu chạm lớn (56 px), full chiều rộng trên điện thoại.
            SizedBox(
              height: 56,
              width: mobile ? double.infinity : 320,
              child: FilledButton.icon(
                onPressed: _reading || tg == null ? null : _readOne,
                icon: _reading
                    ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2.5))
                    : const Icon(Icons.play_arrow, size: 26),
                label: Text(_reading ? 'Đang đọc…' : (tg == null ? 'Đã đọc hết' : 'ĐỌC'),
                    style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w800, letterSpacing: 0.5)),
              ),
            ),
            const SizedBox(height: 8),
            Wrap(spacing: 14, runSpacing: 4, crossAxisAlignment: WrapCrossAlignment.center, children: [
              if (_lastRead.isNotEmpty)
                Text('Vừa đọc: $_lastRead', style: TextStyle(color: cs.onSurfaceVariant, fontSize: 13)),
              Text('Hoặc Enter · hoặc nút ĐỎ trên máy', style: TextStyle(color: cs.outline, fontSize: 12)),
              if (tg != null && tg != _firstEmptyCell())
                TextButton(
                  onPressed: () => setState(() => _target = _firstEmptyCell()),
                  child: const Text('Về ô trống kế'),
                ),
            ]),
            const SizedBox(height: 8),
            ClipRRect(
              borderRadius: BorderRadius.circular(4),
              child: LinearProgressIndicator(
                value: total == 0 ? 0 : filled / total,
                minHeight: 8,
                backgroundColor: cs.surfaceContainerHighest,
                color: filled >= total && total > 0 ? sem.success : cs.primary,
              ),
            ),
            const SizedBox(height: 4),
            Text('$filled / $total ống đã đo', style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12)),
          ],
        ],
      ),
    );
  }

  // Thẻ 4
  Widget _rankCard(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final r = _rank;
    final picked = r == null ? 0 : r.suggested.where((c) => _picked.contains(c.key)).length;
    final nSets = ((doc['n_sets'] as num?) ?? 0).toInt();
    return _card(
      '4. Chọn bộ ống đạt',
      done: nSets > 0,
      hint: 'Bấm "Tìm bộ đạt": máy tính ghép mỗi bộ 4 ống (một ống mỗi nồng độ) và chỉ giữ bộ đạt chuẩn. '
          'Tick bộ muốn giữ rồi bấm Đóng gói.',
      tech: 'Mọi tổ hợp 1 ống/nồng độ → hồi quy raw = slope·nM + intercept, xếp R² rồi slope. '
          'LOD = 3,3·SD(ống 0 nM)/slope theo WI. Gợi ý bộ = tổ hợp PASS từ trên xuống, KHÔNG dùng chung ống, '
          'loại ống đã đóng bộ.',
      actions: [
        FilledButton.tonalIcon(
          onPressed: _ranking || _loading ? null : _doRank,
          icon: _ranking
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : const Icon(Icons.auto_awesome, size: 18),
          label: const Text('Tìm bộ đạt'),
        ),
      ],
      children: [
        if (r == null)
          Text('Đo xong các ống rồi bấm "Tìm bộ đạt".', style: TextStyle(color: cs.outline))
        else ...[
          if (r.missing.isNotEmpty)
            Text('Chưa đo ống ${r.missing.join(', ')} nM nên chưa ghép được bộ nào — quay lại việc 3.',
                style: TextStyle(color: cs.error)),
          Text(
            r.suggested.isEmpty
                ? 'Không có bộ nào đạt chuẩn.'
                : 'Có ${r.suggested.length} bộ đạt chuẩn (ống không trùng nhau). Tick bộ muốn đóng:',
            style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 15),
          ),
          if (r.suggested.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 6),
              child: Text(
                  r.pass == 0
                      ? 'Số đo chưa đủ tốt để ghép bộ — kiểm lại ống pha/khe đọc và đo lại, hoặc gọi kỹ sư (Chi tiết kỹ thuật bên dưới).'
                      : 'Các bộ đạt đều dùng ống đã đóng gói rồi.',
                  style: TextStyle(color: cs.onSurfaceVariant)),
            ),
          for (final c in r.suggested)
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              value: _picked.contains(c.key),
              onChanged: _editable
                  ? (v) => setState(() => v == true ? _picked.add(c.key) : _picked.remove(c.key))
                  : null,
              title: Text('Bộ #${c.rank}  —  ${_tubesPlain(c.tubes)}',
                  style: const TextStyle(fontWeight: FontWeight.w600)),
              subtitle: Text('ĐẠT · độ tuyến tính R² ${calibNum(c.r2, 6)}'
                  '${c.lod == null ? '' : ' · phát hiện tới ${calibNum(c.lod, 1)} nM'}',
                  style: TextStyle(color: sem.success)),
            ),
          if (r.suggested.isNotEmpty && _editable)
            Padding(
              padding: const EdgeInsets.only(top: 6, bottom: 10),
              child: Wrap(spacing: 10, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
                SizedBox(
                  width: 170,
                  child: TextField(
                    controller: _expiresDays,
                    keyboardType: TextInputType.number,
                    decoration: InputDecoration(
                      labelText: 'Hạn dùng (ngày)',
                      hintText: 'mặc định ${r.limits['shelf_days']}',
                      isDense: true,
                    ),
                  ),
                ),
                FilledButton.icon(
                  onPressed: picked == 0 || _saving ? null : _pack,
                  icon: const Icon(Icons.inventory_2_outlined, size: 18),
                  label: Text('Đóng gói $picked bộ'),
                ),
              ]),
            ),
          const Divider(),
          Theme(
            data: Theme.of(context).copyWith(dividerColor: Colors.transparent),
            child: ExpansionTile(
              tilePadding: EdgeInsets.zero,
              dense: true,
              title: Text('Chi tiết kỹ thuật: ${r.total} tổ hợp · ${r.pass} PASS',
                  style: TextStyle(color: cs.outline, fontSize: 13)),
              children: [
          Wrap(spacing: 14, runSpacing: 4, children: [
            Text('Ngưỡng ${r.limits['version']}: R² ≥ ${r.limits['r2_min']}'
                '${(r.limits['lod_max'] ?? 0) == 0 ? '' : ', LOD ≤ ${r.limits['lod_max']} nM'}'),
            Text('Blank (0 nM): n=${r.blank['n']} · mean ${_num(r.blank['mean'])} · SD ${_num(r.blank['sd'])}'
                ' · SNR3,3 ${_num(r.blank['snr33'])}'),
            if (r.tubesInSets.isNotEmpty)
              Text('Ống đã đóng bộ: ${r.tubesInSets.join(', ')}', style: TextStyle(color: cs.outline)),
          ]),
          const SizedBox(height: 6),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: DataTable(
              headingRowHeight: 34,
              dataRowMinHeight: 30,
              dataRowMaxHeight: 34,
              columnSpacing: 18,
              columns: const [
                DataColumn(label: Text('#')),
                DataColumn(label: Text('Ống (nM/số)')),
                DataColumn(label: Text('Slope'), numeric: true),
                DataColumn(label: Text('Intercept'), numeric: true),
                DataColumn(label: Text('R²'), numeric: true),
                // "Lệch" = sai số dư (đơn vị raw). Có vì R² với 4 điểm bão hoà
                // ở 0,9999xx — cả chục dòng đầu bảng in ra y hệt nhau, nhìn
                // tưởng máy tính sai (chủ dự án báo 2026-09-23). Số này KHÔNG
                // bão hoà nên so được hai tổ hợp mà R² trông như nhau.
                DataColumn(label: Text('Lệch'), numeric: true),
                DataColumn(label: Text('LOD nM'), numeric: true),
                DataColumn(label: Text('KQ')),
              ],
              rows: [
                for (final c in r.combos)
                  DataRow(cells: [
                    DataCell(Text('${c.rank}')),
                    DataCell(Text(calibTubesLabel(c.tubes), style: const TextStyle(fontFamily: 'monospace'))),
                    DataCell(Text(calibNum(c.slope))),
                    DataCell(Text(calibNum(c.intercept, 1))),
                    DataCell(Text(calibNum(c.r2, 6))),
                    DataCell(Text(c.se == null ? '—' : calibNum(c.se, 2))),
                    DataCell(Text(c.lod == null ? '—' : calibNum(c.lod, 1))),
                    DataCell(Text(c.status,
                        style: TextStyle(
                          fontWeight: FontWeight.w700,
                          color: c.status == 'PASS' ? sem.success : cs.error,
                        ))),
                  ]),
              ],
            ),
          ),
              ],
            ),
          ),
        ],
      ],
    );
  }

  /// "300/3 · 200/5" → "ống 300 nM số 3, 200 nM số 5, …" — đọc được không cần biết ký hiệu.
  static String _tubesPlain(Map<String, String> tubes) {
    final ks = tubes.keys.toList()
      ..sort((a, b) => (double.tryParse(b) ?? 0).compareTo(double.tryParse(a) ?? 0));
    return ks.map((k) => '$k nM số ${tubes[k]}').join(', ');
  }

  // Thẻ 5
  Widget _setsCard(Map<String, dynamic> doc) {
    final cs = Theme.of(context).colorScheme;
    final sets = [
      for (final s in (doc['sets'] as List?) ?? const [])
        if (s is Map) CalibSet.fromJson(Map<String, dynamic>.from(s))
    ];
    return _card(
      '5. Bộ ống đã đóng  (${sets.length})',
      hint: sets.isEmpty ? null : 'Ghi mã bộ lên nhãn túi zip. Cấp máy / thu hồi ở từng bộ.',
      tech: sets.isEmpty ? null : 'Ngưỡng lúc đóng: ${doc['limits_ver']}.',
      children: [
        if (sets.isEmpty) Text('Chưa đóng bộ nào — làm việc 4 trước.', style: TextStyle(color: cs.outline)),
        for (final s in sets)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: CalibSetTile(set: s, api: _api, onChanged: _load),
          ),
      ],
    );
  }
}
