import 'package:compositor_dart/compositor_dart.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart' show Size;

class TestPointerEventEncoder with PointerEventEncoder;

void main() {
  group('PointerEventEncoder', () {
    final encoder = TestPointerEventEncoder();

    test('encodes PointerDownEvent correctly', () {
      const event = PointerDownEvent(
        position: Offset(150, 250),
        device: 2,
        kind: PointerDeviceKind.mouse,
      );

      final msg = encoder.encodePointerEvent(
        handle: 42,
        event: event,
        widgetSize: const Size(800, 600),
      );

      expect(msg, isA<SurfacePointerEventMessage>());
      expect(msg.surfaceHandle, equals(42));
      expect(msg.buttons, equals(1));
      expect(msg.device, equals(2));
      expect(msg.deviceKind, equals(pointerKindMouse));
      expect(msg.eventType, equals(pointerDownEvent));
      expect(msg.globalPosX, equals(150));
      expect(msg.globalPosY, equals(250));
      expect(msg.widgetSizeX, equals(800));
      expect(msg.widgetSizeY, equals(600));
      expect(msg.scale, equals(1.0));
      expect(msg.rotation, equals(0.0));
    });

    test('applies coordTransform when provided', () {
      const event = PointerMoveEvent(
        position: Offset(100, 200),
        kind: PointerDeviceKind.mouse,
      );

      final msg = encoder.encodePointerEvent(
        handle: 10,
        event: event,
        widgetSize: const Size(400, 300),
        coordTransform: (offset) => Offset(offset.dx + 5, offset.dy + 10),
      );

      expect(msg.deviceKind, equals(pointerKindMouse));
      expect(msg.eventType, equals(pointerMoveEvent));
      expect(msg.localPosX, equals(event.localPosition.dx + 5));
      expect(msg.localPosY, equals(event.localPosition.dy + 10));
    });

    test('encodes PointerScrollEvent and scroll deltas', () {
      const event = PointerScrollEvent(
        position: Offset(50, 50),
        scrollDelta: Offset(0, 120),
        kind: PointerDeviceKind.trackpad,
      );

      final msg = encoder.encodePointerEvent(
        handle: 1,
        event: event,
        widgetSize: const Size(500, 500),
      );

      expect(msg.deviceKind, equals(pointerKindTrackpad));
      expect(msg.eventType, equals(pointerScrollEvent));
      expect(msg.scrollDeltaX, equals(0));
      expect(msg.scrollDeltaY, equals(120));
    });

    test('encodes PointerPanZoomUpdateEvent correctly', () {
      const event = PointerPanZoomUpdateEvent(
        pan: Offset(12, 34),
        panDelta: Offset(1, 2),
        scale: 1.5,
        rotation: 0.25,
      );

      final msg = encoder.encodePointerEvent(
        handle: 1,
        event: event,
        widgetSize: const Size(500, 500),
      );

      expect(msg.eventType, equals(pointerPanZoomUpdateEvent));
      expect(msg.panX, equals(12));
      expect(msg.panY, equals(34));
      expect(msg.scrollDeltaX, equals(1));
      expect(msg.scrollDeltaY, equals(2));
      expect(msg.scale, equals(1.5));
      expect(msg.rotation, equals(0.25));
    });
  });
}
