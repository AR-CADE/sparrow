import 'dart:io' show Process, ProcessStartMode;

import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/core/shell_controllers.dart';
import 'package:shell/src/core/ticker_provider.dart';
import 'package:shell/src/data/repositories/configuration_repository.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/domain/shell/bloc/shell_bloc.dart';

import '../../helpers/mock_compositor_api.dart';

class MockConfigurationRepository extends Mock
    implements ConfigurationRepository;

class FakeProcess implements Process {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class FakeConfigurationRepository implements ConfigurationRepository {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class TestConfigurationCubit extends ConfigurationCubit {
  new({ConfigurationRepository? repository})
    : super(repository: repository ?? FakeConfigurationRepository());

  void emitState(ConfigurationState state) => emit(state);
}

class TestShellBloc extends ShellBloc {
  void emitState(ShellState state) => emit(state);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('ShellControllers', () {
    late ShellControllers controllers;
    final launchedCommands = <String>[];

    setUp(() {
      setUpMockCompositorHostApi();
      launchedCommands.clear();
      controllers = ShellControllers(
        vsync: const TickerProviderImpl(),
        processStarter: (exe, args, {mode = ProcessStartMode.normal}) async {
          launchedCommands.add('$exe ${args.join(' ')}'.trim());
          return FakeProcess();
        },
      );
    });

    tearDown(() {
      controllers.dispose();
    });

    test('initializes all controllers and animations', () {
      expect(controllers.overviewController, isNotNull);
      expect(controllers.zoomController, isNotNull);
      expect(controllers.fractionAnim, isNotNull);
      expect(controllers.gapAnim, isNotNull);
      expect(controllers.radiusAnim, isNotNull);
      expect(controllers.pageController, isNotNull);
      expect(controllers.focusNode, isNotNull);
      expect(controllers.isSwipingOverview, isFalse);
      expect(controllers.swipeStartVal, equals(0.0));
    });

    test('updates swipe gesture state', () {
      controllers
        ..isSwipingOverview = true
        ..swipeStartVal = 0.5;
      expect(controllers.isSwipingOverview, isTrue);
      expect(controllers.swipeStartVal, equals(0.5));
    });

    test(
      'resetMouseIdleTimer unhides cursor when showImmediately is true',
      () async {
        final shellBloc = TestShellBloc()
          ..emitState(const ShellState(hideCursor: true));

        controllers.resetMouseIdleTimer(shellBloc);
        await pumpEventQueue();
        expect(shellBloc.state.hideCursor, isFalse);

        await shellBloc.close();
      },
    );

    test('attachKeyboardHandler attaches handler without throwing', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit();

      expect(
        () => controllers.attachKeyboardHandler(
          getSurfaces: () => [],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {},
          onUpdateCurrentPageIndex: (index) async {},
        ),
        returnsNormally,
      );

      // Subsequent call also returns normally
      expect(
        () => controllers.attachKeyboardHandler(
          getSurfaces: () => [],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {},
          onUpdateCurrentPageIndex: (index) async {},
        ),
        returnsNormally,
      );

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent handles Alt key to toggle overview', () async {
      var toggled = false;
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit();

      controllers.attachKeyboardHandler(
        getSurfaces: () => [],
        shellBloc: shellBloc,
        configurationCubit: configCubit,
        onToggleOverview: () async {
          toggled = true;
        },
        onUpdateCurrentPageIndex: (index) async {},
      );

      final handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.altLeft,
          logicalKey: LogicalKeyboardKey.altLeft,
          timeStamp: Duration.zero,
        ),
      );

      expect(handled, isTrue);
      await pumpEventQueue();
      expect(toggled, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent in overview handles arrow keys and enter', () async {
      int? targetIndex;
      var toggled = false;
      final shellBloc = TestShellBloc()
        ..emitState(const ShellState(isOverview: true));
      final configCubit = TestConfigurationCubit();

      controllers
        ..attachKeyboardHandler(
          getSurfaces: () => [
            const Surface(
              handle: 1,
              pid: 100,
              gid: 100,
              uid: 100,
              title: 'Surface 1',
            ),
            const Surface(
              handle: 2,
              pid: 101,
              gid: 100,
              uid: 100,
              title: 'Surface 2',
            ),
          ],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {
            toggled = true;
          },
          onUpdateCurrentPageIndex: (index) async {
            targetIndex = index;
          },
        )
        // Arrow Right
        ..handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.arrowRight,
            logicalKey: LogicalKeyboardKey.arrowRight,
            timeStamp: Duration.zero,
          ),
        );
      await pumpEventQueue();
      expect(targetIndex, 1);

      // Arrow Left
      controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.arrowLeft,
          logicalKey: LogicalKeyboardKey.arrowLeft,
          timeStamp: Duration.zero,
        ),
      );
      await pumpEventQueue();
      expect(targetIndex, -1);

