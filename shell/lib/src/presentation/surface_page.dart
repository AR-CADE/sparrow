import 'package:compositor_dart/compositor_dart.dart' show Surface, SurfaceView;
import 'package:flutter_bloc/flutter_bloc.dart' show BlocSelector;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        FilterQuality,
        RepaintBoundary,
        SizedBox,
        StatelessWidget,
        Widget;
import 'package:shell/src/domain/configuration/cubit/configuration_cubit.dart'
    show ConfigurationCubit;
import 'package:shell/src/domain/configuration/cubit/configuration_state.dart'
    show ConfigurationState;

/// Page widget rendering a single compositor [SurfaceView].
class SurfacePage extends StatelessWidget {
  /// Creates a new [SurfacePage].
  const new({
    required this.surface,
    this.interactive = true,
    this.freeze = false,
    this.cornerRadius = 0.0,
    this.toggleOverview,
    super.key,
  });

  /// The surface to render.
  final Surface? surface;

  /// Whether the surface accepts interactive input.
  final bool interactive;

  /// Whether rendering of the surface is frozen.
  final bool freeze;

  /// Effective corner radius applied by parent overview container.
  final double cornerRadius;

  /// Callback to toggle overview.
  final Future<void> Function()? toggleOverview;

  @override
  Widget build(BuildContext context) {
    final surf = surface;
    return surf != null
        ? BlocSelector<ConfigurationCubit, ConfigurationState, FilterQuality>(
            selector: (state) =>
                state.configuration.filterQuality ?? FilterQuality.none,
            builder: (context, filterQuality) {
              return RepaintBoundary(
                child: SurfaceView(
                  freeze: freeze,
                  surface: surf,
                  interactive: interactive,
                  filterQuality: filterQuality,
                  cornerRadius: cornerRadius,
                ),
              );
            },
          )
        : const SizedBox.shrink();
  }
}
