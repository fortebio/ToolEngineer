import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../data/machine_info_content.dart';
import '../theme/app_theme.dart';
import '../util/i18n.dart';
import '../widgets/app_search_box.dart';

/// Mục **Thông tin máy** (tab Chăm sóc KH): tài liệu cho nhân viên CSKH hiểu
/// sản phẩm Forte Rapid+ và cách vận hành — mỗi mục một thẻ, có chip "nhảy tới"
/// và ô tìm. Mục đánh dấu `advanced` (kỹ thuật) thu gọn mặc định.
///
/// Nội dung nằm ở `data/machine_info_content.dart` (tiếng Việt, một chỗ);
/// màn này chỉ là cách trình bày. Chỉ HTTP/không phần cứng → chạy cả web.
class SupportInfoScreen extends StatefulWidget {
  const SupportInfoScreen({super.key});

  @override
  State<SupportInfoScreen> createState() => _SupportInfoScreenState();
}

class _SupportInfoScreenState extends State<SupportInfoScreen> {
  String _q = '';
  final ScrollController _scroll = ScrollController();

  /// Key theo mục để chip "nhảy tới" cuộn tới đúng thẻ. Danh sách dựng bằng
  /// `SingleChildScrollView` + `Column` (KHÔNG `ListView.builder`): ~10 thẻ chữ
  /// là rẻ, và `ensureVisible` cần thẻ đích ĐÃ được dựng.
  final Map<String, GlobalKey> _keys = {
    for (final s in kMachineInfoSections) s.id: GlobalKey(),
  };

  /// Mục kỹ thuật đang được mở ra (mặc định thu gọn).
  final Set<String> _opened = {};

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  bool _hit(String s) => s.toLowerCase().contains(_q.trim().toLowerCase());

  /// Mục + các dòng còn lại sau khi lọc. Khớp TIÊU ĐỀ/tóm tắt thì giữ nguyên
  /// cả mục (người gõ "WiFi" muốn cả mục WiFi, không phải vài dòng lẻ); không
  /// thì chỉ giữ dòng khớp.
  List<(InfoSection, List<InfoItem>)> get _shown {
    if (_q.trim().isEmpty) {
      return [for (final s in kMachineInfoSections) (s, s.items)];
    }
    final out = <(InfoSection, List<InfoItem>)>[];
    for (final s in kMachineInfoSections) {
      if (_hit(s.title) || _hit(s.intro)) {
        out.add((s, s.items));
        continue;
      }
      final items = [
        for (final i in s.items)
          if (_hit(i.label) || _hit(i.text)) i
      ];
      if (items.isNotEmpty) out.add((s, items));
    }
    return out;
  }

