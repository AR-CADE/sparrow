part of 'shell_bloc.dart';

/// Base class for all [ShellBloc] events.
sealed class ShellBlocEvent extends Equatable {
  /// Const constructor.
  const new();

  @override
  List<Object?> get props => [];
}

/// Dispatched to toggle overview mode on or off.
final class ShellOverviewToggled extends ShellBlocEvent {
  /// Creates a new [ShellOverviewToggled] event.
  const new({this.targetIndex});

  /// Optional target surface index to focus when toggling.
  final int? targetIndex;

  @override
  List<Object?> get props => [targetIndex];
}

/// Dispatched when overview mode is explicitly opened.
final class ShellOverviewOpened extends ShellBlocEvent {
  /// Creates a new [ShellOverviewOpened] event.
  const new();
}

/// Dispatched when overview mode is closed.
final class ShellOverviewClosed extends ShellBlocEvent {
  /// Creates a new [ShellOverviewClosed] event.
  const new({this.targetSurface, this.targetIndex});

  /// Optional target surface to focus.
  final Surface? targetSurface;

  /// Optional target surface index.
  final int? targetIndex;

  @override
  List<Object?> get props => [targetSurface, targetIndex];
}

/// Dispatched as the overview animation fraction, gap, or radius updates.
final class ShellOverviewAnimationChanged extends ShellBlocEvent {
  /// Creates a new [ShellOverviewAnimationChanged] event.
  const new({required this.fraction, required this.gap, required this.radius});

  /// Viewport fraction.
  final double fraction;

  /// Gap spacing between surfaces.
  final double gap;

  /// Corner radius of surfaces.
  final double radius;

  @override
  List<Object?> get props => [fraction, gap, radius];
}

/// Dispatched when the active page index changes.
final class ShellPageChanged extends ShellBlocEvent {
  /// Creates a new [ShellPageChanged] event.
  const new(this.index);

  /// The new page index.
  final int index;

  @override
  List<Object?> get props => [index];
}

/// Dispatched when a touchpad overview swipe gesture begins.
final class ShellGestureSwipeBegin extends ShellBlocEvent {
  /// Creates a new [ShellGestureSwipeBegin] event.
  const new({this.startVal = 0.0});

  /// The animation value when the swipe began.
  final double startVal;

  @override
  List<Object?> get props => [startVal];
}

/// Dispatched during an active touchpad overview swipe gesture.
final class ShellGestureSwipeUpdate extends ShellBlocEvent {
  /// Creates a new [ShellGestureSwipeUpdate] event.
  const new(this.dy);

  /// Vertical delta of the swipe gesture.
  final double dy;

  @override
  List<Object?> get props => [dy];
}

/// Dispatched when an overview swipe gesture ends.
final class ShellGestureSwipeEnd extends ShellBlocEvent {
  /// Creates a new [ShellGestureSwipeEnd] event.
  const new({required this.cancelled});

  /// Whether the swipe was cancelled.
  final bool cancelled;

  @override
  List<Object?> get props => [cancelled];
}

/// Dispatched when the mouse cursor moves.
final class ShellCursorMoved extends ShellBlocEvent {
  /// Creates a new [ShellCursorMoved] event.
  const new();
}

/// Dispatched when the mouse cursor idle timer times out.
final class ShellCursorIdleTimeout extends ShellBlocEvent {
  /// Creates a new [ShellCursorIdleTimeout] event.
  const new();
}

/// Dispatched when the active display output changes or is added.
final class ShellOutputChanged extends ShellBlocEvent {
  /// Creates a new [ShellOutputChanged] event.
  const new(this.output);

  /// The display output.
  final DisplayOutput output;

  @override
  List<Object?> get props => [output];
}

/// Dispatched when a display output is removed.
final class ShellOutputRemoved extends ShellBlocEvent {
  /// Creates a new [ShellOutputRemoved] event.
  const new(this.outputId);

  /// The ID of the removed output.
  final int outputId;

  @override
  List<Object?> get props => [outputId];
}

/// Dispatched when a surface is mapped, inserting its handle at [targetIndex].
final class ShellSurfaceMapped extends ShellBlocEvent {
  /// Creates a new [ShellSurfaceMapped] event.
  const new({required this.surfaceHandle, required this.targetIndex});

  /// The handle of the newly mapped surface.
  final int surfaceHandle;

  /// The target index where the surface is inserted.
  final int targetIndex;

  @override
  List<Object?> get props => [surfaceHandle, targetIndex];
}

/// Dispatched when a surface is unmapped, removing its handle from the shell.
final class ShellSurfaceUnmapped extends ShellBlocEvent {
  /// Creates a new [ShellSurfaceUnmapped] event.
  const new(this.surfaceHandle);

  /// The handle of the unmapped surface.
  final int surfaceHandle;

  @override
  List<Object?> get props => [surfaceHandle];
}
