import 'package:compositor_dart/data/models/surface.dart' show Surface;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/presentation/pointer_event_encoder.dart'
    show PointerEventEncoder;
import 'package:flutter/services.dart' show PlatformViewController;
import 'package:material_ui/material_ui.dart' show PointerEvent, Size;

class CompositorPlatformViewController extends PlatformViewController
    with PointerEventEncoder {
  CompositorPlatformViewController({required this.surface});
  final Surface surface;
  Size size = const Size(100, 100);

  @override
  Future<void> clearFocus() =>
      CompositorRepository().platform.clearFocus(surface);

  @override
  Future<void> dispatchPointerEvent(PointerEvent event) async {
    if (!CompositorRepository().platform.interactive) {
      return;
    }
    final data = encodePointerEvent(
      handle: surface.handle,
      event: event,
      widgetSize: size,
    );

    await CompositorRepository().platform.channel.invokeMethod(
      'surface_pointer_event',
      data,
    );
  }

  @override
  Future<void> dispose() async {}

  @override
  int get viewId => surface.handle;
}
