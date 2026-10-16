part of 'shell_bloc.dart';

/// Represents the reactive state of the Sparrow shell.
final class ShellState extends Equatable {
  /// Creates a new [ShellState].
  const new({
    this.isOverview = false,
    this.fraction = maxFraction,
    this.gap = 0.0,
    this.radius = 0.0,
    this.currentPageIndex = 0,
    this.hideCursor = false,
    this.isSwipingOverview = false,
    this.swipeStartVal = 0.0,
    this.activeOutput,
    this.surfaceHandles = const [],
  });

  /// Whether the shell is in overview mode.
  final bool isOverview;

  /// Viewport fraction of surfaces.
  final double fraction;

  /// Gap spacing between surfaces in overview mode.
  final double gap;

  /// Corner radius applied to surfaces.
  final double radius;

  /// Index of the currently active / visible page.
  final int currentPageIndex;

  /// Whether the mouse cursor is hidden due to inactivity.
  final bool hideCursor;

  /// Whether an overview swipe gesture is active.
  final bool isSwipingOverview;

  /// Animation value when swipe gesture began.
  final double swipeStartVal;

  /// The active display output.
  final DisplayOutput? activeOutput;

  /// Ordered list of surface handles in the shell carousel.
  final List<int> surfaceHandles;

  /// Returns the surfaces from [surfacesMap] ordered according to
  /// [overrideHandles] (if provided) or [surfaceHandles].
  ///
  /// The compositor provides surfaces as an unordered map ([surfacesMap]).
  /// This method maps them into a list preserving the exact carousel order
  /// (e.g. inserting new windows next to the active window).
  ///
  /// [overrideHandles] allows callers (such as `ShellView`) to supply an
  /// updated list of handles immediately (synchronously) before the
  /// asynchronous [ShellBloc] event loop has emitted a new state.
  List<Surface> orderedSurfaces(
    Map<int, Surface> surfacesMap, {
    List<int>? overrideHandles,
  }) {
    final handles = overrideHandles ?? surfaceHandles;
    final ordered = <Surface>[];
    for (final handle in handles) {
      final s = surfacesMap[handle];
      if (s != null) {
        ordered.add(s);
      }
    }
    for (final s in surfacesMap.values) {
      if (!handles.contains(s.handle)) {
        ordered.add(s);
      }
    }
    return ordered;
  }

  /// Returns a copy of this state with updated properties.
  ShellState copyWith({
    bool? isOverview,
    double? fraction,
    double? gap,
    double? radius,
    int? currentPageIndex,
    bool? hideCursor,
    bool? isSwipingOverview,
    double? swipeStartVal,
    DisplayOutput? activeOutput,
    bool clearActiveOutput = false,
    List<int>? surfaceHandles,
  }) {
    return ShellState(
      isOverview: isOverview ?? this.isOverview,
      fraction: fraction ?? this.fraction,
      gap: gap ?? this.gap,
      radius: radius ?? this.radius,
      currentPageIndex: currentPageIndex ?? this.currentPageIndex,
      hideCursor: hideCursor ?? this.hideCursor,
      isSwipingOverview: isSwipingOverview ?? this.isSwipingOverview,
      swipeStartVal: swipeStartVal ?? this.swipeStartVal,
      activeOutput: clearActiveOutput
          ? null
          : (activeOutput ?? this.activeOutput),
      surfaceHandles: surfaceHandles ?? this.surfaceHandles,
    );
  }

  @override
  List<Object?> get props => [
    isOverview,
    fraction,
    gap,
    radius,
    currentPageIndex,
    hideCursor,
    isSwipingOverview,
    swipeStartVal,
    activeOutput,
    surfaceHandles,
  ];
}
