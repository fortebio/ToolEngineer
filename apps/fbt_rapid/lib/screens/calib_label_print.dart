/// **In nhãn QR cho bộ ống chuẩn** — hộp thoại xem trước + lệnh in.
///
/// Dùng ở hai chỗ trong tab Hiệu chuẩn: nút "Nhãn QR" trên từng bộ
/// ([showCalibLabelDialog]) và nút "In nhãn" của cả danh sách đang lọc
/// ([showCalibLabelSheetDialog]). Nội dung nhãn + mã QR dựng ở
/// `services/calib_label.dart`; mở trang in ở `util/printable.dart`.
///
/// Quy tắc chung của màn này: **chỉ bộ ĐẠT còn dùng được mới in được**. Nút
/// nhãn không hiện với bộ FAIL / đã huỷ / đã dùng hết, và nếu danh sách lọc ra
/// không còn bộ nào in được thì nói thẳng lý do thay vì mở tờ giấy trắng.
library;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/calib_api.dart';
import '../services/calib_label.dart';
import '../services/session_store.dart';
import '../theme/app_theme.dart';
import '../util/printable.dart';
import '../widgets/qr_view.dart';
import 'calib_screen.dart' show calibFmtTs, calibNum, calibSnack, calibTubesLabel;

/// Dựng tờ nhãn rồi mở trình duyệt để in. Báo kết quả bằng snack.
Future<void> printCalibLabelSheet(
  BuildContext context,
  List<CalibSet> sets, {
  int copies = 1,
}) async {
  if (sets.isEmpty) {
    calibSnack(context, 'Không có bộ ống ĐẠT nào để in nhãn.', error: true);
    return;
  }
  final html = calibLabelSheetHtml(sets,
      printedBy: SessionStore.username, copies: copies);
  final n = sets.length * copies;
  try {
    final r = await openHtmlForPrint(html, baseName: 'nhan_ong_chuan');
    if (!context.mounted) return;
    if (r.downloaded) {
      // Pop-up bị chặn → đã tải file xuống. Nói rõ việc phải làm tiếp, đừng để
      // người dùng tưởng máy in hỏng.
      calibSnack(
          context,
          'Trình duyệt chặn cửa sổ in nên đã TẢI ${r.path} xuống Downloads — '
          'mở file đó rồi bấm Ctrl+P.');
      return;
    }
    calibSnack(
      context,
      r.path.isEmpty
          ? 'Đã mở tab in $n nhãn — chọn máy in trong hộp thoại.'
          : 'Đã mở $n nhãn để in: ${r.path}',
    );
  } catch (e) {
    if (!context.mounted) return;
    calibSnack(context, 'Không mở được bản in: $e', error: true);
  }
}

/// Xem trước nhãn của MỘT bộ: mã QR thật (cùng bộ mã hoá với bản in) + các dòng
/// sẽ in + nội dung QR dạng chữ (chép được để dán vào sổ/tin nhắn).
Future<void> showCalibLabelDialog(BuildContext context, CalibSet set) async {
  final payload = calibQrPayload(set);
  await showDialog<void>(
    context: context,
    builder: (ctx) {
      final cs = Theme.of(ctx).colorScheme;
      return AlertDialog(
        title: Text('Nhãn QR — ${set.id}'),
        content: SizedBox(
          width: 420,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Center(child: QrView(data: payload, size: 168)),
                const SizedBox(height: 14),
                _line('Ống', calibTubesLabel(set.tubes), mono: true),
                _line('Đường chuẩn',
                    'slope ${calibNum(set.slope)} · R² ${calibNum(set.r2, 5)} · '
                        'LOD ${set.lod == null ? '—' : '${calibNum(set.lod, 1)} nM'}'),
                _line('Lô', '${set.batch} · ngưỡng ${set.limitsVer}'),
                _line('Hạn dùng', set.expiresAt.isEmpty ? '—' : set.expiresAt),
                _line('Đóng gói',
                    '${calibFmtTs(set.createdAt)}${set.createdBy.isEmpty ? '' : ' bởi ${set.createdBy}'}'),
                const SizedBox(height: 12),
                Text('Nội dung mã QR', style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
                const SizedBox(height: 4),
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(8),
                  decoration: BoxDecoration(
                    color: cs.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(AppRadius.base),
                  ),
                  child: SelectableText(payload,
                      style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 11.5)),
                ),
              ],
            ),
          ),
        ),
        actions: [
          TextButton.icon(
            onPressed: () async {
              await Clipboard.setData(ClipboardData(text: payload));
              if (!ctx.mounted) return;
              calibSnack(ctx, 'Đã chép nội dung mã QR.');
            },
            icon: const Icon(Icons.copy_outlined, size: 18),
            label: const Text('Chép mã'),
          ),
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Đóng')),
          FilledButton.icon(
            onPressed: () {
              Navigator.pop(ctx);
              printCalibLabelSheet(context, [set]);
            },
            icon: const Icon(Icons.print_outlined, size: 18),
            label: const Text('In nhãn'),
          ),
        ],
      );
    },
  );
}

/// In nhãn cho CẢ danh sách đang xem: nói rõ in bao nhiêu bộ, bỏ qua bao nhiêu
/// và vì sao, cho chọn số bản mỗi nhãn.
Future<void> showCalibLabelSheetDialog(
  BuildContext context,
  List<CalibSet> all,
) async {
  final ok = calibPrintableSets(all);
  final skipped = all.length - ok.length;
  var copies = 1;
  await showDialog<void>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setState) {
        final cs = Theme.of(ctx).colorScheme;
        return AlertDialog(
          title: const Text('In nhãn QR'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(ok.isEmpty
                  ? 'Danh sách đang xem không có bộ nào in được.'
                  : 'In nhãn cho ${ok.length} bộ ĐẠT trong danh sách đang xem '
                      '(2 nhãn/hàng, 10 nhãn/tờ A4).'),
              if (skipped > 0) ...[
                const SizedBox(height: 8),
                Text(
                  'Bỏ qua $skipped bộ: FAIL, đã huỷ hoặc đã dùng hết — '
                  'nhãn chỉ in cho bộ còn dùng được.',
                  style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant),
                ),
              ],
              if (ok.isNotEmpty) ...[
                const SizedBox(height: 14),
                Row(children: [
                  const Text('Số bản mỗi nhãn'),
                  const SizedBox(width: 12),
                  for (final n in const [1, 2, 3])
                    Padding(
                      padding: const EdgeInsets.only(right: 6),
                      child: ChoiceChip(
                        label: Text('$n'),
                        selected: copies == n,
                        onSelected: (_) => setState(() => copies = n),
                      ),
                    ),
                ]),
                const SizedBox(height: 6),
                Text('Một bản dán lên túi, bản thứ hai cho sổ giao nhận.',
                    style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
              ],
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Đóng')),
            FilledButton.icon(
              onPressed: ok.isEmpty
                  ? null
                  : () {
                      Navigator.pop(ctx);
                      printCalibLabelSheet(context, ok, copies: copies);
                    },
              icon: const Icon(Icons.print_outlined, size: 18),
              label: Text('In ${ok.length * copies} nhãn'),
            ),
          ],
        );
      },
    ),
  );
}

Widget _line(String label, String value, {bool mono = false}) => Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        SizedBox(
          width: 92,
          child: Text(label, style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
        ),
        Expanded(
          child: Text(value,
              style: TextStyle(
                  fontSize: 12.5, fontFamily: mono ? 'JetBrains Mono' : null)),
        ),
      ]),
    );
