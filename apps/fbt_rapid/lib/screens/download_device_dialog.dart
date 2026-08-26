import 'package:flutter/material.dart';

import '../models/test_result.dart';
import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/rapid_erp_api.dart' show buildCloudClient;
import '../services/result_export.dart';
import '../theme/app_theme.dart';
import '../util/curve_processing.dart';
import '../util/format.dart';
import '../util/i18n.dart';
import '../widgets/run_chart_export.dart';

/// Định dạng tải xuống.
enum DownloadKind { json, charts }

/// Tải dữ liệu của một máy: **bảng chọn từng lần đo** + chọn định dạng.
///
/// Ba giai đoạn, cố ý tách rời:
///  1. `listing` — lật hết trang tóm tắt để BIẾT máy có những lần đo nào. Nhanh
///     (mỗi trang 50 bản ghi) và không tải đường cong.
///  2. `choosing` — hiện bảng, người dùng tích dòng + chọn JSON hay ảnh đồ thị.
///  3. `working` — chỉ tải chi tiết những dòng ĐÃ TÍCH.
///
/// Vì sao tách: bản đầu tải tuốt rồi mới lưu, nên khi dừng/lỗi giữa chừng vẫn ra
/// một file trông bình thường mà thiếu dữ liệu — đã dẫm phải thật (file ghi
/// `count: 2` trong khi máy có 47 lần đo). Giờ mẫu số là **số dòng người dùng
/// tích**, và nếu lấy thiếu thì nói thẳng trên UI lẫn ghi vào file.
class DownloadDeviceDialog extends StatefulWidget {
  final AppSettings settings;
  final CloudSource source;
  final CloudDevice device;
  final int readingIntervalSec;

  const DownloadDeviceDialog({
    super.key,
    required this.settings,
    required this.source,
    required this.device,
    this.readingIntervalSec = 30,
  });

  @override
  State<DownloadDeviceDialog> createState() => _DownloadDeviceDialogState();
}

enum _Phase { listing, choosing, working, finished }

class _DownloadDeviceDialogState extends State<DownloadDeviceDialog> {
  static const int _pageSize = 50;

  late final CloudHistoryClient _api =
      buildCloudClient(widget.settings, widget.source);

  _Phase _phase = _Phase.listing;
  final List<TestResult> _summaries = [];
  final Set<String> _picked = {}; // id lần đo đã tích
  DownloadKind _kind = DownloadKind.json;

  int _listed = 0; // đã liệt kê được bao nhiêu (giai đoạn 1)
  int _total = 0; // tổng server báo
  int _done = 0; // đã xử lý xong bao nhiêu dòng đã tích (giai đoạn 3)
  int _failed = 0; // số dòng lấy chi tiết hỏng
  bool _stopping = false;
  String? _error;
  String? _savedDir; // '' = web (đã tải xuống), null = chưa lưu

  @override
  void initState() {
    super.initState();
    _listAll();
  }

  /// Giai đoạn 1: lật hết trang tóm tắt.
  Future<void> _listAll() async {
    try {
      var offset = 0;
      while (!_stopping) {
        final page = await _api.listRuns(widget.device.id,
            limit: _pageSize, offset: offset);
        if (!mounted) return;
        setState(() {
          _summaries.addAll(page.runs);
          _listed = _summaries.length;
          _total = page.total;
        });
        offset += _pageSize;
        if (page.runs.isEmpty || _summaries.length >= page.total) break;
      }
      // Mặc định tích HẾT: "tải tất cả" là việc hay làm nhất, người dùng chỉ cần
      // bỏ tích vài dòng thay vì tự tay tích mấy chục dòng.
      if (mounted) {
        setState(() {
          _picked.addAll(_summaries.map((e) => e.id));
          _phase = _Phase.choosing;
        });
      }
    } on CloudApiException catch (e) {
      if (mounted) {
        setState(() {
          _error = e.message;
          _phase = _Phase.choosing;
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _error = '$e';
          _phase = _Phase.choosing;
        });
      }
    }
  }

