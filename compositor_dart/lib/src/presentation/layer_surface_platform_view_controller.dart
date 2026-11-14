import 'package:compositor_dart/src/data/models/layer_surface.dart'
    show LayerSurface;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/presentation/pointer_event_encoder.dart'
    show PointerEventEncoder;
import 'package:material_ui/material_ui.dart' show PointerEvent, Size;

/// Controller for dispatching pointer events from LayerSurfaceView to wlroots.
class LayerSurfacePlatformViewController with PointerEventEncoder {
  /// Creates a [LayerSurfacePlatformViewController] for the given
  /// [layerSurface].
  const new({required this.layerSurface});

  /// The layer-surface model associated with this controller.
  final LayerSurface layerSurface;

  /// The surface size in logical pixels for coordinate calculations.
  Size get size => Size(
    layerSurface.width > 0 ? layerSurface.width.toDouble() : 100,
    layerSurface.height > 0 ? layerSurface.height.toDouble() : 100,
  );

  /// Dispatches an encoded [event] to the native compositor layer-surface
  /// handler.
  Future<void> dispatchPointerEvent(PointerEvent event) async {
    if (!CompositorRepository().platform.interactive) {
      return;
    }
    final message = encodePointerEvent(
      handle: layerSurface.handle,
      event: event,
      widgetSize: size,
    );

    await CompositorRepository().platform.layerSurfacePointerEvent(message);
  }
}
