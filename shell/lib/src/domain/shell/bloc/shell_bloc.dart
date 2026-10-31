import 'package:compositor_dart/compositor_dart.dart'
    show DisplayOutput, Surface;
import 'package:equatable/equatable.dart';
import 'package:hydrated_bloc/hydrated_bloc.dart';
import 'package:shell/src/core/constants.dart' show maxFraction, minFraction;

part 'shell_event.dart';
part 'shell_state.dart';

/// Business logic component managing shell state (overview, page index,
/// fraction, radius, gap, cursor visibility, swipe gestures, and active
/// output).
class ShellBloc extends HydratedBloc<ShellBlocEvent, ShellState> {
  /// Creates a new [ShellBloc] initialized to [ShellState].
  new() : super(const ShellState()) {
    on<ShellOverviewToggled>(_onOverviewToggled);
    on<ShellOverviewOpened>(_onOverviewOpened);
    on<ShellOverviewClosed>(_onOverviewClosed);
    on<ShellOverviewAnimationChanged>(_onOverviewAnimationChanged);
    on<ShellPageChanged>(_onPageChanged);
    on<ShellGestureSwipeBegin>(_onGestureSwipeBegin);
    on<ShellGestureSwipeUpdate>(_onGestureSwipeUpdate);
    on<ShellGestureSwipeEnd>(_onGestureSwipeEnd);
    on<ShellCursorMoved>(_onCursorMoved);
    on<ShellCursorIdleTimeout>(_onCursorIdleTimeout);
    on<ShellOutputChanged>(_onOutputChanged);
    on<ShellOutputRemoved>(_onOutputRemoved);
    on<ShellSurfaceMapped>(_onSurfaceMapped);
    on<ShellSurfaceUnmapped>(_onSurfaceUnmapped);
  }

  void _onOverviewToggled(
    ShellOverviewToggled event,
    Emitter<ShellState> emit,
  ) {
    final nextOverview = !state.isOverview;
    emit(
      state.copyWith(
        isOverview: nextOverview,
        fraction: nextOverview ? minFraction : maxFraction,
        gap: nextOverview ? 32.0 : 0.0,
        radius: nextOverview ? 24.0 : 0.0,
        currentPageIndex: event.targetIndex ?? state.currentPageIndex,
      ),
    );
  }

  void _onOverviewOpened(ShellOverviewOpened event, Emitter<ShellState> emit) {
    emit(
      state.copyWith(
        isOverview: true,
        fraction: minFraction,
        gap: 32,
        radius: 24,
      ),
    );
  }

  void _onOverviewClosed(ShellOverviewClosed event, Emitter<ShellState> emit) {
    emit(
      state.copyWith(
        isOverview: false,
        fraction: maxFraction,
        gap: 0,
        radius: 0,
        currentPageIndex: event.targetIndex ?? state.currentPageIndex,
      ),
    );
  }

  void _onOverviewAnimationChanged(
    ShellOverviewAnimationChanged event,
    Emitter<ShellState> emit,
  ) {
    emit(
      state.copyWith(
        fraction: event.fraction,
        gap: event.gap,
        radius: event.radius,
        isOverview: event.fraction == minFraction,
      ),
    );
  }

  void _onPageChanged(ShellPageChanged event, Emitter<ShellState> emit) {
    emit(state.copyWith(currentPageIndex: event.index));
  }

  void _onGestureSwipeBegin(
    ShellGestureSwipeBegin event,
    Emitter<ShellState> emit,
  ) {
    emit(
      state.copyWith(isSwipingOverview: true, swipeStartVal: event.startVal),
    );
  }

  void _onGestureSwipeUpdate(
    ShellGestureSwipeUpdate event,
    Emitter<ShellState> emit,
  ) {
    // dy < 0 opens overview (moves towards minFraction)
    // dy > 0 closes overview (moves towards maxFraction)
    final delta = -event.dy / 280.0;
    const range = maxFraction - minFraction;
    final currentProgress = 1.0 - (state.fraction - minFraction) / range;
    final newProgress = (currentProgress + delta).clamp(0.0, 1.0);
    final newFraction = maxFraction - (newProgress * range);
    final newGap = newProgress * 32.0;
    final newRadius = newProgress * 24.0;

    emit(state.copyWith(fraction: newFraction, gap: newGap, radius: newRadius));
  }

  void _onGestureSwipeEnd(
    ShellGestureSwipeEnd event,
    Emitter<ShellState> emit,
  ) {
    emit(state.copyWith(isSwipingOverview: false));
  }

  void _onCursorMoved(ShellCursorMoved event, Emitter<ShellState> emit) {
    if (state.hideCursor) {
      emit(state.copyWith(hideCursor: false));
    }
  }

  void _onCursorIdleTimeout(
    ShellCursorIdleTimeout event,
    Emitter<ShellState> emit,
  ) {
    if (!state.hideCursor) {
      emit(state.copyWith(hideCursor: true));
    }
  }

  void _onOutputChanged(ShellOutputChanged event, Emitter<ShellState> emit) {
    emit(state.copyWith(activeOutput: event.output));
  }

  void _onOutputRemoved(ShellOutputRemoved event, Emitter<ShellState> emit) {
    if (state.activeOutput?.id == event.outputId) {
      emit(state.copyWith(clearActiveOutput: true));
    }
  }

  void _onSurfaceMapped(ShellSurfaceMapped event, Emitter<ShellState> emit) {
    final live = event.liveHandles;
    final handles = live == null
        ? List<int>.from(state.surfaceHandles)
        : state.surfaceHandles
              .where((h) => h != event.surfaceHandle && live.contains(h))
              .toList();
    if (handles.contains(event.surfaceHandle)) {
      return;
    }
    final target = event.targetIndex.clamp(0, handles.length);
    handles.insert(target, event.surfaceHandle);
    emit(state.copyWith(surfaceHandles: handles, currentPageIndex: target));
  }

  void _onSurfaceUnmapped(
    ShellSurfaceUnmapped event,
    Emitter<ShellState> emit,
  ) {
    if (!state.surfaceHandles.contains(event.surfaceHandle)) {
      return;
    }
    final unmappedIndex = state.surfaceHandles.indexOf(event.surfaceHandle);
    final handles = List<int>.from(state.surfaceHandles)
      ..remove(event.surfaceHandle);
    var newIndex = state.currentPageIndex;
    if (unmappedIndex < state.currentPageIndex) {
      newIndex = (state.currentPageIndex - 1).clamp(
        0,
        handles.isNotEmpty ? handles.length - 1 : 0,
      );
    } else {
      newIndex = state.currentPageIndex.clamp(
        0,
        handles.isNotEmpty ? handles.length - 1 : 0,
      );
    }
    emit(state.copyWith(surfaceHandles: handles, currentPageIndex: newIndex));
  }

  @override
  ShellState? fromJson(Map<String, dynamic> json) => ShellState.fromJson(json);

  @override
  Map<String, dynamic>? toJson(ShellState state) => state.toJson();
}
