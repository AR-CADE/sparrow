import 'package:bloc_test/bloc_test.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:mocktail/mocktail.dart';
import 'package:shell/src/data/models/shell_configuration.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart';
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart';
import 'package:shell/src/presentation/background.dart';

class MockConfigurationCubit extends MockCubit<ConfigurationState>
    implements ConfigurationCubit;

void main() {
  group('Background', () {
    late MockConfigurationCubit configCubit;

    setUp(() {
      configCubit = MockConfigurationCubit();
    });

    testWidgets(
      'renders fallback pastel blue background when wallpaper is null',
      (tester) async {
        when(() => configCubit.state).thenReturn(const ConfigurationState());

        await tester.pumpWidget(
          MaterialApp(
            home: BlocProvider<ConfigurationCubit>.value(
              value: configCubit,
              child: const Background(),
            ),
          ),
        );

        expect(
          find.byWidgetPredicate(
            (w) => w is ColoredBox && w.color == Background.pastelFlutterBlue,
          ),
          findsOneWidget,
        );
      },
    );

    testWidgets('renders fallback when wallpaper file does not exist', (
      tester,
    ) async {
      when(() => configCubit.state).thenReturn(
        const ConfigurationState(
          configuration: ShellConfiguration(
            wallpaper: '/non/existent/wallpaper.png',
          ),
        ),
      );

      await tester.pumpWidget(
        MaterialApp(
          home: BlocProvider<ConfigurationCubit>.value(
            value: configCubit,
            child: const Background(),
          ),
        ),
      );

      expect(
        find.byWidgetPredicate(
          (w) => w is ColoredBox && w.color == Background.pastelFlutterBlue,
        ),
        findsOneWidget,
      );
    });
  });
}
