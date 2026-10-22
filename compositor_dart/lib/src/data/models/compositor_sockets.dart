import 'package:equatable/equatable.dart' show Equatable;
import 'package:json_annotation/json_annotation.dart' show JsonSerializable;

part 'compositor_sockets.g.dart';

/// Represents the Wayland and X11 display socket identifiers for the
/// compositor.
@JsonSerializable()
class CompositorSockets extends Equatable {
  /// Creates a [CompositorSockets] with [wayland] and [x] socket identifiers.
  const new({required this.wayland, required this.x});

  /// Creates a [CompositorSockets] decoded from a JSON map.
  factory fromJson(Map<String, dynamic> json) =>
      _$CompositorSocketsFromJson(json);

  /// The Wayland socket name or path.
  final String wayland;

  /// The X11 display name (e.g. `":0"`).
  final String x;

  /// Converts this socket configuration to a JSON map.
  Map<String, dynamic> toJson() => _$CompositorSocketsToJson(this);

  /// Creates a copy of this configuration with optional field replacements.
  CompositorSockets copyWith({String? wayland, String? x}) {
    return CompositorSockets(wayland: wayland ?? this.wayland, x: x ?? this.x);
  }

  @override
  List<Object?> get props => [wayland, x];
}
