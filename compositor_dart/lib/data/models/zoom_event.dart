import 'package:flutter/foundation.dart';

@immutable
class ZoomScrollEvent {
  const new({required this.delta, required this.x, required this.y});

  factory fromJson(Map<String, dynamic> json) {
    return ZoomScrollEvent(
      delta: (json['delta'] as num?)?.toDouble() ?? 0.0,
      x: (json['x'] as num?)?.toDouble() ?? 0.0,
      y: (json['y'] as num?)?.toDouble() ?? 0.0,
    );
  }

  final double delta;
  final double x;
  final double y;
}

@immutable
class ZoomKeyEvent {
  const new({required this.action, required this.x, required this.y});

  factory fromJson(Map<String, dynamic> json) {
    return ZoomKeyEvent(
      action: (json['action'] as num?)?.toInt() ?? 0,
      x: (json['x'] as num?)?.toDouble() ?? 0.0,
      y: (json['y'] as num?)?.toDouble() ?? 0.0,
    );
  }

  /// 1 = zoom in, -1 = zoom out, 0 = reset zoom
  final int action;
  final double x;
  final double y;
}
