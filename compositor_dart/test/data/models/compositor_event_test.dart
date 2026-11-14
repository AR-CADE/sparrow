import 'package:compositor_dart/src/data/models/compositor_event.dart';
import 'package:compositor_dart/src/data/models/compositor_event_type.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CompositorEvent', () {
    const compEvent = CompositorEvent(
      type: CompositorEventType.surfaceMap,
      event: {'id': 1},
    );

    test('props are correct', () {
      expect(
        compEvent.props,
        equals([
          CompositorEventType.surfaceMap,
          {'id': 1},
        ]),
      );
    });

    test('equality works', () {
      expect(
        compEvent,
        equals(
          const CompositorEvent(
            type: CompositorEventType.surfaceMap,
            event: {'id': 1},
          ),
        ),
      );
      expect(
        compEvent,
        isNot(
          equals(
            const CompositorEvent(
              type: CompositorEventType.surfaceUnMap,
              event: {'id': 1},
            ),
          ),
        ),
      );
    });

    test('toJson and fromJson serialize properly', () {
      final json = compEvent.toJson();
      expect(json['type'], equals('surfaceMap'));

      final deserialized = CompositorEvent.fromJson(json);
      expect(deserialized.type, equals(CompositorEventType.surfaceMap));
    });

    test('copyWith updates properties', () {
      final updated = compEvent.copyWith(
        type: CompositorEventType.outputAdded,
        event: 'test',
      );

      expect(updated.type, equals(CompositorEventType.outputAdded));
      expect(updated.event, equals('test'));
    });
  });
}
