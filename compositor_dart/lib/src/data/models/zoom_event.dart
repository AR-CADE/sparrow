import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'zoom_event.g.dart';

/// Represents a zoom gesture or action triggered by mouse scroll wheel.
@JsonSerializable()
class ZoomScrollEvent extends Equatable {
  /// Creates a [ZoomScrollEvent] with [delta] amount and focal point
  /// ([x], [y]).
  const new({double? delta, double? x, double? y})
    : delta = delta ?? 0.0,
      x = x ?? 0.0,
      y = y ?? 0.0;

  /// Creates a [ZoomScrollEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$ZoomScrollEventFromJson(json);

  /// The scroll delta amount.
  final double delta;

  /// The horizontal focal coordinate of the zoom action.
  final double x;

  /// The vertical focal coordinate of the zoom action.
  final double y;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$ZoomScrollEventToJson(this);

  @override
  List<Object?> get props => [delta, x, y];
}

/// Represents a zoom action triggered by a keyboard shortcut.
@JsonSerializable()
class ZoomKeyEvent extends Equatable {
  /// Creates a [ZoomKeyEvent] with [action] and focal point ([x], [y]).
  const new({int? action, double? x, double? y})
    : action = action ?? 0,
      x = x ?? 0.0,
      y = y ?? 0.0;

  /// Creates a [ZoomKeyEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) => _$ZoomKeyEventFromJson(json);

  /// The zoom action: `1` for zoom in, `-1` for zoom out, `0` for reset zoom.
  final int action;

  /// The horizontal focal coordinate of the zoom action.
  final double x;

  /// The vertical focal coordinate of the zoom action.
  final double y;

  /// Converts this event to a JSON map.
  Map<String, dynamic> toJson() => _$ZoomKeyEventToJson(this);

  @override
  List<Object?> get props => [action, x, y];
}
