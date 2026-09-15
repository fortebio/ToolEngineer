import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';

/// Chụp một RepaintBoundary (theo [key]) thành PNG bytes.
/// Trả về null nếu chưa render (vd. widget đang bị cuộn khuất).
Future<Uint8List?> captureBoundaryPng(GlobalKey key,
    {double pixelRatio = 2.0}) async {
  final ctx = key.currentContext;
  if (ctx == null) return null;
  final obj = ctx.findRenderObject();
  if (obj is! RenderRepaintBoundary) return null;
  final image = await obj.toImage(pixelRatio: pixelRatio);
  try {
    final bd = await image.toByteData(format: ui.ImageByteFormat.png);
    return bd?.buffer.asUint8List();
  } finally {
    image.dispose();
  }
}
