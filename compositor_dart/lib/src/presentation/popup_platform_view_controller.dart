/// @docImport 'package:compositor_dart/src/presentation/compositor_platform_view_controller.dart';
library;

import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/data/repositories/compositor/compositor_repository.dart'
    show CompositorRepository;
import 'package:compositor_dart/src/presentation/pointer_event_encoder.dart'
    show PointerEventEncoder;
import 'package:material_ui/material_ui.dart' show PointerEvent, Size;

/// Controller for dispatching pointer events from PopupView to wlroots.
///
/// Follows the same pattern as [CompositorPlatformViewController] but uses
/// the popup pointer event channel.
class PopupPlatformViewController with PointerEventEncoder {
  /// Creates a [PopupPlatformViewController] for the given [popup].
  const new({required this.popup, this.scaleX = 1.0, this.scaleY = 1.0});

  /// The popup model associated with this controller.
  final Popup popup;

  /// Horizontal scaling factor applied to the popup.
  final double scaleX;

  /// Vertical scaling factor applied to the popup.
  final double scaleY;

  /// The popup size in logical pixels for coordinate calculations.
  Size get size => Size(
    (popup.width > 0 ? popup.width.toDouble() : 100) * scaleX,
    (popup.height > 0 ? popup.height.toDouble() : 100) * scaleY,
  );

  /// Dispatches an encoded [event] to the native compositor popup handler.
  Future<void> dispatchPointerEvent(PointerEvent event) async {
    if (!CompositorRepository().platform.interactive) {
      return;
    }
    final data = encodePointerEvent(
      handle: popup.handle,
      event: event,
      widgetSize: size,
    );

    await CompositorRepository().platform.popupPointerEvent(data);
  }
}
