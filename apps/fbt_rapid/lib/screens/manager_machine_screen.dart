import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart';
import '../models/test_result.dart';
import '../services/firmware_history.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../widgets/app_search_box.dart';
import '../widgets/app_tab_scaffold.dart';

/// Tab **Quản lý máy** (nhân sự) — mẫu segmented giống tab Kỹ Thuật, gộp 2 mục:
/// **Cập nhật OTA** | **Trạng thái máy**.
///
/// Cả 2 mục chỉ nói chuyện HTTP với **Engineer Server** (không `dart:io`) nên
/// chạy được cả trên web.
///
/// ⚠️ Giới hạn CÓ THẬT của "kích hoạt update": server chỉ ĐÁNH DẤU bản firmware
/// mục tiêu (`PUT /ota/target/{file}`), KHÔNG đẩy được xuống máy — thiết bị nằm
/// sau NAT, không có cổng vào. Máy phải TỰ gọi `GET /ota/check` rồi tải về và
/// nạp. Firmware hiện chưa làm việc đó (repo `FBT-DXD` riêng) → chọn bản ở đây
/// là **xếp hàng chờ**, máy sẽ nhận khi firmware có phần kiểm tra OTA.
class ManagerMachineScreen extends StatefulWidget {
  final AppSettings settings;
  const ManagerMachineScreen({super.key, required this.settings});

  @override
  State<ManagerMachineScreen> createState() => _ManagerMachineScreenState();
}

class _ManagerMachineScreenState extends State<ManagerMachineScreen> {
  int _seg = 0; // 0 = cập nhật OTA, 1 = trạng thái máy

  @override
  Widget build(BuildContext context) {
    return AppTabScaffold(
      title: tr('nav.manager'),
      // Mỗi mục nói một chuyện khác nhau; dùng chung một câu là mất thông tin.
      subtitle: _seg == 0 ? tr('mm.shellHint') : tr('mm.statusHint'),
      index: _seg,
      onChanged: (i) => setState(() => _seg = i),
      tabs: [
        AppTab(
          icon: Icons.system_update_alt_outlined,
          label: tr('mm.ota'),
          page: _OtaTab(settings: widget.settings),
        ),
        AppTab(
          icon: Icons.monitor_heart_outlined,
          label: tr('mm.status'),
          page: _StatusTab(settings: widget.settings),
        ),
      ],
    );
  }
}

// ---------------------------------------------------------------------------
// Mục 1: Cập nhật OTA
// ---------------------------------------------------------------------------


class _OtaTab extends StatefulWidget {
  final AppSettings settings;
  const _OtaTab({required this.settings});

  @override
  State<_OtaTab> createState() => _OtaTabState();
}

class _OtaTabState extends State<_OtaTab> {
  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  OtaState _state = const OtaState();
  String _q = ''; // lọc theo tên file, chỉ trên danh sách đã tải

  /// Kho `.bin` sau khi lọc + XẾP. Tên file MANG version nên gõ "2.4.5" là ra đúng bản cần.
  ///
  /// Thứ tự: **bản ĐANG CHỌN lên đầu**, phần còn lại **mới nhất trước theo ngày tải lên**.
  /// Bản đang chọn là thứ đang được đẩy cho cả fleet — bắt người ta cuộn đi tìm nó giữa một
  /// danh sách dài là chỗ dễ bấm nhầm sang bản khác nhất. Còn ngày tải lên giảm dần vì bản
  /// vừa up gần như luôn là bản người ta đang định làm gì với nó.
  List<OtaFile> get _files {
    final out = _q.trim().isEmpty
        ? [..._state.files] // COPY: `_state.files` là của model, không sort tại chỗ
        : _state.files.where((f) => matchesQuery(_q, [f.name])).toList();
    out.sort((a, b) {
      final at = a.name == _state.target, bt = b.name == _state.target;
      if (at != bt) return at ? -1 : 1;
      final am = a.modified, bm = b.modified;
      // Thiếu ngày (server đời cũ không trả `modified`) → xuống cuối, KHÔNG coi là cũ nhất
      // hay mới nhất: xếp nó lẫn vào giữa là nói dối về thứ tự.
      if (am == null || bm == null) {
        if (am != bm) return am == null ? 1 : -1;
        return a.name.compareTo(b.name);
      }
      return bm.compareTo(am);
    });
    return out;
  }
  bool _loading = false;
  String? _error;
  String? _busyName; // file đang xoá/chọn → hiện spinner ở đúng dòng đó

