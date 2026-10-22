import 'dart:async';

import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/core/shell_controllers.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart';
import 'package:shell/src/presentation/shell_view.dart';

import '../../helpers/mock_compositor_api.dart';

class MockConfigurationCubit extends MockCubit<ConfigurationState>
    implements ConfigurationCubit;

class MockCompositorBloc extends MockBloc<CompositorBlocEvent, CompositorState>
    implements CompositorBloc;

class MockShellBloc extends MockBloc<ShellBlocEvent, ShellState>
    implements ShellBloc;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('ShellView', () {
    late MockConfigurationCubit configCubit;
    late MockCompositorBloc compBloc;
    late MockShellBloc shellBloc;
    late ShellControllers controllers;
    late StreamController<CompositorState> compStreamController;
    late StreamController<ShellState> shellStreamController;

    const testOutput = DisplayOutput(id: 1, width: 1920, height: 1080);
    const testSurface = Surface(
      handle: 1,
      pid: 100,
      gid: 100,
      uid: 100,
      title: 'Surface 1',
    );

    setUp(() {
      setUpMockCompositorHostApi();
      configCubit = MockConfigurationCubit();
      compBloc = MockCompositorBloc();
      shellBloc = MockShellBloc();
      compStreamController = StreamController<CompositorState>.broadcast();
      shellStreamController = StreamController<ShellState>.broadcast();

      controllers = ShellControllers(vsync: const TickerProviderImpl());

      when(() => configCubit.state).thenReturn(const ConfigurationState());

      const initialCompState = CompositorState(
        outputs: {1: testOutput},
        surfaces: {1: testSurface},
      );
      when(() => compBloc.state).thenReturn(initialCompState);
      whenListen(
        compBloc,
        compStreamController.stream,
        initialState: initialCompState,
      );

      const initialShellState = ShellState(activeOutput: testOutput);
      when(() => shellBloc.state).thenReturn(initialShellState);
      whenListen(
        shellBloc,
        shellStreamController.stream,
        initialState: initialShellState,
      );
    });

    tearDown(() async {
      controllers.dispose();
      await compStreamController.close();
      await shellStreamController.close();
    });

    Future<void> pumpShellView(WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
              BlocProvider<ShellBloc>.value(value: shellBloc),
            ],
            child: Scaffold(body: ShellView(controllers: controllers)),
          ),
        ),
      );
    }

    testWidgets('renders ShellView hierarchy', (tester) async {
      await pumpShellView(tester);

      expect(find.byType(ShellView), findsOneWidget);
      expect(find.byType(MouseRegion), findsWidgets);
    });

    testWidgets('handles zoomScroll and zoomKey events', (tester) async {
      await pumpShellView(tester);

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.zoomScroll,
            event: ZoomScrollEvent(delta: -15, x: 100, y: 100),
          ),
        ),
      );
      await tester.pump();
      expect(controllers.zoomController.targetZoom, greaterThan(1.0));

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.zoomKey,
            event: ZoomKeyEvent(action: 0, x: 100, y: 100),
          ),
        ),
      );
      await tester.pump();
      expect(controllers.zoomController.targetZoom, equals(1.0));
      await tester.pumpAndSettle();
    });

    testWidgets('handles gestureSwipe lifecycle', (tester) async {
      await pumpShellView(tester);

      // swipe begin
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeBegin,
            event: GestureSwipeBeginEvent(fingers: 3, timeMsec: 0),
          ),
        ),
      );
      await tester.pump();

      // swipe update
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeUpdate,
            event: GestureSwipeUpdateEvent(dx: 0, dy: -140, timeMsec: 0),
          ),
        ),
      );
      await tester.pump();
      expect(controllers.overviewController.value, greaterThan(0.0));

      // swipe end (cancelled with val < 0.1)
      controllers.overviewController.value = 0.05;
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeEnd,
            event: GestureSwipeEndEvent(cancelled: true, timeMsec: 0),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // swipe begin for opening overview
      controllers.overviewController.value = 0.0;
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeBegin,
            event: GestureSwipeBeginEvent(fingers: 3, timeMsec: 1),
          ),
        ),
      );
      await tester.pump();

      // swipe end (not cancelled with val >= 0.14)
      controllers.overviewController.value = 0.2;
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeEnd,
            event: GestureSwipeEndEvent(cancelled: false, timeMsec: 1),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // swipe begin for closing overview (startVal = 1.0)
      controllers.overviewController.value = 1.0;
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeBegin,
            event: GestureSwipeBeginEvent(fingers: 3, timeMsec: 2),
          ),
        ),
      );
      await tester.pump();

      // swipe end when closing (val <= 0.75 closes overview)
      controllers.overviewController.value = 0.5;
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.gestureSwipeEnd,
            event: GestureSwipeEndEvent(cancelled: false, timeMsec: 2),
          ),
        ),
      );
      await tester.pumpAndSettle();
    });

    testWidgets('handles outputAdded, outputChanged, outputRemoved', (
      tester,
    ) async {
      await pumpShellView(tester);

      const newOutput = DisplayOutput(id: 2, width: 2560, height: 1440);

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput, 2: newOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputAdded,
            event: newOutput,
          ),
        ),
      );
      await tester.pump();
      verify(() => shellBloc.add(const ShellOutputChanged(newOutput)))
          .called(1);

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput, 2: newOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputChanged,
            event: newOutput,
          ),
        ),
      );
      await tester.pump();

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.outputRemoved,
            event: newOutput,
          ),
        ),
      );
      await tester.pump();
      verify(() => shellBloc.add(const ShellOutputRemoved(2))).called(1);
    });

    testWidgets('handles surfaceMap and surfaceUnMap', (tester) async {
      await pumpShellView(tester);

      const surface2 = Surface(
        handle: 2,
        pid: 101,
        gid: 100,
        uid: 100,
        title: 'Surface 2',
      );

      // surfaceMap when not in overview
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface, 2: surface2},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceMap,
            event: surface2,
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(controllers.pageController.page?.round(), equals(1));

      // surfaceUnMap when surfaces become empty in overview
      when(() => shellBloc.state).thenReturn(
        const ShellState(activeOutput: testOutput, isOverview: true),
      );

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceUnMap,
            event: testSurface,
          ),
        ),
      );
      await tester.pumpAndSettle();
      verify(() => shellBloc.add(const ShellOverviewClosed())).called(1);
    });

    testWidgets(
      'surfaceMap while in overview closes overview and updates state',
      (tester) async {
        await pumpShellView(tester);

        controllers.overviewController.value = 1.0;

        const surface2 = Surface(
          handle: 2,
          pid: 101,
          gid: 100,
          uid: 100,
          title: 'Surface 2',
        );

        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface2,
            ),
          ),
        );
        await tester.pumpAndSettle();

        expect(controllers.overviewController.value, equals(0.0));
        verify(() => shellBloc.add(const ShellOverviewClosed(targetIndex: 1)))
            .called(1);
      },
    );

    testWidgets(
      'animates previousPage on surfaceUnMap when closing active surface',
      (tester) async {
        await pumpShellView(tester);

        const surface2 = Surface(
          handle: 2,
          pid: 101,
          gid: 100,
          uid: 100,
          title: 'Surface 2',
        );

        // Map surface2 and animate to page 1
        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface2,
            ),
          ),
        );
        await tester.pumpAndSettle();
        expect(controllers.pageController.page?.round(), equals(1));

        // Now unmap surface2 while active at page 1
        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceUnMap,
              event: surface2,
            ),
          ),
        );
        await tester.pumpAndSettle();
        expect(controllers.pageController.page?.round(), equals(0));
      },
    );

    testWidgets(
      'inserts window 3 between window 1 and 2 when returning to window 1',
      (tester) async {
        await pumpShellView(tester);

        const surface2 = Surface(
          handle: 2,
          pid: 101,
          gid: 100,
          uid: 100,
          title: 'Surface 2',
        );
        const surface3 = Surface(
          handle: 3,
          pid: 102,
          gid: 100,
          uid: 100,
          title: 'Surface 3',
        );

        // 1. Map surface2 -> animates to page 1
        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface2,
            ),
          ),
        );
        await tester.pumpAndSettle();
        expect(controllers.pageController.page?.round(), equals(1));
        expect(controllers.surfaceHandles, equals([1, 2]));

        // 2. Return to window 1 (page 0)
        controllers.pageController.jumpToPage(0);
        controllers.targetPageIndex = 0;
        await tester.pumpAndSettle();
        expect(controllers.pageController.page?.round(), equals(0));

        // 3. Map surface3 while on window 1 -> should insert between 1 and 2
        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2, 3: surface3},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface3,
            ),
          ),
        );
        await tester.pumpAndSettle();

        // Surface 3 must be inserted at index 1, between 1 and 2
        expect(controllers.surfaceHandles, equals([1, 3, 2]));
        // Active page should be 1 (surface 3)
        expect(controllers.pageController.page?.round(), equals(1));
      },
    );

    testWidgets('focuses first surface on surfaceUnMap when at index 0', (
      tester,
    ) async {
      await pumpShellView(tester);

      const surface2 = Surface(
        handle: 2,
        pid: 101,
        gid: 100,
        uid: 100,
        title: 'Surface 2',
      );

      // Initially 2 surfaces, page at 0
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface, 2: surface2},
        ),
      );
      await tester.pumpAndSettle();

      // Unmap testSurface (at index 0)
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {2: surface2},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceUnMap,
            event: testSurface,
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(controllers.pageController.page?.round(), equals(0));
    });

    testWidgets('handles rapid surfaceMap events with animations', (
      tester,
    ) async {
      await pumpShellView(tester);

      const surface2 = Surface(
        handle: 2,
        pid: 101,
        gid: 100,
        uid: 100,
        title: 'Surface 2',
      );
      const surface3 = Surface(
        handle: 3,
        pid: 102,
        gid: 100,
        uid: 100,
        title: 'Surface 3',
      );

      // Emit surfaceMap for surface2
      compStreamController
        ..add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface2,
            ),
          ),
        )
        // Quickly emit surfaceMap for surface3 before settling
        ..add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2, 3: surface3},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceMap,
              event: surface3,
            ),
          ),
        );
      await tester.pumpAndSettle();
      expect(controllers.pageController.page?.round(), equals(2));
    });

    testWidgets('handles surfaceRequestActivate', (tester) async {
      await pumpShellView(tester);

      const surface2 = Surface(
        handle: 2,
        pid: 102,
        gid: 100,
        uid: 100,
        title: 'Surface 2',
      );

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface, 2: surface2},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceRequestActivate,
            event: SurfaceRequestActivateEvent(handle: 1, surface: testSurface),
          ),
        ),
      );
      await tester.pumpAndSettle();
      verify(() => shellBloc.add(const ShellPageChanged(0))).called(1);

      // In overview mode with surface 2
      when(() => shellBloc.state).thenReturn(
        const ShellState(activeOutput: testOutput, isOverview: true),
      );

      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface, 2: surface2},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceRequestActivate,
            event: SurfaceRequestActivateEvent(handle: 2, surface: surface2),
          ),
        ),
      );
      await tester.pumpAndSettle();
      verify(() => shellBloc.add(const ShellOverviewClosed(targetIndex: 1)))
          .called(1);
    });

    testWidgets(
      'handles surfaceRequestActivate when surface object in event is null',
      (tester) async {
        await pumpShellView(tester);

        const surface2 = Surface(
          handle: 2,
          pid: 102,
          gid: 100,
          uid: 100,
          title: 'Surface 2',
        );

        compStreamController.add(
          const CompositorState(
            outputs: {1: testOutput},
            surfaces: {1: testSurface, 2: surface2},
            lastEvent: CompositorEvent(
              type: CompositorEventType.surfaceRequestActivate,
              event: SurfaceRequestActivateEvent(
                handle: 2,
                token: 'tok',
                appId: 'steam',
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();
        expect(controllers.targetPageIndex, equals(1));
        expect(controllers.pageController.page?.round(), equals(1));
        verify(() => shellBloc.add(const ShellPageChanged(1)))
            .called(greaterThanOrEqualTo(1));
      },
    );

    testWidgets('stores pendingActivateHandle when surface is not yet mapped', (
      tester,
    ) async {
      await pumpShellView(tester);

      // 1. Activate request arrives for handle 5 (not mapped yet)
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceRequestActivate,
            event: SurfaceRequestActivateEvent(
              handle: 5,
              token: 'tok',
              appId: 'rpcs3',
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(controllers.pendingActivateHandle, equals(5));

      // 2. Later, handle 5 maps
      const surface5 = Surface(
        handle: 5,
        pid: 105,
        gid: 100,
        uid: 100,
        title: 'RPCS3 Game',
      );
      compStreamController.add(
        const CompositorState(
          outputs: {1: testOutput},
          surfaces: {1: testSurface, 5: surface5},
          lastEvent: CompositorEvent(
            type: CompositorEventType.surfaceMap,
            event: surface5,
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(controllers.pendingActivateHandle, isNull);
      expect(controllers.surfaceHandles, contains(5));
    });

    testWidgets('resets mouse idle timer on mouse hover', (tester) async {
      await pumpShellView(tester);

      final gesture = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await gesture.addPointer(location: Offset.zero);
      await gesture.moveTo(const Offset(100, 100));
      await tester.pump();
      await gesture.removePointer();
      await tester.pump(const Duration(seconds: 4));
    });

    testWidgets('renders when surfaces are empty', (tester) async {
      when(() => compBloc.state)
          .thenReturn(const CompositorState(outputs: {1: testOutput}));

      await pumpShellView(tester);
      expect(find.byType(ShellView), findsOneWidget);
    });
  });
}
