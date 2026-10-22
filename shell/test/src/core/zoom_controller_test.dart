import 'package:flutter_test/flutter_test.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/core/zoom_controller.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('ZoomController', () {
    late ZoomController controller;

    setUp(() {
      controller = ZoomController(vsync: const TickerProviderImpl());
    });

    tearDown(() {
      controller.dispose();
    });

    test('initial values are not zoomed', () {
      expect(controller.currentZoom, 1.0);
      expect(controller.cursorPos, Offset.zero);
      expect(controller.isZoomed, isFalse);
    });

    test('handleKey increases zoom for action > 0', () {
      controller.handleKey(1, const Offset(100, 100));
      expect(controller.cursorPos, const Offset(100, 100));
    });

    test('handleKey resets zoom for action == 0', () {
      controller
        ..handleKey(1, const Offset(100, 100))
        ..handleKey(0, const Offset(100, 100));
      expect(controller.cursorPos, const Offset(100, 100));
    });

    test('handleKey decreases zoom for action < 0', () {
      controller
        ..handleKey(1, const Offset(50, 50))
        ..handleKey(-1, const Offset(50, 50));
      expect(controller.cursorPos, const Offset(50, 50));
    });

    test('handleScroll adjusts target zoom', () {
      controller.handleScroll(-15, const Offset(200, 200));
      expect(controller.cursorPos, const Offset(200, 200));
    });

    test('updateCursor updates position', () {
      controller.updateCursor(const Offset(300, 400));
      expect(controller.cursorPos, const Offset(300, 400));
    });
  });
}
