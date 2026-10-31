import 'dart:collection';

import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/src/data/models/compositor_event.dart';
import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:compositor_dart/src/data/models/display_output.dart';
import 'package:compositor_dart/src/data/models/surface.dart';
import 'package:compositor_dart/src/data/models/surface_request_activate_event.dart';
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart';
import 'package:compositor_dart/src/domain/compositor/bloc/compositor_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:rxdart/subjects.dart';

class MockCompositorRepository extends Mock implements CompositorRepository;

void main() {
  group('CompositorBloc', () {
    late CompositorRepository repository;
    late PublishSubject<CompositorEvent> eventsController;
    final surfacesMap = HashMap<int, Surface>();
    final outputsMap = HashMap<int, DisplayOutput>();

    const surface1 = Surface(
      handle: 1,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'App 1',
    );
    const surface2 = Surface(
      handle: 2,
      pid: 200,
      gid: 200,
      uid: 200,
      title: 'App 2',
    );
    const output1 = DisplayOutput(id: 1, name: 'eDP-1');

    setUp(() {
      repository = MockCompositorRepository();
      eventsController = PublishSubject<CompositorEvent>();
      surfacesMap.clear();
      outputsMap.clear();

      when(() => repository.events).thenAnswer((_) => eventsController);
      when(() => repository.surfaces).thenReturn(surfacesMap);
      when(() => repository.outputs).thenReturn(outputsMap);
    });

    tearDown(() async {
      await eventsController.close();
    });

    test('initial state is correct', () async {
      final bloc = CompositorBloc(repository: repository);
      expect(bloc.state, equals(const CompositorState()));
      await bloc.close();
    });

    blocTest<CompositorBloc, CompositorState>(
      'emits [ready] when CompositorSubscriptionRequested is added',
      build: () {
        surfacesMap[1] = surface1;
        outputsMap[1] = output1;
        return CompositorBloc(repository: repository);
      },
      act: (bloc) => bloc.add(const CompositorSubscriptionRequested()),
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1},
          outputs: {1: output1},
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceMap event',
      build: () => CompositorBloc(repository: repository),
      act: (bloc) async {
        bloc.add(const CompositorSubscriptionRequested());
        await Future<void>.delayed(Duration.zero);
        eventsController.add(
          const CompositorEvent(
            type: CompositorEventType.surfaceMap,
            event: surface1,
          ),
        );
      },
      expect: () => [
        const CompositorState(status: CompositorStatus.ready),
        const CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1},
          activeSurfaceId: 1,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceMap,
            event: surface1,
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceUnMap event',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1, 2: surface2},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        bloc.add(
          const CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.surfaceUnMap,
              event: surface1,
            ),
          ),
        );
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {2: surface2},
          activeSurfaceId: 2,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceUnMap,
            event: surface1,
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceTitleChange event',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        final updatedSurface = surface1.copyWith(title: 'Updated Title');
        bloc.add(
          CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.surfaceTitleChange,
              event: updatedSurface,
            ),
          ),
        );
      },
      expect: () => [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1.copyWith(title: 'Updated Title')},
          activeSurfaceId: 1,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceTitleChange,
            event: surface1.copyWith(title: 'Updated Title'),
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceFullscreenChange event',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        final updatedSurface = surface1.copyWith(fullscreen: true);
        bloc.add(
          CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.surfaceFullscreenChange,
              event: updatedSurface,
            ),
          ),
        );
      },
      expect: () => [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1.copyWith(fullscreen: true)},
          activeSurfaceId: 1,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceFullscreenChange,
            event: surface1.copyWith(fullscreen: true),
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceRequestActivate event',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1, 2: surface2},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        bloc.add(
          const CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.surfaceRequestActivate,
              event: SurfaceRequestActivateEvent(handle: 2, surface: surface2),
            ),
          ),
        );
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1, 2: surface2},
          activeSurfaceId: 2,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceRequestActivate,
            event: SurfaceRequestActivateEvent(handle: 2, surface: surface2),
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles surfaceRequestActivate event when surface is null',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1, 2: surface2},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        bloc.add(
          const CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.surfaceRequestActivate,
              event: SurfaceRequestActivateEvent(handle: 2),
            ),
          ),
        );
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1, 2: surface2},
          activeSurfaceId: 2,
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceRequestActivate,
            event: SurfaceRequestActivateEvent(handle: 2),
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles outputAdded and outputRemoved events',
      seed: () => const CompositorState(status: CompositorStatus.ready),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        bloc
          ..add(
            const CompositorEventReceived(
              CompositorEvent(
                type: CompositorEventType.outputAdded,
                event: output1,
              ),
            ),
          )
          ..add(
            const CompositorEventReceived(
              CompositorEvent(
                type: CompositorEventType.outputRemoved,
                event: output1,
              ),
            ),
          );
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          outputs: {1: output1},
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputAdded,
            event: output1,
          ),
        ),
        CompositorState(
          status: CompositorStatus.ready,
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputRemoved,
            event: output1,
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles CompositorSurfaceFocusRequested',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        surfaces: {1: surface1, 2: surface2},
        activeSurfaceId: 1,
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        bloc
          ..add(const CompositorSurfaceFocusRequested(2))
          ..add(const CompositorSurfaceFocusRequested(999)); // Unknown
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          surfaces: {1: surface1, 2: surface2},
          activeSurfaceId: 2,
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'emits failure when event stream receives an error',
      build: () => CompositorBloc(repository: repository),
      act: (bloc) async {
        bloc.add(const CompositorSubscriptionRequested());
        await Future<void>.delayed(Duration.zero);
        eventsController.addError(Exception('stream error'));
      },
      expect: () => [
        const CompositorState(status: CompositorStatus.ready),
        const CompositorState(
          status: CompositorStatus.failure,
          errorMessage: 'Exception: stream error',
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles outputChanged event',
      seed: () => const CompositorState(
        status: CompositorStatus.ready,
        outputs: {1: output1},
      ),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        const updatedOutput = DisplayOutput(
          id: 1,
          name: 'eDP-1',
          isPrimary: true,
        );
        bloc.add(
          const CompositorEventReceived(
            CompositorEvent(
              type: CompositorEventType.outputChanged,
              event: updatedOutput,
            ),
          ),
        );
      },
      expect: () => const [
        CompositorState(
          status: CompositorStatus.ready,
          outputs: {1: DisplayOutput(id: 1, name: 'eDP-1', isPrimary: true)},
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputChanged,
            event: DisplayOutput(id: 1, name: 'eDP-1', isPrimary: true),
          ),
        ),
      ],
    );

    blocTest<CompositorBloc, CompositorState>(
      'handles other event types without modifying surfaces or outputs',
      seed: () => const CompositorState(status: CompositorStatus.ready),
      build: () => CompositorBloc(repository: repository),
      act: (bloc) {
        const events = [
          CompositorEventType.subSurfaceMap,
          CompositorEventType.subSurfaceUnMap,
          CompositorEventType.subsurfacePositionChange,
          CompositorEventType.popupMap,
          CompositorEventType.popupUnMap,
          CompositorEventType.gestureSwipeBegin,
          CompositorEventType.gestureSwipeUpdate,
          CompositorEventType.gestureSwipeEnd,
          CompositorEventType.zoomScroll,
          CompositorEventType.zoomKey,
        ];

        for (final type in events) {
          bloc.add(
            CompositorEventReceived(CompositorEvent(type: type, event: null)),
          );
        }
      },
      expect: () => [
        for (final type in [
          CompositorEventType.subSurfaceMap,
          CompositorEventType.subSurfaceUnMap,
          CompositorEventType.subsurfacePositionChange,
          CompositorEventType.popupMap,
          CompositorEventType.popupUnMap,
          CompositorEventType.gestureSwipeBegin,
          CompositorEventType.gestureSwipeUpdate,
          CompositorEventType.gestureSwipeEnd,
          CompositorEventType.zoomScroll,
          CompositorEventType.zoomKey,
        ])
          CompositorState(
            status: CompositorStatus.ready,
            lastEvent: CompositorEvent(type: type, event: null),
          ),
      ],
    );
  });
}
