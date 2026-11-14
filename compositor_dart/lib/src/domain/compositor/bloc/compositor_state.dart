part of 'compositor_bloc.dart';

/// Status of the [CompositorBloc].
enum CompositorStatus {
  /// Initial uninitialized state before subscribing to events.
  initial,

  /// Connecting or preparing event subscription.
  loading,

  /// Actively receiving events from the compositor.
  ready,

  /// An error occurred in the compositor event stream.
  failure,
}

/// Represents the state of the compositor including active surfaces and
/// outputs.
class CompositorState extends Equatable {
  /// Creates a [CompositorState] snapshot.
  const new({
    this.status = CompositorStatus.initial,
    this.surfaces = const {},
    this.layerSurfaces = const {},
    this.outputs = const {},
    this.activeSurfaceId,
    this.lastEvent,
    this.errorMessage,
  });

  /// The current lifecycle status of the compositor bloc.
  final CompositorStatus status;

  /// Map of known top-level surfaces indexed by their compositor handle.
  final Map<int, Surface> surfaces;

  /// Map of active layer surfaces indexed by handle.
  final Map<int, LayerSurface> layerSurfaces;

  /// Map of detected display outputs indexed by output ID.
  final Map<int, DisplayOutput> outputs;

  /// The handle of the currently focused surface, if any.
  final int? activeSurfaceId;

  /// The most recent compositor event processed by the bloc.
  final CompositorEvent? lastEvent;

  /// The error message if [status] is [CompositorStatus.failure].
  final String? errorMessage;

  /// The currently active [Surface], or `null` if no surface has focus.
  Surface? get activeSurface =>
      activeSurfaceId != null ? surfaces[activeSurfaceId] : null;

  /// Background layer surfaces (wallpaper).
  Iterable<LayerSurface> get backgroundSurfaces =>
      layerSurfaces.values.where((s) => s.isBackground);

  /// Bottom layer surfaces.
  Iterable<LayerSurface> get bottomSurfaces =>
      layerSurfaces.values.where((s) => s.isBottom);

  /// Top layer surfaces (panels, status bars).
  Iterable<LayerSurface> get topSurfaces =>
      layerSurfaces.values.where((s) => s.isTop);

  /// Overlay layer surfaces (notifications, lock screens, osd).
  Iterable<LayerSurface> get overlaySurfaces =>
      layerSurfaces.values.where((s) => s.isOverlay);

  /// Creates a copy of this state with optional field replacements.
  CompositorState copyWith({
    CompositorStatus? status,
    Map<int, Surface>? surfaces,
    Map<int, LayerSurface>? layerSurfaces,
    Map<int, DisplayOutput>? outputs,
    int? activeSurfaceId,
    CompositorEvent? lastEvent,
    String? errorMessage,
  }) {
    return CompositorState(
      status: status ?? this.status,
      surfaces: surfaces ?? this.surfaces,
      layerSurfaces: layerSurfaces ?? this.layerSurfaces,
      outputs: outputs ?? this.outputs,
      activeSurfaceId: activeSurfaceId ?? this.activeSurfaceId,
      lastEvent: lastEvent ?? this.lastEvent,
      errorMessage: errorMessage ?? this.errorMessage,
    );
  }

  @override
  List<Object?> get props => [
    status,
    surfaces,
    layerSurfaces,
    outputs,
    activeSurfaceId,
    lastEvent,
    errorMessage,
  ];
}
