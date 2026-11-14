import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:rxdart/subjects.dart';

class MockCompositorRepository extends Mock implements CompositorRepository;

void main() {
  group('LayerSurfaceState', () {
    test('supports value comparisons and copyWith', () {
      const state1 = LayerSurfaceState();
      const state2 = LayerSurfaceState();
      expect(state1, equals(state2));
      expect(state1.props, equals([<SubSurface>[]]));

      const sub = SubSurface(
        handle: 5,
        parentHandle: 300001,
        textureId: 100005,
      );
      final updated = state1.copyWith(subsurfaces: [sub]);
      expect(updated.subsurfaces, [sub]);
      expect(state1.copyWith(), equals(state1));
    });
  });

  group('LayerSurfaceCubit', () {
    late CompositorRepository repository;
    late PublishSubject<CompositorEvent> eventsController;

    const layerSurface = LayerSurface(handle: 1, textureId: 300001);
    const parentHandle = LayerSurfaceCubit.parentHandleOffset + 1;

    setUp(() {
      repository = MockCompositorRepository();
      eventsController = PublishSubject<CompositorEvent>();
      when(() => repository.events).thenAnswer((_) => eventsController);
      when(() => repository.subSurfaceSetLookUp(any())).thenReturn(null);
    });

    tearDown(() async {
      await eventsController.close();
    });

    test('loads initial subsurfaces using offset parent handle', () {
      const sub = SubSurface(
        handle: 7,
        parentHandle: parentHandle,
        textureId: 100007,
        width: 400,
        height: 40,
      );
      when(() => repository.subSurfaceSetLookUp(parentHandle))
          .thenReturn({sub});

      final cubit = LayerSurfaceCubit(
        layerSurface: layerSurface,
        repository: repository,
      );

      expect(cubit.state.subsurfaces, equals([sub]));
      return cubit.close();
    });

    test(
      'refreshes on matching subsurface events and ignores others',
      () async {
        const sub1 = SubSurface(
          handle: 7,
          parentHandle: parentHandle,
          textureId: 100007,
          width: 400,
          height: 40,
        );
        const otherSub = SubSurface(
          handle: 9,
          parentHandle: 1,
          textureId: 100009,
        );

        final cubit = LayerSurfaceCubit(
          layerSurface: layerSurface,
          repository: repository,
        );
        expect(cubit.state.subsurfaces, isEmpty);

        when(() => repository.subSurfaceSetLookUp(parentHandle))
            .thenReturn({sub1});
        eventsController.add(
          const CompositorEvent(type: .subSurfaceMap, event: sub1),
        );
        await Future<void>.delayed(Duration.zero);
        expect(cubit.state.subsurfaces, equals([sub1]));

        when(() => repository.subSurfaceSetLookUp(parentHandle))
            .thenReturn(null);
        eventsController.add(
          const CompositorEvent(type: .subSurfaceUnMap, event: otherSub),
        );
        await Future<void>.delayed(Duration.zero);
        expect(cubit.state.subsurfaces, equals([sub1]));

        eventsController.add(
          const CompositorEvent(type: .subSurfaceUnMap, event: sub1),
        );
        await Future<void>.delayed(Duration.zero);
        expect(cubit.state.subsurfaces, isEmpty);

        await cubit.close();
      },
    );

    test('updateLayerSurface refreshes when layer surface changes', () async {
      const layerSurface2 = LayerSurface(handle: 2, textureId: 300002);
      const sub2 = SubSurface(
        handle: 8,
        parentHandle: LayerSurfaceCubit.parentHandleOffset + 2,
        textureId: 100008,
      );
      when(
        () => repository.subSurfaceSetLookUp(
          LayerSurfaceCubit.parentHandleOffset + 2,
        ),
      ).thenReturn({sub2});

      final cubit = LayerSurfaceCubit(
        layerSurface: layerSurface,
        repository: repository,
      );
      expect(cubit.state.subsurfaces, isEmpty);

      cubit.updateLayerSurface(layerSurface2);
      expect(cubit.state.subsurfaces, equals([sub2]));

      await cubit.close();
    });
  });
}
