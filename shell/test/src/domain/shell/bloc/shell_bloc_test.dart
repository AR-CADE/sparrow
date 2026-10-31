import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/constants.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart';

void main() {
  group('ShellBloc', () {
    test('initial state has default values', () {
      final bloc = ShellBloc();
      expect(bloc.state, const ShellState());
    });

    blocTest<ShellBloc, ShellState>(
      'toggles overview from closed to opened',
      build: ShellBloc.new,
      act: (bloc) => bloc.add(const ShellOverviewToggled()),
      expect: () => [
        const ShellState(
          isOverview: true,
          fraction: minFraction,
          gap: 32,
          radius: 24,
        ),
      ],
    );

    blocTest<ShellBloc, ShellState>(
      'toggles overview from opened to closed with targetIndex',
      build: ShellBloc.new,
      seed: () => const ShellState(
        isOverview: true,
        fraction: minFraction,
        gap: 32,
        radius: 24,
      ),
      act: (bloc) => bloc.add(const ShellOverviewToggled(targetIndex: 2)),
      expect: () => [const ShellState(currentPageIndex: 2)],
    );

    blocTest<ShellBloc, ShellState>(
      'opens overview explicitly',
      build: ShellBloc.new,
      act: (bloc) => bloc.add(const ShellOverviewOpened()),
      expect: () => [
        const ShellState(
          isOverview: true,
          fraction: minFraction,
          gap: 32,
          radius: 24,
        ),
      ],
    );

    blocTest<ShellBloc, ShellState>(
      'closes overview explicitly',
      build: ShellBloc.new,
      seed: () => const ShellState(
        isOverview: true,
        fraction: minFraction,
        gap: 32,
        radius: 24,
      ),
      act: (bloc) => bloc.add(const ShellOverviewClosed(targetIndex: 1)),
      expect: () => [const ShellState(currentPageIndex: 1)],
    );

    blocTest<ShellBloc, ShellState>(
      'handles animation changes smoothly',
      build: ShellBloc.new,
      act: (bloc) => bloc.add(
        const ShellOverviewAnimationChanged(
          fraction: 0.75,
          gap: 16,
          radius: 12,
        ),
      ),
      expect: () => [const ShellState(fraction: 0.75, gap: 16, radius: 12)],
    );

    blocTest<ShellBloc, ShellState>(
      'handles page changed',
      build: ShellBloc.new,
      act: (bloc) => bloc.add(const ShellPageChanged(3)),
      expect: () => [const ShellState(currentPageIndex: 3)],
    );

    blocTest<ShellBloc, ShellState>(
      'handles gesture swipe lifecycle',
      build: ShellBloc.new,
      act: (bloc) => bloc
        ..add(const ShellGestureSwipeBegin())
        ..add(const ShellGestureSwipeUpdate(-140))
        ..add(const ShellGestureSwipeEnd(cancelled: false)),
      expect: () => [
        const ShellState(isSwipingOverview: true),
        predicate<ShellState>((s) => s.fraction < maxFraction && s.gap > 0),
        predicate<ShellState>((s) => !s.isSwipingOverview),
      ],
    );

    blocTest<ShellBloc, ShellState>(
      'handles cursor idle and moved',
      build: ShellBloc.new,
      act: (bloc) => bloc
        ..add(const ShellCursorIdleTimeout())
        ..add(const ShellCursorMoved()),
      expect: () => [const ShellState(hideCursor: true), const ShellState()],
    );

    const testOutput = DisplayOutput(
      id: 42,
      width: 2560,
      height: 1440,
      name: 'DP-1',
    );

    blocTest<ShellBloc, ShellState>(
      'handles output changed and removed',
      build: ShellBloc.new,
      act: (bloc) => bloc
        ..add(const ShellOutputChanged(testOutput))
        ..add(const ShellOutputRemoved(42)),
      expect: () => [
        const ShellState(activeOutput: testOutput),
        const ShellState(),
      ],
    );

    blocTest<ShellBloc, ShellState>(
      'handles surface mapped insertion order and unmapped',
      build: ShellBloc.new,
      act: (bloc) => bloc
        ..add(const ShellSurfaceMapped(surfaceHandle: 1, targetIndex: 0))
        ..add(const ShellSurfaceMapped(surfaceHandle: 2, targetIndex: 1))
        ..add(const ShellPageChanged(0))
        ..add(const ShellSurfaceMapped(surfaceHandle: 3, targetIndex: 1))
        ..add(const ShellSurfaceUnmapped(3)),
      expect: () => [
        const ShellState(surfaceHandles: [1]),
        const ShellState(surfaceHandles: [1, 2], currentPageIndex: 1),
        const ShellState(surfaceHandles: [1, 2]),
        const ShellState(surfaceHandles: [1, 3, 2], currentPageIndex: 1),
        const ShellState(surfaceHandles: [1, 2], currentPageIndex: 1),
      ],
    );

    blocTest<ShellBloc, ShellState>(
      'ignores duplicate surface mapped and unmapped events',
      build: ShellBloc.new,
      seed: () => const ShellState(surfaceHandles: [1, 2]),
      act: (bloc) => bloc
        ..add(const ShellSurfaceMapped(surfaceHandle: 1, targetIndex: 0))
        ..add(const ShellSurfaceUnmapped(999)),
      expect: () => <ShellState>[],
    );

    blocTest<ShellBloc, ShellState>(
      'prunes stale persisted handles when liveHandles is provided',
      build: ShellBloc.new,
      seed: () => const ShellState(surfaceHandles: [12, 2, 1, 13, 14]),
      act: (bloc) => bloc
        ..add(
          const ShellSurfaceMapped(
            surfaceHandle: 1,
            targetIndex: 0,
            liveHandles: [1],
          ),
        )
        ..add(
          const ShellSurfaceMapped(
            surfaceHandle: 2,
            targetIndex: 1,
            liveHandles: [1, 2],
          ),
        ),
      expect: () => [
        const ShellState(surfaceHandles: [1]),
        const ShellState(surfaceHandles: [1, 2], currentPageIndex: 1),
      ],
    );

    test(
      'serializes and deserializes state correctly via toJson and fromJson',
      () {
        final bloc = ShellBloc();
        const state = ShellState(
          isOverview: true,
          fraction: minFraction,
          gap: 32,
          radius: 24,
          currentPageIndex: 3,
          surfaceHandles: [10, 20, 30],
        );

        final json = bloc.toJson(state);
        expect(json, {
          'isOverview': true,
          'currentPageIndex': 3,
          'surfaceHandles': [10, 20, 30],
        });

        final restoredState = bloc.fromJson(json!);
        expect(restoredState, state);
      },
    );
  });
}
