import 'package:compositor_dart/src/data/models/layer_surface.dart'
    show LayerSurface;
import 'package:compositor_dart/src/domain/layer_surface/cubit/layer_surface_cubit.dart';
import 'package:compositor_dart/src/domain/layer_surface/cubit/layer_surface_state.dart';
import 'package:compositor_dart/src/presentation/layer_surface_platform_view_controller.dart'
    show LayerSurfacePlatformViewController;
import 'package:compositor_dart/src/presentation/surface.dart' show SubSurfaces;
import 'package:flutter_bloc/flutter_bloc.dart'
    show BlocBuilder, BlocProvider, ReadContext;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Clip,
        FilterQuality,
        HitTestBehavior,
        Listener,
        MouseRegion,
        SizedBox,
        Stack,
        StackFit,
        StatelessWidget,
        Texture,
        ValueKey,
        Widget;

/// Widget rendering a Wayland layer-shell surface (wlr_layer_surface_v1)
/// together with any child subsurfaces attached to it.
class LayerSurfaceView extends StatelessWidget {
  /// Creates a [LayerSurfaceView] widget.
  const new({
    required this.layerSurface,
    this.freeze = false,
    this.filterQuality = FilterQuality.none,
    this.forwardPanZoom = true,
    this.cubit,
    super.key,
  });

  /// The layer surface entity to render.
  final LayerSurface layerSurface;

  /// Whether to freeze texture updates on this surface.
  final bool freeze;

  /// The filter quality used when rendering the underlying texture.
  final FilterQuality filterQuality;

  /// Whether trackpad pan/zoom events are forwarded to this Wayland surface.
  ///
  /// When `false`, pan/zoom events are not handled by this surface's [Listener]
  /// so an ancestor (e.g. desktop PageView navigation) can intercept them.
  final bool forwardPanZoom;

  /// Optional external cubit (primarily for testing).
  final LayerSurfaceCubit? cubit;

  @override
  Widget build(BuildContext context) {
    final content = _LayerSurfaceContent(
      layerSurface: layerSurface,
      freeze: freeze,
      filterQuality: filterQuality,
      forwardPanZoom: forwardPanZoom,
    );

    if (cubit != null) {
      return BlocProvider<LayerSurfaceCubit>.value(
        value: cubit!,
        child: content,
      );
    }

    return BlocProvider<LayerSurfaceCubit>(
      key: ValueKey<int>(layerSurface.handle),
      create: (_) => LayerSurfaceCubit(layerSurface: layerSurface),
      child: content,
    );
  }
}

class _LayerSurfaceContent extends StatelessWidget {
  const new({
    required this.layerSurface,
    required this.freeze,
    required this.filterQuality,
    required this.forwardPanZoom,
  });

  final LayerSurface layerSurface;
  final bool freeze;
  final FilterQuality filterQuality;
  final bool forwardPanZoom;

  @override
  Widget build(BuildContext context) {
    context.read<LayerSurfaceCubit>().updateLayerSurface(layerSurface);

    final controller = LayerSurfacePlatformViewController(
      layerSurface: layerSurface,
    );

    final Widget textureWidget = Texture(
      freeze: freeze,
      textureId: layerSurface.textureId,
      filterQuality: filterQuality,
    );

    final w = layerSurface.width > 0 ? layerSurface.width.toDouble() : null;
    final h = layerSurface.height > 0 ? layerSurface.height.toDouble() : null;

    final sized = (w != null || h != null)
        ? SizedBox(width: w, height: h, child: textureWidget)
        : textureWidget;

    final stacked = BlocBuilder<LayerSurfaceCubit, LayerSurfaceState>(
      builder: (context, state) {
        if (state.subsurfaces.isEmpty) {
          return sized;
        }
        return Stack(
          clipBehavior: Clip.none,
          fit: StackFit.passthrough,
          children: [
            sized,
            SubSurfaces(
              subsurfaces: state.subsurfaces,
              filterQuality: filterQuality,
            ),
          ],
        );
      },
    );

    return MouseRegion(
      onEnter: controller.dispatchPointerEvent,
      onExit: controller.dispatchPointerEvent,
      child: Listener(
        onPointerDown: controller.dispatchPointerEvent,
        onPointerMove: controller.dispatchPointerEvent,
        onPointerUp: controller.dispatchPointerEvent,
        onPointerHover: controller.dispatchPointerEvent,
        onPointerCancel: controller.dispatchPointerEvent,
        onPointerSignal: controller.dispatchPointerEvent,
        onPointerPanZoomStart: forwardPanZoom
            ? controller.dispatchPointerEvent
            : null,
        onPointerPanZoomUpdate: forwardPanZoom
            ? controller.dispatchPointerEvent
            : null,
        onPointerPanZoomEnd: forwardPanZoom
            ? controller.dispatchPointerEvent
            : null,
        behavior: HitTestBehavior.opaque,
        child: stacked,
      ),
    );
  }
}