      // Enter key
      controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.enter,
          logicalKey: LogicalKeyboardKey.enter,
          timeStamp: Duration.zero,
        ),
      );
      await pumpEventQueue();
      expect(toggled, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent launches app on Super key release', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit()
        ..emitState(
          const ConfigurationState(
            configuration: ShellConfiguration(launcher: 'grid2'),
          ),
        );

      controllers
        ..attachKeyboardHandler(
          getSurfaces: () => [],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {},
          onUpdateCurrentPageIndex: (index) async {},
        )
        // Press Super
        ..handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.superKey,
            logicalKey: LogicalKeyboardKey.superKey,
            timeStamp: Duration.zero,
          ),
        );

      // Release Super
      final handled = controllers.handleKeyEvent(
        const KeyUpEvent(
          physicalKey: PhysicalKeyboardKey.superKey,
          logicalKey: LogicalKeyboardKey.superKey,
          timeStamp: Duration.zero,
        ),
      );

      expect(handled, isTrue);
      expect(launchedCommands, contains('grid2'));

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent launches terminal on Super+Enter', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit()
        ..emitState(
          const ConfigurationState(
            configuration: ShellConfiguration(terminal: 'alacritty'),
          ),
        );

      controllers.attachKeyboardHandler(
        getSurfaces: () => [],
        shellBloc: shellBloc,
        configurationCubit: configCubit,
        onToggleOverview: () async {},
        onUpdateCurrentPageIndex: (index) async {},
      );

      // Super + Enter
      final handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.superKey,
          logicalKey: LogicalKeyboardKey.superKey,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);

      final enterHandled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.enter,
          logicalKey: LogicalKeyboardKey.enter,
          timeStamp: Duration.zero,
        ),
      );
      expect(enterHandled, isTrue);
      expect(launchedCommands, contains('alacritty'));

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent forwards regular key to active surface', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit();

      controllers.attachKeyboardHandler(
        getSurfaces: () => [
          const Surface(
            handle: 1,
            pid: 100,
            gid: 100,
            uid: 100,
            title: 'Surface 1',
          ),
        ],
        shellBloc: shellBloc,
        configurationCubit: configCubit,
        onToggleOverview: () async {},
        onUpdateCurrentPageIndex: (index) async {},
      );

      final handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.keyA,
          logicalKey: LogicalKeyboardKey.keyA,
          timeStamp: Duration.zero,
        ),
      );

      expect(handled, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent launches logout on Ctrl+Alt+Delete', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit()
        ..emitState(
          const ConfigurationState(
            configuration: ShellConfiguration(
              systemActions: SystemActions(
                logout: SystemAction(command: 'systemctl poweroff'),
              ),
            ),
          ),
        );

      controllers
        ..attachKeyboardHandler(
          getSurfaces: () => [],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {},
          onUpdateCurrentPageIndex: (index) async {},
        )
        // Ctrl + Alt + Delete
        ..handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.controlLeft,
            logicalKey: LogicalKeyboardKey.controlLeft,
            timeStamp: Duration.zero,
          ),
        )
        ..handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.altLeft,
            logicalKey: LogicalKeyboardKey.altLeft,
            timeStamp: Duration.zero,
          ),
        );
      final handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.delete,
          logicalKey: LogicalKeyboardKey.delete,
          timeStamp: Duration.zero,
        ),
      );

      expect(handled, isTrue);
      expect(launchedCommands, contains('systemctl poweroff'));

      await shellBloc.close();
      await configCubit.close();
    });

    test('handleKeyEvent handles Super + zoom keys and Super + Q', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit();

      controllers
        ..attachKeyboardHandler(
          getSurfaces: () => [
            const Surface(
              handle: 1,
              pid: 100,
              gid: 100,
              uid: 100,
              title: 'Surface 1',
            ),
          ],
          shellBloc: shellBloc,
          configurationCubit: configCubit,
          onToggleOverview: () async {},
          onUpdateCurrentPageIndex: (index) async {},
        )
        // Press Super
        ..handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.superKey,
            logicalKey: LogicalKeyboardKey.superKey,
            timeStamp: Duration.zero,
          ),
        );

      // Super + Equal (zoom in)
      var handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.equal,
          logicalKey: LogicalKeyboardKey.equal,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);

      // Super + Minus (zoom out)
      handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.minus,
          logicalKey: LogicalKeyboardKey.minus,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);

      // Super + 0 (reset zoom)
      handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.digit0,
          logicalKey: LogicalKeyboardKey.digit0,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);

      // Super + Q (close surface)
      handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.keyQ,
          logicalKey: LogicalKeyboardKey.keyQ,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });

    test(
      'handleKeyEvent handles Ctrl + ArrowLeft/Right to switch page',
      () async {
        final shellBloc = ShellBloc();
        final configCubit = TestConfigurationCubit();
        var targetPage = 0;

        controllers
          ..attachKeyboardHandler(
            getSurfaces: () => [
              const Surface(
                handle: 1,
                pid: 100,
                gid: 100,
                uid: 100,
                title: 'Surface 1',
              ),
              const Surface(
                handle: 2,
                pid: 101,
                gid: 100,
                uid: 100,
                title: 'Surface 2',
              ),
            ],
            shellBloc: shellBloc,
            configurationCubit: configCubit,
            onToggleOverview: () async {},
            onUpdateCurrentPageIndex: (index) async {
              targetPage = index;
            },
          )
          // Ctrl + Right
          ..handleKeyEvent(
            const KeyDownEvent(
              physicalKey: PhysicalKeyboardKey.controlLeft,
              logicalKey: LogicalKeyboardKey.controlLeft,
              timeStamp: Duration.zero,
            ),
          );
        var handled = controllers.handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.arrowRight,
            logicalKey: LogicalKeyboardKey.arrowRight,
            timeStamp: Duration.zero,
          ),
        );
        expect(handled, isTrue);
        expect(targetPage, 1);

        // Ctrl + Left
        handled = controllers.handleKeyEvent(
          const KeyDownEvent(
            physicalKey: PhysicalKeyboardKey.arrowLeft,
            logicalKey: LogicalKeyboardKey.arrowLeft,
            timeStamp: Duration.zero,
          ),
        );
        expect(handled, isTrue);
        expect(targetPage, -1);

        await shellBloc.close();
        await configCubit.close();
      },
    );

    test('handleKeyEvent in overview handles escape, up, and down', () async {
      final shellBloc = TestShellBloc()
        ..emitState(const ShellState(isOverview: true));
      final configCubit = TestConfigurationCubit();
      var toggled = false;

      controllers.attachKeyboardHandler(
        getSurfaces: () => [
          const Surface(
            handle: 1,
            pid: 100,
            gid: 100,
            uid: 100,
            title: 'Surface 1',
          ),
        ],
        shellBloc: shellBloc,
        configurationCubit: configCubit,
        onToggleOverview: () async {
          toggled = true;
        },
        onUpdateCurrentPageIndex: (index) async {},
      );

      // Escape
      var handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.escape,
          logicalKey: LogicalKeyboardKey.escape,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);
      expect(toggled, isTrue);

      toggled = false;
      // Arrow Up
      handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.arrowUp,
          logicalKey: LogicalKeyboardKey.arrowUp,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);
      expect(toggled, isTrue);

      toggled = false;
      // Arrow Down
      handled = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.arrowDown,
          logicalKey: LogicalKeyboardKey.arrowDown,
          timeStamp: Duration.zero,
        ),
      );
      expect(handled, isTrue);
      expect(toggled, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });

    test('revealDock forwards animation and hideDock reverses it', () async {
      expect(controllers.dockRevealController.value, equals(1.0));
      controllers.hideDock();
      expect(controllers.dockRevealController.status, isNotNull);

      controllers
        ..revealDock(duration: const Duration(milliseconds: 50))
        ..resetDockTimer(duration: const Duration(milliseconds: 100));
      expect(controllers.dockRevealController.value, isNotNull);
    });

    test('handleKeyEvent toggles fullscreen on F11 and Super+F', () async {
      final shellBloc = ShellBloc();
      final configCubit = TestConfigurationCubit();

      controllers.attachKeyboardHandler(
        getSurfaces: () => [
          const Surface(
            handle: 42,
            pid: 1,
            gid: 1,
            uid: 1,
            title: 'Fullscreen Test',
            fullscreen: 0,
          ),
        ],
        shellBloc: shellBloc,
        configurationCubit: configCubit,
        onToggleOverview: () async {},
        onUpdateCurrentPageIndex: (index) async {},
      );

      // F11
      final handledF11 = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.f11,
          logicalKey: LogicalKeyboardKey.f11,
          timeStamp: Duration.zero,
        ),
      );
      expect(handledF11, isTrue);

      // Super+F
      controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.metaLeft,
          logicalKey: LogicalKeyboardKey.metaLeft,
          timeStamp: Duration.zero,
        ),
      );
      final handledSuperF = controllers.handleKeyEvent(
        const KeyDownEvent(
          physicalKey: PhysicalKeyboardKey.keyF,
          logicalKey: LogicalKeyboardKey.keyF,
          timeStamp: Duration.zero,
        ),
      );
      expect(handledSuperF, isTrue);

      await shellBloc.close();
      await configCubit.close();
    });
  });
}
