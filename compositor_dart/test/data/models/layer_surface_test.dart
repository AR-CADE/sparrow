import 'package:compositor_dart/src/data/models/layer_surface.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('LayerSurface', () {
    const layerSurface = LayerSurface(
      handle: 10,
      textureId: 100,
      layer: 2,
      anchor: 15,
      x: 5,
      y: 10,
      width: 1920,
      height: 32,
      bufferWidth: 1920,
      bufferHeight: 32,
      marginTop: 1,
      marginBottom: 2,
      marginLeft: 3,
      marginRight: 4,
      exclusiveZone: 32,
      keyboardInteractive: true,
      outputId: 1,
      scope: 'panel',
    );

    test('props are correct', () {
      expect(
        layerSurface.props,
        equals([
          10,
          100,
          2,
          15,
          5,
          10,
          1920,
          32,
          1920,
          32,
          1,
          2,
          3,
          4,
          32,
          true,
          1,
          'panel',
        ]),
      );
    });

    test('equality works', () {
      expect(
        layerSurface,
        equals(
          const LayerSurface(
            handle: 10,
            textureId: 100,
            layer: 2,
            anchor: 15,
            x: 5,
            y: 10,
            width: 1920,
            height: 32,
            bufferWidth: 1920,
            bufferHeight: 32,
            marginTop: 1,
            marginBottom: 2,
            marginLeft: 3,
            marginRight: 4,
            exclusiveZone: 32,
            keyboardInteractive: true,
            outputId: 1,
            scope: 'panel',
          ),
        ),
      );
      expect(layerSurface, isNot(equals(layerSurface.copyWith(handle: 11))));
    });

    test('toJson and fromJson serialize properly', () {
      final json = layerSurface.toJson();
      expect(json['handle'], equals(10));
      expect(json['texture_id'], equals(100));
      expect(json['layer'], equals(2));
      expect(json['anchor'], equals(15));
      expect(json['width'], equals(1920));
      expect(json['height'], equals(32));
      expect(json['exclusive_zone'], equals(32));
      expect(json['keyboard_interactive'], isTrue);
      expect(json['output_id'], equals(1));
      expect(json['scope'], equals('panel'));

      final deserialized = LayerSurface.fromJson(json);
      expect(deserialized, equals(layerSurface));
    });

    test('helper getters work as expected', () {
      expect(layerSurface.isTop, isTrue);
      expect(layerSurface.isBackground, isFalse);
      expect(layerSurface.isBottom, isFalse);
      expect(layerSurface.isOverlay, isFalse);

      expect(layerSurface.anchorTop, isTrue);
      expect(layerSurface.anchorBottom, isTrue);
      expect(layerSurface.anchorLeft, isTrue);
      expect(layerSurface.anchorRight, isTrue);

      const bgSurface = LayerSurface(handle: 1, textureId: 1, anchor: 1);
      expect(bgSurface.isBackground, isTrue);
      expect(bgSurface.anchorTop, isTrue);
      expect(bgSurface.anchorBottom, isFalse);
      expect(bgSurface.anchorLeft, isFalse);
      expect(bgSurface.anchorRight, isFalse);

      const overlaySurface = LayerSurface(
        handle: 2,
        textureId: 2,
        layer: 3,
        anchor: 6,
      );
      expect(overlaySurface.isOverlay, isTrue);
      expect(overlaySurface.anchorBottom, isTrue);
      expect(overlaySurface.anchorLeft, isTrue);

      const bottomSurface = LayerSurface(
        handle: 3,
        textureId: 3,
        layer: 1,
        anchor: 8,
      );
      expect(bottomSurface.isBottom, isTrue);
      expect(bottomSurface.anchorRight, isTrue);
    });

    test('copyWith updates properties properly', () {
      final copy = layerSurface.copyWith(
        width: 1000,
        height: 50,
        layer: 3,
        scope: 'osd',
      );
      expect(copy.width, equals(1000));
      expect(copy.height, equals(50));
      expect(copy.layer, equals(3));
      expect(copy.scope, equals('osd'));
      expect(copy.handle, equals(layerSurface.handle));
    });
  });
}
