import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/core/zoom_controller.dart';
import 'package:shell/src/presentation/screen_zoom_overlay.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('ScreenZoomOverlay', () {
    late ZoomController controller;

    setUp(() {
      controller = ZoomController(vsync: const TickerProviderImpl());
    });

    testWidgets('renders SizedBox.shrink when not zoomed', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ScreenZoomOverlay(
              controller: controller,
              size: const Size(1920, 1080),
            ),
          ),
        ),
      );

      expect(find.byType(SizedBox), findsOneWidget);
      expect(find.byType(RawMagnifier), findsNothing);
      controller.dispose();
    });

    testWidgets('renders RawMagnifier when zoomed', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ScreenZoomOverlay(
              controller: controller,
              size: const Size(1920, 1080),
            ),
          ),
        ),
      );

      expect(find.byType(RawMagnifier), findsNothing);

      controller.handleKey(1, const Offset(500, 500));
      await tester.pump(const Duration(milliseconds: 50));
      await tester.pump(const Duration(milliseconds: 50));

      expect(find.byType(RawMagnifier), findsOneWidget);
      controller.dispose();
    });

    testWidgets('uses Offset.zero focal point when cursor is at (0, 0)', (
      tester,
    ) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ScreenZoomOverlay(
              controller: controller,
              size: const Size(1920, 1080),
            ),
          ),
        ),
      );

      controller.handleScroll(-15, Offset.zero);
      await tester.pump(const Duration(milliseconds: 50));
      await tester.pump(const Duration(milliseconds: 50));

      final magnifier = tester.widget<RawMagnifier>(find.byType(RawMagnifier));
      expect(magnifier.focalPointOffset.dx, lessThan(0));
      expect(magnifier.focalPointOffset.dy, lessThan(0));
      controller.dispose();
    });

    testWidgets('falls back to screen center when no cursor was recorded', (
      tester,
    ) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ScreenZoomOverlay(
              controller: controller,
              size: const Size(1920, 1080),
            ),
          ),
        ),
      );

      controller.handleKey(1);
      await tester.pump(const Duration(milliseconds: 50));
      await tester.pump(const Duration(milliseconds: 50));

      final magnifier = tester.widget<RawMagnifier>(find.byType(RawMagnifier));
      expect(magnifier.focalPointOffset, Offset.zero);
      controller.dispose();
    });
  });
}
