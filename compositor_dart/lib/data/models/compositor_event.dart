import 'package:compositor_dart/data/models/compositor_event_type.dart';
import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart'
    show $enumDecode, JsonSerializable;

part 'compositor_event.g.dart';

@JsonSerializable()
class CompositorEvent extends Equatable {
  const CompositorEvent({required this.type, required this.event});

  factory CompositorEvent.fromJson(Map<String, dynamic> json) =>
      _$CompositorEventFromJson(json);
  final CompositorEventType type;
  final dynamic event;

  Map<String, dynamic> toJson() => _$CompositorEventToJson(this);

  CompositorEvent copyWith({CompositorEventType? type, dynamic event}) {
    return CompositorEvent(type: type ?? this.type, event: event ?? this.event);
  }

  @override
  List<Object?> get props => [type, event];
}
