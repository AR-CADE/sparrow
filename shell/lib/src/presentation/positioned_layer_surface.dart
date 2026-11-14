import 'package:compositor_dart/compositor_dart.dart'
    show LayerSurface, LayerSurfaceView;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Positioned,
        SizedBox,
        Stack,
        StackFit,
        StatelessWidget,
        ValueKey,
        Widget;

/// Positioned overlay rendering Wayland layer-shell surfaces
/// (`BOTTOM`, `TOP`, and `OVERLAY` layers) via a reactive [Stack].
class PositionedLayerSurface extends StatelessWidget {
  /// Creates a new [PositionedLayerSurface].
  const new({required this.layerSurfaces, super.key});

  /// The list of layer surfaces to render in this layer stack.
  final List<LayerSurface> layerSurfaces;

  @override
  Widget build(BuildContext context) {
    if (layerSurfaces.isEmpty) {
      return const SizedBox.shrink();
    }

    return Stack(
      fit: StackFit.expand,
      children: [
        for (final surface in layerSurfaces)
          _PositionedLayerSurfaceItem(
            key: ValueKey<int>(surface.handle),
            surface: surface,
          ),
      ],
    );
  }
}

class _PositionedLayerSurfaceItem extends StatelessWidget {
  const new({required this.surface, super.key});

  final LayerSurface surface;

  @override
  Widget build(BuildContext context) {
    final anchorTop = surface.anchorTop;
    final anchorBottom = surface.anchorBottom;
    final anchorLeft = surface.anchorLeft;
    final anchorRight = surface.anchorRight;

    final top = anchorTop
        ? surface.marginTop.toDouble()
        : (!anchorBottom ? surface.y.toDouble() : null);

    final bottom = anchorBottom ? surface.marginBottom.toDouble() : null;

    final left = anchorLeft
        ? surface.marginLeft.toDouble()
        : (!anchorRight ? surface.x.toDouble() : null);

    final right = anchorRight ? surface.marginRight.toDouble() : null;

    final width = (anchorLeft && anchorRight)
        ? null
        : (surface.width > 0
              ? surface.width.toDouble()
              : (surface.bufferWidth > 0
                    ? surface.bufferWidth.toDouble()
                    : null));

    final height = (anchorTop && anchorBottom)
        ? null
        : (surface.height > 0
              ? surface.height.toDouble()
              : (surface.bufferHeight > 0
                    ? surface.bufferHeight.toDouble()
                    : null));

    final isFullscreenTopOverlay =
        surface.layer >= 2 && (surface.anchor & 15) == 15;

    return Positioned(
      top: top,
      bottom: bottom,
      left: left,
      right: right,
      width: width,
      height: height,
      child: LayerSurfaceView(
        layerSurface: surface,
        forwardPanZoom: !isFullscreenTopOverlay,
      ),
    );
  }
}