  /// Lý do KHÔNG được xoá file này, hoặc `null` nếu xoá được.
  ///
  /// Hai ca, và chúng khác nhau nên phải nói khác nhau: bản đang chọn cho CẢ FLEET, và bản
  /// đang ghim RIÊNG cho một số máy. Gộp thành một câu "đang được dùng" là bắt người ta đi
  /// dò xem dùng ở đâu.
  String? _khoaXoa(String name) {
    if (name == _state.target) return tr('mm.delBlockedTarget');
    final n = _state.devices.values.where((p) => p.file == name).length;
    if (n > 0) {
      return tr('mm.delBlockedPinned').replaceFirst('{n}', '$n');
    }
    return null;
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    if (_loading) return;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final s = await _api.listOta();
      if (mounted) setState(() => _state = s);
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  /// Bọc 1 thao tác ghi: hiện lỗi bằng snackbar rồi tải lại danh sách.
  Future<void> _write(Future<void> Function() action, {String? name}) async {
    setState(() => _busyName = name ?? '*');
    try {
      await action();
    } on CloudApiException catch (e) {
      _snack(e.message, error: true);
    } catch (e) {
      _snack('$e', error: true);
    } finally {
      if (mounted) setState(() => _busyName = null);
    }
    await _load();
  }

  /// Tải firmware lên: chọn file (tên gì cũng được) → HỎI TÊN PHIÊN BẢN → tên file
  /// trên server do app đặt (`fbt_v<version>.bin`).
  ///
  /// Vì sao không dùng luôn tên file gốc: server coi tên file là "bản mục tiêu" và
  /// tiến độ triển khai được suy ra bằng cách đối chiếu version TRONG TÊN FILE với
  /// version máy báo về. Tên kiểu `firmware.bin` là mất khả năng đó. Bắt đặt version
  /// ở đây thì mọi bản tải lên đều đối chiếu được, người dùng khỏi phải tự nhớ quy ước.
  Future<void> _upload() async {
    final f = await openFile(acceptedTypeGroups: const [
      XTypeGroup(label: 'Firmware', extensions: ['bin'])
    ]);
    if (f == null) return;
    if (!mounted) return;

    final version = await showDialog<String>(
      context: context,
      builder: (_) => _VersionNameDialog(
        pickedFileName: f.name,
        existing: _state.files.map((e) => e.name).toList(),
      ),
    );
    if (version == null) return; // huỷ

    final bytes = await f.readAsBytes();
    final name = otaFileNameFor(version);
    await _write(() => _api.uploadOta(name, bytes), name: name);
    if (mounted) _snack(tr('mm.uploadedAs').replaceFirst('{name}', name));
  }

  Future<void> _confirmDelete(OtaFile f) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr('mm.deleteTitle')),
        content: Text(tr('mm.deleteBody').replaceFirst('{name}', f.name)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(tr('common.cancel')),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(ctx).colorScheme.error,
            ),
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(tr('mm.delete')),
          ),
        ],
      ),
    );
    if (ok == true) await _write(() => _api.deleteOta(f.name), name: f.name);
  }

  void _snack(String msg, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(msg),
      backgroundColor: error ? kErrorSnackBg : null,
    ));
  }

  /// Chọn bản = ĐẨY CHO CẢ FLEET (server chưa có target theo từng máy) → bắt xác
  /// nhận bằng cách gõ chữ, không cho bấm nhầm 1 nút là 109 máy nạp firmware lỗi.
  Future<void> _confirmDeploy(OtaFile f) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => _ConfirmDeployDialog(file: f, api: _api),
    );
    if (ok == true) {
      await _write(
          () => _api.setOtaTarget(f.name,
              by: SessionStore.current?.username ?? ''),
          name: f.name);
    }
  }

  /// Pop-up theo dõi: bao nhiêu máy đã lên bản đang chọn.
  void _showRollout(String target) {
    showDialog(
      context: context,
      builder: (_) => _RolloutDialog(target: target, api: _api),
    );
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final canWrite = SessionStore.canWrite;

    return Column(
      // `stretch` chứ không để mặc định (`center`): dải trạng thái nay co theo nội
      // dung (nó là `Wrap`, không còn là `Row` chiếm hết bề ngang), nên `Column` mặc
      // định sẽ CĂN GIỮA cả hàng banner — ở cửa sổ rộng nó trôi vào giữa màn trong
      // khi ô tìm ngay dưới vẫn kéo hết bề ngang. Ép full width thì `spaceBetween`
      // của `Wrap` mới đẩy được cụm nút về mép phải như thiết kế.
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 8),
          // `Wrap` chứ KHÔNG `Row`: dải trạng thái ("Máy sẽ nạp: …" + Tiến độ +
          // Huỷ chọn) và cụm nút không thể cùng một dòng ở khổ điện thoại — đo
          // ở 412px thì nút "Tải firmware lên" ĐÈ LÊN chữ "Huỷ chọn".
          //
          // Con của `Row` không-flex nhận `maxWidth: infinity` nên bọc `Wrap` bên
          // trong `Row` KHÔNG bao giờ xuống dòng — cùng cái bẫy đã sửa ở
          // `AppTabScaffold`. Chính hàng này phải là `Wrap`.
          child: Wrap(
            spacing: 12,
            runSpacing: 8,
            alignment: WrapAlignment.spaceBetween,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              ConstrainedBox(
                constraints: const BoxConstraints(minWidth: 200),
                child: _targetBanner(context),
              ),
              Wrap(
                spacing: 8,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  if (canWrite)
                    FilledButton.icon(
                      onPressed: _busyName == null ? _upload : null,
                      icon: const Icon(Icons.upload_file_outlined),
                      label: Text(tr('mm.upload')),
                    ),
                  IconButton(
                    tooltip: tr('history.jsonRefresh'),
                    onPressed: _loading ? null : _load,
                    icon: const Icon(Icons.refresh),
                  ),
                ],
              ),
            ],
          ),
        ),
        if (_state.files.length > 1 || _q.isNotEmpty)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
            child: AppSearchBox(
              hint: tr('mm.searchOta'),
              count: _q.trim().isEmpty
                  ? null
                  : '${_files.length}/${_state.files.length}',
              onChanged: (v) => setState(() => _q = v),
            ),
          ),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        if (_loading) const LinearProgressIndicator(),
        Expanded(
          child: _files.isEmpty && !_loading
              ? Center(
                  child: Text(
                      // Rỗng vì LỌC khác rỗng vì CHƯA CÓ GÌ — nói nhầm là người dùng đi
                      // tải lên lại một file vốn đã nằm sẵn đó.
                      _q.trim().isEmpty ? tr('mm.otaEmpty') : tr('mm.searchEmpty'),
                      style: TextStyle(color: cs.onSurfaceVariant)))
              : ListView.builder(
                  padding: const EdgeInsets.fromLTRB(16, 4, 16, 16),
                  itemCount: _files.length,
                  itemBuilder: (_, i) => _fileRow(context, _files[i]),
                ),
        ),
      ],
    );
  }

  /// Dải trạng thái: bản nào đang được đánh dấu cho thiết bị nạp.
  Widget _targetBanner(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final target = _state.target;
    // Ai đẩy bản chung — cùng câu hỏi mà cột "Người thiết lập" trả lời cho ghim riêng.
    // Rỗng = đặt trước khi có trường này, hoặc đặt bằng curl.
    final who = _state.targetBy.trim();
    final label = target == null
        ? tr('mm.noTarget')
        : tr('mm.targetIs').replaceFirst('{name}', target) +
            (who.isEmpty ? '' : ' · $who');
    // `Wrap` chứ KHÔNG `Row`: ở khổ điện thoại hai nút "Tiến độ" + "Huỷ chọn" đứng
    // cùng hàng bóp nhãn còn **"Máy sẽ nạ…"** — đúng lúc câu duy nhất cần đọc là
    // *bản nào* đang đẩy cho fleet. Trong `Wrap`, hàng nhãn giữ bề rộng nó cần và
    // hai nút tự rơi xuống dòng dưới; cửa sổ rộng thì cả ba vẫn nằm một hàng như cũ.
    return Wrap(
      spacing: 4,
      runSpacing: 4,
      crossAxisAlignment: WrapCrossAlignment.center,
      children: [
        Row(
          // min: hàng nhãn chỉ chiếm đúng chỗ nó cần, để hai nút còn cơ hội cùng dòng.
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(target == null ? Icons.pause_circle_outline : Icons.campaign_outlined,
                size: 20, color: target == null ? cs.onSurfaceVariant : sem.success),
            const SizedBox(width: 8),
            Flexible(
              // Tooltip = nguyên văn (tên file dài bị cắt) + giờ đặt.
              child: Tooltip(
                message: _state.targetAt == null
                    ? label
                    : '$label · ${_date(_state.targetAt!)}',
                child: Text(
                  label,
                  // 2 dòng: khổ hẹp vẫn đọc được tên file + người đặt, thay vì cụt
                  // ngay giữa chữ "nạp".
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: target == null ? cs.onSurfaceVariant : sem.success,
                    fontWeight: target == null ? null : FontWeight.w600,
                  ),
                ),
              ),
            ),
          ],
        ),
        if (target != null)
          TextButton.icon(
            onPressed: () => _showRollout(target),
            icon: const Icon(Icons.donut_large_outlined, size: 18),
            label: Text(tr('mm.progress')),
          ),
        if (target != null && SessionStore.canWrite)
          TextButton(
            onPressed: _busyName == null
                ? () => _write(_api.clearOtaTarget, name: target)
                : null,
            child: Text(tr('mm.clearTarget')),
          ),
      ],
    );
  }

  Widget _fileRow(BuildContext context, OtaFile f) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final isTarget = f.name == _state.target;
    final busy = _busyName == f.name;

    // Khổ điện thoại: chip "Đang chọn" + nút xoá ăn ~160px của một hàng ~300px, phần
    // còn lại không đủ cho tên file → nó vỡ GIỮA CHỮ ("fbt_v2. / 4.4_rc1 / .bin") và
    // ngày giờ vỡ theo ("12/08/202 / 6 16:00"). Ở màn này **tên file LÀ version**
    // (server không hiểu ngữ nghĩa version, app đọc version TỪ tên) nên đọc nhầm một
    // ký tự là chọn nhầm bản cho cả fleet → hạ hành động xuống hàng riêng, trả hết bề
    // ngang cho tên. Cùng hướng bảng "Trạng thái máy" đã đổi sang thẻ dọc ở khổ này.
    final mobile = isMobileWidth(context);

    final info = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(f.name,
            style: const TextStyle(
                fontFamily: 'JetBrains Mono',
                fontWeight: FontWeight.w600)),
        const SizedBox(height: 2),
        Text(
          '${_size(f.size)}'
          '${f.modified == null ? '' : ' · ${_date(f.modified!)}'}',
          style: TextStyle(
            fontSize: 12,
            color: cs.onSurfaceVariant,
            fontFeatures: const [FontFeature.tabularFigures()],
          ),
        ),
      ],
    );

    final actions = <Widget>[
      if (busy)
        const SizedBox(
            width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
      else if (isTarget)
        Chip(
          label: Text(tr('mm.selected')),
          visualDensity: VisualDensity.compact,
          backgroundColor: sem.success.withValues(alpha: 0.12),
          side: BorderSide(color: sem.success.withValues(alpha: 0.4)),
          labelStyle: TextStyle(color: sem.success, fontSize: 12),
        )
      else if (SessionStore.canWrite)
        TextButton(
          onPressed: _busyName == null ? () => _confirmDeploy(f) : null,
          child: Text(tr('mm.select')),
        ),
      if (SessionStore.canWrite) ...[
        const SizedBox(width: 4),
        // KHÔNG cho xoá bản ĐANG ĐƯỢC DÙNG. Server thì vẫn cho (xoá `.bin` gỡ luôn ghim —
        // ngữ nghĩa cố ý, xem server/CLAUDE.md), nên đây là chặn Ở TAY NGƯỜI BẤM: một cú
        // bấm nhầm sẽ âm thầm thả những máy đang cố ý giữ ở bản cũ trôi theo bản chung, và
        // không có gì báo. Nút HIỆN NHƯNG DISABLED kèm lý do — ẩn đi thì người dùng tưởng
        // mình không có quyền xoá.
        Tooltip(
          message: _khoaXoa(f.name) ?? tr('mm.delete'),
          child: IconButton(
            tooltip: null,
            onPressed: (_busyName == null && _khoaXoa(f.name) == null)
                ? () => _confirmDelete(f)
                : null,
            icon: Icon(Icons.delete_outline,
                color: _khoaXoa(f.name) == null
                    ? cs.error
                    : cs.onSurfaceVariant.withValues(alpha: 0.5)),
          ),
        ),
      ],
    ];

    final head = Row(
      children: [
        Icon(Icons.memory_outlined,
            color: isTarget ? sem.success : cs.onSurfaceVariant),
        const SizedBox(width: 12),
        Expanded(child: info),
        if (!mobile) ...actions,
      ],
    );

    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: AppCard(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        child: mobile
            ? Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  head,
                  Row(mainAxisAlignment: MainAxisAlignment.end, children: actions),
                ],
              )
            : head,
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Mục 2: Trạng thái máy
// ---------------------------------------------------------------------------

