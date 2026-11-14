import 'package:compositor_dart/src/data/models/gesture_swipe_event.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('GestureSwipeEvent', () {
    test('GestureSwipeBeginEvent fromJson, toJson, and properties', () {
      const event = GestureSwipeBeginEvent(fingers: 3, timeMsec: 12345);
      expect(event.fingers, equals(3));
      expect(event.timeMsec, equals(12345));
      expect(event.props, equals([3, 12345]));

      final json = <String, dynamic>{'fingers': 4, 'time_msec': 54321};
      final deserialized = GestureSwipeBeginEvent.fromJson(json);
      expect(deserialized.fingers, equals(4));
      expect(deserialized.timeMsec, equals(54321));
      expect(deserialized.toJson(), equals(json));

      final fallback = GestureSwipeBeginEvent.fromJson(const {});
      expect(fallback.fingers, equals(3));
      expect(fallback.timeMsec, equals(0));
    });

    test('GestureSwipeUpdateEvent fromJson, toJson, and properties', () {
      const event = GestureSwipeUpdateEvent(
        dx: 10.5,
        dy: -5.2,
        timeMsec: 12345,
      );
      expect(event.dx, equals(10.5));
      expect(event.dy, equals(-5.2));
      expect(event.timeMsec, equals(12345));
      expect(event.props, equals([10.5, -5.2, 12345]));

      final json = <String, dynamic>{'dx': 2.0, 'dy': 3.0, 'time_msec': 999};
      final deserialized = GestureSwipeUpdateEvent.fromJson(json);
      expect(deserialized.dx, equals(2.0));
      expect(deserialized.dy, equals(3.0));
      expect(deserialized.timeMsec, equals(999));
      expect(deserialized.toJson(), equals(json));

      final fallback = GestureSwipeUpdateEvent.fromJson(const {});
      expect(fallback.dx, equals(0.0));
      expect(fallback.dy, equals(0.0));
      expect(fallback.timeMsec, equals(0));
    });

    test('GestureSwipeEndEvent fromJson, toJson, and properties', () {
      const event = GestureSwipeEndEvent(cancelled: false, timeMsec: 12345);
      expect(event.cancelled, isFalse);
      expect(event.timeMsec, equals(12345));
      expect(event.props, equals([false, 12345]));

      final json = <String, dynamic>{'cancelled': true, 'time_msec': 888};
      final deserialized = GestureSwipeEndEvent.fromJson(json);
      expect(deserialized.cancelled, isTrue);
      expect(deserialized.timeMsec, equals(888));
      expect(deserialized.toJson(), equals(json));

      final jsonNum = <String, dynamic>{'cancelled': 1, 'time_msec': 888};
      final deserializedNum = GestureSwipeEndEvent.fromJson(jsonNum);
      expect(deserializedNum.cancelled, isTrue);

      final fallback = GestureSwipeEndEvent.fromJson(const {});
      expect(fallback.cancelled, isFalse);
      expect(fallback.timeMsec, equals(0));
    });
  });
}
