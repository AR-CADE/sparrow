import 'package:compositor_dart/src/data/models/sub_surface.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('SubSurface', () {
    const subSurface = SubSurface(
      handle: 10,
      textureId: 10,
      parentHandle: 1,
      x: 5,
      y: 5,
      width: 100,
      height: 100,
      bufferWidth: 100,
      bufferHeight: 100,
    );

    test('props are correct', () {
      expect(subSurface.props, equals([10, 10, 1, 5, 5, 100, 100, 100, 100]));
    });

    test('equality works', () {
      expect(
        subSurface,
        equals(
          const SubSurface(
            handle: 10,
            textureId: 10,
            parentHandle: 1,
            x: 5,
            y: 5,
            width: 100,
            height: 100,
            bufferWidth: 100,
            bufferHeight: 100,
          ),
        ),
      );
      expect(subSurface, isNot(equals(subSurface.copyWith(handle: 11))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = subSurface.toJson();
      expect(json['handle'], equals(10));
      expect(json['parent_handle'], equals(1));
      expect(json['x'], equals(5));

      final deserialized = SubSurface.fromJson(json);
      expect(deserialized, equals(subSurface));
    });

    test('copyWith updates properties', () {
      final updated = subSurface.copyWith(
        handle: 20,
        textureId: 21,
        parentHandle: 2,
        x: 10,
        y: 20,
        width: 200,
        height: 150,
        bufferWidth: 200,
        bufferHeight: 150,
      );

      expect(updated.handle, equals(20));
      expect(updated.textureId, equals(21));
      expect(updated.parentHandle, equals(2));
      expect(updated.x, equals(10));
      expect(updated.y, equals(20));
      expect(updated.width, equals(200));
      expect(updated.height, equals(150));
    });
  });
}
