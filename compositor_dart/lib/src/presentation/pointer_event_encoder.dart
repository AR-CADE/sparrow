import 'package:compositor_dart/src/core/constants.dart'
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
import 'package:compositor_dart/src/data/models/surface_pointer_event_message.dart'
    show SurfacePointerEventMessage;
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
  /// Encode a PointerEvent into the [SurfacePointerEventMessage] expected by
  /// the C side.
  /// Returns the typed message ready for platform channel invocation.
  SurfacePointerEventMessage encodePointerEvent({
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

    // Apply coordinate transformation if provided, otherwise pass through
    final localPos = coordTransform != null
        ? coordTransform(event.localPosition)
        : event.localPosition;

    return SurfacePointerEventMessage(
      surfaceHandle: handle,
      device: event.device,
      buttons: event.buttons,
      embedderId: event.embedderId,
      deviceKind: deviceKind,
      kind: event.kind.index,
      viewId: event.viewId,
      localPosX: localPos.dx,
      localPosY: localPos.dy,
      globalPosX: event.position.dx,
      globalPosY: event.position.dy,
      platformId: event.platformData,
      pointer: event.pointer,
      widgetSizeX: widgetSize.width,
      widgetSizeY: widgetSize.height,
      scrollDeltaX: scrollAmount.dx,
      scrollDeltaY: scrollAmount.dy,
      panX: pan.dx,
      panY: pan.dy,
      scale: scale,
      rotation: rotation,
      eventType: eventType,
      timestamp: event.timeStamp.inMicroseconds,
    );
  }
}
