import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:shell/src/presentation/positioned_layer_surface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('PositionedLayerSurface', () {
    testWidgets('renders SizedBox.shrink when layerSurfaces is empty', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(body: PositionedLayerSurface(layerSurfaces: [])),
        ),
      );

      expect(
        find.descendant(
          of: find.byType(PositionedLayerSurface),
          matching: find.byType(SizedBox),
        ),
        findsOneWidget,
      );
      expect(
        find.descendant(
          of: find.byType(PositionedLayerSurface),
          matching: find.byType(Stack),
        ),
        findsNothing,
      );
    });

    testWidgets('renders positioned LayerSurfaceView entries', (tester) async {
      const topBar = LayerSurface(
        handle: 1,
        textureId: 300001,
        layer: 2,
        anchor: 1 | 4 | 8, // top | left | right
        height: 48,
        bufferWidth: 1920,
        bufferHeight: 48,
        marginTop: 4,
        marginLeft: 8,
        marginRight: 8,
      );

      const bottomDock = LayerSurface(
        handle: 2,
        textureId: 300002,
        layer: 3,
        anchor: 2, // bottom only
        width: 600,
        height: 64,
        marginBottom: 12,
        x: 660,
      );

      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: PositionedLayerSurface(layerSurfaces: [topBar, bottomDock]),
          ),
        ),
      );

      expect(
        find.descendant(
          of: find.byType(PositionedLayerSurface),
          matching: find.byType(Stack),
        ),
        findsOneWidget,
      );
      expect(find.byType(LayerSurfaceView), findsNWidgets(2));

      final positionedWidgets = tester
          .widgetList<Positioned>(find.byType(Positioned))
          .toList();
      expect(positionedWidgets.length, 2);

      // topBar checks: top=4, left=8, right=8, height=48, width=null
      final topPos = positionedWidgets[0];
      expect(topPos.top, 4.0);
      expect(topPos.left, 8.0);
      expect(topPos.right, 8.0);
      expect(topPos.bottom, isNull);
      expect(topPos.height, 48.0);
      expect(topPos.width, isNull);

      // bottomDock checks: bottom=12, width=600, height=64, left=660
      final bottomPos = positionedWidgets[1];
      expect(bottomPos.top, isNull);
      expect(bottomPos.bottom, 12.0);
      expect(bottomPos.left, 660.0);
      expect(bottomPos.right, isNull);
      expect(bottomPos.width, 600.0);
      expect(bottomPos.height, 64.0);

      final views = tester
          .widgetList<LayerSurfaceView>(find.byType(LayerSurfaceView))
          .toList();
      expect(views.every((v) => v.forwardPanZoom), isTrue);
    });

    testWidgets(
      'disables forwardPanZoom for fullscreen 4-anchor TOP/OVERLAY surfaces',
      (tester) async {
        const slurpOverlay = LayerSurface(
          handle: 50,
          textureId: 300050,
          layer: 3,
          anchor: 15,
          width: 1920,
          height: 1080,
        );

        await tester.pumpWidget(
          const MaterialApp(
            home: Scaffold(
              body: PositionedLayerSurface(layerSurfaces: [slurpOverlay]),
            ),
          ),
        );

        final view = tester.widget<LayerSurfaceView>(
          find.byType(LayerSurfaceView),
        );
        expect(view.forwardPanZoom, isFalse);
      },
    );
  });
}
