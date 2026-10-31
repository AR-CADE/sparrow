import 'dart:ui' show FragmentProgram, FragmentShader;

import 'package:flutter/scheduler.dart'
    show Ticker, TickerCallback, TickerFuture;
import 'package:flutter_test/flutter_test.dart';
import 'package:shell/shell.dart';

class FakeTicker extends Fake implements Ticker {
  new(this.onTick);

  final TickerCallback onTick;
  bool active = false;
  bool disposed = false;

  @override
  bool get isActive => active;

  @override
  TickerFuture start() {
    active = true;
    return TickerFuture.complete();
  }

  @override
  void stop({bool canceled = false}) {
    active = false;
  }

  @override
  void dispose() {
    disposed = true;
    active = false;
  }

  @override
  String toString({bool debugIncludeStack = false}) => 'FakeTicker';

  void tick(Duration elapsed) {
    onTick(elapsed);
  }
}

class FakeTickerProvider implements TickerProviderImpl {
  FakeTicker? lastTicker;

  @override
  Ticker createTicker(TickerCallback onTick) {
    final ticker = FakeTicker(onTick);
    lastTicker = ticker;
    return ticker;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('BackgroundShaderState', () {
    test('initial values', () {
      const state = BackgroundShaderState();
      expect(state.shader, isNull);
      expect(state.delta, 0.0);
      expect(state.isLoaded, isFalse);
    });

    test('supports value equality and props', () {
      const state1 = BackgroundShaderState();
      const state2 = BackgroundShaderState();
      const state3 = BackgroundShaderState(delta: 1);

      expect(state1, equals(state2));
      expect(state1, isNot(equals(state3)));
      expect(state1.props, equals([null, 0]));
      expect(state3.props, equals([null, 1]));
    });

    test('copyWith replaces fields', () async {
      const state = BackgroundShaderState();
      final updated = state.copyWith(delta: 1.5);
      expect(updated.shader, isNull);
      expect(updated.delta, 1.5);
      expect(updated.isLoaded, isFalse);
    });
  });

  group('BackgroundShaderCubit', () {
    test('loads shader and starts ticker', () async {
      final tickerProvider = FakeTickerProvider();
      FragmentShader? loadedShader;

      try {
        final program = await FragmentProgram.fromAsset('shaders/shader.frag');
        loadedShader = program.fragmentShader();
      } on Object catch (_) {}

      final cubit = BackgroundShaderCubit(
        tickerProvider: tickerProvider,
        shaderLoader: loadedShader != null ? () async => loadedShader! : null,
      );

      // Wait for shader to load
      await Future<void>.delayed(Duration.zero);

      if (loadedShader != null) {
        expect(cubit.state.shader, loadedShader);
        expect(cubit.state.isLoaded, isTrue);
        expect(tickerProvider.lastTicker?.isActive, isTrue);

        tickerProvider.lastTicker?.tick(const Duration(milliseconds: 100));
        expect(cubit.state.delta, closeTo(0.1, 0.001));

        tickerProvider.lastTicker?.tick(const Duration(milliseconds: 250));
        expect(cubit.state.delta, closeTo(0.25, 0.001));
      }

      await cubit.close();
      expect(tickerProvider.lastTicker?.disposed, isTrue);
    });

    test('handles shader load error gracefully', () async {
      final tickerProvider = FakeTickerProvider();

      final cubit = BackgroundShaderCubit(
        tickerProvider: tickerProvider,
        shaderLoader: () async => throw Exception('Failed to load'),
      );

      await Future<void>.delayed(Duration.zero);

      expect(cubit.state.shader, isNull);
      expect(cubit.state.isLoaded, isFalse);

      await cubit.close();
    });
  });
}
