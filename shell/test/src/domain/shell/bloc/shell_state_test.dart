import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/constants.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart';

void main() {
  group('ShellState', () {
    test('supports value equality', () {
      expect(const ShellState(), equals(const ShellState()));
    });

    test('default state has maxFraction, gap 0, radius 0, not in overview', () {
      const state = ShellState();
      expect(state.fraction, maxFraction);
      expect(state.gap, 0.0);
      expect(state.radius, 0.0);
      expect(state.isOverview, isFalse);
      expect(state.currentPageIndex, 0);
      expect(state.hideCursor, isFalse);
      expect(state.activeOutput, isNull);
    });

    test('copyWith updates fields and clears activeOutput if requested', () {
      const out = DisplayOutput(
        id: 1,
        width: 1920,
        height: 1080,
        name: 'HDMI-1',
      );

      final state = const ShellState().copyWith(
        activeOutput: out,
        isOverview: true,
        fraction: minFraction,
      );

      expect(state.activeOutput, out);
      expect(state.isOverview, isTrue);
      expect(state.fraction, minFraction);

      final cleared = state.copyWith(clearActiveOutput: true);
      expect(cleared.activeOutput, isNull);
    });

    test('orderedSurfaces returns surfaces sorted by surfaceHandles', () {
      const s1 = Surface(handle: 1, pid: 1, gid: 1, uid: 1, title: 'S1');
      const s2 = Surface(handle: 2, pid: 2, gid: 2, uid: 2, title: 'S2');
      const s3 = Surface(handle: 3, pid: 3, gid: 3, uid: 3, title: 'S3');

      const state = ShellState(surfaceHandles: [1, 3, 2]);
      final surfacesMap = {1: s1, 2: s2, 3: s3};

      final ordered = state.orderedSurfaces(surfacesMap);
      expect(ordered, equals([s1, s3, s2]));
    });

    test('orderedSurfaces appends unlisted surfaces safely as fallback', () {
      const s1 = Surface(handle: 1, pid: 1, gid: 1, uid: 1, title: 'S1');
      const s2 = Surface(handle: 2, pid: 2, gid: 2, uid: 2, title: 'S2');

      const state = ShellState(surfaceHandles: [2]);
      final surfacesMap = {1: s1, 2: s2};

      final ordered = state.orderedSurfaces(surfacesMap);
      expect(ordered, equals([s2, s1]));
    });
  });
}
