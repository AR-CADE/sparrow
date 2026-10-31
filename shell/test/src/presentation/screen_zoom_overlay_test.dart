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
  });
}
