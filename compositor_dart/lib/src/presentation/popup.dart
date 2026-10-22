import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/presentation/popup_platform_view_controller.dart'
    show PopupPlatformViewController;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        FilterQuality,
        HitTestBehavior,
        Listener,
        StatelessWidget,
        Texture,
        Widget;

/// Widget for rendering popup surfaces such as menus, dropdowns, and tooltips.
///
/// Handles input through Flutter and forwards to wlroots via platform channel.
class PopupView extends StatelessWidget {
  /// Creates a [PopupView] widget.
  const new({
    required this.popup,
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The popup surface model to render.
  final Popup popup;

  /// Whether to freeze texture updates on this popup.
  final bool freeze;

  /// The horizontal scaling factor applied to the popup.
  final double scaleX;

  /// The vertical scaling factor applied to the popup.
  final double scaleY;

  /// The filter quality used when rendering the underlying texture.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    final controller = PopupPlatformViewController(
      popup: popup,
      scaleX: scaleX,
      scaleY: scaleY,
    );
    // Use Listener to capture all pointer events and forward to wlroots
    // This maintains Flutter-first architecture while enabling popup input
    return Listener(
      onPointerDown: controller.dispatchPointerEvent,
      onPointerMove: controller.dispatchPointerEvent,
      onPointerUp: controller.dispatchPointerEvent,
      onPointerHover: controller.dispatchPointerEvent,
      onPointerCancel: controller.dispatchPointerEvent,
      onPointerSignal: controller.dispatchPointerEvent,
      onPointerPanZoomStart: controller.dispatchPointerEvent,
      onPointerPanZoomUpdate: controller.dispatchPointerEvent,
      onPointerPanZoomEnd: controller.dispatchPointerEvent,
      behavior: HitTestBehavior.opaque,
      child: Texture(
        freeze: freeze,
        textureId: popup.textureId,
        filterQuality: filterQuality,
      ),
    );
  }
}