/// Nhãn cho máy CHƯA từng báo version. Tách hẳn khỏi mọi chuỗi version thật để nó không
/// bao giờ trùng một bản nào — và để bộ lọc trả lời được câu "máy nào chưa báo gì?".
const String _kFwChuaBao = '— chưa báo —';

class _StatusTab extends StatefulWidget {
  final AppSettings settings;
  const _StatusTab({required this.settings});

  @override
  State<_StatusTab> createState() => _StatusTabState();
}

class _StatusTabState extends State<_StatusTab> {
  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  List<CloudDevice> _devices = const [];
  // Kho OTA đi kèm: cần `devices` (máy nào đang ghim bản nào) và `files` (danh sách để chọn).
  OtaState _ota = const OtaState();
  String _q = ''; // lọc theo mã máy HOẶC version
  String _fw = ''; // lọc theo firmware; rỗng = tất cả
  bool _loading = false;
  String? _error;

  /// Máy sau khi lọc. Khớp cả **mã máy lẫn version** — kho đang có 18 chuỗi version khác
  /// nhau ngoài hiện trường, nên "cho tôi xem hết máy v2.4.3" là câu hỏi thường xuyên hơn
  /// cả tìm một máy cụ thể.
  List<CloudDevice> get _shown {
    if (_q.trim().isEmpty && _fw.isEmpty) return _devices;
    return _devices
        .where((d) => _fw.isEmpty || _nhanFw(d.version) == _fw)
        .where((d) => _q.trim().isEmpty || matchesQuery(_q, [d.id, d.version]))
        .toList();
  }

  /// Nhãn firmware dùng cho bộ lọc — giữ NGUYÊN VĂN chuỗi máy báo về.
  ///
  /// ⚠️ KHÔNG chuẩn hoá, không gộp hoa/thường: kho thật có cả `V2.3.1` lẫn `v2.4.3`, và
  /// `v2.4.4AT` là bản KHÁC `v2.4.4` (hậu tố mang nghĩa — xem `isDeviceOnTarget`). Gộp lại
  /// cho gọn là nói dối về việc fleet đang chạy gì. Máy chưa báo version thì có nhãn RIÊNG
  /// chứ không lẫn vào bản nào.
  String _nhanFw(String v) =>
      v.trim().isEmpty ? _kFwChuaBao : v.trim();

