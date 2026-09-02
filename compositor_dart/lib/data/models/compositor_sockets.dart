import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'compositor_sockets.g.dart';

@JsonSerializable()
class CompositorSockets extends Equatable {
  const CompositorSockets({required this.wayland, required this.x});

  factory CompositorSockets.fromJson(Map<String, dynamic> json) =>
      _$CompositorSocketsFromJson(json);
  final String wayland;
  final String x;

  Map<String, dynamic> toJson() => _$CompositorSocketsToJson(this);

  CompositorSockets copyWith({String? wayland, String? x}) {
    return CompositorSockets(wayland: wayland ?? this.wayland, x: x ?? this.x);
  }

  @override
  List<Object?> get props => [wayland, x];
}
