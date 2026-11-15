import 'package:compositor_dart/src/data/models/popup.dart' show Popup;
import 'package:compositor_dart/src/presentation/popup_platform_view_controller.dart'
    show PopupPlatformViewController;
import 'package:material_ui/material_ui.dart'
    show
        BuildContext,
        Clip,
        FilterQuality,
        HitTestBehavior,
        IgnorePointer,
        Listener,
        Positioned,
        SizedBox,
        Stack,
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
    this.scaleX = 1.0,
    this.scaleY = 1.0,
    this.filterQuality = FilterQuality.none,
    super.key,
  });

  /// The popup surface model to render.
  final Popup popup;

  /// The horizontal scaling factor applied to the popup.
  final double scaleX;

  /// The vertical scaling factor applied to the popup.
  final double scaleY;

  /// The filter quality used when rendering the underlying texture.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    final texture = Texture(
      textureId: popup.textureId,
      filterQuality: filterQuality,
    );
    if (!popup.acceptsInput) {
      return IgnorePointer(child: texture);
    }

    final inW = (popup.inputWidth ?? popup.width).toDouble() * scaleX;
    final inH = (popup.inputHeight ?? popup.height).toDouble() * scaleY;
    if ((popup.inputWidth != null && inW <= 0) ||
        (popup.inputHeight != null && inH <= 0)) {
      return IgnorePointer(child: texture);
    }

    final controller = PopupPlatformViewController(
      popup: popup,
      scaleX: scaleX,
      scaleY: scaleY,
    );
    final listener = Listener(
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
      child: const SizedBox.expand(),
    );

    if (popup.inputX == 0 &&
        popup.inputY == 0 &&
        (popup.inputWidth == null || popup.inputWidth == popup.width) &&
        (popup.inputHeight == null || popup.inputHeight == popup.height)) {
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
        child: texture,
      );
    }

    return Stack(
      clipBehavior: Clip.none,
      children: [
        IgnorePointer(child: texture),
        Positioned(
          left: popup.inputX.toDouble() * scaleX,
          top: popup.inputY.toDouble() * scaleY,
          width: inW > 0 ? inW : null,
          height: inH > 0 ? inH : null,
          child: listener,
        ),
      ],
    );
  }
}
