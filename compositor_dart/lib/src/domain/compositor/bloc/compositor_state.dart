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
    this.outputs = const {},
    this.activeSurfaceId,
    this.lastEvent,
    this.errorMessage,
  });

  /// The current lifecycle status of the compositor bloc.
  final CompositorStatus status;

  /// Map of known top-level surfaces indexed by their compositor handle.
  final Map<int, Surface> surfaces;

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

  /// Creates a copy of this state with optional field replacements.
  CompositorState copyWith({
    CompositorStatus? status,
    Map<int, Surface>? surfaces,
    Map<int, DisplayOutput>? outputs,
    int? activeSurfaceId,
    CompositorEvent? lastEvent,
    String? errorMessage,
  }) {
    return CompositorState(
      status: status ?? this.status,
      surfaces: surfaces ?? this.surfaces,
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
    outputs,
    activeSurfaceId,
    lastEvent,
    errorMessage,
  ];
}
