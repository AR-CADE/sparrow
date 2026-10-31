import 'dart:ui' show Canvas, FragmentProgram, Offset, Paint, Size;

import 'package:flutter/widgets.dart';
import 'package:flutter_bloc/flutter_bloc.dart' show BlocProvider;
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart'
    show CircularProgressIndicator, MaterialApp, Scaffold;
import 'package:mocktail/mocktail.dart';
import 'package:shell/shell.dart';

import '../domain/background_shader/cubit/background_shader_cubit_test.dart'
    show FakeTickerProvider;

class MockCanvas extends Mock implements Canvas;

void main() {
  setUpAll(() {
    registerFallbackValue(Offset.zero & const Size(1, 1));
    registerFallbackValue(Paint());
  });

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

    testWidgets('renders CustomPaint when shader is loaded and ready', (
      tester,
    ) async {
      final tickerProvider = FakeTickerProvider();
      final cubit = BackgroundShaderCubit(tickerProvider: tickerProvider);

      await tester.pumpWidget(
        BlocProvider<BackgroundShaderCubit>.value(
          value: cubit,
          child: const BackgroundShader(),
        ),
      );
      await tester.pump();
      await tester.pump();

      expect(find.byType(CustomPaint), findsWidgets);
      await cubit.close();
    });

    testWidgets('BackgroundShaderPainter paints and repaints on time change', (
      tester,
    ) async {
      final program = await FragmentProgram.fromAsset('shaders/shader.frag');
      final shader = program.fragmentShader();
      final canvas = MockCanvas();
      final painter1 = BackgroundShaderPainter(shader, 1);
      final painter2 = BackgroundShaderPainter(shader, 2);
      final painter3 = BackgroundShaderPainter(shader, 1);

      painter1.paint(canvas, const Size(800, 600));

      verify(() => canvas.drawRect(any(), any())).called(1);
      expect(painter2.shouldRepaint(painter1), isTrue);
      expect(painter3.shouldRepaint(painter1), isFalse);
    });

    testWidgets('renders when no ambient BackgroundShaderCubit exists', (
      tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: BackgroundShader())),
      );
      expect(find.byType(BackgroundShader), findsOneWidget);
    });
  });
}