  /// Nút lọc theo firmware — nằm TRONG ô tìm (khe `trailing`), không chiếm thêm hàng nào.
  ///
  /// Menu liệt kê **chính các version đang có mặt** kèm số máy, thay vì bắt gõ tay: fleet có
  /// tới 18 chuỗi version khác nhau và người vận hành không thuộc chúng. Đang lọc thì nút đổi
  /// thành chip mang tên version — nếu không, lọc xong quên mất là đang lọc rồi kết luận
  /// "sao mất máy".
  Widget _locFwBtn(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final ds = _fwDangCo;
    // Một version duy nhất (hoặc chưa có máy nào) thì không có gì để lọc.
    if (ds.length < 2 && _fw.isEmpty) return const SizedBox.shrink();

    return PopupMenuButton<String>(
      tooltip: tr('mm.fwFilter'),
      position: PopupMenuPosition.under,
      onSelected: (v) => setState(() => _fw = v),
      itemBuilder: (ctx) => [
        CheckedPopupMenuItem<String>(
          value: '',
          checked: _fw.isEmpty,
          child: Text(tr('mm.fwFilterAll').replaceFirst('{n}', '${_devices.length}')),
        ),
        const PopupMenuDivider(),
        for (final e in ds)
          CheckedPopupMenuItem<String>(
            value: e.key,
            checked: _fw == e.key,
            child: Text('${e.key}  (${e.value})',
                style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 13)),
          ),
      ],
      child: _fw.isEmpty
          ? Icon(Icons.filter_list, size: 20, color: cs.onSurfaceVariant)
          : Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
              decoration: BoxDecoration(
                color: cs.primary.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(AppRadius.pill),
                border: Border.all(color: cs.primary.withValues(alpha: 0.4)),
              ),
              child: Row(mainAxisSize: MainAxisSize.min, children: [
                Icon(Icons.filter_list, size: 15, color: cs.primary),
                const SizedBox(width: 4),
                ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 96),
                  child: Text(_fw,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                          fontSize: 11.5,
                          fontWeight: FontWeight.w600,
                          color: cs.primary)),
                ),
              ]),
            ),
    );
  }

  /// Các version đang có mặt + số máy, nhiều máy nhất lên trước.
  List<MapEntry<String, int>> get _fwDangCo {
    final dem = <String, int>{};
    for (final d in _devices) {
      dem.update(_nhanFw(d.version), (n) => n + 1, ifAbsent: () => 1);
    }
    final out = dem.entries.toList()
      ..sort((a, b) =>
          a.value != b.value ? b.value.compareTo(a.value) : a.key.compareTo(b.key));
    return out;
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    if (_loading) return;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final all = await _api.listDevices(fresh: true);
      final ota = await _api.listOta();
      // Lọc quyền ở CLIENT như các màn cloud khác (server chưa lọc theo ids).
      final session = SessionStore.current;
      final seen = all.where((d) => session?.canSee(d.id) ?? false).toList();
      if (mounted) {
        setState(() {
          _devices = seen;
          _ota = ota;
        });
      }
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Column(
      children: [
        // Gợi ý đã ở subtitle của khuôn chung. Nút làm mới đi CÙNG HÀNG với ô
        // tìm — để riêng một hàng thì nó chiếm nguyên dải ngang cho một icon.
        Padding(
          padding: const EdgeInsets.fromLTRB(4, 0, 4, 10),
          child: Row(
            children: [
              if (_devices.length > 1 || _q.isNotEmpty)
                Expanded(
                  child: AppSearchBox(
                    hint: tr('mm.searchStatus'),
                    // Đếm phải tính CẢ hai bộ lọc, không chỉ ô tìm — lọc firmware mà con số
                    // vẫn nói "95/95" là nói dối đúng lúc người ta cần con số nhất.
                    count: (_q.trim().isEmpty && _fw.isEmpty)
                        ? null
                        : '${_shown.length}/${_devices.length}',
                    onChanged: (v) => setState(() => _q = v),
                    trailing: [_locFwBtn(context)],
                  ),
                )
              else
                const Spacer(),
              const SizedBox(width: 8),
              IconButton(
                tooltip: tr('history.jsonRefresh'),
                onPressed: _loading ? null : _load,
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
        ),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Text(_error!, style: TextStyle(color: cs.error)),
          ),
        if (_loading) const LinearProgressIndicator(),
        Expanded(
          child: _shown.isEmpty && !_loading
              ? Center(
                  child: Text(
                      _q.trim().isEmpty
                          ? tr('mm.statusEmpty')
                          : tr('mm.searchEmpty'),
                      style: TextStyle(color: cs.onSurfaceVariant)))
              : Padding(
                  padding: const EdgeInsets.fromLTRB(16, 4, 16, 16),
                  child: AppCard(
                    padding: EdgeInsets.zero,
                    child: ClipRRect(
                      borderRadius: BorderRadius.circular(AppRadius.card),
                      child: Builder(builder: (ctx) {
                        // Điện thoại: 5 cột không thể vừa. Ở 785px cửa sổ đã đo
                        // được "Runs" dính vào "Last upload" (`4619/08/2026`);
                        // dưới 640px thì mọi cách chia cột đều hỏng. Đổi hẳn
                        // sang THẺ XẾP DỌC — cùng dữ liệu, đọc từ trên xuống.
                        final mobile = isMobileWidth(ctx);
                        return Column(
                          children: [
                            // Tiêu đề bảng vô nghĩa khi không còn cột.
                            if (!mobile) _headerRow(context),
                            // ListView.builder (KHÔNG DataTable): giữ tiêu đề CỐ ĐỊNH
                            // khi cuộn + chỉ dựng dòng đang hiện — kho máy đã hơn 100.
                            Expanded(
                              child: ListView.builder(
                                padding: EdgeInsets.zero,
                                itemCount: _shown.length,
                                itemBuilder: (_, i) => mobile
                                    ? _mobileCard(context, _shown[i])
                                    : _dataRow(context, _shown[i]),
                              ),
                            ),
                          ],
                        );
                      }),
                    ),
                  ),
                ),
        ),
      ],
    );
  }

  // Bảng chia cột bằng flex (không cuộn ngang):
  //   Máy | Firmware | Người thiết lập | Phiên | Lần cuối
  // Cột 2 (Trạng thái update) rộng hơn: nó mang chip chế độ + tên file .bin đầy đủ.
  static const List<int> _flex = [4, 3, 5, 2, 3];

  Widget _cell(int col, Widget child, {bool right = false}) => Expanded(
        flex: _flex[col],
        child: Align(
          alignment: right ? Alignment.centerRight : Alignment.centerLeft,
          child: child,
        ),
      );

  /// Áp lựa chọn ghim. Chuỗi RỖNG = "theo bản chung" → gỡ ghim.
  ///
  /// LUÔN báo kết quả bằng SnackBar — cả thành công lẫn thất bại. Bản đầu chỉ nhét lỗi vào
  /// `_error` (dòng đỏ ở ĐẦU tab): người dùng đang nhìn một dòng ở giữa bảng 95 máy không hề
  /// thấy nó, nên một cú bấm 401 trông y hệt một cú bấm thành công. Đúng lớp lỗi mà chính
  /// tính năng này sinh ra để tránh — im lặng thì không ai biết mình đang ở đâu.
  Future<void> _applyPin(CloudDevice d, String choice) async {
    final msg = ScaffoldMessenger.of(context);
    try {
      if (choice.isEmpty) {
        await _api.clearOtaTarget(device: d.id);
      } else {
        await _api.setOtaTarget(choice,
            device: d.id, by: SessionStore.current?.username ?? '');
      }
      await _load();
      if (!mounted) return;
      msg.showSnackBar(SnackBar(
        content: Text(choice.isEmpty
            ? tr('mm.pinCleared').replaceFirst('{id}', d.id)
            : tr('mm.pinSaved')
                .replaceFirst('{id}', d.id)
                .replaceFirst('{name}', choice)),
      ));
    } catch (e) {
      if (!mounted) return;
      final cs = Theme.of(context).colorScheme;
      setState(() => _error = '$e');
      msg.showSnackBar(SnackBar(
        backgroundColor: kErrorSnackBg,
        duration: const Duration(seconds: 6),
        content: Text(
          tr('mm.pinFailed').replaceFirst('{id}', d.id).replaceFirst('{err}', '$e'),
          style: TextStyle(color: cs.onError),
        ),
      ));
    }
  }

  /// Ô **Trạng thái update** — và đây LÀ NÚT chọn firmware cho máy.
  ///
  /// Trả lời một câu duy nhất: *máy này sẽ nhận bản nào, và đã nhận chưa?* Ba mẩu đi cùng
  /// nhau vì tách ra là vô nghĩa:
  ///  * **Bản đích thực sự** = ghim riêng nếu có, không thì bản chung. Đây là thứ máy sẽ
  ///    tải ở lượt `/ota/check` tới, không phải thứ đang chạy.
  ///  * **Đã lên chưa** (`isDeviceOnTarget`) — so version máy báo với version rút từ TÊN FILE.
  ///  * **Chung hay Riêng** — chính là lựa chọn, mở bằng cách bấm vào ô.
  ///
  /// Trước 2026-08-20 menu chọn nằm trong ô **Firmware**. Chủ dự án yêu cầu chuyển sang cột
  /// này ("cột này được bấm mới thiết lập firmware cho máy được"), và như thế đúng hơn: ô
  /// Firmware giờ chỉ nói máy đang chạy gì, còn mọi thứ thuộc về *sẽ nạp gì* nằm một chỗ.
  ///
  /// ⚠️ Cột này **thay chỗ** cột "Người thiết lập" cũ — người đặt ghim + thời điểm nay nằm
  /// trong tooltip. Giữ cả hai thì bảng thành 6 cột và bắt đầu dính chữ ở khoảng 700–900px,
  /// dải mà `kMobileMaxWidth` chưa đổi sang thẻ dọc.
  Widget _updateCell(BuildContext context, CloudDevice d) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final pin = _ota.devices[d.id];
    // Ghim THẮNG bản chung — cùng ngữ nghĩa server dùng ở `/ota/check` (server/CLAUDE.md).
    final target = pin?.file ?? _ota.target;
    final can = supportsPerDevicePin(d.version);
    final on = target == null ? null : isDeviceOnTarget(d.version, target);

    // TONE MÃ HOÁ CHẾ ĐỘ, KHÔNG mã hoá trạng thái (chủ dự án chốt 2026-08-20):
    // xanh = đi theo bản chung qua OTA thường · đỏ = bị ghim một bản riêng.
    // Lý do màu ăn theo chế độ chứ không theo "đã lên bản chưa": "chờ nạp" là trạng thái
    // BÌNH THƯỜNG và tạm thời của cả fleet trong mỗi đợt triển khai, tô cảnh báo cho nó là
    // nhuộm đỏ cả bảng rồi chẳng còn ai đọc. Còn "máy này đi đường riêng" là ngoại lệ do
    // người đặt, tồn tại vô hạn tới khi ai đó gỡ — đó mới là thứ đáng đập vào mắt.
    final rieng = pin != null;
    final tone = target == null
        ? cs.onSurfaceVariant
        : (rieng ? cs.error : sem.success);

    late final IconData icon;
    late final String nhan;
    if (target == null) {
      icon = Icons.remove;
      nhan = tr('mm.updNoTarget');
    } else if (on == null) {
      icon = Icons.help_outline;
      nhan = tr('mm.updUnknown');
    } else if (on) {
      icon = Icons.check_circle_outline;
      nhan = tr('mm.updOnTarget');
    } else {
      icon = Icons.schedule;
      nhan = tr('mm.updPending');
    }

    final who = (pin?.by ?? '').trim().isEmpty ? '?' : pin!.by.trim();
    final tip = StringBuffer(pin == null
        ? tr('mm.updTipCommon')
        : tr('mm.updTipPinned').replaceFirst('{by}', who));
    if (pin?.at != null) tip.write('\n${_date(pin!.at!)}');
    if (on == null && target != null) tip.write('\n\n${tr('mm.updTipUnknown')}');
    if (!can) tip.write('\n\n${tr('mm.pinNeeds244')}');

    // Chip mang CHẾ ĐỘ (thứ có tone màu); dòng dưới mang trạng thái + tên bản sẽ nạp.
    final chip = Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      decoration: BoxDecoration(
        color: tone.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: tone.withValues(alpha: 0.40)),
      ),
      child: Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(rieng ? Icons.push_pin : Icons.cloud_download_outlined,
            size: 13, color: tone),
        const SizedBox(width: 4),
        Text(
          rieng ? tr('mm.updModePinned') : tr('mm.updModeCommon'),
          style: TextStyle(
              fontSize: 11.5, fontWeight: FontWeight.w600, color: tone),
        ),
      ]),
    );

    final ruot = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        chip,
        const SizedBox(height: 3),
        Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(icon, size: 13, color: cs.onSurfaceVariant),
          const SizedBox(width: 4),
          Flexible(
            child: Text(
              target == null ? nhan : '$nhan · $target',
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant),
            ),
          ),
        ]),
      ],
    );

    // CẢ Ô LÀ MỘT Ô CHỌN (chủ dự án chọn phương án D, 2026-08-20). Bản trước chỉ có một mũi
    // tên 18px màu chữ mờ đứng cạnh chữ cùng cỡ cùng màu — không tín hiệu nào nói ô bấm
    // được, và người dùng báo đúng chỗ đó. Nay mượn NGUYÊN hình dạng ô nhập của app (viền
    // `outline`, bo `base`, nền thẻ) nên nó đọc ra là control theo thói quen sẵn có, không
    // phải học gì mới.
    // `double.infinity` = MỌI ô rộng bằng nhau (lấp trọn cột), thay vì co theo độ dài tên
    // file. Co theo nội dung thì mép phải của 95 hàng răng cưa mỗi hàng một kiểu, và cột
    // trông như dữ liệu rời rạc chứ không như một cột điều khiển.
    Widget hop(Widget con, {required bool batDuoc}) => Container(
          key: ValueKey('updBox-${d.id}'),
          width: double.infinity,
          padding: const EdgeInsets.fromLTRB(9, 5, 5, 5),
          decoration: BoxDecoration(
            color: batDuoc ? cs.surface : Colors.transparent,
            borderRadius: BorderRadius.circular(AppRadius.base),
            // Không bấm được thì viền NHẠT hẳn (outlineVariant = hairline trang trí) — cùng
            // hình dạng mà khác hẳn sức nặng, đọc ra ngay là "ô này trơ".
            border: Border.all(
                color: batDuoc ? cs.outline : cs.outlineVariant),
          ),
          child: con,
        );

    // Máy < v2.4.4 không gọi `/ota/check` → ghim cho nó hỏng IM LẶNG. Ô trơ + tooltip nói vì
    // sao; KHÔNG ẩn đi, ẩn thì người dùng tưởng tính năng chưa có.
    if (!can) {
      return Tooltip(
        message: tip.toString(),
        child: hop(ruot, batDuoc: false),
      );
    }

    return PopupMenuButton<String>(
      enabled: !_loading,
      tooltip: tip.toString(),
      position: PopupMenuPosition.under,
      padding: EdgeInsets.zero,
      onSelected: (v) => _applyPin(d, v),
      itemBuilder: (ctx) => _pinMenuItems(ctx, d, pin),
      child: hop(
        Row(
          children: [
            // `Expanded` chứ không `Flexible`: đẩy chevron ra sát mép PHẢI ở mọi hàng, nên
            // nó thành một cột thẳng — mắt biết chỗ bấm mà không phải dò từng dòng.
            Expanded(child: ruot),
            const SizedBox(width: 4),
            // Chevron lấy màu CHỮ CHÍNH, không phải màu chữ mờ: nó là phần "đây là control",
            // mờ đi là quay lại đúng lỗi vừa sửa.
            Icon(Icons.keyboard_arrow_down, size: 18, color: cs.onSurface),
          ],
        ),
        batDuoc: true,
      ),
    );
  }

  /// Các mục của menu chọn bản — tách ra vì cả hàng bảng lẫn thẻ điện thoại đều dùng.
  List<PopupMenuEntry<String>> _pinMenuItems(
      BuildContext ctx, CloudDevice d, OtaPin? pinned) {
    final cs = Theme.of(ctx).colorScheme;
    final sem = AppSemantic.of(ctx);
    final fw = d.version.trim(); // luôn có giá trị — xem ghi chú dưới
    return [
      // Dòng đầu không chọn được, mang HAI thứ:
      //  * **bản máy ĐANG CHẠY** — đang đứng ở đây là để chọn bản KHÁC, mà không biết nó
      //    đang chạy gì thì không biết mình đang nâng hay đang HẠ. Menu trước không nhận
      //    `CloudDevice` nên không có cách nào nói ra, và người dùng báo đúng chỗ này.
      //  * ghim KHÔNG tức thì và vẫn cần người bấm ĐỎ — thiếu câu đó người vận hành sẽ
      //    đứng chờ máy tự đổi.
      PopupMenuItem<String>(
        enabled: false,
        height: 40,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            // `fw` KHÔNG BAO GIỜ rỗng ở đây: menu này chỉ dựng khi `supportsPerDevicePin`
            // đúng, mà hàm đó trả false cho máy chưa báo version. Đừng thêm nhánh "chưa
            // biết" — nó không có đường nào tới, và test không phủ được nó (gieo lỗi vào
            // đó vẫn xanh, đúng dấu hiệu code chết).
            Text(
              tr('mm.pinRunning').replaceFirst('{v}', fw),
              style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
                  color: cs.onSurface),
            ),
            const SizedBox(height: 2),
            Text(tr('mm.pinMenuHint'),
                style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
          ],
        ),
      ),
      const PopupMenuDivider(),
      CheckedPopupMenuItem<String>(
        value: '',
        checked: pinned == null,
        child: Text(
            '${tr('mm.pinFollowTarget')} (${_ota.target ?? tr('mm.pinNoTarget')})'),
      ),
      for (final f in _ota.files)
        CheckedPopupMenuItem<String>(
          value: f.name,
          checked: pinned?.file == f.name,
          child: Row(children: [
            Flexible(
              child: Text(f.name,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontFamily: 'JetBrains Mono')),
            ),
            // Đánh dấu ĐÚNG dòng máy đang chạy — dấu tích của `CheckedPopupMenuItem` đã
            // mang nghĩa "đang GHIM" rồi, hai thứ khác nhau nên không được dùng chung ký
            // hiệu. `isDeviceOnTarget` rút version TỪ TÊN FILE nên `v2.4.5AT` không khớp
            // `fbt_v2.4.5.bin`, đúng ý: hậu tố là bản khác.
            if (isDeviceOnTarget(d.version, f.name) == true) ...[
              const SizedBox(width: 8),
              Text(tr('mm.pinIsRunning'),
                  style: TextStyle(fontSize: 11, color: sem.success)),
            ],
          ]),
        ),
    ];
  }

  /// Ô cột **Firmware** — CHỈ nói máy đang chạy bản nào. Không còn là nút.
  ///
  /// Menu chọn bản đã chuyển sang cột **Trạng thái update** (`_updateCell`) theo yêu cầu chủ
  /// dự án 2026-08-20. Trước đó ô này gánh cả hai câu hỏi (*đang chạy gì* + *sẽ nạp gì*) và
  /// cái thứ hai chỉ hiện thành một dòng nhỏ `→ tên.bin` rất dễ bỏ sót.
  Widget _firmwareCell(BuildContext context, CloudDevice d, TextStyle mono) {
    final cs = Theme.of(context).colorScheme;
    final fw = d.version.trim();

    final label = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          fw.isEmpty ? '—' : fw,
          overflow: TextOverflow.ellipsis,
          style: mono.copyWith(
            color: fw.isEmpty ? cs.onSurfaceVariant : cs.onSurface,
          ),
        ),
      ],
    );

    // Cột này KHÔNG phải "máy đang chạy gì lúc này" — nó là bản của LẦN ĐO GẦN NHẤT.
    // Nạp xong mà chưa chạy mẫu thì số này đứng yên. Gặp thật 19/08 với RPL02013: hạ tay
    // về v2.4.4 mà cột vẫn v2.4.5 suốt, vì lần đo cuối là 18/08 — người vận hành đọc ra
    // "bảng hỏng". Nói thẳng nguồn gốc ngay chỗ người ta đọc con số.
    final nguon = d.latest == null
        ? tr('mm.fwNoRun')
        : tr('mm.fwAsOf').replaceFirst('{when}', _date(d.latest!));

    return Tooltip(message: nguon, child: label);
  }

  /// Mở lịch sử cập nhật của một máy.
  void _openHistory(CloudDevice d) {
    showDialog<void>(
      context: context,
      builder: (_) => _FirmwareHistoryDialog(device: d, api: _api),
    );
  }

  /// Nút mở lịch sử — dùng chung cho hàng bảng (desktop) và thẻ (điện thoại).
  Widget _historyBtn(CloudDevice d) => IconButton(
        tooltip: tr('mm.hist'),
        icon: const Icon(Icons.history, size: 18),
        visualDensity: VisualDensity.compact,
        onPressed: () => _openHistory(d),
      );

  Widget _headerRow(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final style = TextStyle(
      fontSize: 12,
      fontWeight: FontWeight.w600,
      color: cs.onSurfaceVariant,
    );
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      decoration: BoxDecoration(
        color: AppSemantic.of(context).surfaceSunken,
        border: Border(bottom: BorderSide(color: cs.outlineVariant)),
      ),
      child: Row(
        children: [
          _cell(0, Text(tr('mm.colDevice'), style: style)),
          _cell(1, Text(tr('mm.firmware'), style: style)),
          _cell(2, Text(tr('mm.colUpdate'), style: style)),
          _cell(3, Text(tr('mm.colRuns'), style: style), right: true),
          _cell(4, Text(tr('mm.colLastSeen'), style: style), right: true),
          // Chừa đúng bề rộng nút lịch sử ở mỗi hàng, để cột không lệch.
          const SizedBox(width: 40),
        ],
      ),
    );
  }

  /// Một máy trên khổ ĐIỆN THOẠI: thẻ xếp dọc thay cho một hàng 5 cột.
  ///
  /// Giữ nguyên MỌI thứ hàng ngang có — kể cả ô Firmware bấm được để ghim bản
  /// riêng (`_firmwareCell`) và cột "người thiết lập" — chỉ đổi cách xếp. Bớt
  /// thông tin trên điện thoại là biến nó thành một bản xem-cho-vui, trong khi
  /// đây đúng là lúc người ta đứng cạnh máy và cần ghim bản.
  Widget _mobileCard(BuildContext context, CloudDevice d) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final fresh = d.latest != null &&
        DateTime.now().difference(d.latest!) < const Duration(hours: 24);
    const mono = TextStyle(
      fontFamily: 'JetBrains Mono',
      fontWeight: FontWeight.w600,
      fontFeatures: [FontFeature.tabularFigures()],
    );
    final small = TextStyle(fontSize: 12, color: cs.onSurfaceVariant);

    return Container(
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
      decoration: BoxDecoration(
        border: Border(
            bottom: BorderSide(color: cs.outlineVariant.withValues(alpha: 0.5))),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: fresh ? sem.success : cs.outline,
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child:
                    Text(d.id, overflow: TextOverflow.ellipsis, style: mono),
              ),
              Text('${d.runCount} ${tr('mm.colRuns').toLowerCase()}',
                  style: small),
              _historyBtn(d),
            ],
          ),
          const SizedBox(height: 8),
          // Hàng dưới: bản đang chạy + lần gửi cuối.
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              _firmwareCell(context, d, mono),
              const Spacer(),
              Text(d.latest == null ? '—' : _date(d.latest!), style: small),
            ],
          ),
          const SizedBox(height: 8),
          // Ô chọn firmware chiếm TRỌN bề ngang thẻ. Nhét chung hàng với hai thứ trên là ép
          // một control có viền vào ~40% màn 390px — chữ trong nó cụt ngay, và đây đúng là
          // lúc người ta đứng cạnh máy cần bấm nó.
          SizedBox(width: double.infinity, child: _updateCell(context, d)),
        ],
      ),
    );
  }

  Widget _dataRow(BuildContext context, CloudDevice d) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    // Chấm xanh chỉ là SUY ĐOÁN từ lần gửi cuối — thiết bị KHÔNG có heartbeat.
    final fresh = d.latest != null &&
        DateTime.now().difference(d.latest!) < const Duration(hours: 24);
    const mono = TextStyle(
      fontFamily: 'JetBrains Mono',
      fontWeight: FontWeight.w600,
      fontFeatures: [FontFeature.tabularFigures()],
    );
    final num_ = TextStyle(
      color: cs.onSurface,
      fontFeatures: const [FontFeature.tabularFigures()],
    );

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 11),
      decoration: BoxDecoration(
        // Kẻ sọc nhẹ cho dễ dò ngang hàng khi bảng dài.
        border: Border(
            bottom: BorderSide(color: cs.outlineVariant.withValues(alpha: 0.5))),
      ),
      // KHÔNG sọc ngựa vằn (gỡ 2026-08-20, chủ dự án yêu cầu). Cột "Trạng thái update"
      // nay là một ô CÓ VIỀN ở giữa hàng; sọc nền cộng viền ô là hai hệ chia khối chồng
      // lên nhau, mắt phải lọc bớt một cái. Đường kẻ ngang giữa các hàng đã đủ dò ngang.
      child: Row(
        children: [
          _cell(
            0,
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Tooltip(
                  message: fresh ? tr('mm.fresh') : tr('mm.stale'),
                  child: Container(
                    width: 8,
                    height: 8,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: fresh ? sem.success : cs.outline,
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                Flexible(
                  child: Text(d.id,
                      overflow: TextOverflow.ellipsis, style: mono),
                ),
              ],
            ),
          ),
          _cell(1, _firmwareCell(context, d, mono)),
          _cell(2, _updateCell(context, d)),
          _cell(3, Text('${d.runCount}', style: num_), right: true),
          _cell(
            4,
            Text(d.latest == null ? '—' : _date(d.latest!), style: num_),
            right: true,
          ),
          SizedBox(width: 40, child: _historyBtn(d)),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Pop-up 0: đặt tên phiên bản khi tải firmware lên
// ---------------------------------------------------------------------------

class _VersionNameDialog extends StatefulWidget {
  final String pickedFileName;
  final List<String> existing; // tên file đã có trên server, để cảnh báo ghi đè
  const _VersionNameDialog({required this.pickedFileName, required this.existing});

  @override
  State<_VersionNameDialog> createState() => _VersionNameDialogState();
}

class _VersionNameDialogState extends State<_VersionNameDialog> {
  late final TextEditingController _ctrl =
      // Tên file chọn có sẵn version thì điền trước cho nhanh; không thì để trống.
      TextEditingController(text: versionInFileName(widget.pickedFileName) ?? '');

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  String get _raw => _ctrl.text.trim();
  bool get _valid => isValidOtaVersion(_raw);
  String get _fileName => otaFileNameFor(_raw);

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final willOverwrite = _valid && widget.existing.contains(_fileName);

    return AlertDialog(
      title: Text(tr('mm.versionTitle')),
      content: SizedBox(
        width: 440,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              tr('mm.versionPickedFile').replaceFirst('{name}', widget.pickedFileName),
              style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant),
            ),
            const SizedBox(height: 14),
            TextField(
              controller: _ctrl,
              autofocus: true,
              decoration: InputDecoration(
                labelText: tr('mm.versionLabel'),
                hintText: '2.4.4',
                prefixText: 'v',
                isDense: true,
                errorText: _raw.isEmpty || _valid ? null : tr('mm.versionInvalid'),
              ),
              style: const TextStyle(fontFamily: 'JetBrains Mono'),
              onChanged: (_) => setState(() {}),
              onSubmitted: (_) {
                if (_valid) Navigator.pop(context, _raw);
              },
            ),
            const SizedBox(height: 12),
            // Cho thấy TRƯỚC tên file sẽ nằm trên server — khỏi đoán.
            Row(
              children: [
                Icon(Icons.save_outlined, size: 16, color: cs.onSurfaceVariant),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    _valid
                        ? tr('mm.versionSavedAs').replaceFirst('{name}', _fileName)
                        : tr('mm.versionHint'),
                    style: TextStyle(
                      fontSize: 12,
                      fontFamily: _valid ? 'JetBrains Mono' : null,
                      color: cs.onSurfaceVariant,
                    ),
                  ),
                ),
              ],
            ),
            if (willOverwrite) ...[
              const SizedBox(height: 10),
              Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: sem.warning.withValues(alpha: 0.10),
                  borderRadius: BorderRadius.circular(AppRadius.sm),
                  border: Border.all(color: sem.warning.withValues(alpha: 0.4)),
                ),
                child: Text(tr('mm.versionOverwrite'),
                    style: TextStyle(fontSize: 12, color: cs.onSurface)),
              ),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(tr('common.cancel')),
        ),
        FilledButton(
          onPressed: _valid ? () => Navigator.pop(context, _raw) : null,
          child: Text(tr('mm.upload')),
        ),
      ],
    );
  }
}

