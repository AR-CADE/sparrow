import 'package:compositor_dart/src/data/models/sub_surface.dart'
    show SubSurface;
import 'package:equatable/equatable.dart';

/// Immutable state containing active subsurfaces for a layer surface.
class LayerSurfaceState extends Equatable {
  /// Creates a new [LayerSurfaceState].
  const new({this.subsurfaces = const []});

  /// Subsurfaces belonging to this layer surface.
  final List<SubSurface> subsurfaces;

  /// Creates a copy of this state with the given fields replaced.
  LayerSurfaceState copyWith({List<SubSurface>? subsurfaces}) {
    return LayerSurfaceState(subsurfaces: subsurfaces ?? this.subsurfaces);
  }

  @override
  List<Object?> get props => [subsurfaces];
}

