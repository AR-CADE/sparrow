import 'package:bloc_test/bloc_test.dart';
import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/presentation/surface_page.dart';

import '../../helpers/mock_compositor_api.dart';

class MockConfigurationCubit extends MockCubit<ConfigurationState>
    implements ConfigurationCubit;

void main() {
  setUp(setUpMockCompositorHostApi);

  group('SurfacePage', () {
    late MockConfigurationCubit configCubit;

    setUp(() {
      configCubit = MockConfigurationCubit();
      when(() => configCubit.state).thenReturn(const ConfigurationState());
    });

    testWidgets('renders SizedBox.shrink when surface is null', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: BlocProvider<ConfigurationCubit>.value(
            value: configCubit,
            child: const Scaffold(body: SurfacePage(surface: null)),
          ),
        ),
      );

      expect(find.byType(SizedBox), findsOneWidget);
    });

    testWidgets(
      'renders SurfaceView when surface is provided with default filterQuality',
      (tester) async {
        const surface = Surface(
          handle: 1,
          pid: 100,
          gid: 100,
          uid: 100,
          title: 'App 1',
        );

        await tester.pumpWidget(
          MaterialApp(
            home: BlocProvider<ConfigurationCubit>.value(
              value: configCubit,
              child: const Scaffold(body: SurfacePage(surface: surface)),
            ),
          ),
        );

        expect(find.byType(SurfaceView), findsOneWidget);
        final surfaceView = tester.widget<SurfaceView>(
          find.byType(SurfaceView),
        );
        expect(surfaceView.filterQuality, equals(FilterQuality.none));
      },
    );

    testWidgets('renders SurfaceView with configured filterQuality', (
      tester,
    ) async {
      when(() => configCubit.state).thenReturn(
        const ConfigurationState(
          configuration: ShellConfiguration(
            filterQuality: FilterQuality.medium,
          ),
        ),
      );

      const surface = Surface(
        handle: 1,
        pid: 100,
        gid: 100,
        uid: 100,
        title: 'App 1',
      );

      await tester.pumpWidget(
        MaterialApp(
          home: BlocProvider<ConfigurationCubit>.value(
            value: configCubit,
            child: const Scaffold(body: SurfacePage(surface: surface)),
          ),
        ),
      );

      expect(find.byType(SurfaceView), findsOneWidget);
      final surfaceView = tester.widget<SurfaceView>(find.byType(SurfaceView));
      expect(surfaceView.filterQuality, equals(FilterQuality.medium));
    });
  });
}
