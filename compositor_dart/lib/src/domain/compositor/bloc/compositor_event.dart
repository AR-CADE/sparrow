part of 'compositor_bloc.dart';

/// Base class for all [CompositorBloc] events.
sealed class CompositorBlocEvent extends Equatable {
  /// Creates a [CompositorBlocEvent].
  const new();

  @override
  List<Object?> get props => [];
}

/// Dispatched to start listening to the [CompositorRepository] event stream.
final class CompositorSubscriptionRequested extends CompositorBlocEvent {
  /// Creates a [CompositorSubscriptionRequested] event.
  const new();
}

/// Dispatched when a new [CompositorEvent] is received from the repository.
final class CompositorEventReceived extends CompositorBlocEvent {
  /// Creates a [CompositorEventReceived] event with the given [event] payload.
  const new(this.event);

  /// The compositor event received from the native compositor.
  final CompositorEvent event;

  @override
  List<Object?> get props => [event];
}

/// Dispatched to request focus or activation on a specific surface handle.
final class CompositorSurfaceFocusRequested extends CompositorBlocEvent {
  /// Creates a [CompositorSurfaceFocusRequested] event targeting [surfaceId].
  const new(this.surfaceId);

  /// The surface handle or identifier to focus.
  final int surfaceId;

  @override
  List<Object?> get props => [surfaceId];
}

/// Dispatched when an error occurs in the repository stream.
final class CompositorErrorReceived extends CompositorBlocEvent {
  /// Creates a [CompositorErrorReceived] event with an error [message].
  const new(this.message);

  /// The error message describing the failure.
  final String message;

  @override
  List<Object?> get props => [message];
}