// ---------------------------------------------------------------------------
// Pop-up 1: xác nhận triển khai
// ---------------------------------------------------------------------------

class _ConfirmDeployDialog extends StatefulWidget {
  final OtaFile file;
  final FbtApi api;
  const _ConfirmDeployDialog({required this.file, required this.api});

  @override
  State<_ConfirmDeployDialog> createState() => _ConfirmDeployDialogState();
}

class _ConfirmDeployDialogState extends State<_ConfirmDeployDialog> {
  int? _deviceCount; // số máy sẽ ảnh hưởng; null = đang đếm / không đếm được

  @override
  void initState() {
    super.initState();
    _countDevices();
  }

  /// Đếm máy để câu cảnh báo nói SỐ THẬT chứ không nói chung chung.
  Future<void> _countDevices() async {
    try {
      final all = await widget.api.listDevices(fresh: true);
      final session = SessionStore.current;
      final n = all.where((d) => session?.canSee(d.id) ?? false).length;
      if (mounted) setState(() => _deviceCount = n);
    } catch (_) {
      // Không đếm được thì vẫn cho triển khai — chỉ mất phần con số trong cảnh báo.
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final ver = versionInFileName(widget.file.name);

    return AlertDialog(
      title: Text(tr('mm.deployTitle')),
      content: SizedBox(
        width: 460,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _kv(context, tr('mm.deployFile'),
                '${widget.file.name}  ·  ${_size(widget.file.size)}'),
            _kv(
              context,
              tr('mm.deployAffected'),
              _deviceCount == null
                  ? '…'
                  : tr('mm.deployNMachines').replaceFirst('{n}', '${_deviceCount!}'),
            ),
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: sem.warning.withValues(alpha: 0.10),
                borderRadius: BorderRadius.circular(AppRadius.sm),
                border: Border.all(color: sem.warning.withValues(alpha: 0.4)),
              ),
              child: Text(tr('mm.deployWarning'),
                  style: TextStyle(fontSize: 12, color: cs.onSurface)),
            ),
            // Tên file không mang version → sau này KHÔNG tính được tiến độ.
            if (ver == null) ...[
              const SizedBox(height: 8),
              Text(tr('mm.deployNoVersion'),
                  style: TextStyle(fontSize: 12, color: sem.warning)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: Text(tr('common.cancel')),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, true),
          child: Text(tr('mm.deployGo')),
        ),
      ],
    );
  }

  Widget _kv(BuildContext context, String k, String v) => Padding(
        padding: const EdgeInsets.only(bottom: 4),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SizedBox(
              width: 110,
              child: Text(k,
                  style: TextStyle(
                      fontSize: 12,
                      color: Theme.of(context).colorScheme.onSurfaceVariant)),
            ),
            Expanded(
              child: Text(v,
                  style: const TextStyle(
                      fontFamily: 'JetBrains Mono', fontSize: 13)),
            ),
          ],
        ),
      );
}

