import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show JsonKey, JsonSerializable;

part 'gesture_swipe_event.g.dart';

Object? _readCancelled(Map<dynamic, dynamic> json, String key) {
  final val = json[key];
  return val == true || val == 1;
}

/// Represents the start of a multi-finger swipe gesture.
@JsonSerializable()
class GestureSwipeBeginEvent extends Equatable {
  /// Creates a [GestureSwipeBeginEvent] with [fingers] count and [timeMsec].
  const new({int? fingers, int? timeMsec})
    : fingers = fingers ?? 3,
      timeMsec = timeMsec ?? 0;

  /// Creates a [GestureSwipeBeginEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$GestureSwipeBeginEventFromJson(json);

  /// The number of fingers involved in the swipe gesture.
  final int fingers;

  /// The timestamp of the event in milliseconds.
  @JsonKey(name: 'time_msec')
  final int timeMsec;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$GestureSwipeBeginEventToJson(this);

  @override
  List<Object?> get props => [fingers, timeMsec];
}

/// Represents an update event during an ongoing multi-finger swipe gesture.
@JsonSerializable()
class GestureSwipeUpdateEvent extends Equatable {
  /// Creates a [GestureSwipeUpdateEvent] with deltas and timestamp.
  const new({double? dx, double? dy, int? timeMsec})
    : dx = dx ?? 0.0,
      dy = dy ?? 0.0,
      timeMsec = timeMsec ?? 0;

  /// Creates a [GestureSwipeUpdateEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$GestureSwipeUpdateEventFromJson(json);

  /// The horizontal movement delta.
  final double dx;

  /// The vertical movement delta.
  final double dy;

  /// The timestamp of the event in milliseconds.
  @JsonKey(name: 'time_msec')
  final int timeMsec;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$GestureSwipeUpdateEventToJson(this);

  @override
  List<Object?> get props => [dx, dy, timeMsec];
}

/// Represents the termination of a multi-finger swipe gesture.
@JsonSerializable()
class GestureSwipeEndEvent extends Equatable {
  /// Creates a [GestureSwipeEndEvent] indicating completion or cancellation.
  const new({bool? cancelled, int? timeMsec})
    : cancelled = cancelled ?? false,
      timeMsec = timeMsec ?? 0;

  /// Creates a [GestureSwipeEndEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$GestureSwipeEndEventFromJson(json);

  /// Whether the swipe gesture was cancelled before normal completion.
  @JsonKey(readValue: _readCancelled)
  final bool cancelled;

  /// The timestamp of the event in milliseconds.
  @JsonKey(name: 'time_msec')
  final int timeMsec;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$GestureSwipeEndEventToJson(this);

  @override
  List<Object?> get props => [cancelled, timeMsec];
}
