/// Mục **Hồ sơ máy** (tab Sản xuất): gõ/quét số máy → hồ sơ khai sinh + mọi lần
/// máy đó qua trạm, kể cả những lần FAIL và chạy lại.
///
/// Chỉ HTTP (`GET /ate/sn/{sn}`, `GET /ate/records/{id}`) nên chạy được cả trên
/// bản web. Vẫn lọc theo quyền: admin/khách chỉ tra được mã máy được cấp
/// (`UserSession.canSee`) — root/`*` thấy hết.
library;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/ate_record.dart';
import '../services/app_settings.dart';
import '../services/ate_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/format.dart';
import '../util/i18n.dart';

class AteProfileScreen extends StatefulWidget {
  final AppSettings settings;

  /// Client thay thế — CHỈ dùng cho test + chụp ảnh tài liệu (bỏ trống = thật).
  final AteApi? api;

  const AteProfileScreen({super.key, required this.settings, this.api});

  @override
  State<AteProfileScreen> createState() => _AteProfileScreenState();
}

class _AteProfileScreenState extends State<AteProfileScreen> {
  late final AteApi _api = widget.api ?? AteApi.of(widget.settings);
  final TextEditingController _sn = TextEditingController();
  bool _loading = false;
  String? _error;
  AteSnProfile? _profile;

  /// Hồ sơ MỚI NHẤT của cả xưởng, nạp ngay khi mở mục.
  ///
  /// Vì sao: bản trước mở ra chỉ có một ô tìm kiếm và một khoảng trắng — muốn
  /// xem gì cũng phải NHỚ SẴN số máy. Ở xưởng, câu hỏi thường gặp nhất lại là
  /// "mấy máy vừa chạy xong thế nào", tức là thứ không cần gõ gì cả.
  List<AteRecord> _recent = const [];
  bool _loadingRecent = false;

  @override
  void initState() {
    super.initState();
    _loadRecent();
  }

  @override
  void dispose() {
    _sn.dispose();
    super.dispose();
  }

  Future<void> _loadRecent() async {
    setState(() => _loadingRecent = true);
    try {
      final r = await _api.listRecords(limit: 15);
      if (!mounted) return;
      setState(() => _recent = r.items);
    } catch (e) {
      // Không hiện lỗi to: đây là phần phụ trợ. Ô tra cứu vẫn dùng được, và nếu
      // mạng hỏng thật thì lần bấm Tra cứu kế tiếp sẽ báo.
      if (!mounted) return;
      setState(() => _recent = const []);
    } finally {
      if (mounted) setState(() => _loadingRecent = false);
    }
  }

  void _clearSearch() {
    _sn.clear();
    setState(() {
      _profile = null;
      _error = null;
    });
  }

