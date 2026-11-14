import 'package:compositor_dart/src/data/models/compositor_sockets.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('CompositorSockets', () {
    const sockets = CompositorSockets(wayland: 'wayland-0', x: ':0');

    test('props are correct', () {
      expect(sockets.props, equals(['wayland-0', ':0']));
    });

    test('equality works', () {
      expect(
        sockets,
        equals(const CompositorSockets(wayland: 'wayland-0', x: ':0')),
      );
      expect(
        sockets,
        isNot(equals(const CompositorSockets(wayland: 'wayland-1', x: ':0'))),
      );
    });

    test('toJson and fromJson serialize properly', () {
      final json = sockets.toJson();
      expect(json['wayland'], equals('wayland-0'));
      expect(json['x'], equals(':0'));

      final deserialized = CompositorSockets.fromJson(json);
      expect(deserialized, equals(sockets));
    });

    test('copyWith updates properties', () {
      final updated = sockets.copyWith(wayland: 'wayland-1', x: ':1');
      expect(updated.wayland, equals('wayland-1'));
      expect(updated.x, equals(':1'));
    });
  });
}
