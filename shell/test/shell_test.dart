import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/shell.dart';

import 'helpers/mock_compositor_api.dart';

class MockConfigurationCubit extends MockCubit<ConfigurationState>
    implements ConfigurationCubit;

class MockCompositorBloc extends MockBloc<CompositorBlocEvent, CompositorState>
    implements CompositorBloc;

class MockShellBloc extends MockBloc<ShellBlocEvent, ShellState>
    implements ShellBloc;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Shell top-level widget', () {
    late MockConfigurationCubit configCubit;
    late MockCompositorBloc compBloc;

    setUp(() {
      setUpMockCompositorHostApi();
      configCubit = MockConfigurationCubit();
      compBloc = MockCompositorBloc();

      when(() => configCubit.state).thenReturn(const ConfigurationState());
      when(() => compBloc.state).thenReturn(const CompositorState());
    });

    testWidgets('renders Shell with self-managed controllers', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
            ],
            child: const Scaffold(body: Shell()),
          ),
        ),
      );

      expect(find.byType(Shell), findsOneWidget);
      expect(find.byType(ShellView), findsOneWidget);
    });

    testWidgets('renders Shell with provided controllers', (tester) async {
      final controllers = ShellControllers(vsync: const TickerProviderImpl());

      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
            ],
            child: Scaffold(body: Shell(controllers: controllers)),
          ),
        ),
      );

      expect(find.byType(Shell), findsOneWidget);
      expect(find.byType(ShellView), findsOneWidget);
      controllers.dispose();
    });

    testWidgets('renders Shell with controllers from ancestor context', (
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
            child: RepositoryProvider<ShellControllers>.value(
              value: controllers,
              child: const Scaffold(body: Shell()),
            ),
          ),
        ),
      );

      expect(find.byType(Shell), findsOneWidget);
      expect(find.byType(ShellView), findsOneWidget);
      controllers.dispose();
    });

    testWidgets('renders Shell with ShellBloc from ancestor context', (
      tester,
    ) async {
      final shellBloc = MockShellBloc();
      when(() => shellBloc.state).thenReturn(const ShellState());

      await tester.pumpWidget(
        MaterialApp(
          home: MultiBlocProvider(
            providers: [
              BlocProvider<ConfigurationCubit>.value(value: configCubit),
              BlocProvider<CompositorBloc>.value(value: compBloc),
              BlocProvider<ShellBloc>.value(value: shellBloc),
            ],
            child: const Scaffold(body: Shell()),
          ),
        ),
      );

      expect(find.byType(Shell), findsOneWidget);
      expect(find.byType(ShellView), findsOneWidget);
    });
  });
}
