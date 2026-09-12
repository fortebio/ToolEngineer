import 'dart:async';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../models/server_status.dart';
import '../services/app_settings.dart';
import '../services/fbt_api.dart';
import '../theme/app_theme.dart';
import '../util/format.dart';
import '../util/i18n.dart';
import '../widgets/app_tab_scaffold.dart';

/// Tab **Giám sát** (root) — tình trạng Engineer Server: dịch vụ, tài nguyên
/// box, luồng dữ liệu nhận được.
///
/// Chỉ nói chuyện HTTP với `GET /monitor` nên chạy được cả trên web, không cần
/// bản `_web` riêng.
///
/// Gác **hai tầng**, và phải hiểu đúng từng tầng:
///  * **Server** — `/monitor` gác bằng `ota_admin`, tức chỉ token NHÂN SỰ
///    (`auth.api_token_for` phát cho root/admin lúc đăng nhập). Token thiết bị
///    KHÔNG đọc được, và đó mới là điều quan trọng: token đó nằm trong 4 KB đầu
///    mọi file `.bin`.
///  * **App (file này)** — ẩn tab với admin, chỉ root thấy. Server không phân
///    biệt được root với admin (chung một token), nên tầng này chỉ là quy ước
///    giao diện: đừng thêm dữ liệu nhạy cảm vào `/monitor` rồi tưởng "chỉ root"
///    che được nó.
class MonitorScreen extends StatefulWidget {
  final AppSettings settings;
  const MonitorScreen({super.key, required this.settings});

  @override
  State<MonitorScreen> createState() => _MonitorScreenState();
}

class _MonitorScreenState extends State<MonitorScreen> {
  late final FbtApi _api = FbtApi(
    widget.settings.cloudUrlFor(CloudSource.engineer),
    headers: widget.settings.cloudHeadersFor(CloudSource.engineer),
  );

  ServerStatus? _st;

  /// Giữ RIÊNG phần luồng dữ liệu: vòng realtime gọi `flow=0` nên `_st.flow` là
  /// null ở hầu hết các lượt — gán thẳng vào là biểu đồ 7 ngày nhấp nháy mất.
  FlowInfo? _flow;
  String? _error;
  bool _loading = false;
  DateTime? _at;
  Timer? _timer;
  bool _visible = false;

  /// Lịch sử CPU/RAM để vẽ. Chỉ nằm trong RAM của màn: server không lưu chuỗi
  /// thời gian nào, và không cần — chuyển tab rồi quay lại thì vẽ lại từ đầu.
  /// ponytail: vòng đệm 60 mẫu (5 phút @ 5s). Cần xem xa hơn thì phải để server
  /// ghi chuỗi (bảng + tiến trình lấy mẫu nền) — chưa ai cần.
  final List<_Sample> _samples = [];
  static const int _maxSamples = 60;
  static const Duration _period = Duration(seconds: 5);

  // KHÔNG nạp trong `initState`: màn được dựng ngay lúc root đăng nhập (xem
  // `didChangeDependencies`), nạp ở đó là một request cho trang chưa ai mở.

