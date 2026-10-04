import 'package:compositor_dart/data/models/popup.dart' show Popup;
import 'package:compositor_dart/presentation/popup_platform_view_controller.dart'
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

/// Widget for rendering popup surfaces (menus, dropdowns, tooltips).
/// Handles input through Flutter and forwards to wlroots via platform channel.
class PopupView extends StatelessWidget {
  const new({
    required this.popup,
    required this.freeze,
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });
  final Popup popup;
  final bool freeze;
  final double scaleX;
  final double scaleY;
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
