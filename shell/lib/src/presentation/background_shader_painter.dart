import 'dart:ui' show Canvas, FragmentShader, Offset, Paint, Size;

import 'package:material_ui/material_ui.dart' show CustomPainter;

/// Custom painter for fragment shader background animation.
class BackgroundShaderPainter extends CustomPainter {
  /// Creates a new [BackgroundShaderPainter].
  const new(FragmentShader fragmentShader, this.time) : shader = fragmentShader;

  /// The active fragment shader.
  final FragmentShader shader;

  /// Current animation timestamp in seconds.
  final double time;

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint();
    shader
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setFloat(2, time);
    paint.shader = shader;
    canvas.drawRect(Offset.zero & size, paint);
  }

  @override
  bool shouldRepaint(covariant BackgroundShaderPainter oldDelegate) =>
      oldDelegate.time != time;
}
