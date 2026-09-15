/// Mục **Tiêu chuẩn** (tab Sản xuất): admin đặt **bộ ngưỡng cho từng lô sản
/// xuất**; trạm chấm máy theo đúng bộ của lô đang chạy.
///
/// Vì sao theo lô chứ không một bộ chung: lô dùng linh kiện quang khác, hoặc
/// chạy ở xưởng có nhiệt phòng khác, thì ngưỡng khác. Ép tất cả về một bộ là
/// hoặc chấm oan lô này, hoặc thả lỏng lô kia — và không ai truy được về sau
/// rằng máy tháng trước đã bị chấm theo con số nào.
///
/// Ba luật của màn này:
/// 1. **Ô trống = CHƯA CHỐT NGƯỠNG**, không phải 0. Bước đo tương ứng sẽ ghi số
///    vào hồ sơ với kết luận `info` thay vì tự nhận là ĐẠT.
/// 2. **Một `version` = một nội dung.** Sửa ngưỡng thì đổi version; server trả
///    400 nếu dùng lại version cũ cho nội dung khác (hồ sơ chỉ ghi `limits_ver`).
/// 3. **Chỉ nhân sự kỹ thuật sửa được** (`canEditLimits`). Quản lý sản xuất và
///    thao tác viên xem để biết lô đang chấm theo bộ nào.
///
/// Chỉ HTTP → chạy cả trên bản web.
library;

import 'dart:convert';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../models/ate_record.dart';
import '../services/app_settings.dart';
import '../services/ate_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../util/platform_files.dart' as pf;

/// Một ô trong biểu mẫu: khoá JSON + nhãn + đơn vị + kiểu.
class _Field {
  final String key;
  final String label;
  final String unit;
  final bool integer;

  /// Ô này để trống được không (trống = chưa chốt ngưỡng).
  final bool nullable;
  const _Field(this.key, this.label, {this.unit = '', this.integer = false, this.nullable = true});
}

const _optoFields = [
  _Field('bright_min', 'Tín hiệu sáng tối thiểu'),
  _Field('bright_max', 'Tín hiệu sáng tối đa'),
  _Field('bright_spread_pct', 'Lệch tối đa giữa 10 kênh', unit: '%'),
];

const _tempFields = [
  _Field('ambient_c', 'Nhiệt phòng xưởng', unit: '°C'),
  _Field('temp_tol_c', 'Lệch cho phép so với nhiệt phòng', unit: '°C'),
  _Field('temp_spread_c', 'Lệch cho phép giữa 6 kênh', unit: '°C'),
  _Field('temp_window_sec', 'Thời gian lấy mẫu nhiệt', unit: 'giây', integer: true, nullable: false),
  _Field('fan_wait_sec', 'Chờ quạt trước khi hỏi', unit: 'giây', integer: true, nullable: false),
];

const _flashFields = [
  _Field('sn_max_len', 'Số máy tối đa', unit: 'ký tự', integer: true, nullable: false),
  _Field('boot_watch_sec', 'Nghe log boot', unit: 'giây', integer: true, nullable: false),
  _Field('ack_timeout_sec', 'Chờ máy trả lời lệnh', unit: 'giây', integer: true, nullable: false),
  _Field('serial_baud', 'Baud đọc UART', integer: true, nullable: false),
];

class AteLimitsScreen extends StatefulWidget {
  final AppSettings settings;

  /// Client thay thế — CHỈ dùng cho test + chụp ảnh tài liệu (bỏ trống = thật).
  final AteApi? api;

  const AteLimitsScreen({super.key, required this.settings, this.api});

  @override
  State<AteLimitsScreen> createState() => _AteLimitsScreenState();
}

class _AteLimitsScreenState extends State<AteLimitsScreen> {
  late final AteApi _api = widget.api ?? AteApi.of(widget.settings);

  final _newBatch = TextEditingController();
  final _version = TextEditingController();
  final _note = TextEditingController();
  final _flashSize = TextEditingController();
  final Map<String, TextEditingController> _num = {};
  bool _verify = true;