  /// Nạp VÀ chạy vòng lấy mẫu CHỈ KHI TAB ĐANG ĐƯỢC XEM.
  ///
  /// `HomeShell` xếp mọi tab vào `IndexedStack` nên màn này được DỰNG ngay lúc
  /// root đăng nhập, dù chưa ai bấm vào — hẹn giờ vô điều kiện ở đây là gọi
  /// mạng suốt phiên cho một trang không ai xem. `HomeShell` bọc mỗi tab trong
  /// `TickerMode(enabled: i == _index)`, nên cờ đó chính là "đang xem".
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final v = TickerMode.valuesOf(context).enabled;
    if (v == _visible) return;
    _visible = v;
    _timer?.cancel();
    if (v) {
      // Hoãn một microtask: `didChangeDependencies` chạy TRONG pha build, mà
      // `_load` gọi `setState` ngay trước await đầu tiên → gọi thẳng là
      // "setState() called during build".
      Future.microtask(() {
        if (mounted) _load(); // vào tab là có số ngay, không đợi hết chu kỳ
      });
      _timer = Timer.periodic(_period, (_) => _load(light: true));
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  /// [light] = vòng vẽ realtime: bỏ phần đếm phiên đo (3 truy vấn quét bảng) và
  /// KHÔNG bật spinner — nhấp nháy 5 giây một lần thì đọc còn khó hơn không có.
  Future<void> _load({bool light = false}) async {
    if (_loading) return; // chống chồng request khi mạng chậm hơn chu kỳ
    _loading = true;
    if (!light) setState(() {});
    try {
      final st = await _api.monitor(flow: !light);
      if (!mounted) return;
      setState(() {
        _st = st;
        if (st.flow != null) _flow = st.flow;
        _error = null;
        _at = DateTime.now();
        _samples.add(_Sample(st.cpu?.load1Pct, st.mem?.usedPct));
        if (_samples.length > _maxSamples) _samples.removeAt(0);
      });
    } catch (e) {
      // KHÔNG nuốt lỗi: server chưa deploy route trả 405, im lặng thì màn trống
      // trơn và trông y hệt "app chưa có tính năng".
      if (!mounted) return;
      setState(() => _error = '$e');
    } finally {
      _loading = false;
      if (mounted && !light) setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    return AppTabScaffold(
      title: tr('nav.monitor'),
      subtitle: _at == null
          ? tr('mon.subtitle')
          : tr('mon.updatedAt').replaceFirst('{t}', formatTime(_at!)),
      index: 0,
      onChanged: (_) {},
      tabs: [
        AppTab(
          icon: Icons.monitor_heart_outlined,
          label: tr('nav.monitor'),
          page: _body(context),
        ),
      ],
      actions: [
        if (_loading)
          const Padding(
            padding: EdgeInsets.only(right: 4),
            child: SizedBox(
                width: 16,
                height: 16,
                child: CircularProgressIndicator(strokeWidth: 2)),
          ),
        IconButton(
          tooltip: tr('mon.refresh'),
          onPressed: _loading ? null : _load,
          icon: const Icon(Icons.refresh),
        ),
      ],
    );
  }

  Widget _body(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final st = _st;

    if (st == null && _error != null) return _errorBox(context, _error!);
    if (st == null) {
      return const Center(child: CircularProgressIndicator());
    }

    final pad = isMobileWidth(context) ? 16.0 : 28.0;
    return ListView(
      padding: EdgeInsets.fromLTRB(pad, 4, pad, 24),
      children: [
        // Lỗi ở lần Làm mới SAU vẫn phải hiện, nhưng không được xoá số liệu cũ:
        // "số liệu lần trước + báo mất kết nối" hữu ích hơn một màn trắng.
        if (_error != null) ...[
          _errorBox(context, _error!, compact: true),
          const SizedBox(height: 12),
        ],
        _healthBanner(context, st),
        const SizedBox(height: 12),
        // Khổ rộng: tài nguyên (thanh) và trạng thái (chấm) nằm cạnh nhau — hai
        // loại thông tin khác nhau, đừng trộn vào một hàng ô giống hệt nhau.
        if (isMobileWidth(context)) ...[
          _resourceCard(context, st),
          const SizedBox(height: 12),
          _serviceCard(context, st),
        ] else
          IntrinsicHeight(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(flex: 3, child: _resourceCard(context, st)),
                const SizedBox(width: 12),
                Expanded(flex: 2, child: _serviceCard(context, st)),
              ],
            ),
          ),
        const SizedBox(height: 12),
        _liveCard(context),
        const SizedBox(height: 12),
        if (_flow != null)
          _flowCard(context, _flow!)
        else
          AppCard(
            child: Text(tr('mon.dbFail'),
                style: TextStyle(color: cs.error, fontWeight: FontWeight.w600)),
          ),
      ],
    );
  }