  Future<void> _search() async {
    final sn = _sn.text.trim();
    if (sn.isEmpty) return;
    // Phạm vi SẢN XUẤT, KHÔNG phải `canSee(ids)`: máy vừa ra khỏi chuyền
    // chưa được cấp cho khách hàng nào, lọc theo `ids` là chặn đúng người vừa
    // làm ra nó (docs/plan/tai-khoan-nha-may.md §3.2).
    if (!SessionStore.canSeeProduction) {
      setState(() {
        _profile = null;
        _error = tr('ate.noPermission').replaceFirst('{sn}', sn);
      });
      return;
    }
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final p = await _api.snProfile(sn);
      if (!mounted) return;
      setState(() {
        _profile = p;
        _loading = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _loading = false;
      });
    }
  }

  Future<void> _openRecord(AteRecord meta) async {
    try {
      final full = await _api.getRecord(meta.id);
      if (!mounted) return;
      showDialog<void>(
        context: context,
        builder: (_) => _RecordDialog(record: full),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('$e')));
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final p = _profile;
    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        // Ô tra cứu KHÔNG kéo hết bề ngang: mã máy dài 9 ký tự, một ô rộng
        // 1200px chỉ làm màn trông rỗng hơn.
        ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 620),
          child: Row(children: [
            Expanded(
              child: TextField(
                controller: _sn,
                textCapitalization: TextCapitalization.characters,
                decoration: InputDecoration(
                  labelText: tr('ate.sn'),
                  hintText: tr('ate.snSearchHint'),
                  border: const OutlineInputBorder(),
                  isDense: true,
                  suffixIcon: _profile == null && _sn.text.isEmpty
                      ? null
                      : IconButton(
                          tooltip: tr('ate.clearSearch'),
                          icon: const Icon(Icons.close, size: 18),
                          onPressed: _clearSearch,
                        ),
                ),
                onSubmitted: (_) => _search(),
              ),
            ),
            const SizedBox(width: 10),
            FilledButton.icon(
              onPressed: _loading ? null : _search,
              icon: const Icon(Icons.search),
              label: Text(tr('ate.search')),
            ),
          ]),
        ),
        if (_loading) const Padding(
          padding: EdgeInsets.all(24),
          child: Center(child: CircularProgressIndicator()),
        ),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(top: 16),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        if (p != null && !_loading) ...[
          const SizedBox(height: 14),
          if (p.records.isEmpty)
            Text(tr('ate.noRecords').replaceFirst('{sn}', p.sn),
                style: TextStyle(color: cs.onSurfaceVariant))
          else ...[
            _birthCard(p, cs),
            const SizedBox(height: 12),
            Text(tr('ate.attempts').replaceFirst('{n}', '${p.attempts}'),
                style: const TextStyle(fontWeight: FontWeight.w600)),
            const SizedBox(height: 6),
            for (final r in p.records) _attemptTile(r, cs),
          ],
        ],
        // Chưa tra cứu gì → hiện HỒ SƠ GẦN ĐÂY thay vì một màn trắng.
        if (p == null && !_loading) ...[
          const SizedBox(height: 18),
          Row(children: [
            Text(tr('ate.recent'),
                style: const TextStyle(fontWeight: FontWeight.w600)),
            const SizedBox(width: 8),
            if (_loadingRecent)
              const SizedBox(
                  width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2))
            else
              IconButton(
                tooltip: tr('history.jsonRefresh'),
                iconSize: 18,
                onPressed: _loadRecent,
                icon: const Icon(Icons.refresh),
              ),
          ]),
          const SizedBox(height: 6),
          if (_recent.isEmpty && !_loadingRecent)
            _emptyState(cs)
          else
            for (final r in _recent) _attemptTile(r, cs, showSn: true),
        ],
      ],
    );
  }

  /// Kho hồ sơ còn trống (xưởng chưa chạy máy nào, hoặc server chưa deploy).
  Widget _emptyState(ColorScheme cs) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 32),
        child: Column(children: [
          Icon(Icons.inventory_2_outlined, size: 40, color: cs.outline),
          const SizedBox(height: 10),
          Text(tr('ate.recentEmpty'),
              textAlign: TextAlign.center,
              style: TextStyle(color: cs.onSurfaceVariant)),
        ]),
      );

  Widget _birthCard(AteSnProfile p, ColorScheme cs) {
    final sem = AppSemantic.of(context);
    final b = p.birth;
    return AppCard(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Icon(b == null ? Icons.help_outline : Icons.verified,
              color: b == null ? sem.warning : sem.success),
          const SizedBox(width: 10),
          Text(p.sn,
              style: const TextStyle(
                  fontFamily: 'JetBrains Mono',
                  fontSize: 20,
                  fontWeight: FontWeight.w700)),
        ]),
        const SizedBox(height: 10),
        if (b == null)
          Text(tr('ate.noBirth'), style: TextStyle(color: sem.warning))
        else ...[
          _kv(tr('ate.birthAt'), formatDateTime(b.startedAt)),
          _kv(tr('ate.station'), b.station),
          _kv(tr('ate.operator'), b.operator),
          _kv(tr('ate.fw'), b.fwVersion.isEmpty ? '—' : 'v${b.fwVersion}'),
          _kv('sha256', b.fwSha256.isEmpty ? '—' : b.fwSha256),
          _kv('MAC', b.mac.isEmpty ? '—' : b.mac),
          _kv(tr('ate.limits'), b.limitsVer),
        ],
      ]),
    );
  }

  Widget _kv(String k, String v) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          SizedBox(
            width: 130,
            child: Text(k,
                style: TextStyle(
                    fontSize: 12.5,
                    color: Theme.of(context).colorScheme.onSurfaceVariant)),
          ),
          Expanded(
            child: SelectableText(v.isEmpty ? '—' : v,
                style: const TextStyle(
                    fontFamily: 'JetBrains Mono', fontSize: 12.5)),
          ),
        ]),
      );

  Widget _attemptTile(AteRecord r, ColorScheme cs, {bool showSn = false}) {
    final sem = AppSemantic.of(context);
    final color = r.verdict == AteVerdict.pass
        ? sem.success
        : r.verdict == AteVerdict.fail
            ? cs.error
            : sem.warning;
    return Card(
      margin: const EdgeInsets.only(bottom: 6),
      child: ListTile(
        leading: Icon(
            r.verdict == AteVerdict.pass ? Icons.check_circle : Icons.cancel,
            color: color),
        title: Text(
          '${showSn ? '${r.sn}  ·  ' : ''}'
          '${formatDateTime(r.startedAt)} · ${r.verdict.name.toUpperCase()}'
          '${r.failCode.isEmpty ? '' : ' @ ${r.failCode}'}',
          style: const TextStyle(fontSize: 13.5, fontWeight: FontWeight.w600),
        ),
        subtitle: Text(
          [
            if (r.batch.isNotEmpty) '${tr('ate.batch')} ${r.batch}',
            if (r.station.isNotEmpty) r.station,
            if (r.operator.isNotEmpty) r.operator,
            if (r.fwVersion.isNotEmpty) 'v${r.fwVersion}',
            '${tr('ate.limits')} ${r.limitsVer}',
          ].join(' · '),
          style: const TextStyle(fontSize: 12),
        ),
        trailing: const Icon(Icons.chevron_right),
        onTap: () => _openRecord(r),
      ),
    );
  }
}

