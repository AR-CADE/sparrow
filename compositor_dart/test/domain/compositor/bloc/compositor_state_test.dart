import 'package:compositor_dart/src/data/models/display_output.dart';
import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:compositor_dart/src/domain/compositor/bloc/compositor_bloc.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CompositorState', () {
    const surface = Surface(handle: 10, pid: 100, gid: 100, uid: 100);
    const output = DisplayOutput(id: 1);

    test('initial state has default values', () {
      const state = CompositorState();
      expect(state.status, equals(CompositorStatus.initial));
      expect(state.surfaces, isEmpty);
      expect(state.outputs, isEmpty);
      expect(state.activeSurfaceId, isNull);
      expect(state.activeSurface, isNull);
      expect(state.lastEvent, isNull);
      expect(state.errorMessage, isNull);
    });

    test('supports value comparisons', () {
      expect(const CompositorState(), equals(const CompositorState()));
    });

    test('activeSurface returns the surface matching activeSurfaceId', () {
      const state = CompositorState(
        surfaces: {10: surface},
        activeSurfaceId: 10,
      );
      expect(state.activeSurface, equals(surface));

      const stateMissing = CompositorState(
        surfaces: {10: surface},
        activeSurfaceId: 99,
      );
      expect(stateMissing.activeSurface, isNull);
    });

    test('copyWith updates state correctly', () {
      const state = CompositorState();
      final updated = state.copyWith(
        status: CompositorStatus.ready,
        surfaces: {10: surface},
        outputs: {1: output},
        activeSurfaceId: 10,
        errorMessage: 'error',
      );

      expect(updated.status, equals(CompositorStatus.ready));
      expect(updated.surfaces, equals({10: surface}));
      expect(updated.outputs, equals({1: output}));
      expect(updated.activeSurfaceId, equals(10));
      expect(updated.errorMessage, equals('error'));
    });
  });
}
