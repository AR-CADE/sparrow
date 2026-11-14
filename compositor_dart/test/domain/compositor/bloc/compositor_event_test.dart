import 'package:compositor_dart/src/data/models/compositor_event.dart';
import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:compositor_dart/src/domain/compositor/bloc/compositor_bloc.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CompositorBlocEvent', () {
    group('CompositorSubscriptionRequested', () {
      test('supports value comparisons', () {
        expect(
          const CompositorSubscriptionRequested(),
          equals(const CompositorSubscriptionRequested()),
        );
      });

      test('props are empty', () {
        expect(const CompositorSubscriptionRequested().props, isEmpty);
      });
    });

    group('CompositorEventReceived', () {
      const compEvent = CompositorEvent(
        type: CompositorEventType.surfaceMap,
        event: null,
      );

      test('supports value comparisons', () {
        expect(
          const CompositorEventReceived(compEvent),
          equals(const CompositorEventReceived(compEvent)),
        );
      });

      test('props contain the event', () {
        expect(
          const CompositorEventReceived(compEvent).props,
          equals([compEvent]),
        );
      });
    });

    group('CompositorSurfaceFocusRequested', () {
      test('supports value comparisons', () {
        expect(
          const CompositorSurfaceFocusRequested(42),
          equals(const CompositorSurfaceFocusRequested(42)),
        );
      });

      test('props contain the surfaceId', () {
        expect(const CompositorSurfaceFocusRequested(42).props, equals([42]));
      });
    });

    group('CompositorErrorReceived', () {
      test('supports value comparisons', () {
        expect(
          const CompositorErrorReceived('error'),
          equals(const CompositorErrorReceived('error')),
        );
      });

      test('props contain the message', () {
        expect(const CompositorErrorReceived('error').props, equals(['error']));
      });
    });
  });
}
