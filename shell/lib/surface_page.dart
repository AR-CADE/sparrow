import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/presentation/surface.dart' show SurfaceView;
import 'package:material_ui/material_ui.dart'
    show BuildContext, RepaintBoundary, SizedBox, StatelessWidget, Widget;

class SurfacePage extends StatelessWidget {
  const new({
    required this.surface,
    this.interactive = true,
    this.freeze = false,
    this.toggleOverview,
    super.key,
  });

  final Surface? surface;
  final bool interactive;
  final bool freeze;
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
