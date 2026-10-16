import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/constants.dart';

void main() {
  group('Constants', () {
    test('maxFraction is 1.0 and minFraction is 0.5', () {
      expect(maxFraction, 1.0);
      expect(minFraction, 0.5);
    });

    test('numeric gesture constants have expected values', () {
      expect(minOpenFraction, 0.90);
      expect(killVelocity, -2.0);
      expect(forwardVelocity, -0.7);
      expect(draggableSurfaceHeight, 30);
      expect(radiusAnimationAccel, 15);
    });

    test('OverviewDragTarget enum contains openOverview and closeOverview', () {
      expect(OverviewDragTarget.values, [
        OverviewDragTarget.openOverview,
        OverviewDragTarget.closeOverview,
      ]);
    });

    test('DragDirection enum contains up and down', () {
      expect(DragDirection.values, [DragDirection.up, DragDirection.down]);
    });

    test('CustomPageController initializes and updates viewportFraction', () {
      final controller = CustomPageController(viewportFraction: 0.8);
      expect(controller.viewportFraction, 0.8);

      controller.setViewportFraction(0.5);
      expect(controller.viewportFraction, 0.5);

      // Same value does nothing
      controller.setViewportFraction(0.5);
      expect(controller.viewportFraction, 0.5);
    });
  });
}