// ---------------------------------------------------------------------------
// Pop-up 2: tiến độ triển khai
// ---------------------------------------------------------------------------

/// Bảng "máy nào đã lên bản đang chọn". Dựng HOÀN TOÀN từ version máy tự báo về
/// trong payload kết quả (`GET /devices`) — server KHÔNG ghi nhận máy nào đã gọi
/// `/ota/check`, nên đây là suy luận, không phải sổ ghi thật. Hệ quả phải nói rõ
/// cho người dùng: máy đã nạp xong nhưng CHƯA đo lại thì vẫn hiện bản cũ.
class _RolloutDialog extends StatefulWidget {
  final String target;
  final FbtApi api;
  const _RolloutDialog({required this.target, required this.api});

  @override
  State<_RolloutDialog> createState() => _RolloutDialogState();
}

class _RolloutDialogState extends State<_RolloutDialog> {
  List<CloudDevice>? _devices;
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _devices = null;
      _error = null;
    });
    try {
      final all = await widget.api.listDevices(fresh: true);
      final session = SessionStore.current;
      final seen = all.where((d) => session?.canSee(d.id) ?? false).toList();
      // Máy CHƯA lên bản mới xếp lên đầu — đó là thứ người triển khai cần nhìn.
      seen.sort((a, b) {
        final oa = isDeviceOnTarget(a.version, widget.target) == true ? 1 : 0;
        final ob = isDeviceOnTarget(b.version, widget.target) == true ? 1 : 0;
        if (oa != ob) return oa - ob;
        return (b.latest ?? DateTime(0)).compareTo(a.latest ?? DateTime(0));
      });
      if (mounted) setState(() => _devices = seen);
    } on CloudApiException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final devices = _devices;
    final ver = versionInFileName(widget.target);

    final done = devices == null
        ? 0
        : devices.where((d) => isDeviceOnTarget(d.version, widget.target) == true).length;
    final total = devices?.length ?? 0;

    return AlertDialog(
      title: Row(
        children: [
          Expanded(
            child: Text(
              tr('mm.progressTitle').replaceFirst('{name}', widget.target),
              style: const TextStyle(fontSize: 16),
            ),
          ),
          IconButton(
            tooltip: tr('history.jsonRefresh'),
            onPressed: devices == null && _error == null ? null : _load,
            icon: const Icon(Icons.refresh, size: 20),
          ),
        ],
      ),
      content: SizedBox(
        width: 560,
        height: 460,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (_error != null) Text(_error!, style: TextStyle(color: cs.error)),
            if (devices == null && _error == null)
              const Expanded(child: Center(child: CircularProgressIndicator()))
            else ...[
              // Tên file không mang version → không thể suy ra tiến độ, nói thẳng.
              if (ver == null)
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: sem.warning.withValues(alpha: 0.10),
                    borderRadius: BorderRadius.circular(AppRadius.sm),
                  ),
                  child: Text(tr('mm.progressNoVersion'),
                      style: TextStyle(fontSize: 12, color: cs.onSurface)),
                )
              else ...[
                ClipRRect(
                  borderRadius: BorderRadius.circular(6),
                  child: LinearProgressIndicator(
                    value: total == 0 ? 0 : done / total,
                    minHeight: 10,
                    backgroundColor: sem.surfaceSunken,
                    valueColor: AlwaysStoppedAnimation(sem.success),
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  tr('mm.progressCount')
                      .replaceFirst('{done}', '$done')
                      .replaceFirst('{total}', '$total')
                      .replaceFirst('{ver}', ver),
                  style: const TextStyle(
                      fontSize: 12, fontFeatures: [FontFeature.tabularFigures()]),
                ),
              ],
              const SizedBox(height: 12),
              Expanded(
                child: devices!.isEmpty
                    ? Center(
                        child: Text(tr('mm.statusEmpty'),
                            style: TextStyle(color: cs.onSurfaceVariant)))
                    : ListView.builder(
                        itemCount: devices.length,
                        itemBuilder: (_, i) => _row(context, devices[i]),
                      ),
              ),
              const SizedBox(height: 8),
              Text(tr('mm.progressCaveat'),
                  style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(tr('common.close')),
        ),
      ],
    );
  }

  Widget _row(BuildContext context, CloudDevice d) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final on = isDeviceOnTarget(d.version, widget.target);
    final fw = d.version.trim();

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 9),
      decoration: BoxDecoration(
        border: Border(
            bottom: BorderSide(color: cs.outlineVariant.withValues(alpha: 0.5))),
      ),
      child: Row(
        children: [
          Icon(
            on == true ? Icons.check_circle : Icons.radio_button_unchecked,
            size: 18,
            color: on == true ? sem.success : cs.outline,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(d.id,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                    fontFamily: 'JetBrains Mono', fontWeight: FontWeight.w600)),
          ),
          SizedBox(
            width: 110,
            child: Text(
              fw.isEmpty ? '—' : fw,
              style: TextStyle(
                fontFamily: 'JetBrains Mono',
                fontSize: 13,
                color: on == true ? sem.success : cs.onSurfaceVariant,
              ),
            ),
          ),
          SizedBox(
            width: 120,
            child: Text(
              d.latest == null ? '—' : _date(d.latest!),
              textAlign: TextAlign.right,
              style: TextStyle(
                fontSize: 12,
                color: cs.onSurfaceVariant,
                fontFeatures: const [FontFeature.tabularFigures()],
              ),
            ),
          ),
        ],
      ),
    );
  }
}