  /// Giai đoạn 3: tải chi tiết + xuất theo định dạng đã chọn.
  Future<void> _download() async {
    final chosen = _summaries.where((e) => _picked.contains(e.id)).toList();
    setState(() {
      _phase = _Phase.working;
      _done = 0;
      _failed = 0;
      _error = null;
    });

    final full = <TestResult>[];
    String? dir;
    try {
      for (final s in chosen) {
        if (_stopping) break;
        TestResult run;
        try {
          run = await _api.fetchRun(s.id);
        } catch (_) {
          // Một lần đo hỏng KHÔNG được làm hỏng cả mẻ. Giữ bản tóm tắt (còn CT
          // + kết quả, thiếu đường cong) và ĐẾM vào _failed để báo cho người dùng.
          run = s;
          _failed++;
        }
        full.add(run);

        if (_kind == DownloadKind.charts && mounted) {
          // Ảnh đồ thị: render y hệt nút "Lưu" ở màn chi tiết (dùng chung
          // widgets/run_chart_export.dart) rồi lưu 4 PNG + data.json mỗi lần đo.
          final pngs = await captureRunCharts(
            context,
            run: run,
            views: CurveView.values,
            readingIntervalSec: widget.readingIntervalSec,
          );
          if (pngs.isNotEmpty) dir = await ResultExport.saveRun(run, pngs);
        }
        if (!mounted) return;
        setState(() => _done = full.length);
      }

      if (_kind == DownloadKind.json && full.isNotEmpty) {
        dir = await ResultExport.saveDeviceArchive(widget.device.id, full);
      }
    } catch (e) {
      if (mounted) setState(() => _error = '${tr('dl.saveError')}: $e');
    }
    if (!mounted) return;
    setState(() {
      _savedDir = dir;
      _phase = _Phase.finished;
    });
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(tr('dl.title').replaceFirst('{id}', widget.device.id)),
      content: SizedBox(width: 640, height: 480, child: _body(context)),
      actions: _actions(context),
    );
  }

  Widget _body(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    switch (_phase) {
      case _Phase.listing:
        return _centered(
          const CircularProgressIndicator(),
          _total == 0
              ? tr('dl.counting')
              : tr('dl.listing')
                  .replaceFirst('{done}', '$_listed')
                  .replaceFirst('{total}', '$_total'),
        );

      case _Phase.choosing:
        if (_summaries.isEmpty) {
          return _centered(
            Icon(Icons.inbox_outlined, size: 40, color: cs.onSurfaceVariant),
            _error ?? tr('dl.empty'),
          );
        }
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _kindPicker(context),
            const SizedBox(height: 10),
            _tableHeader(context),
            Expanded(
              child: ListView.builder(
                itemCount: _summaries.length,
                itemBuilder: (_, i) => _row(context, _summaries[i], i),
              ),
            ),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Text(_error!,
                    style: TextStyle(fontSize: 12, color: cs.error)),
              ),
          ],
        );

      case _Phase.working:
        final total = _picked.length;
        return _centered(
          SizedBox(
            width: 320,
            child: ClipRRect(
              borderRadius: BorderRadius.circular(6),
              child: LinearProgressIndicator(
                value: total == 0 ? null : (_done / total).clamp(0.0, 1.0),
                minHeight: 8,
              ),
            ),
          ),
          tr('dl.progress')
              .replaceFirst('{done}', '$_done')
              .replaceFirst('{total}', '$total'),
          hint: _kind == DownloadKind.charts
              ? tr('dl.slowCharts')
              : tr('dl.slowHint'),
        );

      case _Phase.finished:
        return _finishedBody(context);
    }
  }

  Widget _centered(Widget icon, String text, {String? hint}) {
    final cs = Theme.of(context).colorScheme;
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          icon,
          const SizedBox(height: 14),
          Text(text, textAlign: TextAlign.center),
          if (hint != null) ...[
            const SizedBox(height: 8),
            SizedBox(
              width: 420,
              child: Text(hint,
                  textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
            ),
          ],
        ],
      ),
    );
  }

  Widget _finishedBody(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final want = _picked.length;
    final ok = _done;
    final short = ok < want;

    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(ok == 0 ? Icons.error_outline : Icons.check_circle,
                  color: ok == 0 ? cs.error : sem.success),
              const SizedBox(width: 10),
              Text(
                ok == 0
                    ? tr('dl.empty')
                    : tr('dl.done').replaceFirst('{n}', '$ok'),
                style: const TextStyle(fontSize: 14),
              ),
            ],
          ),
          // Thiếu so với số đã tích → NÓI THẲNG. Đây đúng chỗ bản trước im lặng.
          if (short) ...[
            const SizedBox(height: 10),
            SizedBox(
              width: 520,
              child: Text(
                tr('dl.partial')
                    .replaceFirst('{n}', '$ok')
                    .replaceFirst('{total}', '$want'),
                style: TextStyle(fontSize: 12, color: sem.warning),
              ),
            ),
          ],
          if (_failed > 0) ...[
            const SizedBox(height: 8),
            SizedBox(
              width: 520,
              child: Text(
                tr('dl.noCurve').replaceFirst('{n}', '$_failed'),
                style: TextStyle(fontSize: 12, color: sem.warning),
              ),
            ),
          ],
          if (_savedDir != null && _savedDir!.isNotEmpty) ...[
            const SizedBox(height: 12),
            SizedBox(
              width: 520,
              child: Text(_savedDir!,
                  style: TextStyle(
                      fontSize: 11,
                      fontFamily: 'JetBrains Mono',
                      color: cs.onSurfaceVariant)),
            ),
          ],
          if (_savedDir == '') ...[
            const SizedBox(height: 12),
            Text(tr('dl.webSaved'),
                style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
          ],
          if (_error != null) ...[
            const SizedBox(height: 10),
            SizedBox(
              width: 520,
              child:
                  Text(_error!, style: TextStyle(fontSize: 12, color: cs.error)),
            ),
          ],
        ],
      ),
    );
  }

  Widget _kindPicker(BuildContext context) {
    return SegmentedButton<DownloadKind>(
      showSelectedIcon: false,
      segments: [
        ButtonSegment(
          value: DownloadKind.json,
          icon: const Icon(Icons.data_object, size: 18),
          label: Text(tr('dl.kindJson')),
        ),
        ButtonSegment(
          value: DownloadKind.charts,
          icon: const Icon(Icons.show_chart, size: 18),
          label: Text(tr('dl.kindCharts')),
        ),
      ],
      selected: {_kind},
      onSelectionChanged: (s) => setState(() => _kind = s.first),
    );
  }

  Widget _tableHeader(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final all = _picked.length == _summaries.length && _summaries.isNotEmpty;
    final some = _picked.isNotEmpty && !all;
    final style = TextStyle(
        fontSize: 12, fontWeight: FontWeight.w600, color: cs.onSurfaceVariant);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 6),
      decoration: BoxDecoration(
        color: AppSemantic.of(context).surfaceSunken,
        border: Border(bottom: BorderSide(color: cs.outlineVariant)),
      ),
      child: Row(
        children: [
          Checkbox(
            value: all ? true : (some ? null : false),
            tristate: true,
            onChanged: (_) => setState(() {
              if (all) {
                _picked.clear();
              } else {
                _picked
                  ..clear()
                  ..addAll(_summaries.map((e) => e.id));
              }
            }),
          ),
          Expanded(flex: 4, child: Text(tr('dl.colTime'), style: style)),
          Expanded(flex: 2, child: Text(tr('dl.colFw'), style: style)),
          Expanded(flex: 3, child: Text(tr('dl.colResult'), style: style)),
        ],
      ),
    );
  }

  Widget _row(BuildContext context, TestResult r, int i) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final on = _picked.contains(r.id);
    final pos = r.countOf(Classification.positive);
    final mono = TextStyle(
      fontSize: 12.5,
      fontFamily: 'JetBrains Mono',
      color: cs.onSurface,
      fontFeatures: const [FontFeature.tabularFigures()],
    );

    return InkWell(
      onTap: () => setState(() => on ? _picked.remove(r.id) : _picked.add(r.id)),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 4),
        decoration: BoxDecoration(
          color: i.isOdd ? sem.surfaceSunken.withValues(alpha: 0.4) : null,
          border: Border(
              bottom:
                  BorderSide(color: cs.outlineVariant.withValues(alpha: 0.5))),
        ),
        child: Row(
          children: [
            Checkbox(
              value: on,
              onChanged: (_) =>
                  setState(() => on ? _picked.remove(r.id) : _picked.add(r.id)),
            ),
            Expanded(flex: 4, child: Text(formatDateTime(r.timestamp), style: mono)),
            Expanded(
              flex: 2,
              child: Text(r.version.isEmpty ? '—' : r.version, style: mono),
            ),
            Expanded(
              flex: 3,
              child: Text(
                pos > 0
                    ? tr('dl.nPositive').replaceFirst('{n}', '$pos')
                    : tr('dl.allNegative'),
                style: mono.copyWith(color: pos > 0 ? cs.error : sem.success),
              ),
            ),
          ],
        ),
      ),
    );
  }

  List<Widget> _actions(BuildContext context) {
    switch (_phase) {
      case _Phase.listing:
        return [
          TextButton(
            onPressed: _stopping ? null : () => setState(() => _stopping = true),
            child: Text(tr('dl.stop')),
          ),
        ];

      case _Phase.choosing:
        return [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(tr('common.cancel')),
          ),
          FilledButton.icon(
            onPressed: _picked.isEmpty ? null : _download,
            icon: const Icon(Icons.download_outlined, size: 18),
            label: Text(
              tr('dl.downloadN').replaceFirst('{n}', '${_picked.length}'),
            ),
          ),
        ];

      case _Phase.working:
        return [
          TextButton(
            onPressed: _stopping ? null : () => setState(() => _stopping = true),
            child: Text(_stopping ? tr('dl.stopping') : tr('dl.stop')),
          ),
        ];

      case _Phase.finished:
        return [
          if (_savedDir != null && _savedDir!.isNotEmpty)
            TextButton(
              onPressed: () => ResultExport.revealInExplorer(_savedDir!),
              child: Text(tr('dl.open')),
            ),
          FilledButton(
            onPressed: () => Navigator.pop(context),
            child: Text(tr('common.close')),
          ),
        ];
    }
  }
}
