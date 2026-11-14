import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/data/models/sub_surface.dart'
    show SubSurface;
import 'package:equatable/equatable.dart';

/// Immutable state containing the active popups and subsurfaces for a surface.
class SurfaceState extends Equatable {
  /// Creates a new [SurfaceState].
  const new({this.popups = const [], this.subsurfaces = const []});

  /// Popups belonging to this surface.
  final List<Popup> popups;

  /// Subsurfaces belonging to this surface.
  final List<SubSurface> subsurfaces;

  /// Creates a copy of this state with the given fields replaced.
  SurfaceState copyWith({List<Popup>? popups, List<SubSurface>? subsurfaces}) {
    return SurfaceState(
      popups: popups ?? this.popups,
      subsurfaces: subsurfaces ?? this.subsurfaces,
    );
  }

  @override
  List<Object?> get props => [popups, subsurfaces];
}