  List<({String batch, String version, String updatedAt, String updatedBy})> _batches = const [];
  String _batch = ''; // '' = bộ CHUNG
  String _source = '';
  String _updated = '';
  /// Khoá lạ đọc được từ file nhập vào (không có ô trên biểu mẫu). GIỮ lại và
  /// gửi kèm khi lưu — app cũ gặp tiêu chuẩn của app mới thì không được âm thầm
  /// làm mất một ngưỡng chỉ vì chưa biết hiển thị nó.
  Map<String, dynamic> _extra = const {};

  bool _loading = false;
  bool _saving = false;

  /// Đang nạp hoặc đang ghi → khoá mọi nút đọc/ghi biểu mẫu. Bấm Xuất/Lưu giữa
  /// lúc `_select` chưa xong là thao tác trên nội dung của bộ TRƯỚC — đã gặp
  /// thật: đổi lô rồi bấm Xuất ngay, file ra mang version của bộ chung.
  bool get _busy => _loading || _saving;
  String? _error;
  String? _ok;

  @override
  void initState() {
    super.initState();
    for (final f in [..._optoFields, ..._tempFields, ..._flashFields]) {
      _num[f.key] = TextEditingController();
    }
    _reload();
  }

  @override
  void dispose() {
    _newBatch.dispose();
    _version.dispose();
    _note.dispose();
    _flashSize.dispose();
    for (final c in _num.values) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> _reload() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final list = await _api.listLimits();
      if (!mounted) return;
      setState(() => _batches = list);
      await _select(_batch);
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  /// Nạp bộ ngưỡng ĐANG ÁP DỤNG cho [batch] vào biểu mẫu.
  Future<void> _select(String batch) async {
    setState(() {
      _batch = batch;
      _loading = true;
      _error = null;
      _ok = null;
    });
    try {
      final l = await _api.limits(batch: batch);
      if (!mounted) return;
      final raw = l.raw;
      setState(() {
        _source = l.source;
        _updated = [
          if ((raw['updated_at'] ?? '').toString().isNotEmpty)
            (raw['updated_at']).toString().replaceFirst('T', ' ').split('.').first,
          if ((raw['updated_by'] ?? '').toString().isNotEmpty)
            '· ${raw['updated_by']}',
        ].join(' ');
      });
      // Bộ đang dùng đến từ lô khác/mặc định → gợi ý version mới cho lô này,
      // đừng để admin lưu đè version của bộ chung.
      _applyDoc(raw,
          version: l.source == 'batch'
              ? l.version
              : (batch.isEmpty ? l.version : '$batch-1'));
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  /// Đổ một bộ ngưỡng (từ server hoặc từ file) vào biểu mẫu.
  void _applyDoc(Map<String, dynamic> raw, {String? version}) {
    setState(() {
      _version.text = (version ?? raw['version'] ?? '').toString();
      _note.text = (raw['note'] ?? '').toString();
      _flashSize.text = (raw['flash_size_expect'] ?? '').toString();
      _verify = raw['require_flash_verify'] != false;
      for (final e in _num.entries) {
        final v = raw[e.key];
        e.value.text = v == null ? '' : '$v';
      }
      _extra = {
        for (final kv in raw.entries)
          if (!kAteLimitKeys.contains(kv.key) &&
              !kAteLimitServerKeys.contains(kv.key))
            kv.key: kv.value
      };
    });
  }

  /// Bộ ngưỡng ĐANG Ở BIỂU MẪU, dạng JSON gửi server / ghi ra file.
  /// Trả null (kèm lỗi hiện trên màn) nếu có ô sai.
  Map<String, dynamic>? _formBody() {
    final version = _version.text.trim();
    if (version.isEmpty) {
      setState(() => _error = tr('ate.limitsNeedVersion'));
      return null;
    }
    final body = <String, dynamic>{
      ..._extra, // khoá lạ nhập từ file: giữ nguyên, đừng làm mất
      'version': version,
      if (_note.text.trim().isNotEmpty) 'note': _note.text.trim(),
      if (_flashSize.text.trim().isNotEmpty)
        'flash_size_expect': _flashSize.text.trim(),
      'require_flash_verify': _verify,
    };
    for (final f in [..._optoFields, ..._tempFields, ..._flashFields]) {
      final t = _num[f.key]!.text.trim().replaceAll(',', '.');
      if (t.isEmpty) continue; // trống = CHƯA CHỐT → không gửi khoá
      final v = f.integer ? int.tryParse(t) : num.tryParse(t);
      if (v == null) {
        setState(() =>
            _error = tr('ate.limitsBadNumber').replaceFirst('{f}', f.label));
        return null;
      }
      body[f.key] = v;
    }
    return body;
  }

  // ------------------------------------------------------- nhập / xuất file

  /// Nhập tiêu chuẩn từ file JSON.
  ///
  /// Một bộ → đổ vào biểu mẫu để admin KIỂM rồi mới bấm Lưu (không tự ghi:
  /// một `version` = một nội dung, ghi mù thì dễ đè nhầm). Nhiều lô trong một
  /// file → hỏi xác nhận rồi ghi từng lô, báo kết quả từng dòng.
  Future<void> _importJson() async {
    if (!SessionStore.canEditLimits) return;
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'JSON', extensions: ['json'])
    ]);
    if (f == null) return;
    Object? json;
    try {
      json = jsonDecode(await f.readAsString());
    } catch (e) {
      setState(() => _error = tr('ate.limitsImportErr').replaceFirst('{e}', '$e'));
      return;
    }
    final res = parseAteLimitsImport(json);
    if (!res.ok) {
      setState(() => _error =
          tr('ate.limitsImportErr').replaceFirst('{e}', res.error ?? '?'));
      return;
    }
    if (!mounted) return;
    if (res.isMulti) {
      await _importMany(res);
      return;
    }
    final entry = res.sets.entries.first;
    setState(() {
      _error = null;
      if (entry.key.isNotEmpty) _batch = entry.key; // file tự khai lô
    });
    _applyDoc(entry.value);
    setState(() => _ok = tr('ate.limitsImported')
        .replaceFirst('{n}', f.name)
        .replaceFirst('{b}', _batch.isEmpty ? tr('ate.limitsCommon') : _batch));
  }

