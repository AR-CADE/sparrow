import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/core/shell_controllers.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/presentation/bottom_indicator.dart';
import 'package:shell/src/presentation/compositor_controller.dart';
import 'package:shell/src/presentation/taskbar.dart';

import '../../helpers/mock_compositor_api.dart';

class MockConfigurationCubit extends MockCubit<ConfigurationState>
    implements ConfigurationCubit;

class MockCompositorBloc extends MockBloc<CompositorBlocEvent, CompositorState>
    implements CompositorBloc;

void main() {
  group('CompositorController', () {
    late MockConfigurationCubit configCubit;
    late MockCompositorBloc compBloc;
    late PageController pageController;

    setUp(() {
      setUpMockCompositorHostApi();
      configCubit = MockConfigurationCubit();
      compBloc = MockCompositorBloc();
      pageController = PageController();

      when(() => configCubit.state).thenReturn(
        const ConfigurationState(
          configuration: ShellConfiguration(
            launcher: 'grid2',
            terminal: 'alacritty',
            systemActions: SystemActions(
              logout: SystemAction(
                name: 'Logout',
                command: 'logout',
                icon: Icons.logout,
              ),
            ),
          ),
        ),
      );

      when(() => compBloc.state).thenReturn(const CompositorState());
    });

    tearDown(() {
      pageController.dispose();
    });

    testWidgets('renders TaskBar with commands from configuration', (
      tester,
    ) async {
      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
            ],
            child: Scaffold(
              body: CompositorController(
                pageController: pageController,
                toggleOverview: () async {},
                onUpdateCurrentPageIndex: (i) async {},
              ),
            ),
          ),
        ),
      );

      expect(find.byType(TaskBar), findsOneWidget);
    });

    testWidgets(
      'slides TaskBar offscreen in fullscreen and reveals on animation',
      (tester) async {
        final controllers = ShellControllers(vsync: const TickerProviderImpl());

        await tester.pumpWidget(
          MaterialApp(
            home: MultiBlocProvider(
              providers: [
                BlocProvider<ConfigurationCubit>.value(value: configCubit),
                BlocProvider<CompositorBloc>.value(value: compBloc),
              ],
              child: Scaffold(
                body: CompositorController(
                  pageController: pageController,
                  controllers: controllers,
                  isFullscreen: true,
                  toggleOverview: () async {},
                  onUpdateCurrentPageIndex: (i) async {},
                ),
              ),
            ),
          ),
        );

        expect(find.byType(TaskBar), findsOneWidget);

        controllers.hideDock();
        await tester.pump();

        controllers.showDock();
        await tester.pump();

        controllers.dispose();
      },
    );

    testWidgets(
      'renders BottomIndicator in edgeToEdge mode when surfaces exist',
      (tester) async {
        await tester.pumpWidget(
          MaterialApp(
            home: MultiBlocProvider(
              providers: [
                BlocProvider<ConfigurationCubit>.value(value: configCubit),
                BlocProvider<CompositorBloc>.value(value: compBloc),
              ],
              child: Scaffold(
                body: CompositorController(
                  pageController: pageController,
                  hasSurfaces: true,
                  toggleOverview: () async {},
                  onUpdateCurrentPageIndex: (i) async {},
                ),
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();

        expect(find.byType(BottomIndicator), findsOneWidget);
        expect(find.byType(TaskBar), findsNothing);
      },
    );

    testWidgets('renders TaskBar in overview mode even when surfaces exist', (
      tester,
    ) async {
      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
            ],
            child: Scaffold(
              body: CompositorController(
                pageController: pageController,
                hasSurfaces: true,
                isOverview: true,
                toggleOverview: () async {},
                onUpdateCurrentPageIndex: (i) async {},
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byType(TaskBar), findsOneWidget);
      expect(find.byType(BottomIndicator), findsNothing);
    });

    testWidgets(
      'toggles overview when tapping BottomIndicator in edgeToEdge mode',
      (tester) async {
        var toggled = false;

        await tester.pumpWidget(
          MaterialApp(
            home: MultiBlocProvider(
              providers: [
                BlocProvider<ConfigurationCubit>.value(value: configCubit),
                BlocProvider<CompositorBloc>.value(value: compBloc),
              ],
              child: Scaffold(
                body: CompositorController(
                  pageController: pageController,
                  hasSurfaces: true,
                  toggleOverview: () async {
                    toggled = true;
                  },
                  onUpdateCurrentPageIndex: (i) async {},
                ),
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();

        await tester.tap(find.byType(BottomIndicator), warnIfMissed: false);
        await tester.pump();

        expect(toggled, isTrue);
      },
    );

    testWidgets(
      'renders BottomIndicator when fullscreen and dock is not revealed',
      (tester) async {
        final controllers = ShellControllers(vsync: const TickerProviderImpl());

        await tester.pumpWidget(
          MaterialApp(
            home: MultiBlocProvider(
              providers: [
                BlocProvider<ConfigurationCubit>.value(value: configCubit),
                BlocProvider<CompositorBloc>.value(value: compBloc),
              ],
              child: Scaffold(
                body: CompositorController(
                  pageController: pageController,
                  controllers: controllers,
                  hasSurfaces: true,
                  isFullscreen: true,
                  toggleOverview: () async {},
                  onUpdateCurrentPageIndex: (i) async {},
                ),
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();

        expect(find.byType(BottomIndicator), findsOneWidget);
        expect(find.byType(TaskBar), findsNothing);

        controllers.dispose();
      },
    );

    testWidgets('switches to TaskBar when fullscreen and dock is revealed', (
      tester,
    ) async {
      final controllers = ShellControllers(vsync: const TickerProviderImpl());

      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
            ],
            child: Scaffold(
              body: CompositorController(
                pageController: pageController,
                controllers: controllers,
                hasSurfaces: true,
                isFullscreen: true,
                toggleOverview: () async {},
                onUpdateCurrentPageIndex: (i) async {},
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byType(BottomIndicator), findsOneWidget);

      controllers.hideDock(animate: false);
      await tester.pump();

      controllers.showDock();
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 300));

      expect(find.byType(TaskBar), findsOneWidget);

      controllers.dispose();
    });

    testWidgets(
      'switches to TaskBar when edgeToEdge and dock is revealed, then '
      'restores BottomIndicator on hideDock',
      (tester) async {
        final controllers = ShellControllers(vsync: const TickerProviderImpl());

        await tester.pumpWidget(
          MaterialApp(
            home: MultiBlocProvider(
              providers: [
                BlocProvider<ConfigurationCubit>.value(value: configCubit),
                BlocProvider<CompositorBloc>.value(value: compBloc),
              ],
              child: Scaffold(
                body: CompositorController(
                  pageController: pageController,
                  controllers: controllers,
                  hasSurfaces: true,
                  toggleOverview: () async {},
                  onUpdateCurrentPageIndex: (i) async {},
                ),
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();

        expect(find.byType(BottomIndicator), findsOneWidget);
        expect(find.byType(TaskBar), findsNothing);

        // Size check for hit-box restriction
        final indicatorBox = tester.getRect(
          find.byKey(const ValueKey('bottom_indicator')),
        );
        expect(indicatorBox.width, equals(360.0));
        expect(indicatorBox.height, equals(8.0));

        // Reveal dock
        controllers.showDock();
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 300));

        expect(find.byType(TaskBar), findsOneWidget);

        // Hide dock
        controllers.hideDock();
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 300));

        expect(find.byType(BottomIndicator), findsOneWidget);

        await tester.pump(const Duration(milliseconds: 300));

        expect(find.byType(TaskBar), findsNothing);

        controllers.dispose();
      },
    );
  });
}
