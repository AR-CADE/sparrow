import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show $enumDecode, JsonKey, JsonSerializable;

part 'compositor_event.g.dart';

/// Represents an event received from or sent to the native compositor.
@JsonSerializable()
class CompositorEvent extends Equatable {
  /// Creates a [CompositorEvent] with the given [type] and payload [event].
  const new({required this.type, required this.event, this.id});

  /// Creates a [CompositorEvent] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$CompositorEventFromJson(json);

  /// The type of compositor event.
  final CompositorEventType type;

  /// The payload data associated with this event.
  final dynamic event;

  /// Unique event sequence number or identifier.
  @JsonKey(includeFromJson: false, includeToJson: false)
  final int? id;

  /// Converts this event instance to a JSON map.
  Map<String, dynamic> toJson() => _$CompositorEventToJson(this);

  /// Creates a copy of this event with optional field replacements.
  CompositorEvent copyWith({
    CompositorEventType? type,
    dynamic event,
    int? id,
  }) {
    return CompositorEvent(
      type: type ?? this.type,
      event: event ?? this.event,
      id: id ?? this.id,
    );
  }

  @override
  List<Object?> get props => id != null ? [type, event, id] : [type, event];
}
