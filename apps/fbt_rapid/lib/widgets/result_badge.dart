import 'package:flutter/material.dart';

import '../models/test_result.dart';

/// Chip màu hiển thị phân loại kết quả.
class ResultBadge extends StatelessWidget {
  final Classification classification;
  final bool dense;

  const ResultBadge({
    super.key,
    required this.classification,
    this.dense = false,
  });

  @override
  Widget build(BuildContext context) {
    final c = classification.color;
    return Container(
      padding: EdgeInsets.symmetric(
        horizontal: dense ? 6 : 10,
        vertical: dense ? 2 : 4,
      ),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: c),
      ),
      child: Text(
        classification.label,
        style: TextStyle(
          color: c,
          fontWeight: FontWeight.w600,
          fontSize: dense ? 11 : 13,
        ),
      ),
    );
  }
}
