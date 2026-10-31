import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:rxdart/subjects.dart';

class MockCompositorRepository extends Mock implements CompositorRepository;

class MockCompositorPlatformApi extends Mock implements CompositorPlatformApi;

void main() {
  group('SurfaceState', () {
    test('supports value comparisons and copyWith', () {
      const state1 = SurfaceState();
      const state2 = SurfaceState();
      expect(state1, equals(state2));
      expect(state1.props, equals([<Popup>[], <SubSurface>[]]));

      const popup = Popup(
        handle: 10,
        textureId: 1,
        parentHandle: 1,
        x: 5,
        y: 5,
        width: 100,
        height: 100,
      );
      const sub = SubSurface(handle: 20, parentHandle: 1, textureId: 2);

      final updated = state1.copyWith(popups: [popup], subsurfaces: [sub]);
      expect(updated.popups, [popup]);
      expect(updated.subsurfaces, [sub]);

      final unchanged = state1.copyWith();
      expect(unchanged, equals(state1));
    });
  });

  group('SurfaceCubit', () {
    late CompositorRepository repository;
    late CompositorPlatformApi platform;
    late PublishSubject<CompositorEvent> eventsController;

    const surface = Surface(
      handle: 1,
      outputId: 10,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'App 1',
    );

    setUpAll(() {
      registerFallbackValue(const Surface(handle: 0, pid: 0, gid: 0, uid: 0));
    });

    setUp(() {
      repository = MockCompositorRepository();
      platform = MockCompositorPlatformApi();
      eventsController = PublishSubject<CompositorEvent>();

      when(() => repository.events).thenAnswer((_) => eventsController);
      when(() => repository.platform).thenReturn(platform);
      when(() => repository.popupSetLookUp(1)).thenReturn(null);
      when(() => repository.subSurfaceSetLookUp(1)).thenReturn(null);
      when(() => platform.surfaceSetPosition(any(), any(), any()))
          .thenAnswer((_) async {});
      when(
        () => platform.surfaceSetSceneBounds(
          any(),
          any(),
          any(),
          any(),
          any(),
          any(),
          visible: any(named: 'visible'),
        ),
      ).thenAnswer((_) async {});
    });

    tearDown(() async {
      await eventsController.close();
    });

    test(
      'initial state queries repository for popups and subsurfaces',
      () async {
        const popup = Popup(
          handle: 10,
          textureId: 1,
          parentHandle: 1,
          width: 50,
          height: 50,
        );
        const sub = SubSurface(handle: 20, parentHandle: 1, textureId: 2);

        when(() => repository.popupSetLookUp(1)).thenReturn({popup});
        when(() => repository.subSurfaceSetLookUp(1)).thenReturn({sub});

        final cubit = SurfaceCubit(surface: surface, repository: repository);

        expect(cubit.state.popups, [popup]);
        expect(cubit.state.subsurfaces, [sub]);

        await cubit.close();
      },
    );

    test('refreshes popups on popupMap event for this surface', () async {
      final cubit = SurfaceCubit(surface: surface, repository: repository);
      expect(cubit.state.popups, isEmpty);

      const popup = Popup(
        handle: 10,
        textureId: 1,
        parentHandle: 1,
        width: 50,
        height: 50,
      );
      when(() => repository.popupSetLookUp(1)).thenReturn({popup});

      eventsController.add(
        const CompositorEvent(type: CompositorEventType.popupMap, event: popup),
      );

      await Future<void>.delayed(Duration.zero);
      expect(cubit.state.popups, [popup]);

      await cubit.close();
    });

    test(
      'refreshes subsurfaces on subsurfaceMap event for this surface',
      () async {
        final cubit = SurfaceCubit(surface: surface, repository: repository);
        expect(cubit.state.subsurfaces, isEmpty);

        const sub = SubSurface(handle: 20, parentHandle: 1, textureId: 2);
        when(() => repository.subSurfaceSetLookUp(1)).thenReturn({sub});

        eventsController.add(
          const CompositorEvent(
            type: CompositorEventType.subSurfaceMap,
            event: sub,
          ),
        );

        await Future<void>.delayed(Duration.zero);
        expect(cubit.state.subsurfaces, [sub]);

        await cubit.close();
      },
    );

    test('refreshes on outputChanged event when outputId matches', () async {
      final cubit = SurfaceCubit(surface: surface, repository: repository);

      const output = DisplayOutput(id: 10, name: 'eDP-1');
      eventsController.add(
        const CompositorEvent(
          type: CompositorEventType.outputChanged,
          event: output,
        ),
      );

      await Future<void>.delayed(Duration.zero);
      await cubit.close();
    });

    test('ignores events for other surface handles', () async {
      final cubit = SurfaceCubit(surface: surface, repository: repository);

      const otherPopup = Popup(
        handle: 99,
        textureId: 9,
        parentHandle: 42,
        width: 50,
        height: 50,
      );

      clearInteractions(repository);

      eventsController.add(
        const CompositorEvent(
          type: CompositorEventType.popupMap,
          event: otherPopup,
        ),
      );

      await Future<void>.delayed(Duration.zero);
      verifyNever(() => repository.popupSetLookUp(1));

      await cubit.close();
    });

    test('updateSurface updates surface and resets position', () async {
      final cubit = SurfaceCubit(surface: surface, repository: repository)
        ..updatePosition(100, 200);
      expect(cubit.lastSentX, 100);
      expect(cubit.lastSentY, 200);

      const updatedSurface = Surface(
        handle: 1,
        outputId: 10,
        pid: 100,
        gid: 100,
        uid: 100,
        title: 'App 1 Updated',
      );
      cubit.updateSurface(updatedSurface);

      expect(cubit.surface, updatedSurface);
      expect(cubit.lastSentX, isNull);
      expect(cubit.lastSentY, isNull);

      await cubit.close();
    });

    test(
      'updatePosition sends platform call only when coordinates change',
      () async {
        final cubit = SurfaceCubit(surface: surface, repository: repository)
          ..updatePosition(10, 20);
        verify(() => platform.surfaceSetPosition(surface, 10, 20)).called(1);

        // Same coordinates should not send again
        cubit.updatePosition(10, 20);
        verifyNever(() => platform.surfaceSetPosition(surface, 10, 20));

        // Different coordinates should send
        cubit.updatePosition(30, 40);
        verify(() => platform.surfaceSetPosition(surface, 30, 40)).called(1);

        await cubit.close();
      },
    );

    test(
      'updateSceneBounds forwards bounds to platform and close cleans up',
      () async {
        final cubit = SurfaceCubit(surface: surface, repository: repository)
          ..updateSceneBounds(
            x: 10,
            y: 20,
            width: 800,
            height: 600,
            cornerRadius: 12,
            visible: true,
          );
        verify(
          () => platform.surfaceSetSceneBounds(surface, 10, 20, 800, 600, 12),
        ).called(1);

        clearInteractions(platform);
        await cubit.close();
        verifyNever(
          () => platform.surfaceSetSceneBounds(
            surface,
            any(),
            any(),
            any(),
            any(),
            any(),
            visible: any(named: 'visible'),
          ),
        );
      },
    );

    test('superseded cubit does not overwrite scene bounds with visible: false '
        'on detach or close', () async {
      final oldCubit = SurfaceCubit(surface: surface, repository: repository)
        ..updateSceneBounds(
          x: 100,
          y: 100,
          width: 600,
          height: 400,
          cornerRadius: 24,
          visible: true,
        );
      final newCubit = SurfaceCubit(surface: surface, repository: repository)
        ..updateSceneBounds(
          x: 80,
          y: 80,
          width: 640,
          height: 440,
          cornerRadius: 20,
          visible: true,
        );

      clearInteractions(platform);

      oldCubit.updateSceneBounds(
        x: 0,
        y: 0,
        width: 0,
        height: 0,
        cornerRadius: 0,
        visible: false,
      );
      await oldCubit.close();

      verifyNever(
        () => platform.surfaceSetSceneBounds(
          any(),
          any(),
          any(),
          any(),
          any(),
          any(),
          visible: false,
        ),
      );

      await newCubit.close();
      verifyNever(
        () => platform.surfaceSetSceneBounds(
          any(),
          any(),
          any(),
          any(),
          any(),
          any(),
          visible: false,
        ),
      );
    });
  });
}
