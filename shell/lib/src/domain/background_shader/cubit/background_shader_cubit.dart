import 'dart:async';
import 'dart:ui' show FragmentProgram, FragmentShader;

import 'package:flutter/scheduler.dart' show Ticker, TickerProvider;
import 'package:flutter_bloc/flutter_bloc.dart' show Cubit;
import 'package:shell/src/core/ticker_provider.dart' show TickerProviderImpl;
import 'package:shell/src/domain/background_shader/cubit/background_shader_state.dart';

/// Signature for loading a [FragmentShader].
typedef ShaderLoader = Future<FragmentShader> Function();

/// Cubit managing loading and time ticker for background shader rendering.
class BackgroundShaderCubit extends Cubit<BackgroundShaderState> {
  /// Creates a new [BackgroundShaderCubit].
  new({TickerProvider? tickerProvider, ShaderLoader? shaderLoader})
    : _tickerProvider = tickerProvider ?? const TickerProviderImpl(),
      _shaderLoader = shaderLoader ?? _defaultShaderLoader,
      super(const BackgroundShaderState()) {
    _init();
  }

  final TickerProvider _tickerProvider;
  final ShaderLoader _shaderLoader;
  Ticker? _ticker;
  Duration _lastElapsed = Duration.zero;

  static Future<FragmentShader> _defaultShaderLoader() async {
    final program = await FragmentProgram.fromAsset('shaders/shader.frag');
    return program.fragmentShader();
  }

  void _init() {
    _ticker = _tickerProvider.createTicker(_onTick);
    unawaited(_loadShader());
  }

  void _onTick(Duration elapsed) {
    if (isClosed) return;
    final dt = (elapsed - _lastElapsed).inMicroseconds / 1000000.0;
    _lastElapsed = elapsed;
    emit(state.copyWith(delta: state.delta + dt));
  }

  Future<void> _loadShader() async {
    try {
      final shader = await _shaderLoader();
      if (isClosed) return;
      emit(state.copyWith(shader: shader));
      if (_ticker != null && !_ticker!.isActive) {
        _ticker!.start();
      }
    } on Object catch (_) {
      // Ignored: shader loading error fallback
    }
  }

  @override
  Future<void> close() {
    _ticker?.dispose();
    _ticker = null;
    return super.close();
  }
}
