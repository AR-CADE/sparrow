import 'package:compositor_dart/src/data/models/zoom_event.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('ZoomEvent', () {
    test('ZoomScrollEvent fromJson, toJson, and properties', () {
      const event = ZoomScrollEvent(delta: 1.5, x: 100, y: 200);
      expect(event.delta, equals(1.5));
      expect(event.x, equals(100.0));
      expect(event.y, equals(200.0));
      expect(event.props, equals([1.5, 100.0, 200.0]));

      final json = <String, dynamic>{'delta': 0.5, 'x': 50.0, 'y': 75.0};
      final deserialized = ZoomScrollEvent.fromJson(json);
      expect(deserialized.delta, equals(0.5));
      expect(deserialized.x, equals(50.0));
      expect(deserialized.y, equals(75.0));
      expect(deserialized.toJson(), equals(json));

      final fallback = ZoomScrollEvent.fromJson(const {});
      expect(fallback.delta, equals(0.0));
      expect(fallback.x, equals(0.0));
      expect(fallback.y, equals(0.0));
    });

    test('ZoomKeyEvent fromJson, toJson, and properties', () {
      const event = ZoomKeyEvent(action: 1, x: 300, y: 400);
      expect(event.action, equals(1));
      expect(event.x, equals(300.0));
      expect(event.y, equals(400.0));
      expect(event.props, equals([1, 300.0, 400.0]));

      final json = <String, dynamic>{'action': -1, 'x': 150.0, 'y': 250.0};
      final deserialized = ZoomKeyEvent.fromJson(json);
      expect(deserialized.action, equals(-1));
      expect(deserialized.x, equals(150.0));
      expect(deserialized.y, equals(250.0));
      expect(deserialized.toJson(), equals(json));

      final fallback = ZoomKeyEvent.fromJson(const {});
      expect(fallback.action, equals(0));
      expect(fallback.x, equals(0.0));
      expect(fallback.y, equals(0.0));
    });
  });
}
