import 'package:compositor_dart/compositor_dart.dart' show Surface, SurfaceView;
import 'package:material_ui/material_ui.dart'
    show BuildContext, RepaintBoundary, SizedBox, StatelessWidget, Widget;

/// Page widget rendering a single compositor [SurfaceView].
class SurfacePage extends StatelessWidget {
  /// Creates a new [SurfacePage].
  const new({
    required this.surface,
    this.interactive = true,
    this.freeze = false,
    this.toggleOverview,
    super.key,
  });

  /// The surface to render.
  final Surface? surface;

  /// Whether the surface accepts interactive input.
  final bool interactive;

  /// Whether rendering of the surface is frozen.
  final bool freeze;

  /// Callback to toggle overview.
  final Future<void> Function()? toggleOverview;

  @override
  Widget build(BuildContext context) {
    final surf = surface;
    return surf != null
        ? RepaintBoundary(
            child: SurfaceView(
              freeze: freeze,
              surface: surf,
              interactive: interactive,
            ),
          )
        : const SizedBox.shrink();
  }
}
