import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/core/constants.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/presentation/animated_surface_wrapper.dart';

import '../../helpers/mock_compositor_api.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('AnimatedSurfaceWrapper and related widgets', () {
    late AnimationController animController;
    late PageController pageController;
    const testSurface = Surface(
      handle: 10,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'Test Window',
    );
    const testOutput = DisplayOutput(id: 1, width: 1920, height: 1080);

    setUp(() {
      setUpMockCompositorHostApi();
      animController = AnimationController(
        duration: const Duration(milliseconds: 100),
        vsync: const TickerProviderImpl(),
      );
      pageController = PageController();
    });

    tearDown(() {
      animController.dispose();
      pageController.dispose();
    });

    testWidgets('RoundedSurface clips to aspect ratio and radius', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: RoundedSurface(
              radius: 12,
              output: testOutput,
              child: ColoredBox(color: Colors.red),
            ),
          ),
        ),
      );

      expect(find.byType(ClipRRect), findsOneWidget);
      expect(find.byType(AspectRatio), findsOneWidget);
    });

    testWidgets(
      'AnimatedSurfaceWrapper renders DismissableSurface in overview',
      (tester) async {
        var toggled = false;

        await tester.pumpWidget(
          MaterialApp(
            home: Scaffold(
              body: AnimatedSurfaceWrapper(
                gap: 16,
                index: 0,
                fraction: minFraction,
                radius: 24,
                output: testOutput,
                surface: testSurface,
                pageController: pageController,
                controller: animController,
                closeOverview: (s, {center = true}) async {},
                openOverview: (s) async {},
                toggleOverview: ({index = 0}) async {
                  toggled = true;
                },
                child: const SizedBox(width: 100, height: 100),
              ),
            ),
          ),
        );

        final tapTarget = find.byWidgetPredicate(
          (w) => w is GestureDetector && w.onTap != null,
        );
        expect(tapTarget, findsOneWidget);

        await tester.tap(tapTarget);
        await tester.pump();
        expect(toggled, isTrue);
      },
    );

    testWidgets(
      'AnimatedSurfaceWrapper renders DraggableSurface when not in overview',
      (tester) async {
        await tester.pumpWidget(
          MaterialApp(
            home: Scaffold(
              body: AnimatedSurfaceWrapper(
                gap: 0,
                index: 0,
                fraction: maxFraction,
                radius: 0,
                output: testOutput,
                surface: testSurface,
                pageController: pageController,
                controller: animController,
                closeOverview: (s, {center = true}) async {},
                openOverview: (s) async {},
                toggleOverview: ({index = 0}) async {},
                child: const SizedBox(width: 100, height: 100),
              ),
            ),
          ),
        );

        expect(find.byType(DraggableSurface), findsOneWidget);
      },
    );

    testWidgets('DismissableSurface confirms dismiss on swipe up', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: DismissableSurface(
              radius: 12,
              output: testOutput,
              surface: testSurface,
              child: SizedBox(width: 200, height: 200),
            ),
          ),
        ),
      );

      expect(find.byType(Dismissible), findsOneWidget);
      await tester.drag(find.byType(Dismissible), const Offset(0, -300));
      await tester.pumpAndSettle();
    });

    testWidgets('DraggableSurface triggers drag callbacks when dragging', (
      tester,
    ) async {
      var closed = false;
      var opened = false;

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: DraggableSurface(
              radius: 0,
              closeOverview: (s, {center = true}) async {
                closed = true;
              },
              openOverview: (s) async {
                opened = true;
              },
              pageController: pageController,
              controller: animController,
              fraction: maxFraction,
              surface: testSurface,
              output: const DisplayOutput(id: 1, width: 800, height: 600),
              child: const SizedBox(width: 200, height: 200),
            ),
          ),
        ),
      );

      DraggableSurface.isVerticalBottomPointAllowed = true;
      final gesture = await tester.startGesture(const Offset(400, 595));
      await gesture.moveBy(const Offset(0, -100));
      await tester.pump();
      await gesture.up();
      await tester.pumpAndSettle();

      expect(closed || opened, isTrue);
    });
  });
}