// ---------------------------------------------------------------------------
// Lịch sử cập nhật firmware của MỘT máy
// ---------------------------------------------------------------------------

/// Hộp thoại: máy này đã chạy những bản firmware nào, đổi lúc nào, bằng cách nào.
///
/// **Server KHÔNG có nhật ký cập nhật** — lịch sử ở đây SUY RA từ `version` mà máy
/// kèm theo mỗi lần gửi kết quả (`buildFirmwareHistory`). Vì vậy hộp thoại nói
/// thẳng giới hạn đó ở chân, thay vì trình bày như một sổ ghi.
class _FirmwareHistoryDialog extends StatefulWidget {
  final CloudDevice device;
  final FbtApi api;
  const _FirmwareHistoryDialog({required this.device, required this.api});

  @override
  State<_FirmwareHistoryDialog> createState() => _FirmwareHistoryDialogState();
}

class _FirmwareHistoryDialogState extends State<_FirmwareHistoryDialog> {
  // Trần đọc. Máy nhiều nhất trong kho ~160 lần đo nên 1000 là dư, nhưng vẫn phải
  // CÓ trần — và chạm trần thì NÓI RA (`mm.histMore`): im lặng cắt bớt sẽ đọc
  // thành "máy chỉ từng chạy chừng này bản".
  static const int _pageSize = 200;
  static const int _maxRuns = 1000;

  List<FirmwareStint>? _hist;
  String? _error;
  int _read = 0;
  int _total = 0;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      // Nhật ký mốc THẬT chạy song song với các trang phiên đo — hai nguồn độc
      // lập, chờ nối tiếp chỉ tốn thêm một vòng mạng.
      final logF = widget.api.fwLog(widget.device.id);
      final runs = <TestResult>[];
      var offset = 0;
      var total = 0;
      while (offset < _maxRuns) {
        final page = await widget.api
            .listRuns(widget.device.id, limit: _pageSize, offset: offset);
        total = page.total;
        runs.addAll(page.runs);
        offset += _pageSize;
        if (page.runs.length < _pageSize || runs.length >= total) break;
      }
      final hist =
          mergeFirmwareLog(await logF, buildFirmwareHistory(runs), lanDo: runs);
      if (!mounted) return;
      setState(() {
        _hist = hist;
        _read = runs.length;
        _total = total;
      });
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final h = _hist;