/// Chi tiết một hồ sơ: từng bước + log thô của bước đó.
class _RecordDialog extends StatelessWidget {
  final AteRecord record;
  const _RecordDialog({required this.record});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    return AlertDialog(
      title: Text('${record.sn} · ${record.verdict.name.toUpperCase()}'),
      content: SizedBox(
        width: 720,
        child: ListView(shrinkWrap: true, children: [
          Text(
            '${formatDateTime(record.startedAt)} → ${formatDateTime(record.finishedAt)}'
            ' · ${tr('ate.limits')} ${record.limitsVer}',
            style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant),
          ),
          if (record.note.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(record.note, style: const TextStyle(fontSize: 13)),
            ),
          const SizedBox(height: 10),
          for (final s in record.steps)
            ExpansionTile(
              dense: true,
              leading: Icon(
                s.verdict == AteVerdict.pass
                    ? Icons.check_circle
                    : s.verdict == AteVerdict.fail
                        ? Icons.cancel
                        : Icons.remove_circle_outline,
                color: s.verdict == AteVerdict.pass
                    ? sem.success
                    : s.verdict == AteVerdict.fail
                        ? cs.error
                        : cs.onSurfaceVariant,
              ),
              title: Text('${s.code} · ${s.name}',
                  style: const TextStyle(fontSize: 13.5)),
              subtitle: Text(s.detail, style: const TextStyle(fontSize: 12)),
              children: [
                if (s.raw.isNotEmpty)
                  Container(
                    width: double.infinity,
                    color: sem.consoleBg,
                    padding: const EdgeInsets.all(10),
                    child: SelectableText(
                      s.raw,
                      style: TextStyle(
                          fontFamily: 'JetBrains Mono',
                          fontSize: 11.5,
                          color: sem.consoleFg),
                    ),
                  ),
              ],
            ),
        ]),
      ),
      actions: [
        TextButton.icon(
          onPressed: () => Clipboard.setData(ClipboardData(
              text: record.steps
                  .map((s) => '${s.code} ${s.verdict.name}: ${s.detail}\n${s.raw}')
                  .join('\n\n'))),
          icon: const Icon(Icons.copy_all_outlined),
          label: Text(tr('common.copy')),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context),
          child: Text(tr('common.close')),
        ),
      ],
    );
  }
}
