import 'package:compositor_dart/src/data/models/surface.dart' show Surface;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/presentation/pointer_event_encoder.dart'
    show PointerEventEncoder;
import 'package:flutter/gestures.dart' show PointerScrollEvent;
import 'package:flutter/services.dart'
    show HardwareKeyboard, PlatformViewController;
import 'package:material_ui/material_ui.dart' show PointerEvent, Size;

/// Platform view controller that translates Flutter pointer events for a
/// compositor [Surface].
class CompositorPlatformViewController extends PlatformViewController
    with PointerEventEncoder {
  /// Creates a [CompositorPlatformViewController] for the given [surface].
  new({required this.surface});

  /// The surface associated with this platform view controller.
  final Surface surface;

  /// The rendered size of the widget displaying the surface.
  Size size = const Size(100, 100);

  @override
  Future<void> clearFocus() =>
      CompositorRepository().platform.clearFocus(surface);

  @override
  Future<void> dispatchPointerEvent(PointerEvent event) async {
    if (!CompositorRepository().platform.interactive) {
      return;
    }
    if (event is PointerScrollEvent &&
        HardwareKeyboard.instance.isMetaPressed) {
      return;
    }
    final data = encodePointerEvent(
      handle: surface.handle,
      event: event,
      widgetSize: size,
    );

    await CompositorRepository().platform.surfacePointerEvent(data);
  }

  @override
  Future<void> dispose() async {}

  @override
  int get viewId => surface.handle;
}