    return AlertDialog(
      title: Text(tr('mm.histFor').replaceAll('{id}', widget.device.id)),
      content: SizedBox(
        width: 560,
        child: _error != null
            ? Text(_error!, style: TextStyle(color: cs.error))
            : h == null
                ? const SizedBox(
                    height: 90,
                    child: Center(child: CircularProgressIndicator()))
                : h.isEmpty
                    ? Text(tr('mm.histEmpty'),
                        style: TextStyle(color: cs.onSurfaceVariant))
                    : Column(
                        mainAxisSize: MainAxisSize.min,
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Flexible(
                            child: SingleChildScrollView(
                              child: Column(children: [
                                _head(cs),
                                for (final s in h) _line(s, cs, sem),
                              ]),
                            ),
                          ),
                          const SizedBox(height: 12),
                          if (_read < _total)
                            Padding(
                              padding: const EdgeInsets.only(bottom: 6),
                              child: Text(
                                tr('mm.histMore')
                                    .replaceAll('{n}', '$_read')
                                    .replaceAll('{total}', '$_total'),
                                style:
                                    TextStyle(fontSize: 12, color: sem.warning),
                              ),
                            ),
                          Text(tr('mm.histNote'),
                              style: TextStyle(
                                  fontSize: 11.5,
                                  height: 1.35,
                                  color: cs.onSurfaceVariant)),
                        ],
                      ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(tr('common.close')),
        ),
      ],
    );
  }

  Widget _head(ColorScheme cs) {
    final st = TextStyle(
        fontSize: 12, fontWeight: FontWeight.w600, color: cs.onSurfaceVariant);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      color: AppSemantic.of(context).surfaceSunken,
      child: Row(children: [
        Expanded(flex: 4, child: Text(tr('mm.histVersion'), style: st)),
        Expanded(flex: 3, child: Text(tr('mm.histHow'), style: st)),
        Expanded(flex: 4, child: Text(tr('mm.histWhen'), style: st)),
      ]),
    );
  }

  Widget _line(FirmwareStint s, ColorScheme cs, AppSemantic sem) {
    final ota = s.updatedViaOta; // null = quãng cũ nhất, không kết luận được
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
      decoration: BoxDecoration(
        border: Border(
            bottom: BorderSide(color: cs.outlineVariant.withValues(alpha: 0.5))),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            flex: 4,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(s.version,
                    style: const TextStyle(
                        fontFamily: 'JetBrains Mono',
                        fontWeight: FontWeight.w600)),
                Text(tr('mm.histRuns').replaceAll('{n}', '${s.runs}'),
                    style:
                        TextStyle(fontSize: 11.5, color: cs.onSurfaceVariant)),
              ],
            ),
          ),
          Expanded(
            flex: 3,
            // Quãng CŨ NHẤT không phải một lần cập nhật — không đoán bừa.
            child: ota == null
                ? Text('—', style: TextStyle(color: cs.onSurfaceVariant))
                : Row(children: [
                    Icon(ota ? Icons.cloud_download_outlined : Icons.usb,
                        size: 16,
                        color: ota ? sem.info : cs.onSurfaceVariant),
                    const SizedBox(width: 6),
                    Flexible(
                      child: Text(
                        ota ? tr('mm.histOta') : tr('mm.histManual'),
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                            fontSize: 13,
                            color: ota ? sem.info : cs.onSurfaceVariant),
                      ),
                    ),
                  ]),
          ),
          Expanded(
            flex: 4,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(_date(s.updatedAt),
                    style: const TextStyle(
                        fontFeatures: [FontFeature.tabularFigures()])),
                if (s.isFirstKnown)
                  Text(tr('mm.histFirst'),
                      style: TextStyle(
                          fontSize: 11.5, color: cs.onSurfaceVariant))
                // Ngày SUY ĐOÁN phải tự nhận là suy đoán. Để trơ cạnh mốc thật
                // thì cả bảng đọc như sổ ghi, mà một nửa số dòng không phải.
                else if (!s.dateIsExact)
                  Text(tr('mm.histGuess'),
                      style: TextStyle(
                          fontSize: 11.5, color: cs.onSurfaceVariant)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// --- So version để tính tiến độ triển khai -----------------------------------

/// Rút "số version" từ TÊN FILE firmware để so với version máy báo về.
/// `fbt_v2.4.4.bin` → `2.4.4` · `firmware.bin` → null (tên không mang version).
///
/// Server KHÔNG hiểu ngữ nghĩa version — nó chỉ coi tên file là "bản mục tiêu".
/// Nên muốn biết máy nào đã lên bản mới thì phải tự đối chiếu ở đây.
String? versionInFileName(String fileName) {
  final base = fileName.replaceFirst(RegExp(r'\.bin$', caseSensitive: false), '');
  final m = RegExp(r'v?(\d+(?:\.\d+)+)', caseSensitive: false).firstMatch(base);
  return m?.group(1);
}

/// Version người dùng gõ có dùng được không.
///
/// Yêu cầu: **ít nhất 2 nhóm số** (`2.4` / `2.4.4`), cho phép chữ `v` đầu và hậu tố
/// chữ-số-gạch (`2.4.4AT`, `2.4.4_rc1`). Chặn ký tự mà `safe_name` phía server sẽ đổi
/// — nếu không server trả 400 "tên file phải là .bin hợp lệ" và người dùng không hiểu vì sao.
bool isValidOtaVersion(String input) {
  final s = input.trim();
  if (s.isEmpty) return false;
  final body = s.toLowerCase().startsWith('v') ? s.substring(1) : s;
  return RegExp(r'^\d+(?:\.\d+)+[A-Za-z0-9_-]*$').hasMatch(body);
}

/// Tên file trên server cho version người dùng gõ: `2.4.4` → `fbt_v2.4.4.bin`.
/// App đặt tên chứ KHÔNG dùng tên file gốc — xem doc ở `_upload()`.
String otaFileNameFor(String version) {
  final s = version.trim();
  final body = s.toLowerCase().startsWith('v') ? s.substring(1) : s;
  return 'fbt_v$body.bin';
}

/// Chuẩn hoá version máy báo về: bỏ khoảng trắng, hạ hoa/thường, bỏ chữ `v` đầu.
/// GIỮ hậu tố (`v2.4.3AT` → `2.4.3at`) — CỐ Ý: bản có hậu tố là build khác, không
/// dám coi là đã cập nhật. Thà báo thiếu còn hơn báo "đã xong" sai.
String normalizeDeviceVersion(String v) {
  final s = v.trim().toLowerCase();
  return s.startsWith('v') ? s.substring(1) : s;
}

/// Khớp chuỗi tìm kiếm với bất kỳ trường nào, **không phân biệt hoa/thường**.
///
/// Không-phân-biệt-hoa-thường không phải chi tiết lịch sự: fleet thật có cả `V2.3.1` (V hoa)
/// lẫn `v2.4.3` (thường), nên lọc phân biệt hoa/thường sẽ **giấu mất một nửa số máy** mà
/// không báo gì. Query rỗng = khớp tất cả (không lọc).
bool matchesQuery(String query, Iterable<String> fields) {
  final q = query.trim().toLowerCase();
  if (q.isEmpty) return true;
  return fields.any((f) => f.toLowerCase().contains(q));
}

/// Máy này có ghim được bản riêng không — tức firmware của nó có gọi `/ota/check` không.
///
/// **v2.4.4 là mốc**: từ bản đó firmware mới hỏi server và mới gửi `?device=<id>`. Ghim cho
/// máy cũ hơn thì server lưu ngoan ngoãn còn máy **không bao giờ biết** — hỏng hoàn toàn im
/// lặng, và người vận hành sẽ tưởng đã đặt xong. Vì vậy UI gác ở đây chứ không để server
/// đoán: server không có cách nào biết máy đang chạy gì.
///
/// So theo **tiền tố số**, bỏ hậu tố: `v2.4.4AT` → 2.4.4 → được (build từ nguồn v2.4.4).
/// Máy chưa báo version lần nào → `false`: thà không cho ghim còn hơn ghim vào hư không.
bool supportsPerDevicePin(String deviceVersion) {
  final m = RegExp(r'^(\d+(?:\.\d+)*)')
      .firstMatch(normalizeDeviceVersion(deviceVersion));
  if (m == null) return false;
  final got = m.group(1)!.split('.').map(int.parse).toList();
  const want = [2, 4, 4];
  for (var i = 0; i < want.length; i++) {
    final g = i < got.length ? got[i] : 0;
    if (g != want[i]) return g > want[i];
  }
  return true;
}

/// Máy này đã chạy đúng bản mục tiêu chưa? null = không kết luận được
/// (tên file không mang version, hoặc máy chưa báo version lần nào).
bool? isDeviceOnTarget(String deviceVersion, String targetFileName) {
  final want = versionInFileName(targetFileName);
  if (want == null || deviceVersion.trim().isEmpty) return null;
  return normalizeDeviceVersion(deviceVersion) == normalizeDeviceVersion(want);
}

// --- Helper dùng chung 2 mục -------------------------------------------------

String _size(int bytes) {
  if (bytes < 1024) return '$bytes B';
  if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
  return '${(bytes / 1024 / 1024).toStringAsFixed(2)} MB';
}

String _date(DateTime t) => '${t.day.toString().padLeft(2, '0')}/'
    '${t.month.toString().padLeft(2, '0')}/${t.year} '
    '${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}';
