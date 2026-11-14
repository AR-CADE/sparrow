import 'dart:ui' show FragmentShader;

import 'package:equatable/equatable.dart';

/// State for the background shader rendering.
class BackgroundShaderState extends Equatable {
  /// Creates a new [BackgroundShaderState].
  const new({this.shader, this.delta = 0.0});

  /// The loaded fragment shader, or null if loading.
  final FragmentShader? shader;

  /// The accumulated delta time in seconds.
  final double delta;

  /// Whether the shader is loaded.
  bool get isLoaded => shader != null;

  /// Creates a copy with the given fields replaced.
  BackgroundShaderState copyWith({FragmentShader? shader, double? delta}) {
    return BackgroundShaderState(
      shader: shader ?? this.shader,
      delta: delta ?? this.delta,
    );
  }

  @override
  List<Object?> get props => [shader, delta];
}
