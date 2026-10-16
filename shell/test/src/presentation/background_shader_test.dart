import 'package:flutter_bloc/flutter_bloc.dart' show BlocProvider;
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show CircularProgressIndicator;
import 'package:shell/shell.dart';

import '../domain/background_shader/cubit/background_shader_cubit_test.dart'
    show FakeTickerProvider;

void main() {
  group('BackgroundShader widget', () {
    testWidgets('renders CircularProgressIndicator when shader is loading', (
      tester,
    ) async {
      final tickerProvider = FakeTickerProvider();
      final cubit = BackgroundShaderCubit(
        tickerProvider: tickerProvider,
        shaderLoader: () async => throw Exception('Not loaded in test'),
      );

      await tester.pumpWidget(
        BlocProvider<BackgroundShaderCubit>.value(
          value: cubit,
          child: const BackgroundShader(),
        ),
      );

      expect(find.byType(CircularProgressIndicator), findsOneWidget);

      await tester.pump();
      await cubit.close();
    });
  });
}