  /// Nhiều lô trong một file: xem trước rồi ghi thẳng lên server từng lô.
  Future<void> _importMany(AteLimitsImport res) async {
    final lines = [
      for (final e in res.sets.entries)
        '• ${e.key.isEmpty ? tr('ate.limitsCommon') : e.key} → '
            '${e.value['version']}'
    ].join('\n');
    final go = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('ate.limitsImportMany')
            .replaceFirst('{n}', '${res.sets.length}')),
        content: SingleChildScrollView(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(lines, style: const TextStyle(fontFamily: 'JetBrains Mono')),
            if (res.unknownKeys.isNotEmpty) ...[
              const SizedBox(height: 10),
              Text(
                tr('ate.limitsUnknownKeys')
                    .replaceFirst('{k}', res.unknownKeys.join(', ')),
                style: TextStyle(
                    fontSize: 12.5,
                    color: Theme.of(c).colorScheme.onSurfaceVariant),
              ),
            ],
          ]),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: Text(tr('common.cancel'))),
          FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: Text(tr('ate.limitsImportManyDo'))),
        ],
      ),
    );
    if (go != true) return;
    setState(() {
      _saving = true;
      _error = null;
      _ok = null;
    });
    final okList = <String>[];
    final errList = <String>[];
    for (final e in res.sets.entries) {
      final name = e.key.isEmpty ? tr('ate.limitsCommon') : e.key;
      try {
        await _api.putLimits(e.value,
            batch: e.key, by: SessionStore.current?.username ?? '');
        okList.add(name);
      } catch (err) {
        // Một lô hỏng (thường là trùng version) KHÔNG được chặn các lô còn lại.
        errList.add('$name: $err');
      }
    }
    if (!mounted) return;
    setState(() {
      _saving = false;
      _ok = okList.isEmpty
          ? null
          : tr('ate.limitsImportedMany')
              .replaceFirst('{n}', '${okList.length}')
              .replaceFirst('{b}', okList.join(', '));
      _error = errList.isEmpty ? null : errList.join('\n');
    });
    await _reload();
  }

  /// Xuất bộ đang ở biểu mẫu ra file JSON (nhập lại được y nguyên).
  Future<void> _exportJson() async {
    final body = _formBody();
    if (body == null) return;
    final doc = {if (_batch.isNotEmpty) 'batch': _batch, ...body};
    final name = 'ate-limits-'
        '${_batch.isEmpty ? 'chung' : _batch}-${body['version']}.json'
        .replaceAll(RegExp(r'[^A-Za-z0-9_.\-]'), '_');
    try {
      final path = await pf.saveTextFileDialog(
          name, const JsonEncoder.withIndent('  ').convert(doc));
      if (!mounted || path == null) return;
      setState(() => _ok = tr('ate.limitsExported')
          .replaceFirst('{p}', path.isEmpty ? name : path));
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    }
  }

  Future<void> _save() async {
    if (!SessionStore.canEditLimits) return;
    final body = _formBody();
    if (body == null) return;
    final version = body['version'];
    setState(() {
      _saving = true;
      _error = null;
      _ok = null;
    });
    try {
      await _api.putLimits(body,
          batch: _batch, by: SessionStore.current?.username ?? '');
      if (!mounted) return;
      setState(() => _ok = tr('ate.limitsSaved').replaceFirst('{v}', version));
      await _reload();
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  // ------------------------------------------------------------------ build

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final canEdit = SessionStore.canEditLimits;
    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        _batchBar(cs, canEdit),
        const SizedBox(height: 12),
        if (_loading) const LinearProgressIndicator(),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        if (_ok != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: Text(_ok!, style: TextStyle(color: AppSemantic.of(context).success)),
          ),
        _headCard(cs, canEdit),
        const SizedBox(height: 12),
        _group(tr('ate.limitsOpto'), tr('ate.limitsOptoHint'), _optoFields, canEdit),
        const SizedBox(height: 12),
        _group(tr('ate.limitsTemp'), tr('ate.limitsTempHint'), _tempFields, canEdit),
        const SizedBox(height: 12),
        _group(tr('ate.limitsFlash'), '', _flashFields, canEdit),
        const SizedBox(height: 16),
        if (canEdit)
          Align(
            alignment: Alignment.centerRight,
            child: FilledButton.icon(
              onPressed: _busy ? null : _save,
              icon: _saving
                  ? const SizedBox(
                      width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.save_outlined),
              label: Text(tr('ate.limitsSave')),
            ),
          )
        else
          Text(tr('ate.limitsReadOnly'),
              style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
      ],
    );
  }

  Widget _batchBar(ColorScheme cs, bool canEdit) {
    return Wrap(spacing: 8, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
      ChoiceChip(
        selected: _batch.isEmpty,
        label: Text(tr('ate.limitsCommon')),
        onSelected: (_) => _select(''),
      ),
      for (final b in _batches.where((e) => e.batch.isNotEmpty))
        ChoiceChip(
          selected: _batch == b.batch,
          label: Text('${b.batch}  ·  ${b.version}'),
          onSelected: (_) => _select(b.batch),
        ),
      if (canEdit)
        SizedBox(
          width: 220,
          child: TextField(
            controller: _newBatch,
            decoration: InputDecoration(
              labelText: tr('ate.limitsNewBatch'),
              isDense: true,
              border: const OutlineInputBorder(),
              suffixIcon: IconButton(
                icon: const Icon(Icons.add),
                onPressed: () {
                  final b = _newBatch.text.trim();
                  if (b.isEmpty) return;
                  _newBatch.clear();
                  _select(b);
                },
              ),
            ),
            onSubmitted: (v) {
              if (v.trim().isEmpty) return;
              _newBatch.clear();
              _select(v.trim());
            },
          ),
        ),
      IconButton(
        tooltip: tr('history.jsonRefresh'),
        onPressed: _loading ? null : _reload,
        icon: const Icon(Icons.refresh),
      ),
      // Nhập/xuất JSON: khai một đợt sản xuất bằng file thay vì gõ tay 14 ô cho
      // từng lô — và để R&D gửi tiêu chuẩn sang dưới dạng file.
      if (canEdit)
        OutlinedButton.icon(
          onPressed: _busy ? null : _importJson,
          icon: const Icon(Icons.upload_file_outlined, size: 18),
          label: Text(tr('ate.limitsImport')),
        ),
      OutlinedButton.icon(
        onPressed: _busy ? null : _exportJson,
        icon: const Icon(Icons.download_outlined, size: 18),
        label: Text(tr('ate.limitsExport')),
      ),
      if (_extra.isNotEmpty)
        Tooltip(
          message: _extra.keys.join(', '),
          child: Chip(
            avatar: const Icon(Icons.help_outline, size: 16),
            label: Text(tr('ate.limitsExtraKept')
                .replaceFirst('{n}', '${_extra.length}')),
          ),
        ),
    ]);
  }

  Widget _headCard(ColorScheme cs, bool canEdit) {
    final sem = AppSemantic.of(context);
    // Lô đang chọn CHƯA có bộ riêng → nói thẳng nó đang ăn theo bộ nào.
    final inherited = _batch.isNotEmpty && _source != 'batch';
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Icon(inherited ? Icons.info_outline : Icons.rule, color: inherited ? sem.warning : cs.primary),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              _batch.isEmpty
                  ? tr('ate.limitsCommon')
                  : tr('ate.limitsForBatch').replaceFirst('{b}', _batch),
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
          ),
          if (_updated.isNotEmpty)
            Text(_updated,
                style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
        ]),
        if (inherited) ...[
          const SizedBox(height: 6),
          Text(
            tr('ate.limitsInherited').replaceFirst('{s}', _source),
            style: TextStyle(fontSize: 12.5, color: sem.warning),
          ),
        ],
        const SizedBox(height: 12),
        Row(children: [
          Expanded(
            child: TextField(
              controller: _version,
              enabled: canEdit,
              decoration: InputDecoration(
                labelText: tr('ate.limitsVersion'),
                helperText: tr('ate.limitsVersionHint'),
                helperMaxLines: 3,
                border: const OutlineInputBorder(),
                isDense: true,
              ),
              style: const TextStyle(fontFamily: 'JetBrains Mono'),
            ),
          ),
          const SizedBox(width: 12),
          SizedBox(
            width: 150,
            child: TextField(
              controller: _flashSize,
              enabled: canEdit,
              decoration: InputDecoration(
                labelText: tr('ate.flashSize'),
                hintText: '8MB',
                border: const OutlineInputBorder(),
                isDense: true,
              ),
            ),
          ),
        ]),
        const SizedBox(height: 10),
        TextField(
          controller: _note,
          enabled: canEdit,
          minLines: 1,
          maxLines: 3,
          decoration: InputDecoration(
            labelText: tr('ate.note'),
            border: const OutlineInputBorder(),
            isDense: true,
          ),
        ),
        const SizedBox(height: 6),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          value: _verify,
          onChanged: canEdit ? (v) => setState(() => _verify = v) : null,
          title: Text(tr('ate.limitsVerify'), style: const TextStyle(fontSize: 14)),
        ),
      ]),
    );
  }

  Widget _group(String title, String hint, List<_Field> fields, bool canEdit) {
    final cs = Theme.of(context).colorScheme;
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(title, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        if (hint.isNotEmpty) ...[
          const SizedBox(height: 2),
          Text(hint, style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
        ],
        const SizedBox(height: 12),
        Wrap(spacing: 12, runSpacing: 12, children: [
          for (final f in fields)
            SizedBox(
              width: 230,
              child: TextField(
                controller: _num[f.key],
                enabled: canEdit,
                keyboardType: const TextInputType.numberWithOptions(decimal: true),
                decoration: InputDecoration(
                  labelText: f.label,
                  suffixText: f.unit.isEmpty ? null : f.unit,
                  // Ô trống KHÔNG phải 0 — nói rõ ngay dưới ô hay bị hiểu nhầm nhất.
                  hintText: f.nullable ? tr('ate.limitsUnset') : null,
                  border: const OutlineInputBorder(),
                  isDense: true,
                ),
                style: const TextStyle(fontFamily: 'JetBrains Mono'),
              ),
            ),
        ]),
      ]),
    );
  }
}
