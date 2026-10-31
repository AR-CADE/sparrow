import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocBuilder, BlocProvider, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Center,
        CircularProgressIndicator,
        CustomPaint,
        StatelessWidget,
        Widget;
import 'package:shell/src/domain/background_shader/cubit/background_shader_cubit.dart'
    show BackgroundShaderCubit;
import 'package:shell/src/domain/background_shader/cubit/background_shader_state.dart'
    show BackgroundShaderState;
import 'package:shell/src/presentation/background_shader_painter.dart'
    show BackgroundShaderPainter;

/// Dynamic fragment shader background widget.
class BackgroundShader extends StatelessWidget {
  /// Creates a new [BackgroundShader].
  const new({super.key});

  @override
  Widget build(BuildContext context) {
    BackgroundShaderCubit? cubit;
    try {
      cubit = context.read<BackgroundShaderCubit>();
    } on Object catch (_) {}

    if (cubit != null) {
      return const _BackgroundShaderContent();
    }

    return BlocProvider(
      create: (_) => BackgroundShaderCubit(),
      child: const _BackgroundShaderContent(),
    );
  }
}

class _BackgroundShaderContent extends StatelessWidget {
  const new();

  @override
  Widget build(BuildContext context) {
    return BlocBuilder<BackgroundShaderCubit, BackgroundShaderState>(
      builder: (context, state) {
        final shader = state.shader;
        if (shader == null) {
          return const Center(child: CircularProgressIndicator());
        }
        return CustomPaint(
          painter: BackgroundShaderPainter(shader, state.delta),
        );
      },
    );
  }
}
