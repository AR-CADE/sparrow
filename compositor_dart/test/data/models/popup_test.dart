import 'package:compositor_dart/src/data/models/popup.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('Popup', () {
    const popup = Popup(
      handle: 50,
      textureId: 50,
      parentHandle: 10,
      x: 100,
      y: 200,
      width: 300,
      height: 400,
      outputId: 1,
      outputScale: 1.5,
    );

    test('props are correct', () {
      expect(popup.props, equals([50, 50, 10, 100, 200, 300, 400, 1, 1.5]));
    });

    test('equality works', () {
      expect(
        popup,
        equals(
          const Popup(
            handle: 50,
            textureId: 50,
            parentHandle: 10,
            x: 100,
            y: 200,
            width: 300,
            height: 400,
            outputId: 1,
            outputScale: 1.5,
          ),
        ),
      );
      expect(popup, isNot(equals(popup.copyWith(handle: 51))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = popup.toJson();
      expect(json['handle'], equals(50));
      expect(json['texture_id'], equals(50));
      expect(json['parent_handle'], equals(10));
      expect(json['output_id'], equals(1));

      final deserialized = Popup.fromJson(json);
      expect(deserialized, equals(popup));
    });

    test('copyWith updates properties', () {
      final updated = popup.copyWith(
        handle: 60,
        textureId: 61,
        parentHandle: 12,
        x: 150,
        y: 250,
        width: 350,
        height: 450,
        outputId: 2,
        outputScale: 2,
      );

      expect(updated.handle, equals(60));
      expect(updated.textureId, equals(61));
      expect(updated.parentHandle, equals(12));
      expect(updated.x, equals(150));
      expect(updated.y, equals(250));
      expect(updated.width, equals(350));
      expect(updated.height, equals(450));
      expect(updated.outputId, equals(2));
      expect(updated.outputScale, equals(2.0));
    });
  });
}
