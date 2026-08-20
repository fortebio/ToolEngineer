import 'package:flutter/material.dart';

import '../models/test_result.dart';
import '../services/app_settings.dart';
import '../services/cloud_history_api.dart';
import '../services/fbt_api.dart' show FbtApi, SensorError;
import '../services/history_store.dart';
import '../services/rapid_erp_api.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/format.dart';
import 'result_detail_screen.dart';

/// Lịch sử các lần chạy của MỘT máy (cloud), phân trang **10 lần chạy/trang**.
/// Bấm 1 lần chạy → tải chi tiết (kèm đường cong) rồi mở màn đồ thị CT.
class CloudRunsScreen extends StatefulWidget {
  final AppSettings settings;
  final CloudDevice device;
  final CloudSource source;

  const CloudRunsScreen({
    super.key,
    required this.settings,
    required this.device,
    this.source = CloudSource.google,
  });

  @override
  State<CloudRunsScreen> createState() => _CloudRunsScreenState();
}

class _CloudRunsScreenState extends State<CloudRunsScreen> {
  static const _pageSize = 10;

  late final CloudHistoryClient _api =
      buildCloudClient(widget.settings, widget.source);
  final _store = HistoryStore();

  List<TestResult> _runs = [];
  int _offset = 0;
  int _total = 0;
  bool _loading = false;
  bool _syncing = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load({int offset = 0}) async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final page = await _api.listRuns(
        widget.device.id,
        limit: _pageSize,
        offset: offset,
      );
      if (!mounted) return;
      setState(() {
        _runs = page.runs;
        _total = page.total;
        _offset = page.offset;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  int get _page => (_offset ~/ _pageSize) + 1;
  int get _pageCount =>
      _total <= 0 ? 1 : ((_total + _pageSize - 1) ~/ _pageSize);
  bool get _hasPrev => _offset > 0;
  bool get _hasNext => _offset + _pageSize < _total;

  void _nextPage() {
    if (_hasNext && !_loading) _load(offset: _offset + _pageSize);
  }

  void _prevPage() {
    if (_hasPrev && !_loading) _load(offset: _offset - _pageSize);
  }

  /// Tải chi tiết (có curves) rồi mở màn đồ thị.
  Future<void> _openDetail(TestResult summary) async {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (_) => const Center(child: CircularProgressIndicator()),
    );
    try {
      final full = await _api.fetchRun(summary.id);
      // Lỗi cảm biến chỉ có ở Engineer Server (Google Drive / RAPID ERP không có endpoint
      // nào cho nó) → kiểm KIỂU thay vì thêm hàm vào `CloudHistoryClient`: một nguồn có,
      // ba nguồn không, nhét vào interface là bắt ba lớp kia cài một hàm trả rỗng.
      // `sessionErrors` tự nuốt lỗi nên hỏng cũng chỉ là không có bảng, không chặn mở màn.
      final api = _api;
      final errors =
          api is FbtApi ? await api.sessionErrors(summary.id) : const <SensorError>[];
      if (!mounted) return;
      Navigator.pop(context); // đóng spinner
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (_) => ResultDetailScreen(
            result: full,
            errors: errors,
            readingIntervalSec: widget.settings.readingIntervalSec,
          ),
        ),
      );
    } catch (e) {
      if (!mounted) return;
      Navigator.pop(context);
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('$e'),
        backgroundColor: kErrorSnackBg,
      ));
    }
  }

  /// Đồng bộ các lần chạy ĐANG HIỂN THỊ (trang hiện tại) về lịch sử cục bộ,
  /// kèm đường cong. Dedupe theo fileId trong [HistoryStore.addAll].
  Future<void> _sync() async {
    final runs = List<TestResult>.from(_runs);
    final total = runs.length;
    if (total == 0) return;

    final progress = ValueNotifier<int>(0);
    var canceled = false;
    var dialogOpen = true;

    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        title: const Text('Đang đồng bộ về máy'),
        content: ValueListenableBuilder<int>(
          valueListenable: progress,
          builder: (_, v, __) => Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              LinearProgressIndicator(value: v / total),
              const SizedBox(height: 12),
              Text('$v / $total lần chạy'),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () {
              canceled = true;
              dialogOpen = false;
              Navigator.pop(ctx);
            },
            child: const Text('Hủy'),
          ),
        ],
      ),
    );

    setState(() => _syncing = true);
    final collected = <TestResult>[];
    for (var i = 0; i < runs.length; i++) {
      if (canceled) break;
      try {
        collected.add(await _api.fetchRun(runs[i].id));
      } catch (_) {
        // bỏ qua lần chạy lỗi, tiếp tục
      }
      progress.value = i + 1;
    }

    if (collected.isNotEmpty) {
      await _store.addAll(collected);
    }
    progress.dispose();

    if (!mounted) return;
    setState(() => _syncing = false);
    if (dialogOpen) Navigator.pop(context); // đóng dialog nếu chưa bị Hủy
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(canceled
          ? 'Đã hủy — đã lưu ${collected.length}/$total lần chạy về máy.'
          : 'Đã đồng bộ ${collected.length}/$total lần chạy (trang này) về máy. '
              'Mở tab Lịch sử → bấm Tải lại để xem.'),
    ));
  }

  @override
  Widget build(BuildContext context) {
    final showPager = _error == null && _runs.isNotEmpty;
    return Scaffold(
      appBar: AppBar(
        title: Text('Máy ${widget.device.id}'),
        actions: [
          // Tính năng Lịch sử cục bộ đang TẮT (`kLocalHistoryEnabled`) → giấu luôn
          // nút ghi vào nó. Bày nút đồng bộ trong khi không có màn nào đọc ra là
          // mời người ta tải dữ liệu về một chỗ không mở được.
          // User read-only cũng không thấy nút này (chỉ admin).
          if (kLocalHistoryEnabled && SessionStore.canWrite)
            IconButton(
              tooltip: 'Đồng bộ trang này về máy',
              onPressed: (_loading || _syncing || _runs.isEmpty) ? null : _sync,
              icon: const Icon(Icons.cloud_download_outlined),
            ),
          IconButton(
            tooltip: 'Tải lại',
            onPressed:
                (_loading || _syncing) ? null : () => _load(offset: _offset),
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: _buildBody(),
      bottomNavigationBar: showPager ? _paginationBar() : null,
    );
  }

  Widget _buildBody() {
    if (_loading && _runs.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (_error != null && _runs.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.error_outline,
                  size: 64,
                  color: Theme.of(context).colorScheme.onSurfaceVariant),
              const SizedBox(height: 16),
              Text('Lỗi tải lịch sử:\n\n$_error', textAlign: TextAlign.center),
              const SizedBox(height: 16),
              OutlinedButton.icon(
                onPressed: () => _load(offset: _offset),
                icon: const Icon(Icons.refresh),
                label: const Text('Thử lại'),
              ),
            ],
          ),
        ),
      );
    }
    if (_runs.isEmpty) {
      return const Center(child: Text('Máy này chưa có lần chạy nào.'));
    }

    return Stack(
      children: [
        ListView.separated(
          padding: const EdgeInsets.all(12),
          itemCount: _runs.length,
          separatorBuilder: (_, __) => const SizedBox(height: 8),
          itemBuilder: (context, i) {
            final r = _runs[i];
            final cs = Theme.of(context).colorScheme;
            final tt = Theme.of(context).textTheme;
            return AppFadeIn(
              index: i,
              child: AppCard(
                hover: true,
                padding:
                    const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
                onTap: () => _openDetail(r),
                child: Row(
                  children: [
                    CircleAvatar(
                      backgroundColor: cs.primaryContainer,
                      foregroundColor: cs.onPrimaryContainer,
                      child: const Icon(Icons.science_outlined, size: 20),
                    ),
                    const SizedBox(width: 14),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            formatDateTime(r.timestamp),
                            style: tt.titleSmall?.copyWith(
                              fontWeight: FontWeight.w600,
                              fontFeatures: const [
                                FontFeature.tabularFigures()
                              ],
                            ),
                          ),
                          if (r.version.isNotEmpty)
                            Text('FW ${r.version}',
                                style: tt.bodySmall
                                    ?.copyWith(color: cs.onSurfaceVariant)),
                          const SizedBox(height: 6),
                          // Chip đếm theo phân loại — chỉ hiện loại có kết quả.
                          Wrap(
                            spacing: 6,
                            runSpacing: 6,
                            children: [
                              for (final c in const [
                                Classification.positive,
                                Classification.slightPositive,
                                Classification.negative,
                                Classification.error,
                              ])
                                if (r.countOf(c) > 0)
                                  _CountChip(
                                      classification: c, count: r.countOf(c)),
                            ],
                          ),
                        ],
                      ),
                    ),
                    Icon(Icons.show_chart, color: cs.onSurfaceVariant),
                  ],
                ),
              ),
            );
          },
        ),
        // Lớp mờ khi đang tải trang khác (giữ danh sách cũ phía dưới).
        //
        // Mờ về phía màu NỀN, không phải đen 20%: trên theme tối, đen-trên-tối
        // gần như không đổi gì — người dùng không thấy tín hiệu "đang tải" nào
        // ngoài cái spinner nhỏ xíu. Mờ về `surface` thì cả hai theme đều thấy
        // rõ danh sách bị đẩy ra sau.
        if (_loading)
          Positioned.fill(
            child: ColoredBox(
              color: Theme.of(context).colorScheme.surface.withValues(alpha: 0.65),
              child: const Center(child: CircularProgressIndicator()),
            ),
          ),
      ],
    );
  }

  Widget _paginationBar() {
    final from = _total == 0 ? 0 : _offset + 1;
    final to = _offset + _runs.length;
    return Material(
      elevation: 8,
      child: SafeArea(
        top: false,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              OutlinedButton.icon(
                onPressed: (_hasPrev && !_loading) ? _prevPage : null,
                icon: const Icon(Icons.chevron_left),
                label: const Text('Trước'),
              ),
              Text(
                '$from–$to / $_total  ·  trang $_page/$_pageCount',
                style: const TextStyle(
                  fontWeight: FontWeight.w600,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
              OutlinedButton.icon(
                onPressed: (_hasNext && !_loading) ? _nextPage : null,
                icon: const Icon(Icons.chevron_right),
                label: const Text('Sau'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Chip "<số> <nhãn>" theo phân loại kết quả. Màu lấy từ `Classification.color`
/// — bảng màu DỮ LIỆU (giống palette đồ thị), cố tình không token-hoá theo theme.
class _CountChip extends StatelessWidget {
  final Classification classification;
  final int count;
  const _CountChip({required this.classification, required this.count});

  @override
  Widget build(BuildContext context) {
    final c = classification.color;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(AppRadius.sm),
        border: Border.all(color: c.withValues(alpha: 0.4)),
      ),
      child: Text(
        '$count ${classification.label}',
        style: TextStyle(
          color: c,
          fontSize: 11,
          fontWeight: FontWeight.w600,
          fontFeatures: const [FontFeature.tabularFigures()],
        ),
      ),
    );
  }
}
