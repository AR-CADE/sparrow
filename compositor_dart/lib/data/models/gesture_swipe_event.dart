import 'package:flutter/foundation.dart';

@immutable
class GestureSwipeBeginEvent {
  const GestureSwipeBeginEvent({
    required this.fingers,
    required this.timeMsec,
  });

  factory GestureSwipeBeginEvent.fromJson(Map<String, dynamic> json) {
    return GestureSwipeBeginEvent(
      fingers: (json['fingers'] as num?)?.toInt() ?? 3,
      timeMsec: (json['time_msec'] as num?)?.toInt() ?? 0,
    );
  }

  final int fingers;
  final int timeMsec;
}

@immutable
class GestureSwipeUpdateEvent {
  const GestureSwipeUpdateEvent({
    required this.dx,
    required this.dy,
    required this.timeMsec,
  });

  factory GestureSwipeUpdateEvent.fromJson(Map<String, dynamic> json) {
    return GestureSwipeUpdateEvent(
      dx: (json['dx'] as num?)?.toDouble() ?? 0.0,
      dy: (json['dy'] as num?)?.toDouble() ?? 0.0,
      timeMsec: (json['time_msec'] as num?)?.toInt() ?? 0,
    );
  }

  final double dx;
  final double dy;
  final int timeMsec;
}

@immutable
class GestureSwipeEndEvent {
  const GestureSwipeEndEvent({
    required this.cancelled,
    required this.timeMsec,
  });

  factory GestureSwipeEndEvent.fromJson(Map<String, dynamic> json) {
    return GestureSwipeEndEvent(
      cancelled: json['cancelled'] == true || json['cancelled'] == 1,
      timeMsec: (json['time_msec'] as num?)?.toInt() ?? 0,
    );
  }

  final bool cancelled;
  final int timeMsec;
}