  void _jump(String id) {
    final ctx = _keys[id]?.currentContext;
    if (ctx == null) return;
    Scrollable.ensureVisible(
      ctx,
      duration: const Duration(milliseconds: 380),
      curve: Curves.easeOutCubic,
      alignment: 0.02,
    );
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final searching = _q.trim().isNotEmpty;
    final shown = _shown;
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(4, 4, 4, 8),
            child: AppSearchBox(
              hint: tr('sp.search'),
              count: searching
                  ? '${shown.length}/${kMachineInfoSections.length}'
                  : null,
              onChanged: (v) => setState(() => _q = v),
            ),
          ),
          // Chip "nhảy tới" — ẩn khi đang lọc (lúc đó danh sách đã ngắn).
          if (!searching)
            Padding(
              padding: const EdgeInsets.fromLTRB(4, 0, 4, 10),
              child: Wrap(
                spacing: 6,
                runSpacing: 6,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  Text(tr('sp.jumpTo'),
                      style:
                          TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant)),
                  for (final s in kMachineInfoSections)
                    if (s.jump.isNotEmpty)
                      ActionChip(
                        visualDensity: VisualDensity.compact,
                        avatar: Icon(s.icon, size: 15),
                        label: Text(s.jump),
                        onPressed: () => _jump(s.id),
                      ),
                ],
              ),
            ),
          Expanded(
            child: shown.isEmpty
                ? Center(
                    child: Text(tr('sp.noMatch'),
                        style: TextStyle(color: cs.onSurfaceVariant)))
                : SingleChildScrollView(
                    controller: _scroll,
                    padding: const EdgeInsets.fromLTRB(4, 0, 4, 16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        if (!searching) _IntroBanner(text: tr('sp.introInfo')),
                        for (var i = 0; i < shown.length; i++)
                          Padding(
                            key: _keys[shown[i].$1.id],
                            padding: const EdgeInsets.only(bottom: 14),
                            child: AppFadeIn(
                              index: i,
                              child: _SectionCard(
                                section: shown[i].$1,
                                items: shown[i].$2,
                                // Mục kỹ thuật: thu gọn trừ khi người dùng mở
                                // hoặc đang tìm và khớp (lọc ra rồi mà vẫn giấu
                                // là vô lý).
                                collapsed: shown[i].$1.advanced &&
                                    !searching &&
                                    !_opened.contains(shown[i].$1.id),
                                onToggle: shown[i].$1.advanced
                                    ? () => setState(() {
                                          final id = shown[i].$1.id;
                                          if (!_opened.remove(id)) {
                                            _opened.add(id);
                                          }
                                        })
                                    : null,
                              ),
                            ),
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

/// Dải mở đầu: nói rõ tài liệu này viết cho ai, đọc thế nào.
class _IntroBanner extends StatelessWidget {
  final String text;
  const _IntroBanner({required this.text});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
      decoration: BoxDecoration(
        color: cs.primaryContainer,
        borderRadius: BorderRadius.circular(AppRadius.base),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(Icons.lightbulb_outline, size: 20, color: cs.onPrimaryContainer),
          const SizedBox(width: 10),
          Expanded(
            child: Text(text,
                style: TextStyle(
                    fontSize: 13.5, height: 1.45, color: cs.onPrimaryContainer)),
          ),
        ],
      ),
    );
  }
}

class _SectionCard extends StatelessWidget {
  final InfoSection section;
  final List<InfoItem> items;
  final bool collapsed;
  final VoidCallback? onToggle;
  const _SectionCard({
    required this.section,
    required this.items,
    this.collapsed = false,
    this.onToggle,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final mobile = isMobileWidth(context);
    final advanced = section.advanced;
    return AppCard(
      padding: EdgeInsets.all(mobile ? 16 : 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                width: 40,
                height: 40,
                decoration: BoxDecoration(
                  // Mục kỹ thuật dùng nền trầm để đọc ra "khác loại" ngay từ icon.
                  color: advanced
                      ? AppSemantic.of(context).surfaceSunken
                      : cs.primaryContainer,
                  borderRadius: BorderRadius.circular(AppRadius.base),
                ),
                child: Icon(section.icon,
                    size: 22,
                    color: advanced
                        ? cs.onSurfaceVariant
                        : cs.onPrimaryContainer),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Wrap(
                      spacing: 8,
                      crossAxisAlignment: WrapCrossAlignment.center,
                      children: [
                        Text(section.title,
                            style: TextStyle(
                                fontSize: 16.5,
                                fontWeight: FontWeight.w700,
                                color: cs.onSurface)),
                        if (advanced)
                          Chip(
                            visualDensity: VisualDensity.compact,
                            padding: EdgeInsets.zero,
                            labelStyle: TextStyle(
                                fontSize: 11, color: cs.onSurfaceVariant),
                            label: Text(tr('sp.advancedTag')),
                            side: BorderSide(color: cs.outlineVariant),
                            backgroundColor: cs.surface,
                          ),
                      ],
                    ),
                    const SizedBox(height: 3),
                    Text(section.intro,
                        style: TextStyle(
                            fontSize: 13, height: 1.4, color: cs.onSurfaceVariant)),
                  ],
                ),
              ),
              if (onToggle != null)
                TextButton.icon(
                  onPressed: onToggle,
                  icon: Icon(collapsed ? Icons.expand_more : Icons.expand_less,
                      size: 18),
                  label: Text(
                      collapsed ? tr('sp.advancedShow') : tr('sp.advancedHide')),
                ),
            ],
          ),
          if (!collapsed) ...[
            const SizedBox(height: 12),
            Divider(height: 1, color: cs.outlineVariant),
            for (final it in items)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(it.label,
                        style: TextStyle(
                            fontSize: 13.5,
                            fontWeight: FontWeight.w600,
                            color: cs.onSurface)),
                    const SizedBox(height: 2),
                    // SelectableText: nhân viên copy được đoạn hướng dẫn dán vào
                    // chat với khách.
                    SelectableText(it.text,
                        style: TextStyle(
                            fontSize: 13.5, height: 1.45, color: cs.onSurface)),
                  ],
                ),
              ),
            if (section.extra == InfoExtra.errorCodeDecoder) ...[
              const SizedBox(height: 14),
              const _ErrorCodeDecoder(),
            ],
          ],
        ],
      ),
    );
  }
}

/// Ô nhập mã lỗi 4 chữ số trên màn TFT → tách module / loại / bước / slot
/// ([decodeErrorCode]). Chỉ tách số — bảng ý nghĩa nằm ở firmware.
class _ErrorCodeDecoder extends StatefulWidget {
  const _ErrorCodeDecoder();

  @override
  State<_ErrorCodeDecoder> createState() => _ErrorCodeDecoderState();
}

class _ErrorCodeDecoderState extends State<_ErrorCodeDecoder> {
  String _code = '';

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final d = decodeErrorCode(_code);
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppSemantic.of(context).surfaceSunken,
        borderRadius: BorderRadius.circular(AppRadius.base),
      ),
      child: Wrap(
        spacing: 12,
        runSpacing: 10,
        crossAxisAlignment: WrapCrossAlignment.center,
        children: [
          SizedBox(
            width: 150,
            child: TextField(
              decoration: InputDecoration(
                labelText: tr('sp.errCodeLabel'),
                hintText: '1234',
                isDense: true,
              ),
              keyboardType: TextInputType.number,
              inputFormatters: [
                FilteringTextInputFormatter.digitsOnly,
                LengthLimitingTextInputFormatter(4),
              ],
              style: const TextStyle(fontFamily: 'JetBrains Mono', fontSize: 15),
              onChanged: (v) => setState(() => _code = v),
            ),
          ),
          if (d != null) ...[
            _Part(tr('sp.errModule'), d.module),
            _Part(tr('sp.errType'), d.type),
            _Part(tr('sp.errStep'), d.step),
            _Part(tr('sp.errSlot'), d.slot),
          ] else
            Text(
              _code.isEmpty ? tr('sp.errCodeHint') : tr('sp.errCodeInvalid'),
              style: TextStyle(fontSize: 12.5, color: cs.onSurfaceVariant),
            ),
        ],
      ),
    );
  }
}

class _Part extends StatelessWidget {
  final String label;
  final int value;
  const _Part(this.label, this.value);

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Chip(
      backgroundColor: cs.surface,
      side: BorderSide(color: cs.outlineVariant),
      label: Text.rich(TextSpan(children: [
        TextSpan(text: '$label ', style: TextStyle(color: cs.onSurfaceVariant)),
        TextSpan(
            text: '$value',
            style: const TextStyle(
                fontFamily: 'JetBrains Mono', fontWeight: FontWeight.w700)),
      ])),
    );
  }
}
