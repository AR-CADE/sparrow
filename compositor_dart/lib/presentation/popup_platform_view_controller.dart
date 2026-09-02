import 'package:compositor_dart/data/models/popup.dart' show Popup;
import 'package:compositor_dart/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/presentation/pointer_event_encoder.dart'
    show PointerEventEncoder;
import 'package:material_ui/material_ui.dart' show PointerEvent, Size;

/// Controller for dispatching pointer events from PopupView to wlroots.
/// Follows the same pattern as CompositorPlatformViewController but uses
/// popup_pointer_event channel method.
class PopupPlatformViewController with PointerEventEncoder {
  const PopupPlatformViewController({
    required this.popup,
    this.ratio = 1.0,
  });
  final Popup popup;
  final double ratio;

  /// Get the popup size for coordinate calculations
  Size get size => Size(
    (popup.width > 0 ? popup.width.toDouble() : 100) * ratio,
    (popup.height > 0 ? popup.height.toDouble() : 100) * ratio,
  );

  Future<void> dispatchPointerEvent(PointerEvent event) async {
    if (!CompositorRepository().platform.interactive) {
      return;
    }
    final data = encodePointerEvent(
      handle: popup.handle,
      event: event,
      widgetSize: size,
    );

    await CompositorRepository().platform.channel.invokeMethod(
      'popup_pointer_event',
      data,
    );
  }
}