  Widget _errorBox(BuildContext context, String msg, {bool compact = false}) {
    final cs = Theme.of(context).colorScheme;
    final box = AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Icon(Icons.error_outline, color: cs.error, size: 20),
            const SizedBox(width: 8),
            Expanded(
              child: Text(tr('mon.error'),
                  style: TextStyle(
                      color: cs.error, fontWeight: FontWeight.w600)),
            ),
          ]),
          const SizedBox(height: 8),
          Text(msg, style: TextStyle(fontSize: 13, color: cs.onSurfaceVariant)),
          if (msg.contains('405')) ...[
            const SizedBox(height: 8),
            Text(tr('mon.noRoute'),
                style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
          ],
        ],
      ),
    );
    return compact ? box : Center(child: SizedBox(width: 560, child: box));
  }

  /// Băng PHÁN QUYẾT ở đầu trang: một dòng trả lời "server có ổn không".
  ///
  /// Bản trước bày 5 ô số ngang hàng và bắt người xem tự kết luận — đó là việc
  /// của trang giám sát, không phải của người đọc nó. Ở đây lấy mức XẤU NHẤT
  /// trong các phép kiểm rồi NÓI RA LÝ DO; xanh nghĩa là đã kiểm hết, không
  /// phải "chưa thấy gì".
  Widget _healthBanner(BuildContext context, ServerStatus st) {
    final cs = Theme.of(context).colorScheme;
    final (color, icon, text) = _health(context, st);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.10),
        borderRadius: BorderRadius.circular(AppRadius.card),
        border: Border.all(color: color.withValues(alpha: 0.35)),
      ),
      child: Row(children: [
        Icon(icon, color: color, size: 22),
        const SizedBox(width: 12),
        Expanded(
          child: Text(text,
              style: TextStyle(
                  fontSize: 15, fontWeight: FontWeight.w700, color: color)),
        ),
        if (st.uptime != null)
          Text(
            formatUptime(st.uptime!),
            style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant),
          ),
      ]),
    );
  }

  /// Mức xấu nhất trong mọi phép kiểm + lý do. Thứ tự ưu tiên là thứ tự THIỆT
  /// HẠI: DB chết = không ghi được kết quả đo; đĩa/RAM/CPU đầy = sắp không ghi
  /// được; máy im lặng = dữ liệu đã mất ở đâu đó rồi nhưng server vẫn sống.
  (Color, IconData, String) _health(BuildContext context, ServerStatus st) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);

    if (!st.dbOk) {
      return (cs.error, Icons.error_outline, tr('mon.healthDb'));
    }

    final checks = <(String, double?)>[
      (tr('mon.cpu'), st.cpu?.load1Pct),
      (tr('mon.mem'), st.mem?.usedPct),
      (tr('mon.disk'), st.disk?.usedPct),
    ];
    (String, double)? worst;
    for (final (name, pct) in checks) {
      if (pct == null) continue;
      if (worst == null || pct > worst.$2) worst = (name, pct);
    }
    if (worst != null && worst.$2 >= 90) {
      return (
        cs.error,
        Icons.error_outline,
        tr('mon.healthBad')
            .replaceFirst('{what}', worst.$1)
            .replaceFirst('{n}', worst.$2.toStringAsFixed(0))
      );
    }
    if (worst != null && worst.$2 >= 75) {
      return (
        sem.warning,
        Icons.warning_amber_outlined,
        tr('mon.healthWarn')
            .replaceFirst('{what}', worst.$1)
            .replaceFirst('{n}', worst.$2.toStringAsFixed(0))
      );
    }
    return (sem.success, Icons.check_circle_outline, tr('mon.healthOk'));
  }

  /// Ngưỡng màu dùng CHUNG cho CPU/RAM/đĩa: <75% bình thường, 75–90% cảnh báo,
  /// ≥90% đỏ. Một ngưỡng cho cả ba để người xem không phải nhớ ba thang.
  Color _pctColor(BuildContext context, double? pct) {
    final sem = AppSemantic.of(context);
    final cs = Theme.of(context).colorScheme;
    if (pct == null) return cs.onSurfaceVariant;
    if (pct >= 90) return cs.error;
    if (pct >= 75) return sem.warning;
    return sem.success;
  }

  /// Ngưỡng nhiệt RIÊNG, không dùng chung với thang phần trăm: 57 °C là bình
  /// thường với x86 nhưng 57% đĩa cũng bình thường — trùng con số, khác ý nghĩa.
  /// <70 ổn · 70–85 chú ý (kiểm quạt/bụi) · ≥85 nóng thật, sắp hạ xung.
  Color _tempColor(BuildContext context, double c) {
    final sem = AppSemantic.of(context);
    final cs = Theme.of(context).colorScheme;
    if (c >= 85) return cs.error;
    if (c >= 70) return sem.warning;
    return sem.success;
  }

  /// Thẻ tài nguyên: mỗi dòng một THANH ĐẦY DẦN thay vì một con số trần. Thanh
  /// trả lời "còn bao nhiêu" bằng hình, con số chỉ để đọc chính xác.
  Widget _resourceCard(BuildContext context, ServerStatus st) {
    final rows = <Widget>[
      if (st.cpu != null)
        _usageRow(
          context,
          icon: Icons.memory_outlined,
          label: tr('mon.cpu'),
          pct: st.cpu!.load1Pct,
          value: '${st.cpu!.load1}',
          hint: tr('mon.cpuLoad').replaceFirst('{n}', '${st.cpu!.cores}'),
        ),
      if (st.mem != null)
        _usageRow(
          context,
          icon: Icons.sd_card_outlined,
          label: tr('mon.mem'),
          pct: st.mem!.usedPct,
          value: st.mem!.usedPct == null
              ? '—'
              : '${st.mem!.usedPct!.toStringAsFixed(0)}%',
          hint: tr('mon.freeOf')
              .replaceFirst('{free}', formatBytes(st.mem!.free))
              .replaceFirst('{total}', formatBytes(st.mem!.total)),
        ),
      if (st.disk != null)
        _usageRow(
          context,
          icon: Icons.save_outlined,
          label: tr('mon.disk'),
          pct: st.disk!.usedPct,
          value: st.disk!.usedPct == null
              ? '—'
              : '${st.disk!.usedPct!.toStringAsFixed(0)}%',
          hint: [
            tr('mon.freeOf')
                .replaceFirst('{free}', formatBytes(st.disk!.free))
                .replaceFirst('{total}', formatBytes(st.disk!.total)),
            if (st.disk!.daysLeft != null)
              tr('mon.fullIn')
                  .replaceFirst('{t}', formatDaysLeft(st.disk!.daysLeft!)),
          ].join(' · '),
        ),
      if (st.temp != null)
        _usageRow(
          context,
          icon: Icons.thermostat_outlined,
          label: tr('mon.temp'),
          // Thang 0–100 °C dùng luôn làm tỉ lệ thanh: box x86 chạy 50–60 °C
          // bình thường, tới 100 là chip tự hạ xung.
          pct: st.temp!.c,
          value: '${st.temp!.c.toStringAsFixed(0)}°C',
          hint: tr('mon.tempSensor').replaceFirst('{s}', st.temp!.sensor),
          color: _tempColor(context, st.temp!.c),
        ),
    ];
    if (rows.isEmpty) return const SizedBox.shrink();

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(tr('mon.resources'),
              style:
                  const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
          const SizedBox(height: 14),
          for (var i = 0; i < rows.length; i++) ...[
            if (i > 0) const SizedBox(height: 16),
            rows[i],
          ],
        ],
      ),
    );
  }

  Widget _usageRow(
    BuildContext context, {
    required IconData icon,
    required String label,
    required double? pct,
    required String value,
    required String hint,
    // Nhiệt độ dùng thang khác hẳn CPU/RAM/đĩa (70/85 °C, không phải 75/90 %)
    // nên cho phép truyền màu vào thay vì nhét thêm nhánh vào `_pctColor`.
    Color? color,
  }) {
    final cs = Theme.of(context).colorScheme;
    color ??= _pctColor(context, pct);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          Icon(icon, size: 16, color: cs.onSurfaceVariant),
          const SizedBox(width: 6),
          Text(label, style: TextStyle(fontSize: 13, color: cs.onSurface)),
          const Spacer(),
          Text(
            value,
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w700,
              color: color,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ]),
        const SizedBox(height: 6),
        // `LinearProgressIndicator` lo sẵn bo góc + nền + a11y.
        // ponytail: không tự vẽ thanh bằng Container lồng nhau.
        LinearProgressIndicator(
          value: pct == null ? 0 : (pct / 100).clamp(0.0, 1.0),
          minHeight: 8,
          color: color,
          backgroundColor: AppSemantic.of(context).surfaceSunken,
          borderRadius: BorderRadius.circular(4),
        ),
        const SizedBox(height: 4),
        Text(hint,
            style: TextStyle(fontSize: 11.5, color: cs.onSurfaceVariant)),
      ],
    );
  }

  /// Thẻ trạng thái: hai dòng NHỊ PHÂN (chạy / không) — dạng chấm màu, không
  /// phải ô số, vì chúng không có "mức độ" để so.
  Widget _serviceCard(BuildContext context, ServerStatus st) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(tr('mon.status'),
              style:
                  const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
          const SizedBox(height: 12),
          _dotRow(context, sem.success, tr('mon.service'),
              st.startedAt == null ? '' : formatDateTime(st.startedAt!)),
          const SizedBox(height: 10),
          _dotRow(
            context,
            st.dbOk ? sem.success : cs.error,
            tr('mon.db'),
            st.dbOk ? tr('mon.dbOk') : (st.dbError ?? tr('mon.dbFail')),
          ),
        ],
      ),
    );
  }

  Widget _dotRow(
      BuildContext context, Color c, String label, String detail) {
    final cs = Theme.of(context).colorScheme;
    return Row(children: [
      Container(
        width: 9,
        height: 9,
        decoration: BoxDecoration(color: c, shape: BoxShape.circle),
      ),
      const SizedBox(width: 10),
      Text(label, style: TextStyle(fontSize: 13, color: cs.onSurface)),
      const Spacer(),
      Flexible(
        child: Text(detail,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
      ),
    ]);
  }

  /// Biểu đồ CPU/RAM theo thời gian thực — cửa sổ trượt 5 phút.
  Widget _liveCard(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    final cpu = <FlSpot>[];
    final mem = <FlSpot>[];
    for (var i = 0; i < _samples.length; i++) {
      final s = _samples[i];
      if (s.cpu != null) cpu.add(FlSpot(i.toDouble(), s.cpu!));
      if (s.mem != null) mem.add(FlSpot(i.toDouble(), s.mem!));
    }

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Expanded(
              child: Text(tr('mon.live'),
                  style: const TextStyle(
                      fontSize: 15, fontWeight: FontWeight.w700)),
            ),
            _legend(context, sem.info, tr('mon.cpu'), _last?.cpu),
            const SizedBox(width: 12),
            _legend(context, sem.warning, tr('mon.mem'), _last?.mem),
          ]),
          const SizedBox(height: 4),
          Text(
            tr(_visible ? 'mon.liveOn' : 'mon.liveOff'),
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant),
          ),
          const SizedBox(height: 12),
          SizedBox(
            height: 170,
            child: cpu.length + mem.length < 2
                // Một mẫu thì chưa có đường nào để vẽ — nói đang thu thập, đừng
                // bày một khung trống trông như hỏng.
                ? Center(
                    child: Text(tr('mon.liveWait'),
                        style: TextStyle(
                            fontSize: 13, color: cs.onSurfaceVariant)),
                  )
                : LineChart(
                    LineChartData(
                      minY: 0,
                      maxY: 100, // phần trăm — trục cố định để nhìn được xu hướng
                      minX: 0,
                      maxX: (_maxSamples - 1).toDouble(),
                      clipData: const FlClipData.all(),
                      gridData: FlGridData(
                        show: true,
                        drawVerticalLine: false,
                        horizontalInterval: 25,
                        getDrawingHorizontalLine: (v) => FlLine(
                            color: cs.outlineVariant, strokeWidth: 1),
                      ),
                      borderData: FlBorderData(show: false),
                      titlesData: FlTitlesData(
                        topTitles: const AxisTitles(),
                        rightTitles: const AxisTitles(),
                        bottomTitles: const AxisTitles(),
                        leftTitles: AxisTitles(
                          sideTitles: SideTitles(
                            showTitles: true,
                            interval: 25,
                            reservedSize: 34,
                            getTitlesWidget: (v, _) => Text('${v.toInt()}%',
                                style: TextStyle(
                                    fontSize: 10,
                                    color: cs.onSurfaceVariant)),
                          ),
                        ),
                      ),
                      lineTouchData: const LineTouchData(enabled: false),
                      lineBarsData: [
                        _line(cpu, sem.info),
                        _line(mem, sem.warning),
                      ],
                    ),
                    // Tắt animation: mỗi 5 giây thêm một điểm, để mặc định thì
                    // cả đường vẽ lại từ đầu, nhìn như giật.
                    duration: Duration.zero,
                  ),
          ),
        ],
      ),
    );
  }

  _Sample? get _last => _samples.isEmpty ? null : _samples.last;

  LineChartBarData _line(List<FlSpot> spots, Color color) => LineChartBarData(
        spots: spots,
        isCurved: false,
        color: color,
        barWidth: 2,
        dotData: const FlDotData(show: false),
        // Tô nền dưới đường: hai đường mảnh chồng nhau khó tách bằng mắt,
        // mảng màu nhạt thì thấy ngay đường nào đang cao hơn.
        belowBarData: BarAreaData(
            show: true, color: color.withValues(alpha: 0.12)),
      );

  /// Chú giải KÈM giá trị hiện tại — nhìn một chỗ là biết đường nào đang ở đâu,
  /// khỏi rê mắt sang thẻ Tài nguyên.
  Widget _legend(BuildContext context, Color c, String label, double? now) {
    final cs = Theme.of(context).colorScheme;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(width: 10, height: 3, color: c),
        const SizedBox(width: 5),
        Text(label, style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
        if (now != null) ...[
          const SizedBox(width: 4),
          Text(
            '${now.toStringAsFixed(0)}%',
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w700,
              color: c,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ],
      ],
    );
  }

  Widget _flowCard(BuildContext context, FlowInfo f) {
    final cs = Theme.of(context).colorScheme;
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(tr('mon.flow'),
              style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
          const SizedBox(height: 14),
          Wrap(spacing: 28, runSpacing: 12, children: [
            _num(context, tr('mon.last24h'), f.last24h),
            _num(context, tr('mon.last7d'), f.last7d),
            _num(context, tr('mon.total'), f.total),
            _num(context, tr('mon.devices'), f.devices),
            if (f.dbBytes > 0)
              _numText(context, tr('mon.dbSize'), formatBytes(f.dbBytes)),
          ]),
          const SizedBox(height: 18),
          Text(tr('mon.byDay'),
              style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
          const SizedBox(height: 10),
          _bars(context, f),
        ],
      ),
    );
  }

  Widget _numText(BuildContext context, String label, String value) {
    final cs = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(value,
            style: const TextStyle(
                fontSize: 24,
                fontWeight: FontWeight.w700,
                fontFeatures: [FontFeature.tabularFigures()])),
        Text(label, style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
      ],
    );
  }

  Widget _num(BuildContext context, String label, int value) {
    final cs = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text('$value',
            style: const TextStyle(
                fontSize: 24,
                fontWeight: FontWeight.w700,
                fontFeatures: [FontFeature.tabularFigures()])),
        Text(label,
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
      ],
    );
  }

  /// Biểu đồ cột 7 ngày dựng bằng `Container` — KHÔNG kéo `fl_chart` vào đây:
  /// bảy con số thì một hàng thanh cao-thấp đọc xong trong một cái liếc, mà
  /// `fl_chart` còn kéo theo trục/tooltip/animation không ai cần ở đây.
  Widget _bars(BuildContext context, FlowInfo f) {
    final cs = Theme.of(context).colorScheme;
    final sem = AppSemantic.of(context);
    if (f.byDay.isEmpty) {
      return Text('—', style: TextStyle(color: cs.onSurfaceVariant));
    }
    final peak = f.peak;
    return SizedBox(
      height: 92,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          for (final d in f.byDay)
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 3),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [
                    Text('${d.count}',
                        style: TextStyle(
                            fontSize: 11,
                            color: cs.onSurfaceVariant,
                            fontFeatures: const [
                              FontFeature.tabularFigures()
                            ])),
                    const SizedBox(height: 3),
                    // Cột tính theo TỈ LỆ phần chiều cao còn lại, không cộng px
                    // cố định: bản trước là 11+3+54+4+10 ≈ 88 trong hộp 92 — chỉ
                    // cần người dùng chỉnh cỡ chữ hệ thống lên 1.15 là tràn
                    // (sọc vàng-đen). Sàn 4% để ngày 0 phiên vẫn thấy được là
                    // một cột chứ không biến mất — "không có dữ liệu" chính là
                    // thứ cần nhìn thấy.
                    Expanded(
                      child: Align(
                        alignment: Alignment.bottomCenter,
                        child: FractionallySizedBox(
                          heightFactor:
                              peak == 0 ? 0.04 : (d.count / peak).clamp(0.04, 1.0),
                          child: Container(
                            decoration: BoxDecoration(
                              color: d.count == 0
                                  ? cs.outlineVariant
                                  : sem.info.withValues(alpha: 0.85),
                              borderRadius: BorderRadius.circular(4),
                            ),
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      // 'YYYY-MM-DD' → 'DD/MM': trục ngày không cần năm
                      d.day.length >= 10
                          ? '${d.day.substring(8)}/${d.day.substring(5, 7)}'
                          : d.day,
                      style:
                          TextStyle(fontSize: 10, color: cs.onSurfaceVariant),
                    ),
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }

}

/// Một mẫu CPU/RAM. `null` = server không đo được mục đó (không phải Linux).
class _Sample {
  final double? cpu;
  final double? mem;
  const _Sample(this.cpu, this.mem);
}
