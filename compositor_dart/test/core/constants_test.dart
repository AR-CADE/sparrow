import 'package:compositor_dart/src/core/constants.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('Constants', () {
    test('KeyStatus enum has expected values', () {
      expect(KeyStatus.values, contains(KeyStatus.released));
      expect(KeyStatus.values, contains(KeyStatus.pressed));
    });

    test('pointer event constants are defined', () {
      expect(pointerDownEvent, equals(1));
      expect(pointerUpEvent, equals(2));
      expect(pointerHoverEvent, equals(3));
      expect(pointerMoveEvent, equals(4));
      expect(pointerEnterEvent, equals(5));
      expect(pointerExitEvent, equals(6));
      expect(pointerScrollEvent, equals(7));
      expect(pointerUnknownEvent, equals(8));
      expect(pointerPanZoomStartEvent, equals(9));
      expect(pointerPanZoomUpdateEvent, equals(10));
      expect(pointerPanZoomEndEvent, equals(11));
      expect(pointerCancelEvent, equals(12));
    });

    test('pointer kind constants are defined', () {
      expect(pointerKindMouse, equals(1));
      expect(pointerKindTouch, equals(2));
      expect(pointerKindUnknown, equals(3));
      expect(pointerKindTrackpad, equals(4));
    });

    test('physicalToXkbMap contains basic key mappings', () {
      expect(physicalToXkbMap[0x00070029], equals(0x00000009)); // escape
      expect(physicalToXkbMap[0x00070028], equals(0x00000024)); // return
      expect(physicalToXkbMap[0x0007002c], equals(0x00000041)); // space
    });

    test('CustomPageController defaults to viewportFraction 1.0', () {
      final controller = CustomPageController();
      expect(controller.viewportFraction, equals(1.0));

      // Setting same value returns early without accessing unattached positions
      controller.viewportFraction = 1.0;
      expect(controller.viewportFraction, equals(1.0));

      controller.dispose();
    });
  });
}
