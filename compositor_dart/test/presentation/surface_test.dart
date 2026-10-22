import 'dart:collection' show HashMap;

import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_bloc/flutter_bloc.dart' show BlocProvider;
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart'
    show Center, ColoredBox, Directionality, SizedBox, TextDirection;
import 'package:mocktail/mocktail.dart';
import 'package:rxdart/subjects.dart';

class MockCompositorRepository extends Mock implements CompositorRepository;

class MockCompositorPlatformApi extends Mock implements CompositorPlatformApi;

void main() {
  group('SurfaceView widget and Presentation components', () {
    late CompositorRepository repository;
    late CompositorPlatformApi platform;
    late PublishSubject<CompositorEvent> eventsController;

    const surfaceZero = Surface(
      handle: 1,
      width: 0,
      height: 0,
      pid: 100,
      gid: 100,
      uid: 100,
    );

    const surfaceValid = Surface(
      handle: 2,
      width: 800,
      height: 600,
      textureId: 42,
      pid: 200,
      gid: 200,
      uid: 200,
    );

    const surfaceFixed = Surface(
      handle: 3,
      width: 400,
      height: 300,
      minWidth: 400,
      maxWidth: 400,
      minHeight: 300,
      maxHeight: 300,
      textureId: 43,
      pid: 300,
      gid: 300,
      uid: 300,
    );

    const surfaceWithGeo = Surface(
      handle: 4,
      width: 800,
      height: 600,
      bufferWidth: 1000,
      bufferHeight: 800,
      geoX: 10,
      geoY: 10,
      textureId: 44,
      pid: 400,
      gid: 400,
      uid: 400,
    );

    setUpAll(() {
      registerFallbackValue(const Surface(handle: 0, pid: 0, gid: 0, uid: 0));
      registerFallbackValue(<String, Object?>{});
    });

    setUp(() {
      repository = MockCompositorRepository();
      platform = MockCompositorPlatformApi();
      eventsController = PublishSubject<CompositorEvent>();

      when(() => repository.events).thenAnswer((_) => eventsController);
      when(() => repository.platform).thenReturn(platform);
      when(() => repository.outputs).thenReturn(HashMap<int, DisplayOutput>());
      when(() => repository.popupSetLookUp(any())).thenReturn(null);
      when(() => repository.subSurfaceSetLookUp(any())).thenReturn(null);
      when(() => platform.interactive).thenReturn(true);
      when(() => platform.surfaceSetPosition(any(), any(), any()))
          .thenAnswer((_) async {});
      when(() => platform.surfacePointerEvent(any())).thenAnswer((_) async {});
      when(() => platform.clearFocus(any())).thenAnswer((_) async {});
    });

    tearDown(() async {
      await eventsController.close();
    });

    testWidgets('renders SizedBox.shrink when surface dimensions are zero', (
      tester,
    ) async {
      await tester.pumpWidget(
        const Directionality(
          textDirection: TextDirection.ltr,
          child: SurfaceView(surface: surfaceZero),
        ),
      );

      expect(find.byType(SizedBox), findsOneWidget);
    });

    testWidgets('renders surface content when dimensions are valid', (
      tester,
    ) async {
      await tester.pumpWidget(
        const Directionality(
          textDirection: TextDirection.ltr,
          child: SurfaceView(surface: surfaceValid),
        ),
      );

      expect(find.byType(SurfaceTree), findsOneWidget);
      expect(find.byType(MainSurface), findsOneWidget);

      await tester.tap(find.byType(SurfaceView));
      await tester.pump();
    });

    testWidgets('letterboxes fixed size surfaces with black background', (
      tester,
    ) async {
      await tester.pumpWidget(
        const Directionality(
          textDirection: TextDirection.ltr,
          child: SurfaceView(surface: surfaceFixed),
        ),
      );

      expect(find.byType(ColoredBox), findsWidgets);
      expect(find.byType(SurfaceTree), findsOneWidget);
    });

    testWidgets('renders surface with geometry offset and buffer mismatch', (
      tester,
    ) async {
      await tester.pumpWidget(
        const Directionality(
          textDirection: TextDirection.ltr,
          child: SurfaceView(surface: surfaceWithGeo),
        ),
      );

      expect(find.byType(MainSurface), findsOneWidget);
    });

    testWidgets(
      'renders surface with orientation mismatch without distortion',
      (tester) async {
        // Landscape surface rendered inside tall portrait viewport
        await tester.pumpWidget(
          const Directionality(
            textDirection: TextDirection.ltr,
            child: Center(
              child: SizedBox(
                width: 300,
                height: 500,
                child: SurfaceView(surface: surfaceValid),
              ),
            ),
          ),
        );

        expect(find.byType(MainSurface), findsOneWidget);
        expect(find.byType(ColoredBox), findsWidgets);
      },
    );

    testWidgets(
      'renders smaller aspect-mismatched surface centered at 1:1 scale',
      (tester) async {
        const surfaceSmallWide = Surface(
          handle: 5,
          width: 400,
          height: 200,
          textureId: 45,
          pid: 500,
          gid: 500,
          uid: 500,
        );

        await tester.pumpWidget(
          const Directionality(
            textDirection: TextDirection.ltr,
            child: SizedBox(
              width: 800,
              height: 600,
              child: SurfaceView(surface: surfaceSmallWide),
            ),
          ),
        );

        expect(find.byType(MainSurface), findsOneWidget);
        expect(find.byType(ColoredBox), findsWidgets);
      },
    );

    testWidgets('renders popups and subsurfaces when present', (tester) async {
      const popup = Popup(
        handle: 10,
        textureId: 50,
        parentHandle: 2,
        width: 100,
        height: 50,
      );
      const sub1 = SubSurface(
        handle: 20,
        parentHandle: 2,
        textureId: 51,
        width: 100,
        height: 50,
        bufferWidth: 120,
        bufferHeight: 60,
      );
      const sub2 = SubSurface(
        handle: 21,
        parentHandle: 2,
        textureId: 52,
        width: 100,
        height: 50,
      );

      when(() => repository.popupSetLookUp(2)).thenReturn({popup});
      when(() => repository.subSurfaceSetLookUp(2)).thenReturn({sub1, sub2});

      final cubit = SurfaceCubit(surface: surfaceValid, repository: repository);

      await tester.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: SurfaceView(surface: surfaceValid, cubit: cubit),
        ),
      );

      expect(find.byType(PopupView), findsOneWidget);
      expect(find.byType(SubSurfaces), findsOneWidget);
    });

    testWidgets('uses existing SurfaceCubit from ancestor tree', (
      tester,
    ) async {
      final cubit = SurfaceCubit(surface: surfaceValid, repository: repository);

      await tester.pumpWidget(
        BlocProvider<SurfaceCubit>.value(
          value: cubit,
          child: const Directionality(
            textDirection: TextDirection.ltr,
            child: SurfaceView(surface: surfaceValid),
          ),
        ),
      );

      expect(find.byType(SurfaceTree), findsOneWidget);
    });
  });
}
