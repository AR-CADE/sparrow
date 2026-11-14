import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart'
    show FilterQuality, Listener, MaterialApp, SizedBox, Texture;
import 'package:mocktail/mocktail.dart';
import 'package:rxdart/subjects.dart';

class _MockCompositorRepository extends Mock implements CompositorRepository;

void main() {
  group('LayerSurfaceView', () {
    const layerSurface = LayerSurface(
      handle: 10,
      textureId: 42,
      width: 800,
      height: 60,
    );

    testWidgets('renders Texture with correct textureId and size', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: LayerSurfaceView(
            layerSurface: layerSurface,
            filterQuality: FilterQuality.medium,
          ),
        ),
      );

      final textureFinder = find.byType(Texture);
      expect(textureFinder, findsOneWidget);

      final texture = tester.widget<Texture>(textureFinder);
      expect(texture.textureId, equals(42));
      expect(texture.filterQuality, equals(FilterQuality.medium));

      final sizedBoxFinder = find.byType(SizedBox);
      expect(sizedBoxFinder, findsOneWidget);
      final sizedBox = tester.widget<SizedBox>(sizedBoxFinder);
      expect(sizedBox.width, equals(800));
      expect(sizedBox.height, equals(60));
    });

    testWidgets('renders without SizedBox when width and height are 0', (
      tester,
    ) async {
      const zeroSizeSurface = LayerSurface(handle: 11, textureId: 43);

      await tester.pumpWidget(
        const MaterialApp(
          home: LayerSurfaceView(layerSurface: zeroSizeSurface),
        ),
      );

      expect(find.byType(Texture), findsOneWidget);
      expect(find.byType(SizedBox), findsNothing);
    });

    test('LayerSurfacePlatformViewController returns correct size', () {
      const controller = LayerSurfacePlatformViewController(
        layerSurface: layerSurface,
      );
      expect(controller.size.width, equals(800));
      expect(controller.size.height, equals(60));

      const fallbackController = LayerSurfacePlatformViewController(
        layerSurface: LayerSurface(handle: 12, textureId: 44),
      );
      expect(fallbackController.size.width, equals(100));
      expect(fallbackController.size.height, equals(100));
    });

    testWidgets('renders child SubSurfaces from LayerSurfaceCubit', (
      tester,
    ) async {
      final repository = _MockCompositorRepository();
      final events = PublishSubject<CompositorEvent>();
      addTearDown(events.close);

      const childSub = SubSurface(
        handle: 5,
        parentHandle: LayerSurfaceCubit.parentHandleOffset + 10,
        textureId: 100005,
        width: 800,
        height: 60,
      );
      when(() => repository.events).thenAnswer((_) => events);
      when(
        () => repository.subSurfaceSetLookUp(
          LayerSurfaceCubit.parentHandleOffset + 10,
        ),
      ).thenReturn({childSub});

      final cubit = LayerSurfaceCubit(
        layerSurface: layerSurface,
        repository: repository,
      );
      addTearDown(cubit.close);

      await tester.pumpWidget(
        MaterialApp(
          home: LayerSurfaceView(
            layerSurface: layerSurface,
            cubit: cubit,
            forwardPanZoom: false,
          ),
        ),
      );

      final textures = tester
          .widgetList<Texture>(find.byType(Texture))
          .toList();
      expect(textures.map((t) => t.textureId), containsAll([42, 100005]));

      final listener = tester.widget<Listener>(
        find.descendant(
          of: find.byType(LayerSurfaceView),
          matching: find.byType(Listener),
        ),
      );
      expect(listener.onPointerPanZoomStart, isNull);
      expect(listener.onPointerPanZoomUpdate, isNull);
      expect(listener.onPointerPanZoomEnd, isNull);
    });
  });
}
