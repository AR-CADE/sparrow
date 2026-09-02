import 'package:compositor_dart/core/constants.dart'
    show
        pointerCancelEvent,
        pointerDownEvent,
        pointerEnterEvent,
        pointerExitEvent,
        pointerHoverEvent,
        pointerKindMouse,
        pointerKindTouch,
        pointerKindTrackpad,
        pointerKindUnknown,
        pointerMoveEvent,
        pointerPanZoomEndEvent,
        pointerPanZoomStartEvent,
        pointerPanZoomUpdateEvent,
        pointerScrollEvent,
        pointerUnknownEvent,
        pointerUpEvent;
import 'package:flutter/gestures.dart'
    show
        PointerCancelEvent,
        PointerDeviceKind,
        PointerEnterEvent,
        PointerExitEvent,
        PointerHoverEvent,
        PointerPanZoomEndEvent,
        PointerPanZoomStartEvent,
        PointerPanZoomUpdateEvent,
        PointerScrollEvent;
import 'package:material_ui/material_ui.dart'
    show
        Offset,
        PointerDownEvent,
        PointerEvent,
        PointerMoveEvent,
        PointerUpEvent,
        Size;

/// Shared mixin for encoding pointer events to the platform channel format.
/// Used by both surface and popup event dispatchers.
mixin PointerEventEncoder {
  /// Encode a PointerEvent into the list format expected by the C side.
  /// Returns the encoded data list ready for platform channel invocation.
  List<dynamic> encodePointerEvent({
    required int handle,
    required PointerEvent event,
    required Size widgetSize,
    Offset Function(Offset)? coordTransform,
  }) {
    final int deviceKind;

    switch (event.kind) {
      case PointerDeviceKind.mouse:
        deviceKind = pointerKindMouse;
      case PointerDeviceKind.trackpad:
        deviceKind = pointerKindTrackpad;
      case PointerDeviceKind.invertedStylus:
      case PointerDeviceKind.stylus:
      case PointerDeviceKind.touch:
        deviceKind = pointerKindTouch;
      case PointerDeviceKind.unknown:
        deviceKind = pointerKindUnknown;
    }

    // print('kind ${event.kind}');

    final int eventType;
    var scrollAmount = Offset.zero;
    var pan = Offset.zero;
    var scale = 1.0;
    var rotation = 0.0;

    if (event is PointerDownEvent) {
      eventType = pointerDownEvent;
    } else if (event is PointerUpEvent) {
      eventType = pointerUpEvent;
    } else if (event is PointerCancelEvent) {
      eventType = pointerCancelEvent;
    } else if (event is PointerHoverEvent) {
      eventType = pointerHoverEvent;
    } else if (event is PointerMoveEvent) {
      eventType = pointerMoveEvent;
    } else if (event is PointerEnterEvent) {
      eventType = pointerEnterEvent;
    } else if (event is PointerExitEvent) {
      eventType = pointerExitEvent;
    } else if (event is PointerScrollEvent) {
      eventType = pointerScrollEvent;
      scrollAmount = event.scrollDelta;
    } else if (event is PointerPanZoomStartEvent) {
      eventType = pointerPanZoomStartEvent;
    } else if (event is PointerPanZoomUpdateEvent) {
      eventType = pointerPanZoomUpdateEvent;
      pan = event.pan;
      scrollAmount = event.panDelta;
      scale = event.scale;
      rotation = event.rotation;
    } else if (event is PointerPanZoomEndEvent) {
      eventType = pointerPanZoomEndEvent;
    } else {
      eventType = pointerUnknownEvent;
    }

    //print('event: ${event.toString()}');

    // Apply coordinate transformation if provided, otherwise pass through
    final localPos = coordTransform != null
        ? coordTransform(event.localPosition)
        : event.localPosition;

    return [
      handle,
      event.buttons,
      event.delta.dx,
      event.delta.dy,
      event.device,
      event.distance,
      event.down,
      event.embedderId,
      deviceKind,
      event.localDelta.dx,
      event.localDelta.dy,
      localPos.dx,
      localPos.dy,
      event.obscured,
      event.orientation,
      event.platformData,
      event.pointer,
      event.position.dx,
      event.position.dy,
      event.pressure,
      event.radiusMajor,
      event.radiusMinor,
      event.size,
      event.synthesized,
      event.tilt,
      event.timeStamp.inMicroseconds,
      eventType,
      widgetSize.width,
      widgetSize.height,
      scrollAmount.dx,
      scrollAmount.dy,
      event.kind.index,
      event.viewId,
      pan.dx,
      pan.dy,
      scale,
      rotation,
    ];
  }
}
