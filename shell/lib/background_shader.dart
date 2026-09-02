import 'dart:async';
import 'dart:ui' show FragmentProgram, FragmentShader;

import 'package:flutter/scheduler.dart' show Ticker;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Center,
        CircularProgressIndicator,
        CustomPaint,
        SingleTickerProviderStateMixin,
        State,
        StatefulWidget,
        Widget;
import 'package:shell/background_shader_painter.dart'
    show BackgroundShaderPainter;

class BackgroundShader extends StatefulWidget {
  const BackgroundShader({super.key});

  @override
  State<BackgroundShader> createState() => _BackgroundShaderState();
}

class _BackgroundShaderState extends State<BackgroundShader>
    with SingleTickerProviderStateMixin {
  late final Ticker _ticker;
  double delta = 0;
  FragmentShader? shader;
  Duration _lastElapsed = Duration.zero;

  Future<void> loadMyShader() async {
    final program = await FragmentProgram.fromAsset('shaders/shader.frag');
    if (!mounted) return;
    shader = program.fragmentShader();
    setState(() {});
    if (!_ticker.isActive) {
      _ticker.start();
    }
  }

  @override
  void initState() {
    super.initState();
    _ticker = createTicker((elapsed) {
      final dt = (elapsed - _lastElapsed).inMicroseconds / 1000000.0;
      _lastElapsed = elapsed;
      setState(() {
        delta += dt;
      });
    });
    unawaited(loadMyShader());
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (shader == null) {
      return const Center(child: CircularProgressIndicator());
    } else {
      return CustomPaint(painter: BackgroundShaderPainter(shader!, delta));
    }
  }
}
