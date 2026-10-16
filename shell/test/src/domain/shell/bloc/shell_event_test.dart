import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart';

void main() {
  group('ShellEvent', () {
    test('ShellOverviewToggled props and equality', () {
      const event1 = ShellOverviewToggled(targetIndex: 1);
      const event2 = ShellOverviewToggled(targetIndex: 1);
      const event3 = ShellOverviewToggled(targetIndex: 2);

      expect(event1, equals(event2));
      expect(event1, isNot(equals(event3)));
      expect(event1.props, [1]);
    });

    test('ShellOverviewOpened props and equality', () {
      const event1 = ShellOverviewOpened();
      const event2 = ShellOverviewOpened();

      expect(event1, equals(event2));
      expect(event1.props, isEmpty);
    });

    test('ShellOverviewClosed props and equality', () {
      const event1 = ShellOverviewClosed(targetIndex: 3);
      const event2 = ShellOverviewClosed(targetIndex: 3);

      expect(event1, equals(event2));
      expect(event1.props, [null, 3]);
    });

    test('ShellOverviewAnimationChanged props and equality', () {
      const event = ShellOverviewAnimationChanged(
        fraction: 0.8,
        gap: 16,
        radius: 12,
      );

      expect(event.props, [0.8, 16.0, 12.0]);
    });

    test('ShellPageChanged props and equality', () {
      const event = ShellPageChanged(2);
      expect(event.props, [2]);
    });

    test('ShellGestureSwipeBegin props and equality', () {
      const event = ShellGestureSwipeBegin(startVal: 0.5);
      expect(event.props, [0.5]);
    });

    test('ShellGestureSwipeUpdate props and equality', () {
      const event = ShellGestureSwipeUpdate(-20);
      expect(event.props, [-20.0]);
    });

    test('ShellGestureSwipeEnd props and equality', () {
      const event = ShellGestureSwipeEnd(cancelled: true);
      expect(event.props, [true]);
    });

    test('ShellCursorMoved and IdleTimeout props and equality', () {
      const moved = ShellCursorMoved();
      const timeout = ShellCursorIdleTimeout();

      expect(moved.props, isEmpty);
      expect(timeout.props, isEmpty);
    });

    test('ShellOutputChanged props and equality', () {
      const out = DisplayOutput(
        id: 1,
        width: 1920,
        height: 1080,
        name: 'HDMI-1',
      );
      const event = ShellOutputChanged(out);
      expect(event.props, [out]);
    });

    test('ShellOutputRemoved props and equality', () {
      const event = ShellOutputRemoved(1);
      expect(event.props, [1]);
    });

    test('ShellSurfaceMapped props and equality', () {
      const event1 = ShellSurfaceMapped(surfaceHandle: 10, targetIndex: 2);
      const event2 = ShellSurfaceMapped(surfaceHandle: 10, targetIndex: 2);
      const event3 = ShellSurfaceMapped(surfaceHandle: 20, targetIndex: 2);

      expect(event1, equals(event2));
      expect(event1, isNot(equals(event3)));
      expect(event1.props, [10, 2]);
    });

    test('ShellSurfaceUnmapped props and equality', () {
      const event1 = ShellSurfaceUnmapped(10);
      const event2 = ShellSurfaceUnmapped(10);
      const event3 = ShellSurfaceUnmapped(20);

      expect(event1, equals(event2));
      expect(event1, isNot(equals(event3)));
      expect(event1.props, [10]);
    });
  });
}
